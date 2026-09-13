#pragma once

#include "models/AuthBid.h"

#include <filesystem>
#include <stdexcept>
#include <string>

namespace auction::services
{
enum class ApiErrorKind { invalid, unauthorized, conflict, notFound };

class ApiError final : public std::runtime_error
{
  public:
    ApiError(ApiErrorKind kind, const std::string& message) : std::runtime_error(message), kind_(kind) {}
    ApiErrorKind kind() const { return kind_; }
  private:
    ApiErrorKind kind_;
};

class AuthService final
{
  public:
    explicit AuthService(std::filesystem::path databasePath);
    models::AuthResult registerUser(const std::string& username, const std::string& password) const;
    models::AuthResult login(const std::string& username, const std::string& password) const;
    models::User authenticate(const std::string& authorization) const;
    void logout(const std::string& authorization) const;
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
