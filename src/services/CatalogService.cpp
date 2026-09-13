#include "services/CatalogService.h"

#include "repositories/CatalogRepository.h"

#include <utility>

namespace auction::services
{
CatalogService::CatalogService(std::filesystem::path databasePath)
    : databasePath_(std::move(databasePath))
{
}

std::vector<models::Category> CatalogService::categories() const
{
    return repositories::CatalogRepository(databasePath_).categories();
}

bool CatalogService::categoryExists(std::int64_t id) const
{
    return repositories::CatalogRepository(databasePath_).categoryExists(id);
}

models::LotPage CatalogService::lots(const models::LotQuery& query) const
{
    return repositories::CatalogRepository(databasePath_).activeLots(query);
}

std::optional<models::Lot> CatalogService::lot(std::int64_t id) const
{
    return repositories::CatalogRepository(databasePath_).lotById(id);
}
}
