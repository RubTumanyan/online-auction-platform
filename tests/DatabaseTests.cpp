#include <drogon/drogon_test.h>
#include <json/json.h>
#include <cctype>
#include <drogon/utils/Utilities.h>

#include "database/Database.h"
#include "runtime/RuntimePaths.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <string>

using auction::database::Connection;
using auction::database::initialize;

namespace
{
bool rejected(Connection& connection, const std::string& sql)
{
    try { connection.execute(sql); }
    catch (const std::exception&) { return true; }
    return false;
}

std::string snapshot(Connection& connection)
{
    auto rows = connection.prepare("SELECT * FROM auctions ORDER BY id");
    std::string result;
    while (rows.step())
    {
        for (int column = 0; column < 13; ++column)
        {
            const auto value = rows.text(column);
            result += std::to_string(value.size()) + ":" + value;
        }
    }
    return result;
}

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Missing local image/resource: " + path.string());
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

int verifyImages()
{
    Connection connection(":memory:");
    initialize(connection, "database");
    Json::Value manifest;
    std::ifstream input("database/image_manifest.json");
    input >> manifest;
    if (!manifest.isArray() || manifest.size() != 1000)
        throw std::runtime_error("Image manifest is incomplete. Run tools/import_commons_images.py, then rebuild.");
    std::set<std::string> sourceUrls, sourceHashes, contentHashes, paths;
    for (const auto& record : manifest)
    {
        const auto local = record["local_path"].asString();
        const auto path = std::filesystem::path(local);
        if (path.is_absolute() || local.find("..") != std::string::npos || !local.starts_with("public/images/products/"))
            throw std::runtime_error("Non-local manifest image path");
        const auto contents = readFile(path);
        const auto hash = drogon::utils::getSha256(contents);
        auto expected = record["sha256"].asString();
        // Drogon represents this digest as uppercase hex.
        for (auto& character : expected) character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
        if (hash != expected) throw std::runtime_error("Image checksum mismatch: " + local);
        if (contents.size() < 4 || static_cast<unsigned char>(contents[0]) != 0xff ||
            static_cast<unsigned char>(contents[1]) != 0xd8)
            throw std::runtime_error("Expected an imported JPEG: " + local);
        if (record["width"].asInt() <= 0 || record["width"].asInt() > 480 ||
            record["height"].asInt() <= 0 || record["height"].asInt() > 320)
            throw std::runtime_error("Image exceeds thumbnail limits: " + local);
        if (record["author"].asString().empty() || record["license"].asString().empty())
            throw std::runtime_error("Missing attribution: " + local);
        if (!sourceUrls.insert(record["original_url"].asString()).second ||
            !sourceHashes.insert(record["source_sha1"].asString()).second ||
            !contentHashes.insert(hash).second || !paths.insert("/" + local.substr(7)).second)
            throw std::runtime_error("Duplicate image source or file");
        auto auction = connection.prepare("SELECT image_url, category_id FROM auctions WHERE id = ?");
        auction.bind(1, record["auction_id"].asInt64());
        if (!auction.step() || auction.text(0) != "/" + local.substr(7))
            throw std::runtime_error("Incorrect manifest/auction mapping");
    }
    std::size_t actual = 0;
    for (const auto& item : std::filesystem::directory_iterator("public/images/products"))
        if (item.is_regular_file()) ++actual;
    if (actual != 1000) throw std::runtime_error("Product directory must contain exactly 1000 files");
    auto images = connection.prepare("SELECT image_url FROM auctions");
    while (images.step())
    {
        if (!paths.contains(images.text(0))) throw std::runtime_error("Auction has no verified image");
    }
    std::cout << "1000 unique local images verified against seeded auctions and manifest.\n";
    return 0;
}
}

