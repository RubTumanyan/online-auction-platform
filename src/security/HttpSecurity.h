#pragma once

#include <drogon/drogon.h>
#include <json/json.h>
#include <string>

namespace auction::security
{
    void registerHttpSecurity();
    bool isHostAllowed(const drogon::HttpRequestPtr& request);
    std::string clientIp(const drogon::HttpRequestPtr& request);
    std::size_t wsMaxConnections();
    std::size_t wsMaxConnectionsPerIp();
    void relaxSecurityForTests(Json::Value& config);
}
