#include "security/HttpSecurity.h"

#include <algorithm>
#include <chrono>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace auction::security
{
namespace
{
struct RateLimitRule
{
    std::size_t max = 0;
    std::chrono::seconds window{0};
};

std::unordered_map<std::string, RateLimitRule> defaultRules()
{
    return {
        {"auth_login",    {5,  std::chrono::seconds{60}}},
        {"auth_register", {3,  std::chrono::seconds{300}}},
        {"auth_resend",   {5,  std::chrono::seconds{60}}},
        {"auth_verify",   {10, std::chrono::seconds{60}}},
        {"bid",           {20, std::chrono::seconds{60}}},
        {"ws",            {200, std::chrono::seconds{60}}},
        {"general",       {1200, std::chrono::seconds{60}}},
    };
}

std::vector<std::string> defaultAllowedHosts()
{
    return {"127.0.0.1", "localhost",
            "bid-quick.site", "www.bid-quick.site",
            "nanny-fedora-alto.ngrok-free.dev"};
}

std::string parseHostname(const std::string& host)
{
    if (host.empty()) return host;
    if (host.front() == '[')
    {
        auto close = host.find(']');
        return close != std::string::npos ? host.substr(1, close - 1) : host.substr(1);
    }
    auto colon = host.rfind(':');
    if (colon == std::string::npos) return host;
    auto port = host.substr(colon + 1);
    if (std::all_of(port.begin(), port.end(), [](unsigned char c){ return std::isdigit(c); }))
        return host.substr(0, colon);
    return host;
}

std::string toLower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return std::tolower(c); });
    return s;
}

bool isLoopback(const std::string& ip)
{
    return ip == "127.0.0.1" || ip == "::1" || ip == "localhost";
}

std::string extractFirstIp(const std::string& value)
{
    auto comma = value.find(',');
    return comma != std::string::npos ? value.substr(0, comma) : value;
}

std::string trim(const std::string& s)
{
    auto start = s.find_first_not_of(" \t\r\n");
    auto end   = s.find_last_not_of(" \t\r\n");
    if (start == std::string::npos) return {};
    return s.substr(start, end - start + 1);
}

std::string ruleFor(const drogon::HttpRequestPtr& req)
{
    const auto path   = req->getPath();
    const auto method = req->getMethodString();
    if (path.starts_with("/api/auth/login")    && std::string(method) == "POST") return "auth_login";
    if (path.starts_with("/api/auth/register") && std::string(method) == "POST") return "auth_register";
    if (path.starts_with("/api/auth/resend-verification") && std::string(method) == "POST") return "auth_resend";
    if (path.starts_with("/api/auth/verify-email") && std::string(method) == "POST") return "auth_verify";
    if (path.starts_with("/api/lots/") && path.ends_with("/bids") && std::string(method) == "POST") return "bid";
    if (path.starts_with("/ws/lots/")) return "ws";
    return "general";
}

bool legacyHopByHopHeader(const std::string_view name)
{
    static const char* const kHeaders[] = {
        "connection","keep-alive","proxy-authenticate","proxy-authorization",
        "te","trailers","transfer-encoding","upgrade","content-encoding","content-length"
    };
    for (const auto* h : kHeaders)
        if (name == h) return true;
    return false;
}

class HostAllowList final
{
public:
    explicit HostAllowList(const std::vector<std::string>& hosts)
    {
        for (auto& h : hosts) allowed_.insert(toLower(h));
    }
    bool contains(const std::string& hostname) const
    {
        return allowed_.count(toLower(hostname));
    }
private:
    std::unordered_set<std::string> allowed_;
};

class SlidingWindowThrottle final
{
public:
    explicit SlidingWindowThrottle(std::unordered_map<std::string, RateLimitRule> rules)
        : rules_(std::move(rules)) {}

