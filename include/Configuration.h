#pragma once

#include "Automation.h"
#include "JoystickShortcut.h"
#include "KeyboardShortcut.h"
#include "PowerTypes.h"

#include <filesystem>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace AbsolutePower {

struct ConfigurationData {
    bool enableLog{true};
    bool automationEnabled{};
    std::string startupPreset{"Balanced"};
    std::vector<Preset> presets;
    std::vector<AutomationRule> rules;
    std::vector<PresetShortcut> keyboardShortcuts;
    std::vector<JoystickShortcut> joystickShortcuts;
};

struct KeyboardShortcutEdit {
    std::string presetId;
    std::optional<KeyboardChord> chord;
};

enum class ConfigurationSourceKind : std::uint8_t {
    BuiltIn,
    Defaults,
    Import,
    Custom,
};

struct ConfigurationRecordSource {
    std::string recordId;
    ConfigurationSourceKind baseKind{ConfigurationSourceKind::BuiltIn};
    std::string baseLabel;
    std::filesystem::path basePath;
    bool userOverride{};
};

struct LoadedConfiguration {
    // `inherited` is defaults plus filename-ordered imports. `effective` adds
    // the user-owned custom overlay. Keeping both lets Power write sparse
    // overrides without asking a frontend to understand precedence.
    ConfigurationData inherited;
    ConfigurationData effective;
    std::vector<ConfigurationRecordSource> presetSources;
    std::vector<ConfigurationRecordSource> ruleSources;
};

enum class SaveConfigurationResult : std::uint8_t {
    Ok,
    InvalidDraft,
    WriteFailure,
    ReloadFailure,
    VerificationMismatch,
};

struct SaveConfigurationReport {
    SaveConfigurationResult result{SaveConfigurationResult::InvalidDraft};
    std::string detail;
    LoadedConfiguration configuration;
};

namespace Configuration {
// Allocates a stable INI-safe ID that does not collide case-insensitively with
// the effective draft. Frontends never guess CustomN identifiers themselves.
[[nodiscard]] std::string AllocatePresetId(
    std::span<const Preset> presets,
    std::span<const Preset> reserved = {});
[[nodiscard]] std::string AllocateRuleId(
    std::span<const AutomationRule> rules,
    std::span<const AutomationRule> reserved = {});
ConfigurationData Load(const std::filesystem::path& defaultsPath,
                       const std::filesystem::path& importsDirectory,
                       const std::filesystem::path& customPath);
LoadedConfiguration LoadDetailed(const std::filesystem::path& defaultsPath,
                                 const std::filesystem::path& importsDirectory,
                                 const std::filesystem::path& customPath);
[[nodiscard]] SaveConfigurationReport Save(
    const std::filesystem::path& defaultsPath,
    const std::filesystem::path& importsDirectory,
    const std::filesystem::path& customPath,
    const ConfigurationData& inherited,
    const ConfigurationData& desired);
[[nodiscard]] bool WriteKeyboardShortcut(const std::filesystem::path& customPath,
                                         std::string_view presetId,
                                         std::optional<KeyboardChord> chord);
[[nodiscard]] bool WriteKeyboardShortcuts(const std::filesystem::path& customPath,
                                          std::span<const KeyboardShortcutEdit> edits);
[[nodiscard]] bool WriteJoystickShortcut(const std::filesystem::path& customPath,
                                         std::string_view presetId,
                                         std::optional<std::string_view> token);
[[nodiscard]] bool WriteJoystickShortcuts(const std::filesystem::path& customPath,
                                          std::span<const JoystickShortcutEdit> edits);
[[nodiscard]] bool WriteAutomationEnabled(const std::filesystem::path& customPath,
                                          bool enabled);
} // namespace Configuration

} // namespace AbsolutePower
