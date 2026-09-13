#include "security/Crypto.h"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include <array>
#include <charconv>
#include <cstdint>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace auction::security
{
namespace
{
constexpr int kIterations = 210000;
constexpr std::size_t kSaltBytes = 16;
constexpr std::size_t kHashBytes = 32;

std::string hex(const unsigned char* bytes, std::size_t size)
{
    constexpr char digits[] = "0123456789abcdef";
    std::string result(size * 2, '0');
    for (std::size_t index = 0; index < size; ++index)
    {
        result[index * 2] = digits[bytes[index] >> 4];
        result[index * 2 + 1] = digits[bytes[index] & 0x0f];
    }
    return result;
}

std::vector<unsigned char> unhex(std::string_view value)
{
    if (value.size() % 2 != 0) return {};
    std::vector<unsigned char> result(value.size() / 2);
    for (std::size_t index = 0; index < result.size(); ++index)
    {
        unsigned int byte = 0;
        const auto part = value.substr(index * 2, 2);
        const auto [end, error] = std::from_chars(part.data(), part.data() + part.size(), byte, 16);
        if (error != std::errc{} || end != part.data() + part.size()) return {};
        result[index] = static_cast<unsigned char>(byte);
    }
    return result;
}

std::array<unsigned char, kHashBytes> derive(const std::string& password,
                                             const unsigned char* salt,
                                             std::size_t saltSize,
                                             int iterations)
{
    std::array<unsigned char, kHashBytes> result{};
    if (PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()), salt,
                          static_cast<int>(saltSize), iterations, EVP_sha256(),
                          static_cast<int>(result.size()), result.data()) != 1)
        throw std::runtime_error("Password hashing failed");
    return result;
}
}

std::string hashPassword(const std::string& password)
{
    std::array<unsigned char, kSaltBytes> salt{};
    if (RAND_bytes(salt.data(), static_cast<int>(salt.size())) != 1)
        throw std::runtime_error("Secure random generation failed");
    const auto digest = derive(password, salt.data(), salt.size(), kIterations);
    return "pbkdf2_sha256$" + std::to_string(kIterations) + "$" +
           hex(salt.data(), salt.size()) + "$" + hex(digest.data(), digest.size());
}

bool verifyPassword(const std::string& password, const std::string& encodedHash)
{
    const auto first = encodedHash.find('$');
    const auto second = encodedHash.find('$', first + 1);
    const auto third = encodedHash.find('$', second + 1);
    if (first == std::string::npos || second == std::string::npos || third == std::string::npos ||
        encodedHash.substr(0, first) != "pbkdf2_sha256") return false;
    int iterations = 0;
    const auto iterationText = std::string_view(encodedHash).substr(first + 1, second - first - 1);
    const auto [end, error] = std::from_chars(iterationText.data(), iterationText.data() + iterationText.size(), iterations);
    if (error != std::errc{} || end != iterationText.data() + iterationText.size() || iterations < 100000) return false;
    const auto salt = unhex(std::string_view(encodedHash).substr(second + 1, third - second - 1));
    const auto expected = unhex(std::string_view(encodedHash).substr(third + 1));
    if (salt.empty() || expected.size() != kHashBytes) return false;
    const auto actual = derive(password, salt.data(), salt.size(), iterations);
    return CRYPTO_memcmp(actual.data(), expected.data(), expected.size()) == 0;
}

std::string createToken()
{
    std::array<unsigned char, 32> bytes{};
    if (RAND_bytes(bytes.data(), static_cast<int>(bytes.size())) != 1)
        throw std::runtime_error("Secure random generation failed");
    return hex(bytes.data(), bytes.size());
}

std::string hashToken(const std::string& token)
{
    std::array<unsigned char, SHA256_DIGEST_LENGTH> digest{};
    SHA256(reinterpret_cast<const unsigned char*>(token.data()), token.size(), digest.data());
    return hex(digest.data(), digest.size());
}
}
