#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace auction
{
inline constexpr std::int64_t kMinimumBidStepCents = 20;
}

namespace auction::models
{
struct ClosedLot
{
    std::int64_t lotId;
    std::int64_t currentPrice;
    std::optional<std::string> winnerUsername;
    std::string closedAt;
};
}
