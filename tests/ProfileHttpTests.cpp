#include <drogon/drogon.h>
#include <drogon/drogon_test.h>

#include "database/Database.h"
#include "runtime/RuntimePaths.h"
#include "security/HttpSecurity.h"
#include "services/AuctionCloseService.h"
#include "services/AuthBidService.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <string>
#include <thread>
#include <utility>

namespace
{
constexpr auto kBaseUrl = "http://127.0.0.1:18851";

std::pair<drogon::ReqResult, drogon::HttpResponsePtr> get(const std::string& path,
                                                           const std::string& token = {})
{
    auto client = drogon::HttpClient::newHttpClient(kBaseUrl);
    auto request = drogon::HttpRequest::newHttpRequest();
    request->setPath(path);
    if (!token.empty()) request->addHeader("Authorization", "Bearer " + token);
    return client->sendRequest(request, 5.0);
}

Json::Value json(const drogon::HttpResponsePtr& response)
{
    const auto body = response->getJsonObject();
    if (!body) throw std::runtime_error("Expected a JSON response");
    return *body;
}

struct TestTokens
{
    std::string alice;
    std::string bob;
};
TestTokens g_tokens;
}

DROGON_TEST(ProfileEndpointsRequireAuthorization)
{
    const auto [summaryResult, summaryResponse] = get("/api/profile/summary");
    REQUIRE(summaryResult == drogon::ReqResult::Ok);
    REQUIRE(summaryResponse != nullptr);
    CHECK(summaryResponse->statusCode() == drogon::k401Unauthorized);

    const auto [auctionsResult, auctionsResponse] = get("/api/profile/auctions");
    REQUIRE(auctionsResult == drogon::ReqResult::Ok);
    REQUIRE(auctionsResponse != nullptr);
    CHECK(auctionsResponse->statusCode() == drogon::k401Unauthorized);
}

DROGON_TEST(SummaryCounts)
{
    const auto [result, response] = get("/api/profile/summary", g_tokens.alice);
    REQUIRE(result == drogon::ReqResult::Ok);
    REQUIRE(response != nullptr);
    CHECK(response->statusCode() == drogon::k200OK);
    const auto body = json(response);
    CHECK(body["user"]["username"].asString() == "alice");
    CHECK(body["user"]["emailVerified"].asBool());
    CHECK(body["stats"]["activeParticipations"].asInt64() == 2);
    CHECK(body["stats"]["wonAuctions"].asInt64() == 1);
    CHECK(body["stats"]["totalParticipations"].asInt64() == 4);
}

DROGON_TEST(OwnDataIsolation)
{
    const auto [result, response] = get("/api/profile/auctions?filter=won", g_tokens.bob);
    REQUIRE(result == drogon::ReqResult::Ok);
    REQUIRE(response != nullptr);
    CHECK(response->statusCode() == drogon::k200OK);
    const auto body = json(response);
    REQUIRE(body["items"].size() == 1);
    CHECK(body["items"][0]["id"].asInt64() == 4);
    CHECK(body["items"][0]["participationStatus"].asString() == "won");
}

DROGON_TEST(AuctionsFilteredAndDeduplicated)
{
    const auto [activeResult, activeResponse] = get("/api/profile/auctions?filter=active", g_tokens.alice);
    REQUIRE(activeResult == drogon::ReqResult::Ok);
    REQUIRE(activeResponse != nullptr);
    CHECK(activeResponse->statusCode() == drogon::k200OK);
    const auto active = json(activeResponse);
    CHECK(active["filter"].asString() == "active");
    REQUIRE(active["items"].size() == 2);
    for (const auto& item : active["items"])
    {
        CHECK(item["participationStatus"].asString() == "active");
        CHECK(item["status"].asString() == "active");
        CHECK(item["userHighestBid"].asInt64() > 0);
        CHECK(item["category"].isMember("name"));
    }

    const auto [wonResult, wonResponse] = get("/api/profile/auctions?filter=won", g_tokens.alice);
    REQUIRE(wonResult == drogon::ReqResult::Ok);
    REQUIRE(wonResponse != nullptr);
    const auto won = json(wonResponse);
    REQUIRE(won["items"].size() == 1);
    CHECK(won["items"][0]["id"].asInt64() == 3);
    CHECK(won["items"][0]["participationStatus"].asString() == "won");
    CHECK(won["items"][0]["userHighestBid"].asInt64() == 4000);
    CHECK(!won["items"][0]["isLeading"].asBool());

    const auto [allResult, allResponse] = get("/api/profile/auctions?filter=all", g_tokens.alice);
    REQUIRE(allResult == drogon::ReqResult::Ok);
    REQUIRE(allResponse != nullptr);
    const auto all = json(allResponse);
    REQUIRE(all["items"].size() == 4);
    bool sawLost = false;
    bool sawActive = false;
    std::int64_t userHighestOnLot2 = 0;
    for (const auto& item : all["items"])
    {
        if (item["id"].asInt64() == 4 && item["participationStatus"].asString() == "lost") sawLost = true;
        if (item["participationStatus"].asString() == "active") sawActive = true;
        if (item["id"].asInt64() == 2) userHighestOnLot2 = item["userHighestBid"].asInt64();
    }
    CHECK(sawLost);
    CHECK(sawActive);
    CHECK(userHighestOnLot2 == 2300);
}

