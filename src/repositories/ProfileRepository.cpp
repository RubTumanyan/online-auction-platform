#include "repositories/ProfileRepository.h"

#include "database/Database.h"

#include <cstdint>
#include <string>
#include <utility>

namespace auction::repositories
{
namespace
{
constexpr const char* kSelectParticipation =
    "SELECT a.id,a.title,a.description,c.id,c.name,a.image_url,"
    "a.starting_price_cents,a.current_price_cents,a.created_at,a.ends_at,"
    "CASE WHEN a.status='active' AND a.ends_at<=strftime('%Y-%m-%dT%H:%M:%SZ','now')"
    " THEN 'closed' ELSE a.status END,"
    "COALESCE(a.closed_at,''),COALESCE(u.username,''),COALESCE(a.current_winner_id,0),"
    "MAX(b.amount_cents)"
    " FROM auctions a JOIN categories c ON c.id=a.category_id"
    " JOIN bids b ON b.auction_id=a.id AND b.user_id=?"
    " LEFT JOIN users u ON u.id=a.current_winner_id";

models::ProfileParticipation readParticipation(database::Statement& statement, std::int64_t userId)
{
    models::ProfileParticipation participation;
    auto& lot = participation.lot;
    lot = models::Lot{
        statement.integer(0), statement.text(1), statement.text(2),
        {statement.integer(3), statement.text(4)}, statement.text(5),
        statement.integer(6), statement.integer(7), statement.text(8),
        statement.text(9), statement.text(10)};
    const auto closedAt = statement.text(11);
    if (!closedAt.empty()) lot.closedAt = closedAt;
    const auto winnerUsername = statement.text(12);
    if (!winnerUsername.empty()) lot.highestBidderUsername = winnerUsername;
    const auto winnerId = statement.integer(13);
    const auto userHighestBid = statement.integer(14);
    if (userHighestBid > 0) participation.userHighestBidCents = userHighestBid;
    if (lot.status == "closed")
        participation.participationStatus = winnerId == userId ? "won" : "lost";
    else
    {
        participation.participationStatus = "active";
        participation.isLeading = winnerId == userId;
    }
    return participation;
}
}

ProfileRepository::ProfileRepository(std::filesystem::path databasePath)
    : databasePath_(std::move(databasePath))
{
}

std::optional<models::User> ProfileRepository::userById(std::int64_t userId) const
{
    database::Connection connection(databasePath_);
    auto statement = connection.prepare(
        "SELECT id,username,email,email_verified FROM users WHERE id = ?");
    statement.bind(1, userId);
    if (!statement.step()) return std::nullopt;
    return models::User{statement.integer(0), statement.text(1), statement.text(2),
                        statement.integer(3) != 0};
}

std::vector<models::ProfileParticipation> ProfileRepository::activeParticipations(
    std::int64_t userId) const
{
    database::Connection connection(databasePath_);
    auto statement = connection.prepare(
        std::string(kSelectParticipation) +
        " WHERE a.status='active'"
        " AND a.ends_at>strftime('%Y-%m-%dT%H:%M:%SZ','now')"
        " GROUP BY a.id ORDER BY a.ends_at ASC,a.id");
    statement.bind(1, userId);
    std::vector<models::ProfileParticipation> result;
    while (statement.step()) result.push_back(readParticipation(statement, userId));
    return result;
}

std::vector<models::ProfileParticipation> ProfileRepository::wonAuctions(
    std::int64_t userId) const
{
    database::Connection connection(databasePath_);
    auto statement = connection.prepare(
        std::string(kSelectParticipation) +
        " WHERE a.status='closed' AND a.current_winner_id=?"
        " GROUP BY a.id ORDER BY a.closed_at DESC,a.id DESC");
    statement.bind(1, userId);
    statement.bind(2, userId);
    std::vector<models::ProfileParticipation> result;
    while (statement.step()) result.push_back(readParticipation(statement, userId));
    return result;
}

std::vector<models::ProfileParticipation> ProfileRepository::allParticipations(
    std::int64_t userId) const
{
    database::Connection connection(databasePath_);
    auto statement = connection.prepare(
        std::string(kSelectParticipation) +
        " GROUP BY a.id ORDER BY a.ends_at DESC,a.id DESC");
    statement.bind(1, userId);
    std::vector<models::ProfileParticipation> result;
    while (statement.step()) result.push_back(readParticipation(statement, userId));
    return result;
}
}