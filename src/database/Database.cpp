#include "database/Database.h"

#include <json/json.h>
#include <sqlite3.h>

#include <array>
#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>

namespace auction::database
{
namespace
{
void check(int code, sqlite3* connection)
{
    if (code != SQLITE_OK)
    {
        throw std::runtime_error("SQLite error " + std::to_string(code) + ": " + sqlite3_errmsg(connection));
    }
}

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        throw std::runtime_error("Database resource not found: " + path.string() + ". Rebuild the server.");
    }
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

Json::Value readJson(const std::filesystem::path& path)
{
    const auto contents = readFile(path);
    Json::Value value;
    Json::CharReaderBuilder builder;
    std::string errors;
    const std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    if (!reader->parse(contents.data(), contents.data() + contents.size(), &value, &errors))
    {
        throw std::runtime_error("Invalid seed JSON: " + path.string() + ": " + errors);
    }
    return value;
}

void seed(Connection& connection, const std::filesystem::path& directory)
{
    const auto categories = readJson(directory / "seed_categories.json");
    const auto auctions = readJson(directory / "seed_auctions.json");
    if (!categories.isArray() || categories.size() != 10 || !auctions.isArray() || auctions.size() != 1000)
    {
        throw std::runtime_error("Seed must contain exactly 10 categories and 1000 auctions");
    }
    auto category = connection.prepare("INSERT INTO categories(id, name, slug) VALUES (?, ?, ?)");
    for (const auto& row : categories)
    {
        category.bind(1, row["id"].asInt64());
        category.bind(2, row["name"].asString());
        category.bind(3, row["slug"].asString());
        category.run();
        category.reset();
    }
    auto auction = connection.prepare(
        "INSERT INTO auctions(id,title,description,image_url,category_id,starting_price_cents,"
        "current_price_cents,current_winner_id,starts_at,ends_at,status,created_at,updated_at) "
        "VALUES (?,?,?,?,?,?,?,NULL,?,?,'active',?,?)");
    std::array<int, 10> counts{};
    for (const auto& row : auctions)
    {
        const auto categoryId = row["category_id"].asInt();
        if (categoryId < 1 || categoryId > 10 || !row["starting_price_cents"].isInt64())
        {
            throw std::runtime_error("Invalid category or integer price in seed");
        }
        ++counts[static_cast<std::size_t>(categoryId - 1)];
        auction.bind(1, row["id"].asInt64());
        auction.bind(2, row["title"].asString());
        auction.bind(3, row["description"].asString());
        auction.bind(4, row["image_url"].asString());
        auction.bind(5, categoryId);
        auction.bind(6, row["starting_price_cents"].asInt64());
        auction.bind(7, row["starting_price_cents"].asInt64());
        auction.bind(8, row["starts_at"].asString());
        auction.bind(9, row["ends_at"].asString());
        auction.bind(10, row["created_at"].asString());
        auction.bind(11, row["updated_at"].asString());
        auction.run();
        auction.reset();
    }
    for (const auto count : counts)
    {
        if (count != 100)
        {
            throw std::runtime_error("Every category must have exactly 100 seeded auctions");
        }
    }
}
}

Statement::Statement(sqlite3* connection, const std::string& sql)
{
    const auto result = sqlite3_prepare_v2(connection, sql.c_str(), -1, &statement_, nullptr);
    if (result != SQLITE_OK)
    {
        sqlite3_finalize(statement_);
        statement_ = nullptr;
        check(result, connection);
    }
    if (!statement_)
    {
        throw std::runtime_error("Empty SQL statement");
    }
}
Statement::~Statement() { sqlite3_finalize(statement_); }
void Statement::bind(int index, std::int64_t value)
{
    check(sqlite3_bind_int64(statement_, index, value), sqlite3_db_handle(statement_));
}
void Statement::bind(int index, const std::string& value)
{
    check(sqlite3_bind_text(statement_, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT),
          sqlite3_db_handle(statement_));
}
void Statement::bindNull(int index)
{
    check(sqlite3_bind_null(statement_, index), sqlite3_db_handle(statement_));
}
bool Statement::step()
{
    const auto result = sqlite3_step(statement_);
    if (result == SQLITE_ROW) return true;
    if (result == SQLITE_DONE) return false;
    check(result, sqlite3_db_handle(statement_));
    return false;
}
void Statement::run()
{
    if (step()) throw std::runtime_error("Expected SQL without result rows");
}
void Statement::reset()
{
    check(sqlite3_reset(statement_), sqlite3_db_handle(statement_));
    check(sqlite3_clear_bindings(statement_), sqlite3_db_handle(statement_));
}
std::int64_t Statement::integer(int column) const { return sqlite3_column_int64(statement_, column); }
std::string Statement::text(int column) const
{
    const auto* text = sqlite3_column_text(statement_, column);
    return text ? std::string(reinterpret_cast<const char*>(text), sqlite3_column_bytes(statement_, column)) : "";
}
void Connection::Close::operator()(sqlite3* connection) const { sqlite3_close_v2(connection); }
Connection::Connection(const std::filesystem::path& path)
{
    if (path != ":memory:" && !path.parent_path().empty())
    {
        std::filesystem::create_directories(path.parent_path());
    }
    const auto utf8 = path.u8string();
    sqlite3* raw = nullptr;
    const auto result = sqlite3_open_v2(reinterpret_cast<const char*>(utf8.c_str()), &raw,
                                       SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
    connection_.reset(raw);
    if (!raw) throw std::runtime_error("Cannot allocate SQLite connection");
    check(result, raw);
    check(sqlite3_busy_timeout(raw, 5000), raw);
    execute("PRAGMA foreign_keys = ON");
    if (scalar("PRAGMA foreign_keys") != 1) throw std::runtime_error("SQLite foreign keys could not be enabled");
}
Connection::~Connection() = default;
Statement Connection::prepare(const std::string& sql) { return Statement(connection_.get(), sql); }
void Connection::execute(const std::string& sql) { prepare(sql).run(); }
std::int64_t Connection::scalar(const std::string& sql)
{
    auto statement = prepare(sql);
    if (!statement.step()) throw std::runtime_error("SQL scalar query returned no row");
    return statement.integer(0);
}
void Connection::executeScript(const std::string& sql)
{
    const char* cursor = sql.c_str();
    while (*cursor)
    {
        sqlite3_stmt* raw = nullptr;
        const char* next = nullptr;
        const auto result = sqlite3_prepare_v2(connection_.get(), cursor, -1, &raw, &next);
        const std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> statement(raw, sqlite3_finalize);
        check(result, connection_.get());
        if (raw)
        {
            const auto stepResult = sqlite3_step(raw);
            if (stepResult != SQLITE_DONE) check(stepResult, connection_.get());
        }
        if (!next || next == cursor) break;
        cursor = next;
    }
}
void initialize(Connection& connection, const std::filesystem::path& dataDirectory)
{
    connection.execute("BEGIN IMMEDIATE");
    try
    {
        const auto version = connection.scalar("PRAGMA user_version");
        if (version == 0)
        {
            connection.executeScript(readFile(dataDirectory / "schema.sql"));
            seed(connection, dataDirectory);
            connection.execute("PRAGMA user_version = 1");
        }
        else if (version != 1)
        {
            throw std::runtime_error("Unsupported database version: " + std::to_string(version));
        }
        connection.execute("COMMIT");
    }
    catch (...)
    {
        connection.execute("ROLLBACK");
        throw;
    }
}
}
