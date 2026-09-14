#include <drogon/drogon.h>
#include <drogon/drogon_test.h>

#include "database/Database.h"
#include "runtime/RuntimePaths.h"
#include "security/HttpSecurity.h"
#include "services/AuthBidService.h"
#include "services/EmailDelivery.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{
constexpr auto kBaseUrl = "http://127.0.0.1:18852";
std::filesystem::path gDatabasePath;

std::vector<std::pair<std::string, std::string>>& capturedMails()
{
    static std::vector<std::pair<std::string, std::string>> mails;
    return mails;
}

// In-memory email transport: records (email, code) pairs, never sends anything.
class CapturingEmailSender final : public auction::services::EmailSender
{
  public:
    void sendVerificationCode(const std::string& email, const std::string& code) override
    {
        capturedMails().emplace_back(email, code);
    }
};

// Simulates a broken transactional email provider.
class FailingEmailSender final : public auction::services::EmailSender
{
  public:
    void sendVerificationCode(const std::string&, const std::string&) override
    {
        throw auction::services::ApiError(auction::services::ApiErrorKind::emailFailure,
                                          "provider temporarily unavailable");
    }
};

std::string latestCode(const std::string& email)
{
    for (auto iterator = capturedMails().rbegin(); iterator != capturedMails().rend(); ++iterator)
    {
        if (iterator->first == email) return iterator->second;
    }
    return {};
}

std::pair<drogon::ReqResult, drogon::HttpResponsePtr> postJson(const std::string& path, const Json::Value& body)
{
    auto client = drogon::HttpClient::newHttpClient(kBaseUrl);
    auto request = drogon::HttpRequest::newHttpJsonRequest(body);
    request->setMethod(drogon::Post); request->setPath(path);
    return client->sendRequest(request, 10.0);
}

std::pair<drogon::ReqResult, drogon::HttpResponsePtr> registerUser(const std::string& username,
                                                                   const std::string& email)
{
    Json::Value body;
    body["username"] = username; body["email"] = email; body["password"] = "StrongPassword123!";
    return postJson("/api/auth/register", body);
}

Json::Value parse(const drogon::HttpResponsePtr& response)
{
    const auto json = response->getJsonObject();
    if (!json) throw std::runtime_error("Expected JSON response");
    return *json;
}

// Returns the login token, or an empty string when the login failed.
std::string loginToken(const std::string& username)
{
    Json::Value body; body["username"] = username; body["password"] = "StrongPassword123!";
    const auto [result, response] = postJson("/api/auth/login", body);
    if (result != drogon::ReqResult::Ok || !response || response->statusCode() != drogon::k200OK)
        return {};
    return parse(response)["token"].asString();
}

std::pair<drogon::ReqResult, drogon::HttpResponsePtr> bid(const std::string& lotId,
                                                          const std::string& token, std::int64_t amount)
{
    auto client = drogon::HttpClient::newHttpClient(kBaseUrl);
    auto request = drogon::HttpRequest::newHttpRequest();
    request->setMethod(drogon::Post); request->setPath("/api/lots/" + lotId + "/bids");
    request->setContentTypeCode(drogon::CT_APPLICATION_JSON);
    Json::Value body; body["amount"] = Json::Int64(amount);
    Json::StreamWriterBuilder writer; writer["indentation"] = "";
    request->setBody(Json::writeString(writer, body));
    request->addHeader("Authorization", "Bearer " + token);
    return client->sendRequest(request, 10.0);
}

// Returns the minimum acceptable bid for lot lotId, or 0 when the lot is unavailable.
std::int64_t winningAmount(std::int64_t lotId)
{
    auto client = drogon::HttpClient::newHttpClient(kBaseUrl);
    auto request = drogon::HttpRequest::newHttpRequest();
    request->setMethod(drogon::Get); request->setPath("/api/lots/" + std::to_string(lotId));
    const auto [result, response] = client->sendRequest(request, 10.0);
    if (result != drogon::ReqResult::Ok || !response) return 0;
    const auto lot = parse(response);
    const auto price = lot.isMember("currentPrice") ? lot["currentPrice"].asInt64() : lot["current_price"].asInt64();
    const auto step = lot.isMember("minimum_step") ? lot["minimum_step"].asInt64() : lot["minimumStep"].asInt64();
    return price + step;
}
}

