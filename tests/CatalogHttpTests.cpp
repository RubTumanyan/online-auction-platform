#include <drogon/drogon.h>
#include <drogon/drogon_test.h>

#include "database/Database.h"
#include "runtime/RuntimePaths.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{
constexpr auto kBaseUrl = "http://127.0.0.1:18850";

std::pair<drogon::ReqResult, drogon::HttpResponsePtr> get(const std::string& path)
{
    auto client = drogon::HttpClient::newHttpClient(kBaseUrl);
    auto request = drogon::HttpRequest::newHttpRequest();
    request->setPath(path);
    return client->sendRequest(request, 5.0);
}

Json::Value json(const drogon::HttpResponsePtr& response)
{
    const auto body = response->getJsonObject();
    if (!body) throw std::runtime_error("Expected a JSON response");
    return *body;
}
}

DROGON_TEST(CategoriesEndpoint)
{
    const auto [result, response] = get("/api/categories");
    REQUIRE(result == drogon::ReqResult::Ok);
    REQUIRE(response != nullptr);
    CHECK(response->statusCode() == drogon::k200OK);
    const auto body = json(response);
    REQUIRE(body["categories"].isArray());
    CHECK(body["categories"].size() == 10);
    CHECK(body["categories"][0]["id"].asInt64() == 1);
    CHECK(body["categories"][0]["name"].asString() == "Clothing");
}

DROGON_TEST(DefaultPaginationAndActiveFilter)
{
    const auto [result, response] = get("/api/lots");
    REQUIRE(result == drogon::ReqResult::Ok);
    REQUIRE(response != nullptr);
    CHECK(response->statusCode() == drogon::k200OK);
    const auto body = json(response);
    CHECK(body["page"].asInt() == 1);
    CHECK(body["limit"].asInt() == 20);
    CHECK(body["total"].asInt64() == 998);
    CHECK(body["total_pages"].asInt64() == 50);
    REQUIRE(body["lots"].size() == 20);
    for (const auto& lot : body["lots"])
    {
        CHECK(lot["status"].asString() == "active");
        CHECK(lot["image_url"].asString().starts_with("/images/products/"));
        CHECK(lot.isMember("short_description"));
    }
}

DROGON_TEST(FilterAndSearch)
{
    const auto [categoryResult, categoryResponse] = get("/api/lots?category_id=1&limit=100");
    REQUIRE(categoryResult == drogon::ReqResult::Ok);
    REQUIRE(categoryResponse != nullptr);
    const auto categoryBody = json(categoryResponse);
    CHECK(categoryBody["total"].asInt64() == 100);
    for (const auto& lot : categoryBody["lots"])
        CHECK(lot["category"]["id"].asInt64() == 1);

    const auto [searchResult, searchResponse] = get("/api/lots?search=DIGITAL&limit=100");
    REQUIRE(searchResult == drogon::ReqResult::Ok);
    REQUIRE(searchResponse != nullptr);
    const auto searchBody = json(searchResponse);
    CHECK(searchBody["total"].asInt64() == 10);
    for (const auto& lot : searchBody["lots"])
        CHECK(lot["category"]["name"].asString() == "Electronics");
}

DROGON_TEST(Sorting)
{
    const auto [priceResult, priceResponse] = get("/api/lots?sort_by=current_price&order=desc&limit=100");
    REQUIRE(priceResult == drogon::ReqResult::Ok);
    REQUIRE(priceResponse != nullptr);
    const auto prices = json(priceResponse)["lots"];
    REQUIRE(prices.size() == 100);
    for (Json::ArrayIndex index = 1; index < prices.size(); ++index)
        CHECK(prices[index - 1]["current_price"].asInt64() >= prices[index]["current_price"].asInt64());

    const auto [endResult, endResponse] = get("/api/lots?sort_by=end_time&order=asc&limit=100");
    REQUIRE(endResult == drogon::ReqResult::Ok);
    REQUIRE(endResponse != nullptr);
    const auto ends = json(endResponse)["lots"];
    REQUIRE(ends.size() == 100);
    for (Json::ArrayIndex index = 1; index < ends.size(); ++index)
        CHECK(ends[index - 1]["end_time"].asString() <= ends[index]["end_time"].asString());
}

