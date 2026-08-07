#include "Configuration.h"

#include <windows.h>

#include <algorithm>
#include <charconv>
#include <cwctype>
#include <limits>
#include <optional>
#include <string_view>

namespace AbsolutePower::Configuration {
namespace {

std::string Narrow(std::wstring_view value) {
    if (value.empty()) {
        return {};
    }
    const auto count = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                                           nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(std::max(0, count)), '\0');
    if (count > 0) {
        WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                            result.data(), count, nullptr, nullptr);
    }
    return result;
}

std::wstring Widen(std::string_view value) {
    if (value.empty()) {
        return {};
    }
    const auto count = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                                           nullptr, 0);
    std::wstring result(static_cast<std::size_t>(std::max(0, count)), L'\0');
    if (count > 0) {
        MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                            result.data(), count);
    }
    return result;
}

std::string Lower(std::string_view value) {
    std::string result(value);
    std::ranges::transform(result, result.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return result;
}

std::optional<std::string> ReadValue(const std::filesystem::path& path,
                                     std::wstring_view section, std::wstring_view key) {
    constexpr wchar_t sentinel[] = L"\x01";
    std::array<wchar_t, 1024> buffer{};
    GetPrivateProfileStringW(section.data(), key.data(), sentinel, buffer.data(),
                             static_cast<DWORD>(buffer.size()), path.c_str());
    if (std::wstring_view(buffer.data()) == sentinel) {
        return std::nullopt;
    }
    return Narrow(buffer.data());
}

std::vector<std::string> ReadSections(const std::filesystem::path& path) {
    std::vector<wchar_t> buffer(4096);
    while (buffer.size() <= 65536) {
        const auto copied = GetPrivateProfileSectionNamesW(
            buffer.data(), static_cast<DWORD>(buffer.size()), path.c_str());
        if (copied + 2 < buffer.size()) {
            std::vector<std::string> result;
            const wchar_t* cursor = buffer.data();
            while (*cursor) {
                const std::wstring_view section(cursor);
                result.push_back(Narrow(section));
                cursor += section.size() + 1;
            }
            return result;
        }
        buffer.resize(buffer.size() * 2);
    }
    return {};
}

bool ParseBool(std::string_view value, bool fallback) {
    const auto normalized = Lower(value);
    if (normalized == "1" || normalized == "true" || normalized == "yes" ||
        normalized == "on") {
        return true;
    }
    if (normalized == "0" || normalized == "false" || normalized == "no" ||
        normalized == "off") {
        return false;
    }
    return fallback;
}

template <class T>
std::optional<T> ParseUnsigned(std::string_view text) {
    unsigned long long value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() ||
        value > std::numeric_limits<T>::max()) {
        return std::nullopt;
    }
    return static_cast<T>(value);
}

SystemId ParseSystem(std::string_view value) {
    const auto normalized = Lower(value);
    for (const auto system : kCockpitOrder) {
        if (Lower(SystemKey(system)) == normalized) {
            return system;
        }
    }
    return SystemId::Invalid;
}

TriggerKind ParseTrigger(std::string_view value, TriggerKind fallback) {
    const auto normalized = Lower(value);
    if (normalized == "weaponfired") return TriggerKind::WeaponFired;
    if (normalized == "incomingdamage") return TriggerKind::IncomingDamage;
    if (normalized == "throttleabove") return TriggerKind::ThrottleAbove;
    if (normalized == "manual") return TriggerKind::Manual;
    return fallback;
}

std::optional<TierPlan> ParseTierPlan(std::string_view value) {
    std::array<std::uint16_t, 3> values{};
    std::size_t start{};
    for (std::size_t index = 0; index < values.size(); ++index) {
        const auto comma = value.find(',', start);
        const auto end = comma == std::string_view::npos ? value.size() : comma;
        const auto parsed = ParseUnsigned<std::uint16_t>(value.substr(start, end - start));
        if (!parsed) return std::nullopt;
        values[index] = *parsed;
        start = end + 1;
        if (index < values.size() - 1 && comma == std::string_view::npos) return std::nullopt;
    }
    if (start < value.size() + 1) return std::nullopt;
    return TierPlan{values[0], values[1], values[2]};
}

