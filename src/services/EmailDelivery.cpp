#include "services/EmailDelivery.h"

#include "services/AuthBidService.h"

#include <drogon/HttpClient.h>
#include <drogon/drogon.h>
#include <json/json.h>

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

namespace auction::services
{
namespace
{
struct EmailSettings
{
    std::string providerUrl;
    std::string endpointPath;
    std::string from;
    std::string apiToken;
};

std::string environmentVariable(const std::string& name)
{
    const char* value = std::getenv(name.c_str());
    return value ? std::string(value) : std::string{};
}

EmailSettings readEmailSettings()
{
    const auto& email = drogon::app().getCustomConfig().get("email", Json::Value(Json::objectValue));
    EmailSettings settings;
    settings.providerUrl = email.get("provider_url", "https://api.resend.com").asString();
    settings.endpointPath = email.get("endpoint_path", "/emails").asString();
    settings.from = email.get("from", "QuickBid <no-reply@example.com>").asString();
    settings.apiToken = environmentVariable(email.get("api_token_env", "AUCTION_EMAIL_API_TOKEN").asString());
    if (settings.providerUrl.empty() || settings.endpointPath.empty() || settings.from.empty())
        settings.providerUrl.clear();
    return settings;
}

// Resend-compatible transactional email API. The same JSON shape is used by
// several simple providers; the endpoint and credentials are configurable.
class HttpEmailSender final : public EmailSender
{
  public:
    HttpEmailSender(const EmailSettings& settings) : settings_(settings) {}

    void sendVerificationCode(const std::string& email, const std::string& code) override
    {
        if (settings_.providerUrl.empty() || settings_.apiToken.empty())
        {
            throw services::ApiError(services::ApiErrorKind::emailFailure,
                                     "Email service is not configured; set AUCTION_EMAIL_API_TOKEN");
        }
        Json::Value message;
        message["from"] = settings_.from;
        message["to"] = email;
        message["subject"] = "Verify your email";
        message["text"] = "Your Online Auction verification code is: " + code +
                          ". This code expires in 10 minutes.";
        auto request = drogon::HttpRequest::newHttpJsonRequest(message);
        request->setMethod(drogon::Post);
        request->setPath(settings_.endpointPath);
        request->addHeader("Authorization", "Bearer " + settings_.apiToken);
        auto client = drogon::HttpClient::newHttpClient(settings_.providerUrl);
        const auto [result, response] = client->sendRequest(request, 10.0);
        const auto httpStatus = response != nullptr ? response->statusCode() : 0;
        const auto responseBody = response != nullptr && !response->body().empty()
            ? std::string(response->body().substr(0, 400))
            : std::string{};
        if (result != drogon::ReqResult::Ok || response == nullptr ||
            httpStatus < 200 || httpStatus >= 300)
        {
            throw services::ApiError(services::ApiErrorKind::emailFailure,
                                     "Email service rejected the delivery request (result=" +
                                         std::to_string(static_cast<int>(result)) + ", http=" +
                                         std::to_string(httpStatus) + ", body=" + responseBody + ")");
        }
    }

  private:
    EmailSettings settings_;
};

// Development-only fallback: when no email provider is configured, the code is
// printed to the server log and appended to runtime/verification-codes.log so
// the verification flow can still be exercised locally. Never use in production.
class DevLogEmailSender final : public EmailSender
{
  public:
    void sendVerificationCode(const std::string& email, const std::string& code) override
    {
        const auto now = std::chrono::system_clock::now();
        std::time_t raw = std::chrono::system_clock::to_time_t(now);
        std::tm utc{};
#if defined(_WIN32)
        gmtime_s(&utc, &raw);
#else
        gmtime_r(&raw, &utc);
#endif
        char stamp[32];
        std::strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%SZ", &utc);
        const auto entry = std::string(stamp) + " " + email + " " + code;
        LOG_INFO << "Verification code for " << email << ": " << code;
        std::lock_guard<std::mutex> lock(mutex_);
        try
        {
            std::filesystem::create_directories("runtime");
            std::ofstream output("runtime/verification-codes.log", std::ios::app);
            if (output) output << entry << '\n';
        }
        catch (const std::exception&)
        {
        }
    }

  private:
    std::mutex mutex_;
};

std::unique_ptr<EmailSender>& activeSender()
{
    static std::unique_ptr<EmailSender> sender;
    return sender;
}
}

EmailSender& verificationEmailSender()
{
    if (!activeSender())
    {
        const auto settings = readEmailSettings();
        if (settings.providerUrl.empty() || settings.apiToken.empty())
        {
            LOG_WARN << "No email provider configured (set the AUCTION_EMAIL_API_TOKEN "
                     << "env var to enable real delivery); verification codes will be "
                     << "written to runtime/verification-codes.log instead.";
            activeSender() = std::make_unique<DevLogEmailSender>();
        }
        else
        {
            activeSender() = std::make_unique<HttpEmailSender>(settings);
        }
    }
    return *activeSender();
}

void installVerificationEmailSender(std::unique_ptr<EmailSender> sender)
{
    activeSender() = std::move(sender);
}
}