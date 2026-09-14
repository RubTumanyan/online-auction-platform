#include "controllers/LotWebSocketController.h"

#include "realtime/AuctionEvents.h"
#include "security/HttpSecurity.h"
#include "services/CatalogService.h"

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <unordered_map>

namespace auction
{
namespace
{
struct Subscription
{
    std::int64_t lotId;
    drogon::SubscriberID id;
    std::string ip;
};

std::atomic<std::size_t> g_totalWsConnections{0};
std::mutex g_ipMutex;
std::unordered_map<std::string, std::size_t> g_ipCounts;

bool claimWsIp(const std::string& ip, std::size_t cap)
{
    std::lock_guard lock(g_ipMutex);
    if (g_ipCounts[ip] >= cap) return false;
    ++g_ipCounts[ip];
    return true;
}

void releaseWsIp(const std::string& ip)
{
    std::lock_guard lock(g_ipMutex);
    auto it = g_ipCounts.find(ip);
    if (it == g_ipCounts.end()) return;
    if (it->second == 0) { g_ipCounts.erase(it); return; }
    if (--it->second == 0) g_ipCounts.erase(it);
}

std::filesystem::path databasePath()
{
    const auto value = drogon::app().getCustomConfig()["database"]
                           .get("path", "runtime/auction.sqlite3").asString();
    return std::filesystem::path(std::u8string(value.begin(), value.end()));
}

std::optional<std::int64_t> lotIdFromPath(std::string_view path)
{
    constexpr std::string_view prefix = "/ws/lots/";
    if (!path.starts_with(prefix)) return std::nullopt;
    path.remove_prefix(prefix.size());
    std::int64_t result = 0;
    const auto [end, error] = std::from_chars(path.data(), path.data() + path.size(), result);
    if (error != std::errc{} || end != path.data() + path.size() || result <= 0)
        return std::nullopt;
    return result;
}

void closeWithError(const drogon::WebSocketConnectionPtr& connection,
                    const std::string& message)
{
    Json::Value event;
    event["type"] = "error";
    event["error"] = message;
    connection->sendJson(event);
    LOG_WARN << "[WS] Connection rejected: " << message;
    connection->shutdown(drogon::CloseCode::kViolation, message);
}
}

void LotWebSocketController::handleNewMessage(
    const drogon::WebSocketConnectionPtr&, std::string&&,
    const drogon::WebSocketMessageType&)
{
    // This endpoint is server-to-client only. Bids stay on the authenticated HTTP API.
}

void LotWebSocketController::handleNewConnection(
    const drogon::HttpRequestPtr& request,
    const drogon::WebSocketConnectionPtr& connection)
{
    if (!security::isHostAllowed(request))
    {
        closeWithError(connection, "Not found");
        return;
    }
    const auto lotId = lotIdFromPath(request->path());
    if (!lotId)
    {
        closeWithError(connection, "Invalid lot id");
        return;
    }
    const auto total = g_totalWsConnections.load(std::memory_order_relaxed);
    if (total >= security::wsMaxConnections())
    {
        closeWithError(connection, "Too many connections");
        return;
    }
    const auto ip = security::clientIp(request);
    if (!claimWsIp(ip, security::wsMaxConnectionsPerIp()))
    {
        closeWithError(connection, "Too many connections");
        return;
    }
    g_totalWsConnections.fetch_add(1, std::memory_order_relaxed);
    try
    {
        if (!services::CatalogService(databasePath()).lot(*lotId))
        {
            closeWithError(connection, "Lot not found");
            return;
        }
        const std::weak_ptr<drogon::WebSocketConnection> weakConnection = connection;
        const auto subscriptionId = realtime::AuctionEventHub::instance().subscribe(
            *lotId, [weakConnection](const Json::Value& event) {
                if (const auto current = weakConnection.lock(); current && current->connected())
                    current->sendJson(event);
            });
        connection->setContext(std::make_shared<Subscription>(Subscription{*lotId, subscriptionId, ip}));
        LOG_INFO << "[WS] Subscribed ip=" << ip << " to lot " << *lotId
                 << " (active connections="
                 << g_totalWsConnections.load(std::memory_order_relaxed) << ")";
    }
    catch (const std::exception&)
    {
        closeWithError(connection, "Unable to subscribe to lot updates");
    }
}

void LotWebSocketController::handleConnectionClosed(
    const drogon::WebSocketConnectionPtr& connection)
{
    if (!connection->hasContext()) return;
    const auto subscription = connection->getContext<Subscription>();
    realtime::AuctionEventHub::instance().unsubscribe(subscription->lotId, subscription->id);
    g_totalWsConnections.fetch_sub(1, std::memory_order_relaxed);
    releaseWsIp(subscription->ip);
    connection->clearContext();
    LOG_INFO << "[WS] Connection closed for lot " << subscription->lotId
             << " (active connections="
             << g_totalWsConnections.load(std::memory_order_relaxed) << ")";
}
}
