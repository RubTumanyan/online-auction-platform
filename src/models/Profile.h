#pragma once

#include "models/AuthBid.h"
#include "models/Catalog.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace auction::models
{
struct ProfileParticipation
{
    Lot lot;
    std::string participationStatus;
    std::optional<std::int64_t> userHighestBidCents;
    bool isLeading = false;
};

struct ProfileSummary
{
    User user;
    std::int64_t activeParticipations = 0;
    std::int64_t wonAuctions = 0;
    std::int64_t totalParticipations = 0;
};
}