#include "controllers/AuthBidController.h"

#include "models/AuthBid.h"
#include "services/AuthBidService.h"

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <optional>
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

drogon::HttpResponsePtr errorResponse(drogon::HttpStatusCode status, const std::string& message)
{
    Json::Value body; body["error"] = message;
    auto response = drogon::HttpResponse::newHttpJsonResponse(body); response->setStatusCode(status); return response;
}

drogon::HttpStatusCode statusFor(services::ApiErrorKind kind)
{
    switch (kind)
    {
        case services::ApiErrorKind::invalid: return drogon::k400BadRequest;
        case services::ApiErrorKind::unauthorized: return drogon::k401Unauthorized;
        case services::ApiErrorKind::conflict: return drogon::k409Conflict;
        case services::ApiErrorKind::notFound: return drogon::k404NotFound;
    }
    return drogon::k500InternalServerError;
}

std::optional<std::int64_t> positiveInt64(std::string_view value)
{
    if (value.empty()) return std::nullopt;
    std::int64_t result = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (error != std::errc{} || end != value.data() + value.size() || result <= 0) return std::nullopt;
    return result;
}

std::optional<int> queryInteger(const std::string& value, int fallback, int maximum)
{
    if (value.empty()) return fallback;
    const auto parsed = positiveInt64(value);
    if (!parsed || *parsed > maximum) return std::nullopt;
    return static_cast<int>(*parsed);
}

Json::Value userJson(const models::User& user)
{
    Json::Value result; result["id"] = Json::Int64(user.id); result["username"] = user.username; return result;
}

Json::Value bidJson(const models::Bid& bid)
{
    Json::Value result;
    result["id"] = Json::Int64(bid.id); result["lotId"] = Json::Int64(bid.lotId);
    result["bidderUsername"] = bid.bidderUsername; result["amount"] = Json::Int64(bid.amount);
    result["createdAt"] = bid.createdAt; return result;
}

template <typename Work>
void handle(std::function<void(const drogon::HttpResponsePtr&)>& callback, Work&& work)
{
    try { work(); }
    catch (const services::ApiError& error) { callback(errorResponse(statusFor(error.kind()), error.what())); }
    catch (const std::exception&) { callback(errorResponse(drogon::k500InternalServerError, "Internal server error")); }
}

std::pair<std::string, std::string> credentials(const drogon::HttpRequestPtr& request)
{
    const auto body = request->getJsonObject();
    if (!body || !(*body)["username"].isString() || !(*body)["password"].isString())
        throw services::ApiError(services::ApiErrorKind::invalid, "JSON username and password are required");
    return {(*body)["username"].asString(), (*body)["password"].asString()};
}
}

void AuthBidController::registerUser(const drogon::HttpRequestPtr& request,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) const
{
    handle(callback, [&] {
        const auto [username, password] = credentials(request);
        const auto result = services::AuthService(databasePath()).registerUser(username, password);
        Json::Value body; body["user"] = userJson(result.user); body["token"] = result.token;
        auto response = drogon::HttpResponse::newHttpJsonResponse(body); response->setStatusCode(drogon::k201Created); callback(response);
    });
}

void AuthBidController::login(const drogon::HttpRequestPtr& request,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) const
{
    handle(callback, [&] {
        const auto [username, password] = credentials(request);
        const auto result = services::AuthService(databasePath()).login(username, password);
        Json::Value body; body["user"] = userJson(result.user); body["token"] = result.token;
        callback(drogon::HttpResponse::newHttpJsonResponse(body));
    });
}

void AuthBidController::me(const drogon::HttpRequestPtr& request,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) const
{
    handle(callback, [&] { callback(drogon::HttpResponse::newHttpJsonResponse(
        userJson(services::AuthService(databasePath()).authenticate(request->getHeader("authorization"))))); });
}

void AuthBidController::logout(const drogon::HttpRequestPtr& request,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) const
{
    handle(callback, [&] {
        services::AuthService(databasePath()).logout(request->getHeader("authorization"));
        auto response = drogon::HttpResponse::newHttpResponse(); response->setStatusCode(drogon::k204NoContent); callback(response);
    });
}

void AuthBidController::bids(const drogon::HttpRequestPtr& request,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::string id) const
{
    handle(callback, [&] {
        const auto lotId = positiveInt64(id);
        const auto page = queryInteger(request->getParameter("page"), 1, 1000000);
        const auto pageSize = queryInteger(request->getParameter("pageSize"), 10, 100);
        if (!lotId) throw services::ApiError(services::ApiErrorKind::invalid, "lot id must be a positive integer");
        if (!page) throw services::ApiError(services::ApiErrorKind::invalid, "page must be a positive integer");
        if (!pageSize) throw services::ApiError(services::ApiErrorKind::invalid, "pageSize must be from 1 to 100");
        const auto result = services::BidService(databasePath()).bids(*lotId, *page, *pageSize);
        Json::Value body; body["items"] = Json::arrayValue;
        for (const auto& bid : result.items) body["items"].append(bidJson(bid));
        body["page"] = result.page; body["pageSize"] = result.pageSize; body["total"] = Json::Int64(result.total);
        callback(drogon::HttpResponse::newHttpJsonResponse(body));
    });
}

void AuthBidController::placeBid(const drogon::HttpRequestPtr& request,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::string id) const
{
    handle(callback, [&] {
        const auto lotId = positiveInt64(id);
        if (!lotId) throw services::ApiError(services::ApiErrorKind::invalid, "lot id must be a positive integer");
        const auto user = services::AuthService(databasePath()).authenticate(request->getHeader("authorization"));
        const auto body = request->getJsonObject();
        if (!body || !(*body)["amount"].isInt64() || (*body)["amount"].asInt64() <= 0)
            throw services::ApiError(services::ApiErrorKind::invalid, "amount must be a positive integer");
        const auto result = services::BidService(databasePath()).place(*lotId, user.id, (*body)["amount"].asInt64());
        Json::Value responseBody; responseBody["bid"] = bidJson(result.bid);
        responseBody["currentPrice"] = Json::Int64(result.currentPrice);
        auto response = drogon::HttpResponse::newHttpJsonResponse(responseBody); response->setStatusCode(drogon::k201Created); callback(response);
    });
}
}
