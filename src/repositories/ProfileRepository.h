#pragma once

#include "models/Profile.h"

#include <filesystem>
#include <optional>
#include <vector>

namespace auction::repositories
{
class ProfileRepository final
{
  public:
    explicit ProfileRepository(std::filesystem::path databasePath);

    std::optional<models::User> userById(std::int64_t userId) const;
    std::vector<models::ProfileParticipation> activeParticipations(std::int64_t userId) const;
    std::vector<models::ProfileParticipation> wonAuctions(std::int64_t userId) const;
    std::vector<models::ProfileParticipation> allParticipations(std::int64_t userId) const;

  private:
    std::filesystem::path databasePath_;
};
}