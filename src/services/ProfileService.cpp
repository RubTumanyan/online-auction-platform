#include "services/ProfileService.h"

#include "repositories/ProfileRepository.h"
#include "services/AuthBidService.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace auction::services
{
ProfileService::ProfileService(std::filesystem::path databasePath)
    : databasePath_(std::move(databasePath))
{
}

models::ProfileSummary ProfileService::summary(std::int64_t userId) const
{
    repositories::ProfileRepository repository(databasePath_);
    const auto user = repository.userById(userId);
    if (!user) throw ApiError(ApiErrorKind::unauthorized, "User does not exist");
    models::ProfileSummary result;
    result.user = *user;
    result.activeParticipations =
        static_cast<std::int64_t>(repository.activeParticipations(userId).size());
    result.wonAuctions = static_cast<std::int64_t>(repository.wonAuctions(userId).size());
    result.totalParticipations =
        static_cast<std::int64_t>(repository.allParticipations(userId).size());
    return result;
}

std::vector<models::ProfileParticipation> ProfileService::participations(
    std::int64_t userId, const std::string& filter) const
{
    repositories::ProfileRepository repository(databasePath_);
    if (filter == "active") return repository.activeParticipations(userId);
    if (filter == "won") return repository.wonAuctions(userId);
    if (filter == "all") return repository.allParticipations(userId);
    throw ApiError(ApiErrorKind::invalid, "filter must be active, won, or all");
}
}