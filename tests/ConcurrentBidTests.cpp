#include <drogon/drogon_test.h>

#include "database/Database.h"
#include "runtime/RuntimePaths.h"
#include "services/AuthBidService.h"

#include <atomic>
#include <barrier>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>
#include <vector>

namespace
{
std::filesystem::path testDatabasePath()
{
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::absolute(std::filesystem::path("runtime") /
        ("concurrent-bid-test-" + std::to_string(suffix) + ".sqlite3"));
}
}

DROGON_TEST(SimultaneousBidsSettleAtomically)
{
    const auto databasePath = testDatabasePath();
    {
        auction::database::Connection database(databasePath);
        auction::database::initialize(database, "database");
        database.execute("UPDATE auctions SET starting_price_cents=1000,current_price_cents=1000,"
                         "current_winner_id=NULL,status='active',starts_at='2026-09-01T00:00:00Z',"
                         "ends_at='2099-01-01T00:00:00Z' WHERE id=1");
        database.execute("DELETE FROM bids WHERE auction_id=1");
    }
    auction::services::AuthService(databasePath).ensureDemoUsers();

    auto userIdOf = [&](const char* username) {
        auction::database::Connection database(databasePath);
        auto statement = database.prepare("SELECT id FROM users WHERE username=?");
        statement.bind(1, std::string(username));
        statement.step();
        return statement.integer(0);
    };
    const auto aliceId = userIdOf("alice");
    const auto bobId = userIdOf("bob");
    REQUIRE(aliceId != bobId);

    std::barrier start(2);
    std::atomic<int> accepted{0};
    std::atomic<int> rejected{0};
    std::atomic<std::int64_t> winnerId{0};
    auto slam = [&](std::int64_t id) {
        start.arrive_and_wait();
        try
        {
            auction::services::BidService(databasePath).place(1, id, 3000);
            accepted.fetch_add(1);
            winnerId.store(id);
        }
        catch (const auction::services::ApiError& error)
        {
            if (error.kind() == auction::services::ApiErrorKind::conflict)
                rejected.fetch_add(1);
        }
    };
    std::thread alice(slam, aliceId);
    std::thread bob(slam, bobId);
    alice.join();
    bob.join();

    CHECK(accepted.load() == 1);
    CHECK(rejected.load() == 1);
    CHECK(winnerId.load() != 0);

    {
        auction::database::Connection database(databasePath);
        CHECK(database.scalar("SELECT count(*) FROM bids WHERE auction_id=1") == 1);
        CHECK(database.scalar("SELECT current_price_cents FROM auctions WHERE id=1") == 3000);
        CHECK(database.scalar("SELECT current_winner_id FROM auctions WHERE id=1") == winnerId.load());
    }

    std::filesystem::remove(databasePath);
}

DROGON_TEST(SequentialBidsOnSameLotAdvancePrice)
{
    const auto databasePath = testDatabasePath();
    {
        auction::database::Connection database(databasePath);
        auction::database::initialize(database, "database");
        database.execute("UPDATE auctions SET starting_price_cents=1000,current_price_cents=1000,"
                         "current_winner_id=NULL,status='active',starts_at='2026-09-01T00:00:00Z',"
                         "ends_at='2099-01-01T00:00:00Z' WHERE id=2");
        database.execute("DELETE FROM bids WHERE auction_id=2");
    }
    auction::services::AuthService(databasePath).ensureDemoUsers();
    auto userIdOf = [&](const char* username) {
        auction::database::Connection database(databasePath);
        auto statement = database.prepare("SELECT id FROM users WHERE username=?");
        statement.bind(1, std::string(username));
        statement.step();
        return statement.integer(0);
    };
    const auto aliceId = userIdOf("alice");
    const auto bobId = userIdOf("bob");

    auction::services::BidService service(databasePath);
    CHECK(service.place(2, aliceId, 3000).currentPrice == 3000);
    CHECK(service.place(2, bobId, 3020).currentPrice == 3020);
    CHECK(service.place(2, aliceId, 3050).currentPrice == 3050);

    bool staleBidRejected = false;
    try { service.place(2, bobId, 3050); }
    catch (const auction::services::ApiError& error)
    {
        staleBidRejected = error.kind() == auction::services::ApiErrorKind::conflict;
    }
    CHECK(staleBidRejected);

    {
        auction::database::Connection database(databasePath);
        CHECK(database.scalar("SELECT count(*) FROM bids WHERE auction_id=2") == 3);
        CHECK(database.scalar("SELECT current_price_cents FROM auctions WHERE id=2") == 3050);
    }

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
        std::cerr << "Concurrent bid test failed: " << error.what() << '\n';
        return 1;
    }
}