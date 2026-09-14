#include <drogon/drogon_test.h>

#include "database/Database.h"
#include "realtime/AuctionEvents.h"
#include "runtime/RuntimePaths.h"
#include "services/AuctionCloseService.h"
#include "services/AuthBidService.h"

#include <atomic>
#include <barrier>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>

namespace
{
std::filesystem::path testDatabasePath()
{
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::absolute(std::filesystem::path("runtime") /
        ("lifecycle-test-" + std::to_string(suffix) + ".sqlite3"));
}
}

DROGON_TEST(AutomaticClosingAndRaceSafety)
{
    const auto databasePath = testDatabasePath();
    {
        auction::database::Connection database(databasePath);
        auction::database::initialize(database, "database");
        database.execute("UPDATE auctions SET ends_at='2099-01-01T00:00:00Z' WHERE status='active'");
    }
    auction::services::AuthService(databasePath).ensureDemoUsers();
    auction::services::AuctionCloseService closer(databasePath);

    CHECK(closer.closeExpired().empty());
    {
        auction::database::Connection database(databasePath);
        CHECK(database.scalar("SELECT count(*) FROM auctions WHERE id=4 AND status='active'") == 1);
    }

    std::int64_t aliceId = 0;
    {
        auction::database::Connection database(databasePath);
        auto alice = database.prepare("SELECT id FROM users WHERE username='alice'");
        REQUIRE(alice.step());
        aliceId = alice.integer(0);
    }
    const auto placed = auction::services::BidService(databasePath).place(1, aliceId, 3175);
    CHECK(placed.currentPrice == 3175);
    {
        auction::database::Connection database(databasePath);
        database.execute("UPDATE auctions SET ends_at='2026-09-12T00:00:01Z' WHERE id=1");
    }
    const auto winnerClosures = closer.closeExpired();
    REQUIRE(winnerClosures.size() == 1);
    CHECK(winnerClosures[0].lotId == 1);
    CHECK(winnerClosures[0].currentPrice == 3175);
    REQUIRE(winnerClosures[0].winnerUsername.has_value());
    CHECK(*winnerClosures[0].winnerUsername == "alice");
    CHECK(winnerClosures[0].closedAt.size() == 20);
    CHECK(closer.closeExpired().empty());
    bool closedBidRejected = false;
    try { auction::services::BidService(databasePath).place(1, aliceId, 4000); }
    catch (const auction::services::ApiError& error)
    {
        closedBidRejected = error.kind() == auction::services::ApiErrorKind::conflict;
    }
    CHECK(closedBidRejected);

    {
        auction::database::Connection database(databasePath);
        database.execute("UPDATE auctions SET ends_at='2026-09-12T00:00:01Z' WHERE id=2");
    }
    const auto noBidClosures = closer.closeExpired();
    REQUIRE(noBidClosures.size() == 1);
    CHECK(noBidClosures[0].lotId == 2);
    CHECK(!noBidClosures[0].winnerUsername.has_value());
    {
        auction::database::Connection database(databasePath);
        CHECK(database.scalar("SELECT count(*) FROM auctions WHERE id=2 AND status='closed' AND current_winner_id IS NULL") == 1);
    }

    {
        auction::database::Connection database(databasePath);
        database.execute("UPDATE auctions SET ends_at='2026-09-12T00:00:01Z' WHERE id=3");
    }
    std::barrier start(2);
    std::atomic<bool> bidRejected{false};
    std::atomic<int> raceClosures{0};
    std::thread bidThread([&] {
        start.arrive_and_wait();
        try { auction::services::BidService(databasePath).place(3, aliceId, 3525); }
        catch (const auction::services::ApiError& error)
        {
            bidRejected = error.kind() == auction::services::ApiErrorKind::conflict;
        }
    });
    std::thread closeThread([&] {
        start.arrive_and_wait();
        raceClosures = static_cast<int>(auction::services::AuctionCloseService(databasePath).closeExpired().size());
    });
    bidThread.join();
    closeThread.join();
    CHECK(bidRejected.load());
    CHECK(raceClosures.load() == 1);
    {
        auction::database::Connection database(databasePath);
        CHECK(database.scalar("SELECT count(*) FROM bids WHERE auction_id=3") == 0);
        CHECK(database.scalar("SELECT count(*) FROM auctions WHERE id=3 AND status='closed'") == 1);
    }

    const auto bidEvent = auction::realtime::bidUpdatedEvent(1, placed);
    CHECK(bidEvent["type"].asString() == "bid_updated");
    CHECK(bidEvent["lotId"].asInt64() == 1);
    CHECK(bidEvent["currentPrice"].asInt64() == 3175);
    CHECK(bidEvent["minimumNextBid"].asInt64() == 3195);
    CHECK(bidEvent["bid"]["bidderUsername"].asString() == "alice");
    const auto closedEvent = auction::realtime::lotClosedEvent(noBidClosures[0]);
    CHECK(closedEvent["type"].asString() == "lot_closed");
    CHECK(closedEvent["status"].asString() == "closed");
    CHECK(closedEvent["winnerUsername"].isNull());

    std::filesystem::remove(databasePath);
}

int main(int argc, char* argv[])
{
    try
    {
        auction::prepareRuntime();
        return drogon::test::run(argc, argv);
    }
    catch (const std::exception& error)
    {
        std::cerr << "Lifecycle test failed: " << error.what() << '\n';
        return 1;
    }
}
