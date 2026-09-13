#pragma once

#include "models/Catalog.h"

#include <filesystem>
#include <optional>
#include <vector>

namespace auction::repositories
{
class CatalogRepository final
{
  public:
    explicit CatalogRepository(std::filesystem::path databasePath);

    std::vector<models::Category> categories() const;
    bool categoryExists(std::int64_t id) const;
    models::LotPage activeLots(const models::LotQuery& query) const;
    std::optional<models::Lot> lotById(std::int64_t id) const;

  private:
    std::filesystem::path databasePath_;
};
}
