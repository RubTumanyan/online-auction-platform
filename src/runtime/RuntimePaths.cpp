#include "runtime/RuntimePaths.h"

#include <stdexcept>
#include <system_error>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace auction
{
namespace
{
std::filesystem::path executablePath()
{
#ifdef _WIN32
    std::vector<wchar_t> buffer(512);
    for (;;)
    {
        const auto length = GetModuleFileNameW(
            nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0)
        {
            throw std::system_error(static_cast<int>(GetLastError()),
                                    std::system_category(),
                                    "Cannot locate the executable");
        }
        if (length < buffer.size())
        {
            return std::filesystem::path(std::wstring(buffer.data(), length));
        }
        buffer.resize(buffer.size() * 2);
    }
#elif defined(__linux__)
    return std::filesystem::canonical("/proc/self/exe");
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buffer(size);
    if (_NSGetExecutablePath(buffer.data(), &size) != 0)
    {
        throw std::runtime_error("Cannot locate the executable");
    }
    return std::filesystem::canonical(buffer.data());
#else
#error Unsupported platform: implement executablePath for this operating system.
#endif
}
}

std::filesystem::path prepareRuntime(const char* configPath)
{
    const auto runtimeDirectory = executablePath().parent_path();
    const auto config = configPath != nullptr
        ? std::filesystem::absolute(configPath)
        : runtimeDirectory / "config" / "config.example.json";
    if (!std::filesystem::is_regular_file(config))
    {
        throw std::runtime_error("Config file " + config.string() +
                                " not found. Build the auction_server target "
                                "to copy its runtime files.");
    }
    std::filesystem::current_path(runtimeDirectory);
    return config;
}
}
