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

namespace
{
constexpr auto kBaseUrl = "http://127.0.0.1:18853";

class NullEmailSender final : public auction::services::EmailSender
{
  public:
    void sendVerificationCode(const std::string&, const std::string&) override {}
};

std::pair<drogon::ReqResult, drogon::HttpResponsePtr> post(const std::string& path, const std::string& body)
{
    auto client = drogon::HttpClient::newHttpClient(kBaseUrl);
    auto value  = drogon::HttpRequest::newHttpRequest();
    value->setMethod(drogon::Post);
    value->setPath(path);
    value->setContentTypeCode(drogon::CT_APPLICATION_JSON);
    value->setBody(body);
    return client->sendRequest(value, 10.0);
}

std::pair<drogon::ReqResult, drogon::HttpResponsePtr> postJson(const std::string& path, const Json::Value& body)
{
    Json::StreamWriterBuilder writer; writer["indentation"] = "";
    return post(path, Json::writeString(writer, body));
}

std::pair<drogon::ReqResult, drogon::HttpResponsePtr> get(const std::string& path, const std::string& host = "127.0.0.1")
{
    auto client = drogon::HttpClient::newHttpClient(kBaseUrl);
    auto value  = drogon::HttpRequest::newHttpRequest();
    value->setPath(path);
    if (host != "127.0.0.1") value->addHeader("Host", host);
    return client->sendRequest(value, 5.0);
}
}

DROGON_TEST(SecurityHeadersPresent)
{
    const auto [result, response] = get("/api/health");
    REQUIRE(result == drogon::ReqResult::Ok);
    REQUIRE(response != nullptr);
    CHECK(!response->getHeader("content-security-policy").empty());
    CHECK(response->getHeader("x-content-type-options") == "nosniff");
    CHECK(response->getHeader("x-frame-options") == "DENY");
    CHECK(response->getHeader("referrer-policy") == "no-referrer");
    CHECK(response->getHeader("strict-transport-security").find("max-age=") != std::string::npos);
}

DROGON_TEST(HostAllowlistRejectsUnknownHost)
{
    const auto [result, response] = get("/api/health", "evil.example");
    REQUIRE(result == drogon::ReqResult::Ok);
    REQUIRE(response != nullptr);
    CHECK(response->statusCode() == drogon::k404NotFound);
    const auto json = response->getJsonObject();
    REQUIRE(json);
    CHECK((*json)["error"].asString() == "Not found");
}

DROGON_TEST(RateLimitAuthLoginExceeded)
{
    Json::Value payload;
    payload["username"] = "does_not_exist";
    payload["password"] = "DoesNotExist123!";
    for (int i = 0; i < 10; ++i)
    {
        const auto [result, response] = postJson("/api/auth/login", payload);
        REQUIRE(result == drogon::ReqResult::Ok);
        REQUIRE(response != nullptr);
        if (response->statusCode() == drogon::k429TooManyRequests)
        {
            const auto json = response->getJsonObject();
            REQUIRE(json);
            CHECK((*json)["error"].asString() == "Rate limit exceeded; please retry later");
            CHECK(!response->getHeader("retry-after").empty());
            return;
        }
        CHECK(response->statusCode() != drogon::k200OK);
    }
    FAIL("Expected 429 rate limit response before exhausting attempts");
}

DROGON_TEST(RateLimitBidPlaceExceeded)
{
    Json::Value payload;
    payload["amount"] = static_cast<Json::Int64>(10000);
    for (int i = 0; i < 30; ++i)
    {
        const auto [result, response] = postJson("/api/lots/1/bids", payload);
        REQUIRE(result == drogon::ReqResult::Ok);
        REQUIRE(response != nullptr);
        if (response->statusCode() == drogon::k429TooManyRequests)
        {
            const auto json = response->getJsonObject();
            REQUIRE(json);
            CHECK((*json)["error"].asString() == "Rate limit exceeded; please retry later");
            return;
        }
        CHECK(response->statusCode() != drogon::k200OK);
    }
    FAIL("Expected 429 rate limit response before exhausting attempts");
}

DROGON_TEST(BodyTooLargeRejected)
{
    std::string big(70000, 'x');
    Json::Value payload;
    payload["data"] = big;
    const auto [result, response] = postJson("/api/auth/login", payload);
    REQUIRE(result == drogon::ReqResult::Ok);
    REQUIRE(response != nullptr);
    CHECK(response->statusCode() == drogon::k413RequestEntityTooLarge);
}

int main(int argc, char* argv[])
{
    Json::Value config;
    std::ifstream input(auction::prepareRuntime());
    input >> config;
    config["listeners"][0]["port"] = 18853;
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto databasePath = std::filesystem::absolute(
        std::filesystem::path("runtime") / ("security-http-test-" + std::to_string(suffix) + ".sqlite3"));
    config["custom_config"]["database"]["path"] = databasePath.string();
    {
        auction::database::Connection database(databasePath);
        auction::database::initialize(database, "database");
    }
    auto& sec = config["custom_config"]["security"];
    sec = Json::objectValue;
    sec["allowed_hosts"]  = Json::arrayValue;
    sec["allowed_hosts"].append("127.0.0.1");
    sec["allowed_hosts"].append("localhost");
    sec["rate_limits"] = Json::objectValue;
    sec["rate_limits"]["auth_login"]["max"]  = static_cast<Json::UInt64>(3);
    sec["rate_limits"]["auth_login"]["window_seconds"] = static_cast<Json::UInt64>(60);
    sec["rate_limits"]["bid"]["max"]  = static_cast<Json::UInt64>(5);
    sec["rate_limits"]["bid"]["window_seconds"] = static_cast<Json::UInt64>(60);
    sec["ws_max_connections"] = static_cast<Json::UInt64>(100);
    sec["ws_max_connections_per_ip"] = static_cast<Json::UInt64>(5);
    sec["client_max_body_bytes"] = static_cast<Json::UInt64>(65536);
    auction::services::installVerificationEmailSender(std::make_unique<NullEmailSender>());
    auction::services::AuthService(databasePath).ensureDemoUsers();
    drogon::app().loadConfigJson(config);
    auction::security::registerHttpSecurity();
    std::thread server([] { drogon::app().run(); });
    while (!drogon::app().isRunning()) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    const auto result = drogon::test::run(argc, argv);
    drogon::app().quit();
    server.join();
    std::filesystem::remove(databasePath);
    return result;
}
