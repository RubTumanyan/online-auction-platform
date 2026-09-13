#include "controllers/LotWebSocketController.h"

#include "realtime/AuctionEvents.h"
#include "services/CatalogService.h"

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>

namespace auction
{
namespace
{
struct Subscription
{
    std::int64_t lotId;
    drogon::SubscriberID id;
};

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
    const auto lotId = lotIdFromPath(request->path());
    if (!lotId)
    {
        closeWithError(connection, "Invalid lot id");
        return;
    }
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
        connection->setContext(std::make_shared<Subscription>(Subscription{*lotId, subscriptionId}));
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
    connection->clearContext();
}
}
