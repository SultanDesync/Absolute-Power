#pragma once

#include "SFSEInterface.h"

#include <string_view>

#ifndef PLUGIN_VERSION_MAJOR
#define PLUGIN_VERSION_MAJOR 0
#endif
#ifndef PLUGIN_VERSION_MINOR
#define PLUGIN_VERSION_MINOR 0
#endif
#ifndef PLUGIN_VERSION_PATCH
#define PLUGIN_VERSION_PATCH 0
#endif
#ifndef PLUGIN_VERSION_PRERELEASE
#define PLUGIN_VERSION_PRERELEASE dev
#endif

#define ABSOLUTE_POWER_STRINGIZE_IMPL(value) #value
#define ABSOLUTE_POWER_STRINGIZE(value) ABSOLUTE_POWER_STRINGIZE_IMPL(value)

namespace Plugin {
using namespace std::string_view_literals;

inline constexpr auto Name = "AbsolutePower"sv;
inline constexpr auto FriendlyName = "Absolute Power"sv;
inline constexpr auto Author = "Antigravity"sv;
inline constexpr auto Version =
    REL::Version{PLUGIN_VERSION_MAJOR, PLUGIN_VERSION_MINOR, PLUGIN_VERSION_PATCH, 0};
inline constexpr std::string_view VersionString =
    ABSOLUTE_POWER_STRINGIZE(PLUGIN_VERSION_MAJOR) "."
    ABSOLUTE_POWER_STRINGIZE(PLUGIN_VERSION_MINOR) "."
    ABSOLUTE_POWER_STRINGIZE(PLUGIN_VERSION_PATCH) "-"
    ABSOLUTE_POWER_STRINGIZE(PLUGIN_VERSION_PRERELEASE);
} // namespace Plugin
