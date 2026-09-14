#include "services/AuctionCloseService.h"

#include "database/Database.h"

#include <trantor/utils/Logger.h>

#include <cstdint>
#include <utility>

namespace auction::services
{
AuctionCloseService::AuctionCloseService(std::filesystem::path databasePath)
    : databasePath_(std::move(databasePath))
{
}

std::vector<models::ClosedLot> AuctionCloseService::closeExpired() const
{
    database::Connection connection(databasePath_);
    {
        auto pending = connection.prepare(
            "SELECT 1 FROM auctions WHERE status='active' "
            "AND ends_at<=strftime('%Y-%m-%dT%H:%M:%SZ','now') LIMIT 1");
        if (!pending.step()) return {};
    }
    connection.execute("BEGIN IMMEDIATE");
    try
    {
        auto expired = connection.prepare(
            "SELECT id FROM auctions WHERE status='active' "
            "AND ends_at<=strftime('%Y-%m-%dT%H:%M:%SZ','now') ORDER BY id");
        std::vector<std::int64_t> lotIds;
        while (expired.step()) lotIds.push_back(expired.integer(0));

        std::vector<models::ClosedLot> closed;
        closed.reserve(lotIds.size());
        for (const auto lotId : lotIds)
        {
            auto update = connection.prepare(
                "UPDATE auctions SET status='closed',"
                "closed_at=strftime('%Y-%m-%dT%H:%M:%SZ','now'),"
                "current_winner_id=(SELECT b.user_id FROM bids b WHERE b.auction_id=? "
                "ORDER BY b.amount_cents DESC,b.created_at,b.id LIMIT 1),"
                "current_price_cents=COALESCE((SELECT max(b.amount_cents) FROM bids b "
                "WHERE b.auction_id=?),current_price_cents),"
                "updated_at=strftime('%Y-%m-%dT%H:%M:%SZ','now') "
                "WHERE id=? AND status='active' "
                "AND ends_at<=strftime('%Y-%m-%dT%H:%M:%SZ','now')");
            update.bind(1, lotId);
            update.bind(2, lotId);
            update.bind(3, lotId);
            update.run();

            auto result = connection.prepare(
                "SELECT a.current_price_cents,u.username,a.closed_at "
                "FROM auctions a LEFT JOIN users u ON u.id=a.current_winner_id "
                "WHERE a.id=? AND a.status='closed'");
            result.bind(1, lotId);
            if (!result.step()) continue;
            models::ClosedLot item{lotId, result.integer(0), std::nullopt, result.text(2)};
            const auto username = result.text(1);
            if (!username.empty()) item.winnerUsername = username;
            closed.push_back(std::move(item));
        }
        connection.execute("COMMIT");
        if (!closed.empty())
            LOG_INFO << "[CLOSE] Closed " << closed.size() << " expired lot(s)";
        return closed;
    }
    catch (...)
    {
        connection.execute("ROLLBACK");
        throw;
    }
}
}
