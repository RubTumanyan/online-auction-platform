#pragma once

#include <drogon/HttpController.h>

namespace auction
{
class CatalogController final : public drogon::HttpController<CatalogController>
{
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(CatalogController::categories, "/api/categories", drogon::Get);
    ADD_METHOD_TO(CatalogController::lots, "/api/lots", drogon::Get);
    ADD_METHOD_TO(CatalogController::lot, "/api/lots/{1}", drogon::Get);
    ADD_METHOD_TO(CatalogController::recommendations, "/api/recommendations", drogon::Get);
    ADD_METHOD_TO(CatalogController::trackView, "/api/lots/{1}/view", drogon::Post);
    METHOD_LIST_END

    void categories(const drogon::HttpRequestPtr& request,
                    std::function<void(const drogon::HttpResponsePtr&)>&& callback) const;
    void lots(const drogon::HttpRequestPtr& request,
              std::function<void(const drogon::HttpResponsePtr&)>&& callback) const;
    void lot(const drogon::HttpRequestPtr& request,
             std::function<void(const drogon::HttpResponsePtr&)>&& callback,
             std::string id) const;
    void recommendations(const drogon::HttpRequestPtr& request,
                         std::function<void(const drogon::HttpResponsePtr&)>&& callback) const;
    void trackView(const drogon::HttpRequestPtr& request,
                   std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                   std::string id) const;
};
}
