#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace auction::models
{
struct User
{
    std::int64_t id;
    std::string username;
    std::string email;
    bool emailVerified = false;
};
struct AuthResult { User user; std::string token; };
struct RegisterResult
{
    AuthResult auth;
    bool emailDeliveryFailed = false;
};
struct Bid
{
    std::int64_t id;
    std::int64_t lotId;
    std::string bidderUsername;
    std::int64_t amount;
    std::string createdAt;
};
struct BidPage
{
    std::vector<Bid> items;
    int page;
    int pageSize;
    std::int64_t total;
};
struct PlacedBid { Bid bid; std::int64_t currentPrice; };
}
