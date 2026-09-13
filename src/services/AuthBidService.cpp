#include "services/AuthBidService.h"

#include "database/Database.h"
#include "security/Crypto.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <regex>
#include <utility>

namespace auction::services
{
namespace
{
constexpr std::int64_t kMinimumStep = 500;

std::string bearerToken(const std::string& authorization)
{
    constexpr std::string_view prefix = "Bearer ";
    if (!authorization.starts_with(prefix) || authorization.size() <= prefix.size())
        throw ApiError(ApiErrorKind::unauthorized, "Authentication required");
    return authorization.substr(prefix.size());
}

void validateCredentials(const std::string& username, const std::string& password)
{
    static const std::regex usernamePattern("^[A-Za-z0-9_]{3,30}$");
    if (!std::regex_match(username, usernamePattern))
        throw ApiError(ApiErrorKind::invalid, "Username must be 3-30 letters, digits, or underscores");
    if (password.size() < 8 || password.size() > 128)
        throw ApiError(ApiErrorKind::invalid, "Password must be 8-128 characters");
}

models::AuthResult createSession(database::Connection& connection, const models::User& user)
{
    const auto token = security::createToken();
    auto insert = connection.prepare(
        "INSERT INTO auth_tokens(token_hash,user_id,expires_at,created_at) "
        "VALUES(?,?,strftime('%Y-%m-%dT%H:%M:%SZ','now','+24 hours'),"
        "strftime('%Y-%m-%dT%H:%M:%SZ','now'))");
    insert.bind(1, security::hashToken(token));
    insert.bind(2, user.id);
    insert.run();
    return {user, token};
}

void insertUser(database::Connection& connection, const std::string& username, const std::string& passwordHash)
{
    std::string email = username;
    std::transform(email.begin(), email.end(), email.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    email += "@auction.demo";
    auto insert = connection.prepare(
        "INSERT INTO users(display_name,email,password_hash,created_at,updated_at,username) "
        "VALUES(?,?,?,strftime('%Y-%m-%dT%H:%M:%SZ','now'),"
        "strftime('%Y-%m-%dT%H:%M:%SZ','now'),?)");
    insert.bind(1, username);
    insert.bind(2, email);
    insert.bind(3, passwordHash);
    insert.bind(4, username);
    insert.run();
}
}

AuthService::AuthService(std::filesystem::path databasePath) : databasePath_(std::move(databasePath)) {}

models::AuthResult AuthService::registerUser(const std::string& username, const std::string& password) const
{
    validateCredentials(username, password);
    database::Connection connection(databasePath_);
    connection.execute("BEGIN IMMEDIATE");
    try
    {
        auto existing = connection.prepare("SELECT 1 FROM users WHERE username = ? COLLATE NOCASE");
        existing.bind(1, username);
        if (existing.step()) throw ApiError(ApiErrorKind::conflict, "Username already exists");
        insertUser(connection, username, security::hashPassword(password));
        const models::User user{connection.lastInsertRowId(), username};
        auto result = createSession(connection, user);
        connection.execute("COMMIT");
        return result;
    }
    catch (...)
    {
        connection.execute("ROLLBACK");
        throw;
    }
}

models::AuthResult AuthService::login(const std::string& username, const std::string& password) const
{
    if (username.empty() || password.empty()) throw ApiError(ApiErrorKind::invalid, "Username and password are required");
    database::Connection connection(databasePath_);
    auto query = connection.prepare("SELECT id,username,password_hash FROM users WHERE username = ? COLLATE NOCASE");
    query.bind(1, username);
    if (!query.step() || !security::verifyPassword(password, query.text(2)))
        throw ApiError(ApiErrorKind::unauthorized, "Invalid credentials");
    return createSession(connection, {query.integer(0), query.text(1)});
}

models::User AuthService::authenticate(const std::string& authorization) const
{
    const auto token = bearerToken(authorization);
    database::Connection connection(databasePath_);
    auto query = connection.prepare(
        "SELECT u.id,u.username FROM auth_tokens t JOIN users u ON u.id=t.user_id "
        "WHERE t.token_hash=? AND t.expires_at>strftime('%Y-%m-%dT%H:%M:%SZ','now')");
    query.bind(1, security::hashToken(token));
    if (!query.step()) throw ApiError(ApiErrorKind::unauthorized, "Invalid or expired token");
    return {query.integer(0), query.text(1)};
}

void AuthService::logout(const std::string& authorization) const
{
    const auto token = bearerToken(authorization);
    authenticate(authorization);
    database::Connection connection(databasePath_);
    auto remove = connection.prepare("DELETE FROM auth_tokens WHERE token_hash=?");
    remove.bind(1, security::hashToken(token));
    remove.run();
}

void AuthService::ensureDemoUsers() const
{
    database::Connection connection(databasePath_);
    for (const auto& [username, password] : {std::pair{"alice", "Alice123!"}, std::pair{"bob", "Bob123!"}})
    {
        auto existing = connection.prepare("SELECT 1 FROM users WHERE username=? COLLATE NOCASE");
        existing.bind(1, std::string(username));
        if (!existing.step()) insertUser(connection, username, security::hashPassword(password));
    }
}

BidService::BidService(std::filesystem::path databasePath) : databasePath_(std::move(databasePath)) {}

models::BidPage BidService::bids(std::int64_t lotId, int page, int pageSize) const
{
    database::Connection connection(databasePath_);
    auto lot = connection.prepare("SELECT 1 FROM auctions WHERE id=?");
    lot.bind(1, lotId);
    if (!lot.step()) throw ApiError(ApiErrorKind::notFound, "Lot not found");
    auto count = connection.prepare("SELECT count(*) FROM bids WHERE auction_id=?");
    count.bind(1, lotId); count.step();
    const auto total = count.integer(0);
    auto query = connection.prepare(
        "SELECT b.id,b.auction_id,u.username,b.amount_cents,b.created_at FROM bids b "
        "JOIN users u ON u.id=b.user_id WHERE b.auction_id=? "
        "ORDER BY b.created_at DESC,b.id DESC LIMIT ? OFFSET ?");
    query.bind(1, lotId); query.bind(2, static_cast<std::int64_t>(pageSize));
    query.bind(3, static_cast<std::int64_t>(page - 1) * pageSize);
    std::vector<models::Bid> items;
    while (query.step()) items.push_back({query.integer(0), query.integer(1), query.text(2), query.integer(3), query.text(4)});
    return {std::move(items), page, pageSize, total};
}

models::PlacedBid BidService::place(std::int64_t lotId, std::int64_t userId, std::int64_t amount) const
{
    if (amount <= 0) throw ApiError(ApiErrorKind::invalid, "amount must be a positive integer");
    database::Connection connection(databasePath_);
    connection.execute("BEGIN IMMEDIATE");
    try
    {
        auto lot = connection.prepare(
            "SELECT current_price_cents,current_winner_id,status,"
            "ends_at<=strftime('%Y-%m-%dT%H:%M:%SZ','now') FROM auctions WHERE id=?");
        lot.bind(1, lotId);
        if (!lot.step()) throw ApiError(ApiErrorKind::notFound, "Lot not found");
        const auto currentPrice = lot.integer(0);
        const auto currentWinner = lot.integer(1);
        if (lot.text(2) != "active" || lot.integer(3) != 0)
            throw ApiError(ApiErrorKind::conflict, "Auction is closed");
        if (currentPrice > std::numeric_limits<std::int64_t>::max() - kMinimumStep ||
            amount < currentPrice + kMinimumStep)
            throw ApiError(ApiErrorKind::conflict, "Bid must be at least current price plus minimum step");
        if (currentWinner == userId)
            throw ApiError(ApiErrorKind::conflict, "You already have the highest bid");
        auto username = connection.prepare("SELECT username FROM users WHERE id=?");
        username.bind(1, userId);
        if (!username.step()) throw ApiError(ApiErrorKind::unauthorized, "Invalid user");
        const auto bidder = username.text(0);
        auto insert = connection.prepare(
            "INSERT INTO bids(auction_id,user_id,amount_cents,created_at) "
            "VALUES(?,?,?,strftime('%Y-%m-%dT%H:%M:%SZ','now'))");
        insert.bind(1, lotId); insert.bind(2, userId); insert.bind(3, amount); insert.run();
        const auto bidId = connection.lastInsertRowId();
        auto update = connection.prepare(
            "UPDATE auctions SET current_price_cents=?,current_winner_id=?,"
            "updated_at=strftime('%Y-%m-%dT%H:%M:%SZ','now') WHERE id=?");
        update.bind(1, amount); update.bind(2, userId); update.bind(3, lotId); update.run();
        auto created = connection.prepare("SELECT created_at FROM bids WHERE id=?");
        created.bind(1, bidId); created.step();
        models::PlacedBid result{{bidId, lotId, bidder, amount, created.text(0)}, amount};
        connection.execute("COMMIT");
        return result;
    }
    catch (...)
    {
        connection.execute("ROLLBACK");
        throw;
    }
}
}
