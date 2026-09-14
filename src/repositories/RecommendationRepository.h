#pragma once

#include "models/Catalog.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace auction::repositories
{
struct InteractionRecord
{
    std::int64_t lotId;
    std::string type;
    double weight;
};

class RecommendationRepository final
{
  public:
    explicit RecommendationRepository(std::filesystem::path databasePath);

    struct LotRow
    {
        std::int64_t id;
        std::string title;
        std::string description;
        std::string categoryName;
    };

    std::vector<LotRow> allActiveLots() const;
    std::string activeLotsVersion() const;
    std::vector<InteractionRecord> userInteractions(std::int64_t userId) const;
    void recordInteraction(std::int64_t userId, std::int64_t lotId,
                           const std::string& type, double weight) const;

    struct InteractionCounts
    {
        std::int64_t lotId;
        std::int64_t bidCount;
        std::int64_t viewCount;
    };

    std::vector<InteractionCounts> lotsByInteraction(int limit) const;
    std::vector<models::Lot> lotsEndingSoon(int limit) const;
    std::vector<models::Lot> diverseActiveLots(int limit) const;
    bool lotExists(std::int64_t lotId) const;

  private:
    std::filesystem::path databasePath_;
};
}