DROGON_TEST(EmailVerification)
{
    capturedMails().clear();

    const auto expectError = [&](const drogon::HttpResponsePtr& response, drogon::HttpStatusCode status,
                                 const std::string& message) {
        CHECK(response != nullptr);
        if (response) CHECK(response->statusCode() == status);
        CHECK(parse(response)["error"].asString() == message);
    };

    // ---------- registration, delivery, initial unverified state ----------
    const auto [registerResult, registerResponse] = registerUser("vera_valid", "vera_valid@example.test");
    REQUIRE(registerResult == drogon::ReqResult::Ok); REQUIRE(registerResponse != nullptr);
    CHECK(registerResponse->statusCode() == drogon::k201Created);
    const auto registered = parse(registerResponse);
    CHECK(registered["user"]["emailVerified"].asBool() == false);
    CHECK(registered["emailDeliveryFailed"].asBool() == false);
    CHECK(!registered["user"]["email"].asString().empty());

    const auto veraCode = latestCode("vera_valid@example.test");
    CHECK(veraCode.size() == 6);
    CHECK(veraCode.find_first_not_of("0123456789") == std::string::npos);

    // Duplicate email is rejected even for a different username.
    const auto [dupResult, dupResponse] = registerUser("vera_dup", "vera_valid@example.test");
    REQUIRE(dupResult == drogon::ReqResult::Ok);
    expectError(dupResponse, drogon::k409Conflict, "Email already exists");

    // ---------- incorrect code ----------
    Json::Value wrong; wrong["email"] = "vera_valid@example.test"; wrong["code"] = "000000";
    const auto [wrongResult, wrongResponse] = postJson("/api/auth/verify-email", wrong);
    REQUIRE(wrongResult == drogon::ReqResult::Ok);
    expectError(wrongResponse, drogon::k400BadRequest, "Invalid verification code.");

    // ---------- correct code ----------
    Json::Value correct; correct["email"] = "vera_valid@example.test"; correct["code"] = veraCode;
    const auto [verifyResult, verifyResponse] = postJson("/api/auth/verify-email", correct);
    REQUIRE(verifyResult == drogon::ReqResult::Ok); REQUIRE(verifyResponse != nullptr);
    CHECK(verifyResponse->statusCode() == drogon::k200OK);
    CHECK(parse(verifyResponse)["message"].asString() == "Email verified");

    // ---------- reused code: an already verified account is reported as verified ----------
    const auto [reuseResult, reuseResponse] = postJson("/api/auth/verify-email", correct);
    REQUIRE(reuseResult == drogon::ReqResult::Ok);
    expectError(reuseResponse, drogon::k409Conflict, "Email is already verified");

    // ---------- already verified ----------
    Json::Value resendVerified; resendVerified["email"] = "vera_valid@example.test";
    const auto [resendVerifiedResult, resendVerifiedResponse] = postJson("/api/auth/resend-verification", resendVerified);
    REQUIRE(resendVerifiedResult == drogon::ReqResult::Ok);
    expectError(resendVerifiedResponse, drogon::k409Conflict, "Email is already verified");

    // ---------- unverified cannot bid, verified can ----------
    const auto [activeRegisterResult, activeRegisterResponse] = registerUser("active_bidder", "active_bidder@example.test");
    REQUIRE(activeRegisterResult == drogon::ReqResult::Ok); REQUIRE(activeRegisterResponse != nullptr);
    CHECK(activeRegisterResponse->statusCode() == drogon::k201Created);
    const auto bidderToken = loginToken("active_bidder");
    REQUIRE(!bidderToken.empty());
    const auto amount = winningAmount(2);
    REQUIRE(amount > 0);
    const auto [blockedResult, blockedResponse] = bid("2", bidderToken, amount);
    REQUIRE(blockedResult == drogon::ReqResult::Ok);
    expectError(blockedResponse, drogon::k403Forbidden, "Email verification is required before placing a bid");
    const auto bidderCode = latestCode("active_bidder@example.test");
    REQUIRE(bidderCode.size() == 6);
    Json::Value bidderVerify; bidderVerify["email"] = "active_bidder@example.test"; bidderVerify["code"] = bidderCode;
    const auto [bidderVerifyResult, bidderVerifyResponse] = postJson("/api/auth/verify-email", bidderVerify);
    REQUIRE(bidderVerifyResult == drogon::ReqResult::Ok);
    CHECK(bidderVerifyResponse->statusCode() == drogon::k200OK);
    const auto [successResult, successResponse] = bid("2", bidderToken, amount);
    REQUIRE(successResult == drogon::ReqResult::Ok);
    CHECK(successResponse->statusCode() == drogon::k201Created);
    CHECK(parse(successResponse)["bid"]["bidderUsername"].asString() == "active_bidder");

    // ---------- expired code ----------
    const auto [expiryRegisterResult, expiryRegisterResponse] = registerUser("expiring_user", "expiring_user@example.test");
    REQUIRE(expiryRegisterResult == drogon::ReqResult::Ok); REQUIRE(expiryRegisterResponse != nullptr);
    CHECK(expiryRegisterResponse->statusCode() == drogon::k201Created);
    {
        auction::database::Connection connection(gDatabasePath);
        auto expire = connection.prepare(
            "UPDATE email_verification_codes SET expires_at='2020-01-01T00:00:00Z' "
            "WHERE user_id=(SELECT id FROM users WHERE email=?)");
        expire.bind(1, std::string("expiring_user@example.test"));
        expire.run();
    }
    Json::Value expiredBody; expiredBody["email"] = "expiring_user@example.test";
    expiredBody["code"] = latestCode("expiring_user@example.test");
    const auto [expiredResult, expiredResponse] = postJson("/api/auth/verify-email", expiredBody);
    REQUIRE(expiredResult == drogon::ReqResult::Ok);
    expectError(expiredResponse, drogon::k400BadRequest, "Verification code has expired. Request a new code.");
    const auto [expiredReuseResult, expiredReuseResponse] = postJson("/api/auth/verify-email", expiredBody);
    REQUIRE(expiredReuseResult == drogon::ReqResult::Ok);
    expectError(expiredReuseResponse, drogon::k400BadRequest, "No verification code found. Request a new code.");

    // ---------- resend, old code invalidation, cooldown ----------
    const auto [resendRegisterResult, resendRegisterResponse] = registerUser("resend_user", "resend_user@example.test");
    REQUIRE(resendRegisterResult == drogon::ReqResult::Ok); REQUIRE(resendRegisterResponse != nullptr);
    CHECK(resendRegisterResponse->statusCode() == drogon::k201Created);
    const auto firstCode = latestCode("resend_user@example.test");
    REQUIRE(firstCode.size() == 6);
    Json::Value resendBody; resendBody["email"] = "resend_user@example.test";

    const auto [cooldownResult, cooldownResponse] = postJson("/api/auth/resend-verification", resendBody);
    REQUIRE(cooldownResult == drogon::ReqResult::Ok);
    CHECK(cooldownResponse->statusCode() == drogon::k429TooManyRequests);
    CHECK(parse(cooldownResponse)["retryAfterSeconds"].asInt() >= 1);
    CHECK(parse(cooldownResponse)["retryAfterSeconds"].asInt() <= 60);

    // Backdate the previous send so the cooldown does not block this test.
    {
        auction::database::Connection connection(gDatabasePath);
        auto backdate = connection.prepare(
            "UPDATE email_verification_codes SET last_sent_at='2000-01-01T00:00:00Z' "
            "WHERE user_id=(SELECT id FROM users WHERE email=?)");
        backdate.bind(1, std::string("resend_user@example.test"));
        backdate.run();
    }
    const auto [resendResult, resendResponse] = postJson("/api/auth/resend-verification", resendBody);
    REQUIRE(resendResult == drogon::ReqResult::Ok); REQUIRE(resendResponse != nullptr);
    CHECK(resendResponse->statusCode() == drogon::k200OK);
    const auto secondCode = latestCode("resend_user@example.test");
    CHECK(secondCode != firstCode);

    // The previous code is invalid once a new one is issued.
    Json::Value reuseOld; reuseOld["email"] = "resend_user@example.test"; reuseOld["code"] = firstCode;
    const auto [reuseOldResult, reuseOldResponse] = postJson("/api/auth/verify-email", reuseOld);
    REQUIRE(reuseOldResult == drogon::ReqResult::Ok);
    expectError(reuseOldResponse, drogon::k400BadRequest, "Invalid verification code.");

    // Immediately after a successful resend the cooldown is active again.
    const auto [cooldown2Result, cooldown2Response] = postJson("/api/auth/resend-verification", resendBody);
    REQUIRE(cooldown2Result == drogon::ReqResult::Ok);
    CHECK(cooldown2Response->statusCode() == drogon::k429TooManyRequests);

    {
        auction::database::Connection connection(gDatabasePath);
        auto backdate = connection.prepare(
            "UPDATE email_verification_codes SET last_sent_at='2000-01-01T00:00:00Z' "
            "WHERE user_id=(SELECT id FROM users WHERE email=?)");
        backdate.bind(1, std::string("resend_user@example.test"));
        backdate.run();
    }
    const auto [resend2Result, resend2Response] = postJson("/api/auth/resend-verification", resendBody);
    REQUIRE(resend2Result == drogon::ReqResult::Ok);
    CHECK(resend2Response->statusCode() == drogon::k200OK);
    const auto thirdCode = latestCode("resend_user@example.test");
    Json::Value resendVerify; resendVerify["email"] = "resend_user@example.test"; resendVerify["code"] = thirdCode;
    const auto [resendVerifyResult, resendVerifyResponse] = postJson("/api/auth/verify-email", resendVerify);
    REQUIRE(resendVerifyResult == drogon::ReqResult::Ok);
    CHECK(resendVerifyResponse->statusCode() == drogon::k200OK);

    // ---------- resend attempts are capped ----------
    const auto [capRegisterResult, capRegisterResponse] = registerUser("capped_user", "capped_user@example.test");
    REQUIRE(capRegisterResult == drogon::ReqResult::Ok); REQUIRE(capRegisterResponse != nullptr);
    CHECK(capRegisterResponse->statusCode() == drogon::k201Created);
    {
        auction::database::Connection connection(gDatabasePath);
        auto cap = connection.prepare(
            "UPDATE email_verification_codes SET last_sent_at='2000-01-01T00:00:00Z',send_count=8 "
            "WHERE user_id=(SELECT id FROM users WHERE email=?)");
        cap.bind(1, std::string("capped_user@example.test"));
        cap.run();
    }
    Json::Value capBody; capBody["email"] = "capped_user@example.test";
    const auto [capResult, capResponse] = postJson("/api/auth/resend-verification", capBody);
    REQUIRE(capResult == drogon::ReqResult::Ok);
    expectError(capResponse, drogon::k429TooManyRequests, "Too many verification code requests. Please contact support.");

    // ---------- excessive incorrect attempts ----------
    const auto [attemptsRegisterResult, attemptsRegisterResponse] = registerUser("attempts_user", "attempts_user@example.test");
    REQUIRE(attemptsRegisterResult == drogon::ReqResult::Ok); REQUIRE(attemptsRegisterResponse != nullptr);
    CHECK(attemptsRegisterResponse->statusCode() == drogon::k201Created);
    const auto attemptsCode = latestCode("attempts_user@example.test");
    REQUIRE(attemptsCode.size() == 6);
    Json::Value wrongAttempt; wrongAttempt["email"] = "attempts_user@example.test"; wrongAttempt["code"] = "111111";
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        const auto [attemptResult, attemptResponse] = postJson("/api/auth/verify-email", wrongAttempt);
        REQUIRE(attemptResult == drogon::ReqResult::Ok);
        expectError(attemptResponse, drogon::k400BadRequest, "Invalid verification code.");
    }
    const auto [fifthResult, fifthResponse] = postJson("/api/auth/verify-email", wrongAttempt);
    REQUIRE(fifthResult == drogon::ReqResult::Ok);
    expectError(fifthResponse, drogon::k400BadRequest, "Too many failed attempts. Request a new code.");
    Json::Value correctCode; correctCode["email"] = "attempts_user@example.test"; correctCode["code"] = attemptsCode;
    const auto [afterBlockResult, afterBlockResponse] = postJson("/api/auth/verify-email", correctCode);
    REQUIRE(afterBlockResult == drogon::ReqResult::Ok);
    expectError(afterBlockResponse, drogon::k400BadRequest, "No verification code found. Request a new code.");
    const auto [attemptsResendResult, attemptsResendResponse] = postJson("/api/auth/resend-verification", wrongAttempt);
    REQUIRE(attemptsResendResult == drogon::ReqResult::Ok);
    CHECK(attemptsResendResponse->statusCode() == drogon::k200OK);
    Json::Value freshCode; freshCode["email"] = "attempts_user@example.test";
    freshCode["code"] = latestCode("attempts_user@example.test");
    const auto [freshResult, freshResponse] = postJson("/api/auth/verify-email", freshCode);
    REQUIRE(freshResult == drogon::ReqResult::Ok);
    CHECK(freshResponse->statusCode() == drogon::k200OK);

    // ---------- simulated provider failure keeps the account recoverable ----------
    auction::services::installVerificationEmailSender(std::make_unique<FailingEmailSender>());
    const auto [failRegisterResult, failRegisterResponse] = registerUser("mail_failure", "mail_failure@example.test");
    REQUIRE(failRegisterResult == drogon::ReqResult::Ok); REQUIRE(failRegisterResponse != nullptr);
    CHECK(failRegisterResponse->statusCode() == drogon::k201Created);
    const auto failedRegistration = parse(failRegisterResponse);
    CHECK(failedRegistration["emailDeliveryFailed"].asBool() == true);
    CHECK(failedRegistration["user"]["emailVerified"].asBool() == false);
    CHECK(latestCode("mail_failure@example.test").empty());

    // No unusable code is stored after a failed delivery.
    Json::Value phantomVerify; phantomVerify["email"] = "mail_failure@example.test"; phantomVerify["code"] = "123456";
    const auto [phantomResult, phantomResponse] = postJson("/api/auth/verify-email", phantomVerify);
    REQUIRE(phantomResult == drogon::ReqResult::Ok);
    expectError(phantomResponse, drogon::k400BadRequest, "No verification code found. Request a new code.");

    // Recovery: a later resend generates and delivers a fresh code.
    auction::services::installVerificationEmailSender(std::make_unique<CapturingEmailSender>());
    Json::Value rescueResend; rescueResend["email"] = "mail_failure@example.test";
    const auto [rescueResult, rescueResponse] = postJson("/api/auth/resend-verification", rescueResend);
    REQUIRE(rescueResult == drogon::ReqResult::Ok);
    CHECK(rescueResponse->statusCode() == drogon::k200OK);
    Json::Value rescueVerify; rescueVerify["email"] = "mail_failure@example.test";
    rescueVerify["code"] = latestCode("mail_failure@example.test");
    const auto [rescueVerifyResult, rescueVerifyResponse] = postJson("/api/auth/verify-email", rescueVerify);
    REQUIRE(rescueVerifyResult == drogon::ReqResult::Ok);
    CHECK(rescueVerifyResponse->statusCode() == drogon::k200OK);
}

int main(int argc, char* argv[])
{
    Json::Value config; std::ifstream input(auction::prepareRuntime()); input >> config;
    config["listeners"][0]["port"] = 18852;
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    gDatabasePath = std::filesystem::absolute(std::filesystem::path("runtime") /
        ("email-verification-test-" + std::to_string(suffix) + ".sqlite3"));
    config["custom_config"]["database"]["path"] = gDatabasePath.string();
    {
        auction::database::Connection database(gDatabasePath);
        auction::database::initialize(database, "database");
    }
    auction::services::installVerificationEmailSender(std::make_unique<CapturingEmailSender>());
    auction::services::AuthService(gDatabasePath).ensureDemoUsers();
    auction::security::relaxSecurityForTests(config);
    drogon::app().loadConfigJson(config);
    auction::security::registerHttpSecurity();
    std::thread server([] { drogon::app().run(); });
    while (!drogon::app().isRunning()) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    const auto result = drogon::test::run(argc, argv);
    drogon::app().quit(); server.join(); std::filesystem::remove(gDatabasePath); return result;
}