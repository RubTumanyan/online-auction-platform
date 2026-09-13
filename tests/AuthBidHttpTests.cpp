#include <drogon/drogon.h>
#include <drogon/drogon_test.h>

#include "database/Database.h"
#include "runtime/RuntimePaths.h"
#include "services/AuthBidService.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

namespace
{
constexpr auto kBaseUrl = "http://127.0.0.1:18851";

std::pair<drogon::ReqResult, drogon::HttpResponsePtr> request(
    drogon::HttpMethod method, const std::string& path,
    const Json::Value& body = Json::Value{}, const std::string& token = {})
{
    auto client = drogon::HttpClient::newHttpClient(kBaseUrl);
    auto value = drogon::HttpRequest::newHttpRequest();
    value->setMethod(method); value->setPath(path);
    if (!body.isNull())
    {
        value->setContentTypeCode(drogon::CT_APPLICATION_JSON);
        Json::StreamWriterBuilder writer; writer["indentation"] = "";
        value->setBody(Json::writeString(writer, body));
    }
    if (!token.empty()) value->addHeader("Authorization", "Bearer " + token);
    return client->sendRequest(value, 10.0);
}

Json::Value parse(const drogon::HttpResponsePtr& response)
{
    const auto json = response->getJsonObject();
    if (!json) throw std::runtime_error("Expected JSON response");
    return *json;
}
}

