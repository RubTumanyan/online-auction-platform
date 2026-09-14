#include <drogon/drogon.h>
#include <drogon/drogon_test.h>
#include <drogon/WebSocketClient.h>

#include "database/Database.h"
#include "realtime/AuctionEvents.h"
#include "runtime/RuntimePaths.h"
#include "security/HttpSecurity.h"
#include "services/AuctionCloseService.h"
#include "services/AuthBidService.h"

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace
{
constexpr auto kBaseUrl = "http://127.0.0.1:18852";

std::mutex g_messageMutex;
std::condition_variable g_messageCv;
std::vector<std::string> g_messages;

void onMessage(std::string&& message, const drogon::WebSocketClientPtr&,
               const drogon::WebSocketMessageType& type)
{
    if (type != drogon::WebSocketMessageType::Text) return;
    std::lock_guard lock(g_messageMutex);
    g_messages.push_back(std::move(message));
    g_messageCv.notify_all();
}

std::optional<std::string> nextMessage(std::chrono::seconds timeout)
{
    std::unique_lock lock(g_messageMutex);
    if (!g_messageCv.wait_for(lock, timeout, [] { return !g_messages.empty(); }))
        return std::nullopt;
    auto message = g_messages.front();
    g_messages.erase(g_messages.begin());
    return message;
}

std::pair<drogon::ReqResult, drogon::HttpResponsePtr> placeBid(const std::string& token,
                                                                std::int64_t amount)
{
    auto client = drogon::HttpClient::newHttpClient(kBaseUrl);
    auto request = drogon::HttpRequest::newHttpRequest();
    request->setMethod(drogon::Post);
    request->setPath("/api/lots/1/bids");
    request->addHeader("Authorization", "Bearer " + token);
    request->setContentTypeCode(drogon::CT_APPLICATION_JSON);
    Json::Value body;
    body["amount"] = Json::Int64(amount);
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";
    request->setBody(Json::writeString(writer, body));
    return client->sendRequest(request, 5.0);
}

std::filesystem::path g_databasePath;
}

DROGON_TEST(WebSocketReceivesBidAndClosureEvents)
{
    const auto& databasePath = g_databasePath;
    const auction::services::AuthService auth(databasePath);
    const auto token = auth.login("alice", "Alice123!").token;

    auto client = drogon::WebSocketClient::newWebSocketClient("127.0.0.1", 18852);
    client->setMessageHandler(onMessage);
    auto request = drogon::HttpRequest::newHttpRequest();
    request->setPath("/ws/lots/1");

    std::promise<bool> connected;
    client->connectToServer(request, [&](drogon::ReqResult result,
                                         const drogon::HttpResponsePtr&,
                                         const drogon::WebSocketClientPtr&) {
        connected.set_value(result == drogon::ReqResult::Ok);
    });
    CHECK(connected.get_future().get());

    const auto [bidResult, bidResponse] = placeBid(token, 3000);
    REQUIRE(bidResult == drogon::ReqResult::Ok);
    REQUIRE(bidResponse != nullptr);
    CHECK(bidResponse->statusCode() == drogon::k201Created);

    const auto bidMessage = nextMessage(std::chrono::seconds(10));
    REQUIRE(bidMessage.has_value());
    Json::Value bidEvent;
    Json::Reader reader;
    REQUIRE(reader.parse(*bidMessage, bidEvent));
    CHECK(bidEvent["type"].asString() == "bid_updated");
    CHECK(bidEvent["lotId"].asInt64() == 1);
    CHECK(bidEvent["currentPrice"].asInt64() == 3000);
    CHECK(bidEvent["bid"]["amount"].asInt64() == 3000);
    CHECK(bidEvent["bid"]["bidderUsername"].asString() == "alice");

    {
        auction::database::Connection database(databasePath);
        database.execute("UPDATE auctions SET starts_at='2026-09-01T00:00:00Z',"
                         " ends_at='2026-09-12T00:00:01Z' WHERE id=1");
    }
    const auto closures = auction::services::AuctionCloseService(databasePath).closeExpired();
    REQUIRE(closures.size() == 1);
    REQUIRE(closures[0].winnerUsername.has_value());
    auction::realtime::AuctionEventHub::instance().publish(closures[0].lotId,
        auction::realtime::lotClosedEvent(closures[0]));

    const auto closeMessage = nextMessage(std::chrono::seconds(10));
    REQUIRE(closeMessage.has_value());
    Json::Value closeEvent;
    REQUIRE(reader.parse(*closeMessage, closeEvent));
    CHECK(closeEvent["type"].asString() == "lot_closed");
    CHECK(closeEvent["lotId"].asInt64() == 1);
    CHECK(closeEvent["status"].asString() == "closed");
    CHECK(closeEvent["winnerUsername"].asString() == "alice");
    CHECK(closeEvent["currentPrice"].asInt64() == 3000);

    client->stop();
}

int main(int argc, char* argv[])
{
    Json::Value config;
    std::ifstream input(auction::prepareRuntime());
    input >> config;
    config["listeners"][0]["port"] = 18852;

    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto databasePath = std::filesystem::absolute(
        std::filesystem::path("runtime") / ("ws-live-test-" + std::to_string(suffix) + ".sqlite3"));
    config["custom_config"]["database"]["path"] = databasePath.string();
    {
        auction::database::Connection database(databasePath);
        auction::database::initialize(database, "database");
        database.execute("UPDATE auctions SET starting_price_cents=1000,current_price_cents=1000,"
                         "current_winner_id=NULL,status='active',starts_at='2026-09-01T00:00:00Z',"
                         "ends_at='2099-01-01T00:00:00Z' WHERE id=1");
        database.execute("DELETE FROM bids WHERE auction_id=1");
    }
    auction::services::AuthService(databasePath).ensureDemoUsers();

    auction::security::relaxSecurityForTests(config);
    drogon::app().loadConfigJson(config);
    auction::security::registerHttpSecurity();
    g_databasePath = databasePath;
    std::thread server([] { drogon::app().run(); });
    while (!drogon::app().isRunning()) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    const auto result = drogon::test::run(argc, argv);
    drogon::app().quit();
    server.join();
    std::filesystem::remove(databasePath);
    return result;
}