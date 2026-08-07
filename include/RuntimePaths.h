#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace RuntimePaths {
std::filesystem::path PluginDirectory();
std::filesystem::path DefaultsPath();
std::filesystem::path CustomPath();
std::filesystem::path WorkbenchPath();
std::filesystem::path LogPath();

void InitializeLogging();
bool IsLoggingEnabled();
void Log(std::string_view area, std::string_view message, bool force = false);
} // namespace RuntimePaths
