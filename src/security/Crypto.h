#pragma once

#include <string>

namespace auction::security
{
std::string hashPassword(const std::string& password);
bool verifyPassword(const std::string& password, const std::string& encodedHash);
std::string createToken();
std::string hashToken(const std::string& token);
std::string generateVerificationCode();
std::string hashVerificationCode(const std::string& code);
bool timingSafeEqual(const std::string& left, const std::string& right);
}
