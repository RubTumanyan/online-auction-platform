#pragma once

#include <memory>
#include <string>

namespace auction::services
{
// Pluggable transactional email delivery. The default implementation posts a
// JSON payload to a simple HTTPS email API (Resend-compatible format). Tests
// install an in-memory fake so real emails are never sent.
class EmailSender
{
  public:
    virtual ~EmailSender() = default;
    virtual void sendVerificationCode(const std::string& email, const std::string& code) = 0;
};

// Returns the active sender. If none has been installed, a default sender is
// created lazily: a real HttpEmailSender when a provider URL and an API token
// (custom_config.email, AUCTION_EMAIL_API_TOKEN) are configured, otherwise a
// development fallback that logs the code to the console and to
// runtime/verification-codes.log. Tests install an in-memory fake.
EmailSender& verificationEmailSender();

// Test hook: replaces the active sender (e.g. with an in-memory fake). The old
// sender is destroyed, so tests must keep any capture storage alive separately.
void installVerificationEmailSender(std::unique_ptr<EmailSender> sender);
}