#pragma once

#include <drogon/HttpController.h>

namespace auction
{
class AuthBidController final : public drogon::HttpController<AuthBidController>
{
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(AuthBidController::registerUser, "/api/auth/register", drogon::Post);
    ADD_METHOD_TO(AuthBidController::login, "/api/auth/login", drogon::Post);
    ADD_METHOD_TO(AuthBidController::me, "/api/auth/me", drogon::Get);
    ADD_METHOD_TO(AuthBidController::logout, "/api/auth/logout", drogon::Post);
    ADD_METHOD_TO(AuthBidController::bids, "/api/lots/{1}/bids", drogon::Get);
    ADD_METHOD_TO(AuthBidController::placeBid, "/api/lots/{1}/bids", drogon::Post);
    METHOD_LIST_END

    void registerUser(const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&&) const;
    void login(const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&&) const;
    void me(const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&&) const;
    void logout(const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&&) const;
    void bids(const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&&, std::string id) const;
    void placeBid(const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&&, std::string id) const;
};
}
