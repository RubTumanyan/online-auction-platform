#include "services/AuthBidService.h"

#include "database/Database.h"
#include "models/AuctionLifecycle.h"
#include "security/Crypto.h"
#include "services/EmailDelivery.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif
#include <drogon/drogon.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <regex>
#include <utility>

namespace auction::services
{
namespace
{
constexpr std::int64_t kMinimumStep = auction::kMinimumBidStepCents;
constexpr int kResendCooldownSeconds = 60;
constexpr int kMaxFailedAttempts = 5;
constexpr int kMaxSends = 8;

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

void validateEmail(const std::string& email)
{
    static const std::regex emailPattern(
        R"(^[A-Za-z0-9.!#$%&'*+/=?^_`{|}~-]+@[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?(?:\.[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?)+$)");
    if (email.size() > 254 || !std::regex_match(email, emailPattern))
        throw ApiError(ApiErrorKind::invalid, "A valid email address is required");
}

std::string currentUtc(database::Connection& connection)
{
    auto query = connection.prepare("SELECT strftime('%Y-%m-%dT%H:%M:%SZ','now')");
    query.step();
    return query.text(0);
}

void insertVerificationCode(database::Connection& connection, std::int64_t userId, const std::string& code,
                            int sendCount)
{
    auto insert = connection.prepare(
        "INSERT INTO email_verification_codes(user_id,code_hash,expires_at,send_count,last_sent_at) "
        "VALUES(?,?,strftime('%Y-%m-%dT%H:%M:%SZ','now','+10 minutes'),?,strftime('%Y-%m-%dT%H:%M:%SZ','now'))");
    insert.bind(1, userId);
    insert.bind(2, security::hashVerificationCode(code));
    insert.bind(3, static_cast<std::int64_t>(sendCount));
    insert.run();
}

void deleteVerificationCode(database::Connection& connection, std::int64_t userId)
{
    auto remove = connection.prepare("DELETE FROM email_verification_codes WHERE user_id=?");
    remove.bind(1, userId);
    remove.run();
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

void insertUser(database::Connection& connection, const std::string& username, const std::string& email,
                const std::string& passwordHash, bool verified)
{
    auto insert = connection.prepare(
        "INSERT INTO users(display_name,email,password_hash,email_verified,created_at,updated_at,username) "
        "VALUES(?,?,?,?,strftime('%Y-%m-%dT%H:%M:%SZ','now'),strftime('%Y-%m-%dT%H:%M:%SZ','now'),?)");
    insert.bind(1, username);
    insert.bind(2, email);
    insert.bind(3, passwordHash);
    insert.bind(4, static_cast<std::int64_t>(verified ? 1 : 0));
    insert.bind(5, username);
    insert.run();
}
}

AuthService::AuthService(std::filesystem::path databasePath) : databasePath_(std::move(databasePath)) {}

models::RegisterResult AuthService::registerUser(const std::string& username, const std::string& email,
                                                 const std::string& password) const
{
    validateCredentials(username, password);
    validateEmail(email);
    database::Connection connection(databasePath_);
    connection.execute("BEGIN IMMEDIATE");
    models::AuthResult session;
    try
    {
        auto existingUser = connection.prepare("SELECT 1 FROM users WHERE username = ? COLLATE NOCASE");
        existingUser.bind(1, username);
        if (existingUser.step()) throw ApiError(ApiErrorKind::conflict, "Username already exists");
        auto existingEmail = connection.prepare("SELECT 1 FROM users WHERE email = ? COLLATE NOCASE");
        existingEmail.bind(1, email);
        if (existingEmail.step()) throw ApiError(ApiErrorKind::conflict, "Email already exists");
        insertUser(connection, username, email, security::hashPassword(password), /*verified=*/false);
        const models::User user{connection.lastInsertRowId(), username, email, false};
        session = createSession(connection, user);
        connection.execute("COMMIT");
    }
    catch (...)
    {
        connection.execute("ROLLBACK");
        throw;
    }
    bool deliveryFailed = false;
    try
    {
        const auto code = security::generateVerificationCode();
        insertVerificationCode(connection, session.user.id, code, /*sendCount=*/1);
        verificationEmailSender().sendVerificationCode(session.user.email, code);
    }
    catch (const std::exception& error)
    {
        LOG_ERROR << "Verification email delivery failed for " << session.user.email << ": " << error.what();
        database::Connection cleanup(databasePath_);
        deleteVerificationCode(cleanup, session.user.id);
        deliveryFailed = true;
    }
    return {session, deliveryFailed};
}

models::AuthResult AuthService::login(const std::string& username, const std::string& password) const
{
    if (username.empty() || password.empty()) throw ApiError(ApiErrorKind::invalid, "Username and password are required");
    database::Connection connection(databasePath_);
    auto query = connection.prepare(
        "SELECT id,username,email,password_hash,email_verified FROM users WHERE username = ? COLLATE NOCASE");
    query.bind(1, username);
    if (!query.step() || !security::verifyPassword(password, query.text(3)))
        throw ApiError(ApiErrorKind::unauthorized, "Invalid credentials");
    return createSession(connection, {query.integer(0), query.text(1), query.text(2), query.integer(4) != 0});
}

models::User AuthService::authenticate(const std::string& authorization) const
{
    const auto token = bearerToken(authorization);
    database::Connection connection(databasePath_);
    auto query = connection.prepare(
        "SELECT u.id,u.username,u.email,u.email_verified FROM auth_tokens t JOIN users u ON u.id=t.user_id "
        "WHERE t.token_hash=? AND t.expires_at>strftime('%Y-%m-%dT%H:%M:%SZ','now')");
    query.bind(1, security::hashToken(token));
    if (!query.step()) throw ApiError(ApiErrorKind::unauthorized, "Invalid or expired token");
    return {query.integer(0), query.text(1), query.text(2), query.integer(3) != 0};
}

void AuthService::verifyEmail(const std::string& email, const std::string& code) const
{
    if (email.empty() || code.empty())
        throw ApiError(ApiErrorKind::invalid, "Email and code are required");
    database::Connection connection(databasePath_);
    auto user = connection.prepare("SELECT id,email_verified FROM users WHERE email = ? COLLATE NOCASE");
    user.bind(1, email);
    if (!user.step()) throw ApiError(ApiErrorKind::notFound, "No account registered with this email");
    const auto userId = user.integer(0);
    if (user.integer(1) != 0) throw ApiError(ApiErrorKind::conflict, "Email is already verified");
    auto row = connection.prepare(
        "SELECT code_hash,expires_at,failed_attempts FROM email_verification_codes "
        "WHERE user_id=? ORDER BY id DESC LIMIT 1");
    row.bind(1, userId);
    if (!row.step())
        throw ApiError(ApiErrorKind::invalid, "No verification code found. Request a new code.");
    const auto failedAttempts = row.integer(2);
    if (row.text(1) <= currentUtc(connection))
    {
        deleteVerificationCode(connection, userId);
        throw ApiError(ApiErrorKind::invalid, "Verification code has expired. Request a new code.");
    }
    if (failedAttempts >= kMaxFailedAttempts)
    {
        deleteVerificationCode(connection, userId);
        throw ApiError(ApiErrorKind::invalid, "Too many failed attempts. Request a new code.");
    }
    if (!security::timingSafeEqual(row.text(0), security::hashVerificationCode(code)))
    {
        auto increment = connection.prepare(
            "UPDATE email_verification_codes SET failed_attempts=failed_attempts+1 WHERE user_id=?");
        increment.bind(1, userId);
        increment.run();
        if (failedAttempts + 1 >= kMaxFailedAttempts)
        {
            deleteVerificationCode(connection, userId);
            throw ApiError(ApiErrorKind::invalid, "Too many failed attempts. Request a new code.");
        }
        throw ApiError(ApiErrorKind::invalid, "Invalid verification code.");
    }
    auto verify = connection.prepare(
        "UPDATE users SET email_verified=1,updated_at=strftime('%Y-%m-%dT%H:%M:%SZ','now') WHERE id=?");
    verify.bind(1, userId);
    verify.run();
    deleteVerificationCode(connection, userId);
}

void AuthService::resendVerification(const std::string& email) const
{
    if (email.empty()) throw ApiError(ApiErrorKind::invalid, "Email is required");
    database::Connection connection(databasePath_);
    auto user = connection.prepare("SELECT id,email_verified FROM users WHERE email = ? COLLATE NOCASE");
    user.bind(1, email);
    if (!user.step()) throw ApiError(ApiErrorKind::notFound, "No account registered with this email");
    const auto userId = user.integer(0);
    if (user.integer(1) != 0) throw ApiError(ApiErrorKind::conflict, "Email is already verified");

    int sendCount = 1;
    auto existing = connection.prepare(
        "SELECT send_count,last_sent_at FROM email_verification_codes WHERE user_id=? ORDER BY id DESC LIMIT 1");
    existing.bind(1, userId);
    if (existing.step())
    {
        auto secondsSince = connection.prepare("SELECT strftime('%s','now') - strftime('%s',?)");
        secondsSince.bind(1, existing.text(1));
        secondsSince.step();
        const auto elapsed = secondsSince.integer(0);
        if (elapsed < kResendCooldownSeconds)
        {
            throw ApiError(ApiErrorKind::tooManyRequests, "Please wait before requesting another code",
                           kResendCooldownSeconds - static_cast<int>(elapsed));
        }
        sendCount = static_cast<int>(existing.integer(0)) + 1;
        if (sendCount > kMaxSends)
            throw ApiError(ApiErrorKind::tooManyRequests, "Too many verification code requests. Please contact support.");
    }
    deleteVerificationCode(connection, userId);
    const auto code = security::generateVerificationCode();
    insertVerificationCode(connection, userId, code, sendCount);
    try
    {
        verificationEmailSender().sendVerificationCode(email, code);
    }
    catch (const std::exception& error)
    {
        deleteVerificationCode(connection, userId);
        LOG_ERROR << "Resent verification email failed for " << email << ": " << error.what();
        throw ApiError(ApiErrorKind::emailFailure, "Email could not be delivered; please try again later.");
    }
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
        if (!existing.step())
        {
            insertUser(connection, username, std::string(username) + "@auction.demo",
                       security::hashPassword(password), /*verified=*/true);
        }
    }
    auto verify = connection.prepare("UPDATE users SET email_verified=1 WHERE username IN ('alice','bob') COLLATE NOCASE");
    verify.run();
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
        auto username = connection.prepare("SELECT username,email_verified FROM users WHERE id=?");
        username.bind(1, userId);
        if (!username.step()) throw ApiError(ApiErrorKind::unauthorized, "Invalid user");
        const auto bidder = username.text(0);
        if (username.integer(1) == 0)
            throw ApiError(ApiErrorKind::forbidden, "Email verification is required before placing a bid");
        if (currentPrice > std::numeric_limits<std::int64_t>::max() - kMinimumStep ||
            amount < currentPrice + kMinimumStep)
            throw ApiError(ApiErrorKind::conflict, "Bid must be at least current price plus minimum step");
        if (currentWinner == userId)
            throw ApiError(ApiErrorKind::conflict, "You already have the highest bid");
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