void ApplyPresetSection(ConfigurationData& data, const std::filesystem::path& path,
                        std::string_view section) {
    const auto id = std::string(section.substr(std::string_view("Preset.").size()));
    if (id.empty()) return;
    const auto wideSection = Widen(section);
    auto found = std::ranges::find(data.presets, id, &Preset::id);
    if (const auto deleted = ReadValue(path, wideSection, L"Deleted");
        deleted && ParseBool(*deleted, false)) {
        if (found != data.presets.end()) data.presets.erase(found);
        return;
    }
    if (found == data.presets.end()) {
        data.presets.push_back(Preset{.id = id, .displayName = id});
        found = std::prev(data.presets.end());
    }
    auto& preset = *found;
    if (const auto name = ReadValue(path, wideSection, L"Name")) preset.displayName = *name;
    for (const auto system : kCockpitOrder) {
        const auto wideKey = Widen(SystemKey(system));
        if (const auto value = ReadValue(path, wideSection, wideKey)) {
            if (const auto parsed = ParseTierPlan(*value)) preset.systems[ToIndex(system)] = *parsed;
        }
    }
    if (const auto order = ReadValue(path, wideSection, L"Order")) {
        std::array<SystemId, kSystemCount> parsed{};
        std::size_t count{};
        std::size_t start{};
        while (count < parsed.size() && start <= order->size()) {
            const auto comma = order->find(',', start);
            const auto end = comma == std::string::npos ? order->size() : comma;
            parsed[count++] = ParseSystem(std::string_view(*order).substr(start, end - start));
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
        if (count == parsed.size()) preset.tieBreakOrder = parsed;
    }
}

void ApplyRuleSection(ConfigurationData& data, const std::filesystem::path& path,
                      std::string_view section) {
    const auto id = std::string(section.substr(std::string_view("Rule.").size()));
    if (id.empty()) return;
    const auto wideSection = Widen(section);
    auto found = std::ranges::find(data.rules, id, &AutomationRule::id);
    if (const auto deleted = ReadValue(path, wideSection, L"Deleted");
        deleted && ParseBool(*deleted, false)) {
        if (found != data.rules.end()) data.rules.erase(found);
        return;
    }
    if (found == data.rules.end()) {
        data.rules.push_back(AutomationRule{.id = id, .displayName = id});
        found = std::prev(data.rules.end());
    }
    auto& rule = *found;
    if (const auto value = ReadValue(path, wideSection, L"Name")) rule.displayName = *value;
    if (const auto value = ReadValue(path, wideSection, L"Enabled"))
        rule.enabled = ParseBool(*value, rule.enabled);
    if (const auto value = ReadValue(path, wideSection, L"Trigger"))
        rule.trigger = ParseTrigger(*value, rule.trigger);
    if (const auto value = ReadValue(path, wideSection, L"Source"))
        rule.sourceSystem = Lower(*value) == "any" ? SystemId::Invalid : ParseSystem(*value);
    if (const auto value = ReadValue(path, wideSection, L"Target"))
        rule.targetSystem = ParseSystem(*value);
    if (const auto value = ReadValue(path, wideSection, L"TargetPips")) {
        rule.targetPips = Lower(*value) == "max" ? std::numeric_limits<std::uint16_t>::max()
                                                  : ParseUnsigned<std::uint16_t>(*value).value_or(rule.targetPips);
    }
    if (const auto value = ReadValue(path, wideSection, L"ThresholdPercent"))
        rule.thresholdPercent = ParseUnsigned<std::uint8_t>(*value).value_or(rule.thresholdPercent);
    if (const auto value = ReadValue(path, wideSection, L"HysteresisPercent"))
        rule.hysteresisPercent = ParseUnsigned<std::uint8_t>(*value).value_or(rule.hysteresisPercent);
    if (const auto value = ReadValue(path, wideSection, L"HoldMilliseconds"))
        rule.holdMilliseconds = ParseUnsigned<std::uint32_t>(*value).value_or(rule.holdMilliseconds);
    if (const auto value = ReadValue(path, wideSection, L"Priority"))
        rule.priority = ParseUnsigned<std::uint16_t>(*value).value_or(rule.priority);
}

void ApplyFile(ConfigurationData& data, const std::filesystem::path& path) {
    if (const auto value = ReadValue(path, L"General", L"bEnableLog"))
        data.enableLog = ParseBool(*value, data.enableLog);
    if (const auto value = ReadValue(path, L"General", L"bAutomationEnabled"))
        data.automationEnabled = ParseBool(*value, data.automationEnabled);
    if (const auto value = ReadValue(path, L"General", L"sStartupPreset"))
        data.startupPreset = *value;

    for (const auto& section : ReadSections(path)) {
        if (section.starts_with("Preset.")) {
            ApplyPresetSection(data, path, section);
        } else if (section.starts_with("Rule.")) {
            ApplyRuleSection(data, path, section);
        }
    }
}

} // namespace

ConfigurationData Load(const std::filesystem::path& defaultsPath,
                       const std::filesystem::path& customPath) {
    ConfigurationData data{};
    if (std::filesystem::exists(defaultsPath)) ApplyFile(data, defaultsPath);
    if (std::filesystem::exists(customPath)) ApplyFile(data, customPath);
    if (data.presets.empty()) {
        Preset balanced{.id = "Balanced", .displayName = "Balanced"};
        for (auto& system : balanced.systems) system.green = 1;
        data.presets.push_back(std::move(balanced));
    }
    return data;
}

} // namespace AbsolutePower::Configuration
