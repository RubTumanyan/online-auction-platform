#pragma once

#include <filesystem>

namespace auction
{
// Resolve an explicit config against the caller's working directory, then
// root Drogon's relative resource paths beside the running executable.
std::filesystem::path prepareRuntime(const char* configPath = nullptr);
}