DROGON_TEST(AuthenticationAndBidding)
{
    Json::Value registration; registration["username"] = "charlie_1"; registration["password"] = "StrongPassword123!";
    const auto [registerResult, registerResponse] = request(drogon::Post, "/api/auth/register", registration);
    REQUIRE(registerResult == drogon::ReqResult::Ok); REQUIRE(registerResponse != nullptr);
    CHECK(registerResponse->statusCode() == drogon::k201Created);
    const auto registered = parse(registerResponse);
    CHECK(registered["user"]["username"].asString() == "charlie_1");
    CHECK(registered["token"].asString().size() == 64);
    CHECK(!registered["user"].isMember("password_hash"));

    const auto [duplicateResult, duplicateResponse] = request(drogon::Post, "/api/auth/register", registration);
    REQUIRE(duplicateResult == drogon::ReqResult::Ok); REQUIRE(duplicateResponse != nullptr);
    CHECK(duplicateResponse->statusCode() == drogon::k409Conflict);

    Json::Value login; login["username"] = "alice"; login["password"] = "Alice123!";
    const auto [loginResult, loginResponse] = request(drogon::Post, "/api/auth/login", login);
    REQUIRE(loginResult == drogon::ReqResult::Ok); REQUIRE(loginResponse != nullptr);
    CHECK(loginResponse->statusCode() == drogon::k200OK);
    const auto aliceToken = parse(loginResponse)["token"].asString();
    REQUIRE(!aliceToken.empty());

    login["password"] = "wrong-password";
    const auto [badLoginResult, badLoginResponse] = request(drogon::Post, "/api/auth/login", login);
    REQUIRE(badLoginResult == drogon::ReqResult::Ok); REQUIRE(badLoginResponse != nullptr);
    CHECK(badLoginResponse->statusCode() == drogon::k401Unauthorized);
    CHECK(parse(badLoginResponse)["error"].asString() == "Invalid credentials");

    const auto [meResult, meResponse] = request(drogon::Get, "/api/auth/me", {}, aliceToken);
    REQUIRE(meResult == drogon::ReqResult::Ok); REQUIRE(meResponse != nullptr);
    CHECK(meResponse->statusCode() == drogon::k200OK);
    CHECK(parse(meResponse)["username"].asString() == "alice");
    const auto [badMeResult, badMeResponse] = request(drogon::Get, "/api/auth/me", {}, "invalid");
    REQUIRE(badMeResult == drogon::ReqResult::Ok); REQUIRE(badMeResponse != nullptr);
    CHECK(badMeResponse->statusCode() == drogon::k401Unauthorized);

    Json::Value bid; bid["amount"] = Json::Int64(3175);
    const auto [anonymousResult, anonymousResponse] = request(drogon::Post, "/api/lots/1/bids", bid);
    REQUIRE(anonymousResult == drogon::ReqResult::Ok); REQUIRE(anonymousResponse != nullptr);
    CHECK(anonymousResponse->statusCode() == drogon::k401Unauthorized);

    const auto [bidResult, bidResponse] = request(drogon::Post, "/api/lots/1/bids", bid, aliceToken);
    REQUIRE(bidResult == drogon::ReqResult::Ok); REQUIRE(bidResponse != nullptr);
    CHECK(bidResponse->statusCode() == drogon::k201Created);
    const auto placed = parse(bidResponse);
    CHECK(placed["currentPrice"].asInt64() == 3175);
    CHECK(placed["bid"]["bidderUsername"].asString() == "alice");

    bid["amount"] = Json::Int64(4000);
    const auto [ownHighestResult, ownHighestResponse] = request(drogon::Post, "/api/lots/1/bids", bid, aliceToken);
    REQUIRE(ownHighestResult == drogon::ReqResult::Ok); REQUIRE(ownHighestResponse != nullptr);
    CHECK(ownHighestResponse->statusCode() == drogon::k409Conflict);
    CHECK(parse(ownHighestResponse)["error"].asString() == "You already have the highest bid");

    bid["amount"] = Json::Int64(3500);
    const auto [lowResult, lowResponse] = request(drogon::Post, "/api/lots/1/bids", bid, aliceToken);
    REQUIRE(lowResult == drogon::ReqResult::Ok); REQUIRE(lowResponse != nullptr);
    CHECK(lowResponse->statusCode() == drogon::k409Conflict);
    CHECK(parse(lowResponse)["error"].asString() == "Bid must be at least current price plus minimum step");

    bid["amount"] = Json::Int64(10000);
    const auto [missingResult, missingResponse] = request(drogon::Post, "/api/lots/9999/bids", bid, aliceToken);
    REQUIRE(missingResult == drogon::ReqResult::Ok); REQUIRE(missingResponse != nullptr);
    CHECK(missingResponse->statusCode() == drogon::k404NotFound);
    const auto [expiredResult, expiredResponse] = request(drogon::Post, "/api/lots/1000/bids", bid, aliceToken);
    REQUIRE(expiredResult == drogon::ReqResult::Ok); REQUIRE(expiredResponse != nullptr);
    CHECK(expiredResponse->statusCode() == drogon::k409Conflict);

    const auto [historyResult, historyResponse] = request(drogon::Get, "/api/lots/1/bids?page=1&pageSize=10");
    REQUIRE(historyResult == drogon::ReqResult::Ok); REQUIRE(historyResponse != nullptr);
    CHECK(historyResponse->statusCode() == drogon::k200OK);
    const auto history = parse(historyResponse);
    CHECK(history["total"].asInt64() == 1); REQUIRE(history["items"].size() == 1);
    CHECK(history["items"][0]["bidderUsername"].asString() == "alice");

    const auto [detailsResult, detailsResponse] = request(drogon::Get, "/api/lots/1");
    REQUIRE(detailsResult == drogon::ReqResult::Ok); REQUIRE(detailsResponse != nullptr);
    CHECK(parse(detailsResponse)["highest_bidder_username"].asString() == "alice");

    const auto [logoutResult, logoutResponse] = request(drogon::Post, "/api/auth/logout", {}, aliceToken);
    REQUIRE(logoutResult == drogon::ReqResult::Ok); REQUIRE(logoutResponse != nullptr);
    CHECK(logoutResponse->statusCode() == drogon::k204NoContent);
    const auto [loggedOutResult, loggedOutResponse] = request(drogon::Get, "/api/auth/me", {}, aliceToken);
    REQUIRE(loggedOutResult == drogon::ReqResult::Ok); REQUIRE(loggedOutResponse != nullptr);
    CHECK(loggedOutResponse->statusCode() == drogon::k401Unauthorized);
}

int main(int argc, char* argv[])
{
    Json::Value config; std::ifstream input(auction::prepareRuntime()); input >> config;
    config["listeners"][0]["port"] = 18851;
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto databasePath = std::filesystem::absolute(std::filesystem::path("runtime") /
        ("auth-bid-http-test-" + std::to_string(suffix) + ".sqlite3"));
    config["custom_config"]["database"]["path"] = databasePath.string();
    {
        auction::database::Connection database(databasePath);
        auction::database::initialize(database, "database");
        database.execute("UPDATE auctions SET status='ended' WHERE id=1000");
    }
    auction::services::AuthService(databasePath).ensureDemoUsers();
    drogon::app().loadConfigJson(config);
    std::thread server([] { drogon::app().run(); });
    while (!drogon::app().isRunning()) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    const auto result = drogon::test::run(argc, argv);
    drogon::app().quit(); server.join(); std::filesystem::remove(databasePath); return result;
}