DROGON_TEST(DatabaseInitialization)
{
    Connection connection(":memory:");
    CHECK(connection.scalar("PRAGMA foreign_keys") == 1);
    initialize(connection, "database");
    CHECK(connection.scalar("PRAGMA user_version") == 2);
    CHECK(connection.scalar("SELECT count(*) FROM categories") == 10);
    CHECK(connection.scalar("SELECT count(*) FROM auctions") == 1000);
    CHECK(connection.scalar("SELECT count(*) FROM users") == 0);
    CHECK(connection.scalar("SELECT count(*) FROM bids") == 0);
    CHECK(connection.scalar("SELECT count(*) FROM (SELECT category_id FROM auctions GROUP BY category_id HAVING count(*) = 100)") == 10);
    CHECK(connection.scalar("SELECT count(*) FROM auctions WHERE status = 'active'") == 1000);
    CHECK(connection.scalar("SELECT count(DISTINCT image_url) FROM auctions") == 1000);
    CHECK(connection.scalar("SELECT count(*) FROM auctions WHERE current_price_cents = starting_price_cents AND current_winner_id IS NULL") == 1000);
    CHECK(connection.scalar("SELECT count(*) FROM auctions WHERE ends_at > strftime('%Y-%m-%dT%H:%M:%SZ','now')") == 1000);
    CHECK(connection.scalar("SELECT count(*) FROM sqlite_master WHERE type = 'index' AND name LIKE 'idx_%'") == 8);
    auto foreignKeys = connection.prepare("PRAGMA foreign_key_check");
    CHECK(!foreignKeys.step());
    auto integrity = connection.prepare("PRAGMA integrity_check");
    REQUIRE(integrity.step());
    CHECK(integrity.text(0) == "ok");
}

DROGON_TEST(IdempotentDeterministicSeed)
{
    Connection first(":memory:");
    initialize(first, "database");
    const auto original = snapshot(first);
    initialize(first, "database");
    CHECK(snapshot(first) == original);
    Connection second(":memory:");
    CHECK(second.scalar("PRAGMA foreign_keys") == 1);
    initialize(second, "database");
    CHECK(snapshot(second) == original);
    // Reinitialization must not undo legitimate later-phase database changes.
    first.execute("UPDATE auctions SET status = 'ended' WHERE id = 1");
    initialize(first, "database");
    CHECK(first.scalar("SELECT count(*) FROM auctions") == 1000);
    CHECK(first.scalar("SELECT count(*) FROM auctions WHERE status = 'ended'") == 1);
}

DROGON_TEST(ConstraintsAndPreparedStatements)
{
    Connection connection(":memory:");
    initialize(connection, "database");
    CHECK(rejected(connection, "UPDATE auctions SET category_id = 9999 WHERE id = 1"));
    CHECK(rejected(connection, "UPDATE auctions SET current_winner_id = 9999 WHERE id = 1"));
    CHECK(rejected(connection, "INSERT INTO bids(auction_id,user_id,amount_cents,created_at) VALUES(1,9999,5000,'2026-09-12T00:00:00Z')"));
    CHECK(rejected(connection, "UPDATE auctions SET current_price_cents = 1 WHERE id = 1"));
    CHECK(rejected(connection, "UPDATE auctions SET starting_price_cents = 0 WHERE id = 1"));
    CHECK(rejected(connection, "UPDATE auctions SET current_price_cents = 5000.5 WHERE id = 1"));
    CHECK(rejected(connection, "UPDATE auctions SET ends_at = starts_at WHERE id = 1"));
    CHECK(rejected(connection, "UPDATE auctions SET ends_at = '2026-02-30T00:00:00Z' WHERE id = 1"));
    CHECK(rejected(connection, "UPDATE auctions SET ends_at = '2027-01-01T00:00:00+03:00' WHERE id = 1"));
    CHECK(rejected(connection, "UPDATE auctions SET title = '   ' WHERE id = 1"));
    CHECK(rejected(connection, "UPDATE auctions SET description = NULL WHERE id = 1"));
    CHECK(rejected(connection, "UPDATE auctions SET status = 'pending' WHERE id = 1"));
    CHECK(rejected(connection, "UPDATE auctions SET image_url = 'https://example.com/photo.jpg' WHERE id = 1"));
    CHECK(rejected(connection, "UPDATE auctions SET image_url = '/images/products/../private.jpg' WHERE id = 1"));
    CHECK(rejected(connection, "INSERT INTO categories(name,slug) VALUES('Clothing','duplicate')"));
    CHECK(rejected(connection, "DELETE FROM categories WHERE id = 1"));
    auto insert = connection.prepare("INSERT INTO users(display_name,email,password_hash,created_at,updated_at,username) VALUES(?,?,?,'2026-09-12T00:00:00Z','2026-09-12T00:00:00Z',?)");
    insert.bind(1, std::string("O'Brien'); DROP TABLE auctions; --"));
    insert.bind(2, std::string("test@example.invalid"));
    insert.bind(3, std::string("test-only-not-a-real-password-hash"));
    insert.bind(4, std::string("sql_test_user"));
    insert.run();
    CHECK(connection.scalar("SELECT count(*) FROM auctions") == 1000);
    CHECK(connection.scalar("SELECT count(*) FROM users") == 1);
    CHECK(rejected(connection, "INSERT INTO bids(auction_id,user_id,amount_cents,created_at) VALUES(9999,1,5000,'2026-09-12T00:00:00Z')"));
    CHECK(rejected(connection, "INSERT INTO bids(auction_id,user_id,amount_cents,created_at) VALUES(1,1,0,'2026-09-12T00:00:00Z')"));
}

