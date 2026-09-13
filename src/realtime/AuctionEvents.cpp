#include "realtime/AuctionEvents.h"

#include <string>
#include <utility>

namespace auction::realtime
{
Json::Value bidUpdatedEvent(std::int64_t lotId, const models::PlacedBid& placed)
{
    Json::Value event;
    event["type"] = "bid_updated";
    event["lotId"] = Json::Int64(lotId);
    event["currentPrice"] = Json::Int64(placed.currentPrice);
    event["minimumNextBid"] = Json::Int64(placed.currentPrice + kMinimumBidStep);
    event["bid"]["id"] = Json::Int64(placed.bid.id);
    event["bid"]["bidderUsername"] = placed.bid.bidderUsername;
    event["bid"]["amount"] = Json::Int64(placed.bid.amount);
    event["bid"]["createdAt"] = placed.bid.createdAt;
    return event;
}

Json::Value lotClosedEvent(const models::ClosedLot& closed)
{
    Json::Value event;
    event["type"] = "lot_closed";
    event["lotId"] = Json::Int64(closed.lotId);
    event["status"] = "closed";
    event["currentPrice"] = Json::Int64(closed.currentPrice);
    if (closed.winnerUsername) event["winnerUsername"] = *closed.winnerUsername;
    else event["winnerUsername"] = Json::nullValue;
    event["closedAt"] = closed.closedAt;
    return event;
}

AuctionEventHub& AuctionEventHub::instance()
{
    static AuctionEventHub hub;
    return hub;
}

drogon::SubscriberID AuctionEventHub::subscribe(std::int64_t lotId, Handler handler)
{
    return events_.subscribe(std::to_string(lotId),
        [handler = std::move(handler)](const std::string&, const Json::Value& event) {
            handler(event);
        });
}

void AuctionEventHub::unsubscribe(std::int64_t lotId, drogon::SubscriberID id)
{
    events_.unsubscribe(std::to_string(lotId), id);
}

void AuctionEventHub::publish(std::int64_t lotId, const Json::Value& event) const
{
    events_.publish(std::to_string(lotId), event);
}
}
