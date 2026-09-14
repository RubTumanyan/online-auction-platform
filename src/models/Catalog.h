#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace auction::models
{
struct Category
{
    std::int64_t id;
    std::string name;
};

struct Lot
{
    std::int64_t id;
    std::string title;
    std::string description;
    Category category;
    std::string imageUrl;
    std::int64_t startingPriceCents;
    std::int64_t currentPriceCents;
    std::string createdAt;
    std::string endTime;
    std::string status;
    std::optional<std::string> closedAt;
    std::optional<std::string> highestBidderUsername;
};

struct LotQuery
{
    int page = 1;
    int limit = 20;
    std::optional<std::int64_t> categoryId;
    std::optional<std::string> search;
    std::optional<std::int64_t> minPriceCents;
    std::optional<std::int64_t> maxPriceCents;
    std::string sortBy = "end_time";
    std::string order = "asc";
};

struct LotPage
{
    int page;
    int limit;
    std::int64_t total;
    std::int64_t totalPages;
    std::vector<Lot> lots;
};
}