DROGON_TEST(InvalidQueryParameters)
{
    const std::vector<std::string> paths = {
        "/api/lots?page=0", "/api/lots?page=no", "/api/lots?limit=0",
        "/api/lots?limit=101", "/api/lots?category_id=no", "/api/lots?category_id=9999",
        "/api/lots?sort_by=id", "/api/lots?order=sideways"};
    for (const auto& path : paths)
    {
        const auto [result, response] = get(path);
        REQUIRE(result == drogon::ReqResult::Ok);
        REQUIRE(response != nullptr);
        CHECK(response->statusCode() == drogon::k400BadRequest);
        CHECK(!json(response)["error"].asString().empty());
    }
}

DROGON_TEST(LotDetailsAndStaticImage)
{
    const auto [result, response] = get("/api/lots/101");
    REQUIRE(result == drogon::ReqResult::Ok);
    REQUIRE(response != nullptr);
    CHECK(response->statusCode() == drogon::k200OK);
    const auto body = json(response);
    CHECK(body["id"].asInt64() == 101);
    CHECK(body["category"]["name"].asString() == "Electronics");
    CHECK(body["minimum_step"].asInt64() == 20);
    CHECK(body["status"].asString() == "active");
    CHECK(body["endsAt"].asString() == body["end_time"].asString());
    CHECK(body["currentPrice"].asInt64() == body["current_price"].asInt64());
    CHECK(body["winnerUsername"].isNull());
    CHECK(body.isMember("description"));
    CHECK(body.isMember("created_at"));

    const auto [imageResult, imageResponse] = get(body["image_url"].asString());
    REQUIRE(imageResult == drogon::ReqResult::Ok);
    REQUIRE(imageResponse != nullptr);
    CHECK(imageResponse->statusCode() == drogon::k200OK);
    CHECK(imageResponse->getHeader("content-type").find("image/jpeg") == 0);
}

DROGON_TEST(MissingLot)
{
    const auto [result, response] = get("/api/lots/9999");
    REQUIRE(result == drogon::ReqResult::Ok);
    REQUIRE(response != nullptr);
    CHECK(response->statusCode() == drogon::k404NotFound);
    CHECK(json(response)["error"].asString() == "Lot not found");
}

DROGON_TEST(ExpiredLotLifecycle)
{
    const auto [result, response] = get("/api/lots/1000");
    REQUIRE(result == drogon::ReqResult::Ok);
    REQUIRE(response != nullptr);
    CHECK(response->statusCode() == drogon::k200OK);
    const auto body = json(response);
    CHECK(body["status"].asString() == "closed");
    CHECK(body["winnerUsername"].isNull());
    CHECK(body["endsAt"].asString() == "2026-09-12T00:00:01Z");
}

int main(int argc, char* argv[])
{
    Json::Value config;
    std::ifstream input(auction::prepareRuntime());
    input >> config;
    config["listeners"][0]["port"] = 18850;

    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto databasePath = std::filesystem::absolute(
        std::filesystem::path("runtime") / ("catalog-http-test-" + std::to_string(suffix) + ".sqlite3"));
    config["custom_config"]["database"]["path"] = databasePath.string();
    {
        auction::database::Connection database(databasePath);
        auction::database::initialize(database, "database");
        database.execute("UPDATE auctions SET status='closed',closed_at=ends_at WHERE id=999");
        database.execute("UPDATE auctions SET ends_at = '2026-09-12T00:00:01Z' WHERE id = 1000");
    }

    drogon::app().loadConfigJson(config);
    std::thread server([] { drogon::app().run(); });
    while (!drogon::app().isRunning()) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    const auto result = drogon::test::run(argc, argv);
    drogon::app().quit();
    server.join();
    std::filesystem::remove(databasePath);
    return result;
}
