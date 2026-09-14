#pragma once

#include <drogon/HttpController.h>

namespace auction
{
class ProfileController final : public drogon::HttpController<ProfileController>
{
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(ProfileController::summary, "/api/profile/summary", drogon::Get);
    ADD_METHOD_TO(ProfileController::auctions, "/api/profile/auctions", drogon::Get);
    METHOD_LIST_END

    void summary(const drogon::HttpRequestPtr& request,
                 std::function<void(const drogon::HttpResponsePtr&)>&& callback) const;
    void auctions(const drogon::HttpRequestPtr& request,
                  std::function<void(const drogon::HttpResponsePtr&)>&& callback) const;
};
}