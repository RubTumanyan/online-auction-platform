#include <drogon/drogon.h>
#include <drogon/drogon_test.h>

#include "runtime/RuntimePaths.h"

#include <fstream>
#include <future>
#include <iterator>
#include <string>
#include <thread>

DROGON_TEST(HealthEndpoint)
{
    auto client = drogon::HttpClient::newHttpClient("http://127.0.0.1:18849");
    auto request = drogon::HttpRequest::newHttpRequest();
    request->setPath("/api/health");
    const auto [result, response] = client->sendRequest(request, 5.0);
    REQUIRE(result == drogon::ReqResult::Ok);
    REQUIRE(response != nullptr);
    CHECK(response->statusCode() == drogon::k200OK);
    CHECK(response->getHeader("content-type").find("application/json") == 0);
    const auto json = response->getJsonObject();
    REQUIRE(json != nullptr);
    CHECK(json->size() == 1);
    CHECK((*json)["status"].asString() == "ok");
}

DROGON_TEST(StaticHomePage)
{
    auto client = drogon::HttpClient::newHttpClient("http://127.0.0.1:18849");
    auto request = drogon::HttpRequest::newHttpRequest();
    request->setPath("/");
    const auto [result, response] = client->sendRequest(request, 5.0);
    REQUIRE(result == drogon::ReqResult::Ok);
    REQUIRE(response != nullptr);
    CHECK(response->statusCode() == drogon::k200OK);
    CHECK(response->getHeader("content-type").find("text/html") == 0);
    std::ifstream page("public/index.html", std::ios::binary);
    REQUIRE(page.is_open());
    const std::string expected((std::istreambuf_iterator<char>(page)),
                               std::istreambuf_iterator<char>());
    CHECK(response->body() == expected);
}

int main(int argc, char* argv[])
{
    // Use the real runtime configuration, with a separate test-only port.
    Json::Value config;
    std::ifstream input(auction::prepareRuntime());
    input >> config;
    config["listeners"][0]["port"] = 18849;
    drogon::app().loadConfigJson(config);

    std::promise<void> ready;
    auto started = ready.get_future();
    drogon::app().getLoop()->queueInLoop([&ready] { ready.set_value(); });
    std::jthread server([] { drogon::app().run(); });
    started.wait();
    const int result = drogon::test::run(argc, argv);
    drogon::app().quit();
    return result;
}