    bool allow(const std::string& key, std::size_t& retryAfter)
    {
        const auto it = rules_.find(key.substr(key.find('|') + 1));
        if (it == rules_.end()) return true;
        const auto& rule = it->second;

        std::lock_guard lock(mutex_);
        if (buckets_.size() > 20000) buckets_.clear();
        auto now = std::chrono::steady_clock::now();
        auto& q  = buckets_[key];
        while (!q.empty() && (now - q.front()) > rule.window)
            q.pop_front();
        if (q.size() >= rule.max)
        {
            retryAfter = std::max<std::size_t>(
                1, std::chrono::ceil<std::chrono::seconds>(rule.window - (now - q.front())).count());
            return false;
        }
        q.push_back(now);
        return true;
    }

private:
    std::unordered_map<std::string, RateLimitRule> rules_;
    std::unordered_map<std::string, std::deque<std::chrono::steady_clock::time_point>> buckets_;
    std::mutex mutex_;
};

class Config final
{
public:
    static Config& instance()
    {
        static Config c;
        return c;
    }
    void load(const Json::Value& security)
    {
        {
            std::vector<std::string> hosts;
            for (const auto& h : security["allowed_hosts"])
                hosts.push_back(h.asString());
            if (hosts.empty()) hosts = defaultAllowedHosts();
            hosts_ = std::make_unique<HostAllowList>(hosts);
        }
        {
            auto rules = defaultRules();
            for (auto it = security["rate_limits"].begin(); it != security["rate_limits"].end(); ++it)
            {
                auto& r = rules[it.name()];
                r.max = it->get("max", static_cast<Json::UInt64>(r.max)).asUInt64();
                r.window = std::chrono::seconds(it->get("window_seconds",
                    static_cast<Json::UInt64>(r.window.count())).asUInt64());
            }
            throttle_ = std::make_unique<SlidingWindowThrottle>(std::move(rules));
        }
        wsMax_        = security.get("ws_max_connections",
                            security.isMember("ws_max_connections") ? security["ws_max_connections"].asUInt64() : 1000).asUInt64();
        wsMaxPerIp_   = security.get("ws_max_connections_per_ip",
                            security.isMember("ws_max_connections_per_ip") ? security["ws_max_connections_per_ip"].asUInt64() : 20).asUInt64();
    }

    [[nodiscard]] bool isHostAllowed(const std::string& hostname) const
    {
        return hosts_ && hosts_->contains(hostname);
    }

    [[nodiscard]] bool allowRequest(const std::string& key, std::size_t& retryAfter) const
    {
        return throttle_ && throttle_->allow(key, retryAfter);
    }

    [[nodiscard]] std::size_t wsMax() const { return wsMax_; }
    [[nodiscard]] std::size_t wsMaxPerIp() const { return wsMaxPerIp_; }

private:
    Config() = default;
    std::unique_ptr<HostAllowList> hosts_;
    std::unique_ptr<SlidingWindowThrottle> throttle_;
    std::size_t wsMax_      = 1000;
    std::size_t wsMaxPerIp_ = 20;
};

drogon::HttpResponsePtr jsonErrorResponse(drogon::HttpStatusCode status, const std::string& message)
{
    Json::Value body;
    body["error"] = message;
    auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
    resp->setStatusCode(status);
    return resp;
}

std::string clientIpFromRequest(const drogon::HttpRequestPtr& req)
{
    const auto peer = req->getPeerAddr().toIp();
    if (!isLoopback(peer)) return peer;
    if (auto cf = trim(req->getHeader("CF-Connecting-IP")); !cf.empty()) return extractFirstIp(cf);
    if (auto xff = trim(req->getHeader("X-Forwarded-For")); !xff.empty()) return extractFirstIp(xff);
    if (auto xr = trim(req->getHeader("X-Real-IP")); !xr.empty()) return xr;
    return peer;
}

std::string hostFromRequest(const drogon::HttpRequestPtr& req)
{
    return parseHostname(trim(req->getHeader("host")));
}
}

bool isHostAllowed(const drogon::HttpRequestPtr& request)
{
    const auto hostname = hostFromRequest(request);
    return !hostname.empty() && Config::instance().isHostAllowed(hostname);
}

std::string clientIp(const drogon::HttpRequestPtr& request)
{
    return clientIpFromRequest(request);
}

