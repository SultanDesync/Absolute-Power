#pragma once

#include "Automation.h"
#include "PowerTypes.h"

#include <filesystem>
#include <string>
#include <vector>

namespace AbsolutePower {

struct ConfigurationData {
    bool enableLog{true};
    bool automationEnabled{};
    std::string startupPreset{"Balanced"};
    std::vector<Preset> presets;
    std::vector<AutomationRule> rules;
};

namespace Configuration {
ConfigurationData Load(const std::filesystem::path& defaultsPath,
                       const std::filesystem::path& customPath);
} // namespace Configuration

} // namespace AbsolutePower
