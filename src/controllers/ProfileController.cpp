#include "controllers/ProfileController.h"

#include "models/AuctionLifecycle.h"
#include "services/AuthBidService.h"
#include "services/ProfileService.h"

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace auction
{
namespace
{
std::filesystem::path databasePath()
{
    const auto value = drogon::app().getCustomConfig()["database"].get("path", "runtime/auction.sqlite3").asString();
    return std::filesystem::path(std::u8string(value.begin(), value.end()));
}

drogon::HttpResponsePtr jsonError(drogon::HttpStatusCode status, const std::string& message)
{
    Json::Value body; body["error"] = message;
    auto response = drogon::HttpResponse::newHttpJsonResponse(body); response->setStatusCode(status);
    return response;
}

std::int64_t kMinimumBidStep = auction::kMinimumBidStepCents;

Json::Value userJson(const models::User& user)
{
    Json::Value result;
    result["id"] = Json::Int64(user.id);
    result["username"] = user.username;
    result["email"] = user.email;
    result["emailVerified"] = user.emailVerified;
    return result;
}

Json::Value profileLotJson(const models::Lot& lot, const models::ProfileParticipation& participation)
{
    Json::Value result;
    result["id"] = Json::Int64(lot.id);
    result["title"] = lot.title;
    result["description"] = lot.description;
    result["category"] = Json::objectValue;
    result["category"]["id"] = Json::Int64(lot.category.id);
    result["category"]["name"] = lot.category.name;
    result["image_url"] = lot.imageUrl;
    result["start_price"] = Json::Int64(lot.startingPriceCents);
    result["current_price"] = Json::Int64(lot.currentPriceCents);
    result["currentPrice"] = Json::Int64(lot.currentPriceCents);
    result["minimum_step"] = Json::Int64(kMinimumBidStep);
    result["end_time"] = lot.endTime;
    result["endsAt"] = lot.endTime;
    result["status"] = lot.status;
    result["closedAt"] = lot.closedAt ? Json::Value(*lot.closedAt) : Json::nullValue;
    result["winnerUsername"] = lot.highestBidderUsername ? Json::Value(*lot.highestBidderUsername) : Json::nullValue;
    result["participationStatus"] = participation.participationStatus;
    result["isLeading"] = participation.isLeading;
    if (participation.userHighestBidCents)
        result["userHighestBid"] = Json::Int64(*participation.userHighestBidCents);
    else result["userHighestBid"] = Json::nullValue;
    return result;
}

template <typename Work>
void handle(const std::function<void(const drogon::HttpResponsePtr&)>& callback, Work&& work)
{
    try { work(); }
    catch (const services::ApiError& error)
    {
        drogon::HttpStatusCode status = drogon::k401Unauthorized;
        switch (error.kind())
        {
            case services::ApiErrorKind::unauthorized: status = drogon::k401Unauthorized; break;
            case services::ApiErrorKind::invalid: status = drogon::k400BadRequest; break;
            default: status = drogon::k500InternalServerError; break;
        }
        callback(jsonError(status, error.what()));
    }
    catch (const std::exception&)
    {
        callback(jsonError(drogon::k500InternalServerError, "Internal server error"));
    }
}
}

void ProfileController::summary(
    const drogon::HttpRequestPtr& request,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) const
{
    handle(callback, [&] {
        const auto user = services::AuthService(databasePath()).authenticate(request->getHeader("authorization"));
        const auto result = services::ProfileService(databasePath()).summary(user.id);
        LOG_INFO << "[PROFILE] Summary for userId=" << user.id << " (active="
                 << result.activeParticipations << ", won=" << result.wonAuctions
                 << ", total=" << result.totalParticipations << ")";
        Json::Value body;
        body["user"] = userJson(result.user);
        body["stats"]["activeParticipations"] = Json::Int64(result.activeParticipations);
        body["stats"]["wonAuctions"] = Json::Int64(result.wonAuctions);
        body["stats"]["totalParticipations"] = Json::Int64(result.totalParticipations);
        callback(drogon::HttpResponse::newHttpJsonResponse(body));
    });
}

void ProfileController::auctions(
    const drogon::HttpRequestPtr& request,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) const
{
    handle(callback, [&] {
        const auto user = services::AuthService(databasePath()).authenticate(request->getHeader("authorization"));
        auto filter = request->getParameter("filter");
        if (filter.empty()) filter = "all";
        if (filter != "active" && filter != "won" && filter != "all")
            return callback(jsonError(drogon::k400BadRequest, "filter must be active, won, or all"));
        const auto items = services::ProfileService(databasePath()).participations(user.id, filter);
        LOG_INFO << "[PROFILE] Participations for userId=" << user.id << " filter=" << filter
                 << " items=" << items.size();
        Json::Value body;
        body["filter"] = filter;
        body["items"] = Json::arrayValue;
        for (const auto& item : items) body["items"].append(profileLotJson(item.lot, item));
        callback(drogon::HttpResponse::newHttpJsonResponse(body));
    });
}
}