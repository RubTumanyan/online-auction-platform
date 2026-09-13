#pragma once

#include "models/Catalog.h"

#include <filesystem>
#include <optional>
#include <vector>

namespace auction::services
{
class CatalogService final
{
  public:
    explicit CatalogService(std::filesystem::path databasePath);

    std::vector<models::Category> categories() const;
    bool categoryExists(std::int64_t id) const;
    models::LotPage lots(const models::LotQuery& query) const;
    std::optional<models::Lot> lot(std::int64_t id) const;

  private:
    std::filesystem::path databasePath_;
};
}
