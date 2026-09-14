#include "controllers/CatalogController.h"

#include "models/AuctionLifecycle.h"
#include "models/Catalog.h"
#include "services/CatalogService.h"

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace auction
{
namespace
{
constexpr std::int64_t kMinimumStepCents = auction::kMinimumBidStepCents;

std::filesystem::path databasePath()
{
    const auto& config = drogon::app().getCustomConfig()["database"];
    const auto value = config.get("path", "runtime/auction.sqlite3").asString();
    return std::filesystem::path(std::u8string(value.begin(), value.end()));
}

drogon::HttpResponsePtr jsonError(drogon::HttpStatusCode status, const std::string& message)
{
    Json::Value body;
    body["error"] = message;
    auto response = drogon::HttpResponse::newHttpJsonResponse(body);
    response->setStatusCode(status);
    return response;
}

std::optional<int> positiveInteger(std::string_view value)
{
    if (value.empty()) return std::nullopt;
    int result = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (error != std::errc{} || end != value.data() + value.size() || result <= 0) return std::nullopt;
    return result;
}

std::string shortDescription(const std::string& description)
{
    constexpr std::size_t maximum = 160;
    if (description.size() <= maximum) return description;
    std::size_t length = maximum;
    while (length > 0 && (static_cast<unsigned char>(description[length]) & 0xc0) == 0x80) --length;
    return description.substr(0, length) + "...";
}

Json::Value categoryJson(const models::Category& category)
{
    Json::Value result;
    result["id"] = Json::Int64(category.id);
    result["name"] = category.name;
    return result;
}

Json::Value lotJson(const models::Lot& lot, bool detailed)
{
    Json::Value result;
    result["id"] = Json::Int64(lot.id);
    result["title"] = lot.title;
    result[detailed ? "description" : "short_description"] =
        detailed ? lot.description : shortDescription(lot.description);
    result["category"] = categoryJson(lot.category);
    result["image_url"] = lot.imageUrl;
    result["start_price"] = Json::Int64(lot.startingPriceCents);
    result["current_price"] = Json::Int64(lot.currentPriceCents);
    result["currentPrice"] = Json::Int64(lot.currentPriceCents);
    result["minimum_step"] = Json::Int64(kMinimumStepCents);
    if (detailed) result["created_at"] = lot.createdAt;
    if (detailed && lot.highestBidderUsername) result["highest_bidder_username"] = *lot.highestBidderUsername;
    result["end_time"] = lot.endTime;
    result["endsAt"] = lot.endTime;
    result["status"] = lot.status;
    if (detailed)
    {
        if (lot.highestBidderUsername) result["winnerUsername"] = *lot.highestBidderUsername;
        else result["winnerUsername"] = Json::nullValue;
        if (lot.closedAt) result["closedAt"] = *lot.closedAt;
        else result["closedAt"] = Json::nullValue;
    }
    return result;
}
}

void CatalogController::categories(
    const drogon::HttpRequestPtr&,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) const
{
    try
    {
        Json::Value body;
        body["categories"] = Json::arrayValue;
        for (const auto& category : services::CatalogService(databasePath()).categories())
            body["categories"].append(categoryJson(category));
        callback(drogon::HttpResponse::newHttpJsonResponse(body));
    }
    catch (const std::exception&)
    {
        callback(jsonError(drogon::k500InternalServerError, "Unable to load categories"));
    }
}

void CatalogController::lots(
    const drogon::HttpRequestPtr& request,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) const
{
    try
    {
        models::LotQuery query;
        const auto page = request->getParameter("page");
        const auto limit = request->getParameter("limit");
        const auto categoryId = request->getParameter("category_id");
        const auto search = request->getParameter("search");
        const auto sortBy = request->getParameter("sort_by");
        const auto order = request->getParameter("order");

        if (!page.empty())
        {
            const auto value = positiveInteger(page);
            if (!value) return callback(jsonError(drogon::k400BadRequest, "page must be a positive integer"));
            query.page = *value;
        }
        if (!limit.empty())
        {
            const auto value = positiveInteger(limit);
            if (!value || *value > 100)
                return callback(jsonError(drogon::k400BadRequest, "limit must be an integer from 1 to 100"));
            query.limit = *value;
        }
        if (!categoryId.empty())
        {
            const auto value = positiveInteger(categoryId);
            if (!value) return callback(jsonError(drogon::k400BadRequest, "category_id must be a positive integer"));
            query.categoryId = *value;
        }
        if (!search.empty())
        {
            if (search.size() > 200)
                return callback(jsonError(drogon::k400BadRequest, "search must not exceed 200 characters"));
            query.search = search;
        }
        if (!sortBy.empty()) query.sortBy = sortBy;
        if (query.sortBy != "current_price" && query.sortBy != "end_time" && query.sortBy != "title")
            return callback(jsonError(drogon::k400BadRequest,
                                      "sort_by must be current_price, end_time, or title"));
        if (!order.empty()) query.order = order;
        if (query.order != "asc" && query.order != "desc")
            return callback(jsonError(drogon::k400BadRequest, "order must be asc or desc"));

        const services::CatalogService service(databasePath());
        if (query.categoryId && !service.categoryExists(*query.categoryId))
            return callback(jsonError(drogon::k400BadRequest, "category_id does not exist"));

        const auto pageResult = service.lots(query);
        Json::Value body;
        body["page"] = pageResult.page;
        body["limit"] = pageResult.limit;
        body["total"] = Json::Int64(pageResult.total);
        body["total_pages"] = Json::Int64(pageResult.totalPages);
        body["lots"] = Json::arrayValue;
        for (const auto& item : pageResult.lots) body["lots"].append(lotJson(item, false));
        callback(drogon::HttpResponse::newHttpJsonResponse(body));
    }
    catch (const std::exception&)
    {
        callback(jsonError(drogon::k500InternalServerError, "Unable to load lots"));
    }
}

void CatalogController::lot(
    const drogon::HttpRequestPtr&,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    std::string id) const
{
    const auto parsed = positiveInteger(id);
    if (!parsed) return callback(jsonError(drogon::k400BadRequest, "lot id must be a positive integer"));
    try
    {
        const auto result = services::CatalogService(databasePath()).lot(*parsed);
        if (!result) return callback(jsonError(drogon::k404NotFound, "Lot not found"));
        callback(drogon::HttpResponse::newHttpJsonResponse(lotJson(*result, true)));
    }
    catch (const std::exception&)
    {
        callback(jsonError(drogon::k500InternalServerError, "Unable to load lot"));
    }
}
}
