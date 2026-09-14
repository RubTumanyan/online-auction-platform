#include "repositories/RecommendationRepository.h"

#include "database/Database.h"

namespace auction::repositories
{
RecommendationRepository::RecommendationRepository(std::filesystem::path databasePath)
    : databasePath_(std::move(databasePath))
{
}

std::vector<RecommendationRepository::LotRow> RecommendationRepository::allActiveLots() const
{
    database::Connection connection(databasePath_);
    auto statement = connection.prepare(
        "SELECT a.id, a.title, a.description, c.name"
        " FROM auctions a JOIN categories c ON c.id = a.category_id"
        " WHERE a.status = 'active'"
        " AND a.ends_at > strftime('%Y-%m-%dT%H:%M:%SZ','now')");
    std::vector<LotRow> result;
    while (statement.step())
        result.push_back({statement.integer(0), statement.text(1), statement.text(2), statement.text(3)});
    return result;
}

std::string RecommendationRepository::activeLotsVersion() const
{
    database::Connection connection(databasePath_);
    auto statement = connection.prepare(
        "SELECT count(*), COALESCE(max(updated_at),''), COALESCE(max(id),0)"
        " FROM auctions WHERE status = 'active'");
    if (!statement.step()) return "";
    return std::to_string(statement.integer(0)) + ":" +
           statement.text(1) + ":" +
           std::to_string(statement.integer(2));
}

std::vector<InteractionRecord> RecommendationRepository::userInteractions(std::int64_t userId) const
{
    database::Connection connection(databasePath_);
    // Bids are derived from the authoritative bids table (weight 5.0);
    // views come from the user_interactions log (weight 1.0).
    auto statement = connection.prepare(
        "SELECT auction_id, 'bid', 5.0 FROM bids WHERE user_id = ?"
        " UNION ALL"
        " SELECT lot_id, interaction_type, weight FROM user_interactions"
        " WHERE user_id = ? AND interaction_type = 'view'"
        " ORDER BY 1");
    statement.bind(1, userId);
    statement.bind(2, userId);
    std::vector<InteractionRecord> result;
    while (statement.step())
        result.push_back({statement.integer(0), statement.text(1), statement.real(2)});
    return result;
}

void RecommendationRepository::recordInteraction(
    std::int64_t userId, std::int64_t lotId,
    const std::string& type, double weight) const
{
    database::Connection connection(databasePath_);
    auto statement = connection.prepare(
        "INSERT INTO user_interactions(user_id, lot_id, interaction_type, weight, created_at)"
        " VALUES (?, ?, ?, ?, strftime('%Y-%m-%dT%H:%M:%SZ','now'))");
    statement.bind(1, userId);
    statement.bind(2, lotId);
    statement.bind(3, type);
    statement.bind(4, weight);
    statement.run();
}

std::vector<RecommendationRepository::InteractionCounts>
RecommendationRepository::lotsByInteraction(int limit) const
{
    database::Connection connection(databasePath_);
    auto statement = connection.prepare(
        "SELECT a.id,"
        " (SELECT count(*) FROM bids WHERE auction_id = a.id) AS bid_count,"
        " (SELECT count(*) FROM user_interactions WHERE lot_id = a.id AND interaction_type = 'view') AS view_count"
        " FROM auctions a"
        " WHERE a.status = 'active' AND a.ends_at > strftime('%Y-%m-%dT%H:%M:%SZ','now')"
        " ORDER BY bid_count DESC, view_count DESC, a.id"
        " LIMIT ?");
    statement.bind(1, static_cast<std::int64_t>(limit));
    std::vector<InteractionCounts> result;
    while (statement.step())
        result.push_back({statement.integer(0), statement.integer(1), statement.integer(2)});
    return result;
}

std::vector<models::Lot> RecommendationRepository::lotsEndingSoon(int limit) const
{
    database::Connection connection(databasePath_);
    auto statement = connection.prepare(
        "SELECT a.id, a.title, a.description, c.id, c.name, a.image_url,"
        " a.starting_price_cents, a.current_price_cents, a.created_at,"
        " a.ends_at, a.status, a.closed_at"
        " FROM auctions a JOIN categories c ON c.id = a.category_id"
        " WHERE a.status = 'active' AND a.ends_at > strftime('%Y-%m-%dT%H:%M:%SZ','now')"
        " ORDER BY a.ends_at ASC, a.id"
        " LIMIT ?");
    statement.bind(1, static_cast<std::int64_t>(limit));
    std::vector<models::Lot> result;
    while (statement.step())
    {
        models::Lot lot{
            statement.integer(0), statement.text(1), statement.text(2),
            {statement.integer(3), statement.text(4)}, statement.text(5),
            statement.integer(6), statement.integer(7), statement.text(8),
            statement.text(9), statement.text(10)};
        const auto closedAt = statement.text(11);
        if (!closedAt.empty()) lot.closedAt = closedAt;
        result.push_back(std::move(lot));
    }
    return result;
}

std::vector<models::Lot> RecommendationRepository::diverseActiveLots(int limit) const
{
    database::Connection connection(databasePath_);
    auto statement = connection.prepare(
        "SELECT a.id, a.title, a.description, c.id, c.name, a.image_url,"
        " a.starting_price_cents, a.current_price_cents, a.created_at,"
        " a.ends_at, a.status, a.closed_at"
        " FROM auctions a JOIN categories c ON c.id = a.category_id"
        " WHERE a.status = 'active' AND a.ends_at > strftime('%Y-%m-%dT%H:%M:%SZ','now')"
        " ORDER BY c.id, a.id"
        " LIMIT ?");
    statement.bind(1, static_cast<std::int64_t>(limit));
    std::vector<models::Lot> result;
    while (statement.step())
    {
        models::Lot lot{
            statement.integer(0), statement.text(1), statement.text(2),
            {statement.integer(3), statement.text(4)}, statement.text(5),
            statement.integer(6), statement.integer(7), statement.text(8),
            statement.text(9), statement.text(10)};
        const auto closedAt = statement.text(11);
        if (!closedAt.empty()) lot.closedAt = closedAt;
        result.push_back(std::move(lot));
    }
    return result;
}

bool RecommendationRepository::lotExists(std::int64_t lotId) const
{
    database::Connection connection(databasePath_);
    auto statement = connection.prepare("SELECT 1 FROM auctions WHERE id = ?");
    statement.bind(1, lotId);
    return statement.step();
}
}