void registerHttpSecurity()
{
    Config::instance().load(drogon::app().getCustomConfig().get("security", Json::Value(Json::objectValue)));
    const auto& security = drogon::app().getCustomConfig().get("security", Json::Value(Json::objectValue));
    drogon::app()
        .setClientMaxBodySize(security.get("client_max_body_bytes",
            security.isMember("client_max_body_bytes") ? security["client_max_body_bytes"].asUInt64() : 65536).asUInt64())
        .setMaxConnectionNumPerIP(512);
    LOG_INFO << "[SECURITY] Middleware configured (host allow-list, rate limiting, "
             << "WS connection caps, hardening headers)";

    drogon::app().registerPreRoutingAdvice(
        [](const drogon::HttpRequestPtr& req,
           drogon::AdviceCallback&& adviceCallback,
           drogon::AdviceChainCallback&& chainCallback)
        {
            const auto hostname = hostFromRequest(req);
            if (hostname.empty() || !Config::instance().isHostAllowed(hostname))
            {
                LOG_WARN << "[SECURITY] Request rejected: host not allowed, host=" << hostname
                         << " ip=" << clientIpFromRequest(req);
                adviceCallback(jsonErrorResponse(drogon::k404NotFound, "Not found"));
                return;
            }
            const auto rule = ruleFor(req);
            const auto key  = clientIpFromRequest(req) + "|" + rule;
            std::size_t retryAfter = 0;
            if (!Config::instance().allowRequest(key, retryAfter))
            {
                LOG_WARN << "[SECURITY] Rate limit exceeded, ip=" << clientIpFromRequest(req)
                         << " rule=" << rule << " retry-after=" << retryAfter;
                auto resp = jsonErrorResponse(drogon::k429TooManyRequests, "Rate limit exceeded; please retry later");
                resp->addHeader("Retry-After", std::to_string(retryAfter));
                adviceCallback(resp);
                return;
            }
            chainCallback();
        });

    drogon::app().registerPreSendingAdvice(
        [](const drogon::HttpRequestPtr&, const drogon::HttpResponsePtr& resp)
        {
            resp->addHeader("Content-Security-Policy",
                "default-src 'self'; script-src 'self' https://static.cloudflareinsights.com; style-src 'self'; img-src 'self' data:; "
                "connect-src 'self' ws: wss:; font-src 'self'; object-src 'none'; "
                "frame-ancestors 'none'; base-uri 'self'; form-action 'self'");
            resp->addHeader("X-Content-Type-Options", "nosniff");
            resp->addHeader("X-Frame-Options", "DENY");
            resp->addHeader("Referrer-Policy", "no-referrer");
            resp->addHeader("Permissions-Policy", "camera=(), microphone=(), geolocation=()");
            resp->addHeader("Strict-Transport-Security", "max-age=63072000");
            resp->addHeader("X-Permitted-Cross-Domain-Policies", "none");
            resp->addHeader("X-DNS-Prefetch-Control", "off");
        });
}

std::size_t wsMaxConnections()
{
    return Config::instance().wsMax();
}

std::size_t wsMaxConnectionsPerIp()
{
    return Config::instance().wsMaxPerIp();
}

void relaxSecurityForTests(Json::Value& config)
{
    auto& sec = config["custom_config"]["security"];
    sec = Json::objectValue;
    sec["allowed_hosts"]  = Json::arrayValue;
    sec["allowed_hosts"].append("127.0.0.1");
    sec["allowed_hosts"].append("localhost");
    sec["rate_limits"] = Json::objectValue;
    for (const auto* name : {"auth_login","auth_register","auth_resend","auth_verify","bid","ws","general"})
    {
        sec["rate_limits"][name]["max"] = static_cast<Json::UInt64>(1000000);
        sec["rate_limits"][name]["window_seconds"] = static_cast<Json::UInt64>(1);
    }
    sec["ws_max_connections"] = static_cast<Json::UInt64>(10000);
    sec["ws_max_connections_per_ip"] = static_cast<Json::UInt64>(10000);
}
}
