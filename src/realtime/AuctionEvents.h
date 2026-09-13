#pragma once

#include "models/AuctionLifecycle.h"
#include "models/AuthBid.h"

#include <drogon/PubSubService.h>
#include <json/value.h>

#include <cstdint>
#include <functional>

namespace auction::realtime
{
constexpr std::int64_t kMinimumBidStep = 500;

Json::Value bidUpdatedEvent(std::int64_t lotId, const models::PlacedBid& placed);
Json::Value lotClosedEvent(const models::ClosedLot& closed);

class AuctionEventHub final
{
  public:
    using Handler = std::function<void(const Json::Value&)>;

    static AuctionEventHub& instance();
    drogon::SubscriberID subscribe(std::int64_t lotId, Handler handler);
    void unsubscribe(std::int64_t lotId, drogon::SubscriberID id);
    void publish(std::int64_t lotId, const Json::Value& event) const;

  private:
    drogon::PubSubService<Json::Value> events_;
};
}
