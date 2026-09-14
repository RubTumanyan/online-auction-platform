#pragma once

#include "models/Profile.h"

#include <filesystem>
#include <string>
#include <vector>

namespace auction::services
{
class ProfileService final
{
  public:
    explicit ProfileService(std::filesystem::path databasePath);

    models::ProfileSummary summary(std::int64_t userId) const;
    std::vector<models::ProfileParticipation> participations(std::int64_t userId,
                                                             const std::string& filter) const;

  private:
    std::filesystem::path databasePath_;
};
}