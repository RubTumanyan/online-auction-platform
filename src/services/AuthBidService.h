#pragma once

#include "models/AuthBid.h"

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>

namespace auction::services
{
enum class ApiErrorKind { invalid, unauthorized, forbidden, conflict, notFound, tooManyRequests, emailFailure };

class ApiError final : public std::runtime_error
{
  public:
    ApiError(ApiErrorKind kind, const std::string& message, std::optional<int> retryAfterSeconds = std::nullopt)
        : std::runtime_error(message), kind_(kind), retryAfterSeconds_(retryAfterSeconds) {}
    ApiErrorKind kind() const { return kind_; }
    std::optional<int> retryAfterSeconds() const { return retryAfterSeconds_; }
  private:
    ApiErrorKind kind_;
    std::optional<int> retryAfterSeconds_;
};

class AuthService final
{
  public:
    explicit AuthService(std::filesystem::path databasePath);
    models::RegisterResult registerUser(const std::string& username, const std::string& email,
                                        const std::string& password) const;
    models::AuthResult login(const std::string& username, const std::string& password) const;
    models::User authenticate(const std::string& authorization) const;
    void logout(const std::string& authorization) const;
    void verifyEmail(const std::string& email, const std::string& code) const;
    void resendVerification(const std::string& email) const;
    void ensureDemoUsers() const;
  private:
    std::filesystem::path databasePath_;
};

class BidService final
{
  public:
    explicit BidService(std::filesystem::path databasePath);
    models::BidPage bids(std::int64_t lotId, int page, int pageSize) const;
    models::PlacedBid place(std::int64_t lotId, std::int64_t userId, std::int64_t amount) const;
  private:
    std::filesystem::path databasePath_;
};
}