DROGON_TEST(AtomicRollback)
{
    Connection connection(":memory:");
    // Missing resources must leave neither a version marker nor partial schema.
    CHECK(rejected(connection, "SELECT * FROM categories"));
    bool failed = false;
    try { initialize(connection, "database/nonexistent"); }
    catch (const std::exception&) { failed = true; }
    CHECK(failed);
    CHECK(connection.scalar("PRAGMA user_version") == 0);
    CHECK(connection.scalar("SELECT count(*) FROM sqlite_master WHERE type = 'table'") == 0);
    initialize(connection, "database");
    CHECK(connection.scalar("SELECT count(*) FROM auctions") == 1000);
}

DROGON_TEST(SeedFailureRollsBackSchema)
{
    const auto name = "broken-seed-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto directory = std::filesystem::path("runtime") / name;
    std::filesystem::create_directories(directory);
    std::filesystem::copy_file("database/schema.sql", directory / "schema.sql");
    std::filesystem::copy_file("database/seed_categories.json", directory / "seed_categories.json");
    Json::Value seed;
    { std::ifstream input("database/seed_auctions.json"); input >> seed; }
    seed[1]["category_id"] = 9999;  // Fail after inserting all categories and the first auction.
    { std::ofstream output(directory / "seed_auctions.json"); output << seed; }
    Connection connection(":memory:");
    bool failed = false;
    try { initialize(connection, directory); }
    catch (const std::exception&) { failed = true; }
    CHECK(failed);
    CHECK(connection.scalar("PRAGMA user_version") == 0);
    CHECK(connection.scalar("SELECT count(*) FROM sqlite_master WHERE type = 'table'") == 0);
    initialize(connection, "database");
    CHECK(connection.scalar("SELECT count(*) FROM auctions") == 1000);
    std::filesystem::remove(directory / "schema.sql");
    std::filesystem::remove(directory / "seed_categories.json");
    std::filesystem::remove(directory / "seed_auctions.json");
    std::filesystem::remove(directory);
}
DROGON_TEST(DiskReopen)
{
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto file = std::filesystem::path("runtime") / ("database-test-" + std::to_string(suffix) + ".sqlite3");
    {
        Connection connection(file);
        initialize(connection, "database");
    }
    {
        Connection reopened(file);
        CHECK(reopened.scalar("PRAGMA foreign_keys") == 1);
        initialize(reopened, "database");
        CHECK(reopened.scalar("SELECT count(*) FROM categories") == 10);
        CHECK(reopened.scalar("SELECT count(*) FROM auctions") == 1000);
    }
    CHECK(std::filesystem::remove(file));
}

int main(int argc, char* argv[])
{
    try
    {
        auction::prepareRuntime();
        if (argc == 2 && std::string(argv[1]) == "--verify-images") return verifyImages();
        return drogon::test::run(argc, argv);
    }
    catch (const std::exception& error)
    {
        std::cerr << "Database test failed: " << error.what() << '\n';
        return 1;
    }
}