DROGON_TEST(LeadingStateOnActiveLots)
{
    const auto [result, response] = get("/api/profile/auctions?filter=active", g_tokens.alice);
    REQUIRE(result == drogon::ReqResult::Ok);
    REQUIRE(response != nullptr);
    const auto body = json(response);
    bool lot1NotLeading = false;
    bool lot2Leading = false;
    for (const auto& item : body["items"])
    {
        if (item["id"].asInt64() == 1 && item["isLeading"].asBool() == false) lot1NotLeading = true;
        if (item["id"].asInt64() == 2 && item["isLeading"].asBool() == true) lot2Leading = true;
    }
    CHECK(lot1NotLeading);
    CHECK(lot2Leading);
}

DROGON_TEST(InvalidFilterIsRejected)
{
    const auto [result, response] = get("/api/profile/auctions?filter=sideways", g_tokens.alice);
    REQUIRE(result == drogon::ReqResult::Ok);
    REQUIRE(response != nullptr);
    CHECK(response->statusCode() == drogon::k400BadRequest);
    CHECK(!json(response)["error"].asString().empty());
}

int main(int argc, char* argv[])
{
    Json::Value config;
    std::ifstream input(auction::prepareRuntime());
    input >> config;
    config["listeners"][0]["port"] = 18851;

    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto databasePath = std::filesystem::absolute(
        std::filesystem::path("runtime") / ("profile-http-test-" + std::to_string(suffix) + ".sqlite3"));
    config["custom_config"]["database"]["path"] = databasePath.string();
    {
        auction::database::Connection database(databasePath);
        auction::database::initialize(database, "database");
        database.execute("UPDATE auctions SET starts_at='2026-09-01T00:00:00Z', ends_at='2099-01-01T00:00:00Z'"
                         " WHERE id IN (1,2,3,4)");
    }
    const auction::services::AuthService auth(databasePath);
    auth.ensureDemoUsers();

    const auto aliceId = [&] {
        auction::database::Connection database(databasePath);
        auto alice = database.prepare("SELECT id FROM users WHERE username='alice'");
        alice.step();
        return alice.integer(0);
    }();
    const auto bobId = [&] {
        auction::database::Connection database(databasePath);
        auto bob = database.prepare("SELECT id FROM users WHERE username='bob'");
        bob.step();
        return bob.integer(0);
    }();

    auction::services::BidService bids(databasePath);
    bids.place(1, aliceId, 3000);
    bids.place(1, bobId, 3050);
    bids.place(2, aliceId, 2200);
    bids.place(2, bobId, 2250);
    bids.place(2, aliceId, 2300);
    bids.place(3, aliceId, 4000);
    bids.place(4, aliceId, 5000);
    bids.place(4, bobId, 5050);

    {
        auction::database::Connection database(databasePath);
        database.execute("UPDATE auctions SET starts_at='2026-09-01T00:00:00Z', ends_at='2026-09-12T00:00:01Z'"
                         " WHERE id IN (3,4)");
    }
    const auto closures = auction::services::AuctionCloseService(databasePath).closeExpired();
    if (closures.size() != 2) return 2;

    const auto aliceToken = auth.login("alice", "Alice123!").token;
    const auto bobToken = auth.login("bob", "Bob123!").token;

    auction::security::relaxSecurityForTests(config);
    drogon::app().loadConfigJson(config);
    auction::security::registerHttpSecurity();
    g_tokens.alice = aliceToken;
    g_tokens.bob = bobToken;
    std::thread server([] { drogon::app().run(); });
    while (!drogon::app().isRunning()) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    const auto result = drogon::test::run(argc, argv);
    drogon::app().quit();
    server.join();
    std::filesystem::remove(databasePath);
    return result;
}
