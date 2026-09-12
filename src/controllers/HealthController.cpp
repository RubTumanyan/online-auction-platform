#include "controllers/HealthController.h"

namespace auction
{
void HealthController::get(
    const drogon::HttpRequestPtr&,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) const
{
    Json::Value body;
    body["status"] = "ok";
    callback(drogon::HttpResponse::newHttpJsonResponse(body));
}
}
