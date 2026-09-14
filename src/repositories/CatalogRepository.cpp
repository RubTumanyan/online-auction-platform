#include "repositories/CatalogRepository.h"

#include "database/Database.h"

#include <cstdint>
#include <string>
#include <utility>

namespace auction::repositories
{
namespace
{
std::string escapeLike(const std::string& value)
{
    std::string escaped;
    escaped.reserve(value.size());
    for (const char character : value)
    {
        if (character == '\\' || character == '%' || character == '_') escaped.push_back('\\');
        escaped.push_back(character);
    }
    return escaped;
}

models::Lot readLot(database::Statement& statement)
{
    models::Lot lot{
        statement.integer(0), statement.text(1), statement.text(2),
        {statement.integer(3), statement.text(4)}, statement.text(5),
        statement.integer(6), statement.integer(7), statement.text(8),
        statement.text(9), statement.text(10)};
    const auto closedAt = statement.text(11);
    if (!closedAt.empty()) lot.closedAt = closedAt;
    return lot;
}

void bindFilters(database::Statement& statement, const models::LotQuery& query)
{
    int parameter = 1;
    if (query.categoryId) statement.bind(parameter++, *query.categoryId);
    if (query.search) statement.bind(parameter++, "%" + escapeLike(*query.search) + "%");
    if (query.minPriceCents) statement.bind(parameter++, *query.minPriceCents);
    if (query.maxPriceCents) statement.bind(parameter++, *query.maxPriceCents);
}

std::string filteredSql(const models::LotQuery& query)
{
    std::string sql = " FROM auctions a JOIN categories c ON c.id = a.category_id"
                      " WHERE a.status = 'active'"
                      " AND a.ends_at > strftime('%Y-%m-%dT%H:%M:%SZ','now')";
    if (query.categoryId) sql += " AND a.category_id = ?";
    if (query.search) sql += " AND a.title LIKE ? ESCAPE '\\' COLLATE NOCASE";
    if (query.minPriceCents) sql += " AND a.current_price_cents >= ?";
    if (query.maxPriceCents) sql += " AND a.current_price_cents <= ?";
    return sql;
}
}

CatalogRepository::CatalogRepository(std::filesystem::path databasePath)
    : databasePath_(std::move(databasePath))
{
}

std::vector<models::Category> CatalogRepository::categories() const
{
    database::Connection connection(databasePath_);
    auto statement = connection.prepare("SELECT id, name FROM categories ORDER BY id");
    std::vector<models::Category> result;
    while (statement.step()) result.push_back({statement.integer(0), statement.text(1)});
    return result;
}

bool CatalogRepository::categoryExists(std::int64_t id) const
{
    database::Connection connection(databasePath_);
    auto statement = connection.prepare("SELECT 1 FROM categories WHERE id = ?");
    statement.bind(1, id);
    return statement.step();
}

models::LotPage CatalogRepository::activeLots(const models::LotQuery& query) const
{
    database::Connection connection(databasePath_);
    const auto filters = filteredSql(query);
    auto count = connection.prepare("SELECT count(*)" + filters);
    bindFilters(count, query);
    count.step();
    const auto total = count.integer(0);

    const std::string sortColumn = query.sortBy == "current_price" ? "a.current_price_cents" :
                                   query.sortBy == "title" ? "a.title COLLATE NOCASE" : "a.ends_at";
    const std::string direction = query.order == "desc" ? " DESC" : " ASC";
    auto statement = connection.prepare(
        "SELECT a.id,a.title,a.description,c.id,c.name,a.image_url,a.starting_price_cents,"
        "a.current_price_cents,a.created_at,a.ends_at,a.status,a.closed_at" + filters +
        " ORDER BY " + sortColumn + direction + ", a.id" + direction + " LIMIT ? OFFSET ?");
    bindFilters(statement, query);
    int parameter = 1 + (query.categoryId ? 1 : 0) + (query.search ? 1 : 0) +
                    (query.minPriceCents ? 1 : 0) + (query.maxPriceCents ? 1 : 0);
    statement.bind(parameter++, static_cast<std::int64_t>(query.limit));
    statement.bind(parameter, static_cast<std::int64_t>(query.page - 1) * query.limit);

    std::vector<models::Lot> lots;
    while (statement.step()) lots.push_back(readLot(statement));
    const auto totalPages = total == 0 ? 0 : (total + query.limit - 1) / query.limit;
    return {query.page, query.limit, total, totalPages, std::move(lots)};
}

std::optional<models::Lot> CatalogRepository::lotById(std::int64_t id) const
{
    database::Connection connection(databasePath_);
    auto statement = connection.prepare(
        "SELECT a.id,a.title,a.description,c.id,c.name,a.image_url,a.starting_price_cents,"
        "a.current_price_cents,a.created_at,a.ends_at,"
        "CASE WHEN a.status='active' AND a.ends_at<=strftime('%Y-%m-%dT%H:%M:%SZ','now') "
        "THEN 'closed' ELSE a.status END,a.closed_at,u.username"
        " FROM auctions a JOIN categories c ON c.id = a.category_id"
        " LEFT JOIN users u ON u.id=a.current_winner_id WHERE a.id = ?");
    statement.bind(1, id);
    if (!statement.step()) return std::nullopt;
    auto lot = readLot(statement);
    const auto bidder = statement.text(12);
    if (!bidder.empty()) lot.highestBidderUsername = bidder;
    return lot;
}
}
