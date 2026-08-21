#include "Configuration.h"

#include <windows.h>

#include <algorithm>
#include <charconv>
#include <cwctype>
#include <fstream>
#include <format>
#include <initializer_list>
#include <limits>
#include <optional>
#include <set>
#include <string_view>

namespace AbsolutePower::Configuration {
namespace {

struct FileSource {
    ConfigurationSourceKind kind{ConfigurationSourceKind::BuiltIn};
    std::string label;
    std::filesystem::path path;
};

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

ConfigurationRecordSource* FindSource(
    std::vector<ConfigurationRecordSource>& sources, std::string_view id) {
    const auto normalized = Lower(id);
    const auto found = std::ranges::find_if(sources, [&](const auto& source) {
        return Lower(source.recordId) == normalized;
    });
    return found == sources.end() ? nullptr : &*found;
}

void RecordApplied(std::vector<ConfigurationRecordSource>& sources,
                   std::string_view id, const FileSource& source,
                   bool deleted) {
    auto* record = FindSource(sources, id);
    if (deleted) {
        std::erase_if(sources, [&](const auto& candidate) {
            return Lower(candidate.recordId) == Lower(id);
        });
        return;
    }
    if (source.kind == ConfigurationSourceKind::Custom && record) {
        record->userOverride = true;
        return;
    }
    ConfigurationRecordSource replacement{
        .recordId = std::string(id),
        .baseKind = source.kind,
        .baseLabel = source.label,
        .basePath = source.path,
        .userOverride = source.kind == ConfigurationSourceKind::Custom,
    };
    if (record) {
        *record = std::move(replacement);
    } else {
        sources.push_back(std::move(replacement));
    }
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

std::vector<std::pair<std::string, std::string>> ReadSectionEntries(
    const std::filesystem::path& path, const wchar_t* sectionName) {
    std::vector<wchar_t> buffer(4096);
    while (buffer.size() <= 65536) {
        const auto copied = GetPrivateProfileSectionW(
            sectionName, buffer.data(), static_cast<DWORD>(buffer.size()), path.c_str());
        if (copied + 2 < buffer.size()) {
            std::vector<std::pair<std::string, std::string>> result;
            for (const wchar_t* cursor = buffer.data(); *cursor;
                 cursor += std::char_traits<wchar_t>::length(cursor) + 1) {
                const std::wstring_view record(cursor);
                const auto separator = record.find(L'=');
                if (separator == std::wstring_view::npos || separator == 0) continue;
                result.emplace_back(Narrow(record.substr(0, separator)),
                                    Narrow(record.substr(separator + 1)));
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

void ApplyKeyboardShortcutSection(ConfigurationData& data,
                                  const std::filesystem::path& path) {
    for (const auto& [presetId, stored] :
         ReadSectionEntries(path, L"KeyboardPresetBindings")) {
        if (presetId.empty()) continue;
        std::erase_if(data.keyboardShortcuts, [&](const PresetShortcut& shortcut) {
            return shortcut.presetId == presetId;
        });
        const auto normalized = Lower(stored);
        if (normalized == "none" || normalized == "disabled" || normalized == "unbound") {
            continue;
        }
        const auto chord = KeyboardShortcutPolicy::Parse(stored);
        if (!chord) continue;
        std::erase_if(data.keyboardShortcuts, [&](const PresetShortcut& shortcut) {
            return shortcut.chord == *chord;
        });
        data.keyboardShortcuts.push_back({presetId, *chord});
    }
}

void ApplyJoystickShortcutSection(ConfigurationData& data,
                                  const std::filesystem::path& path) {
    for (const auto& [presetId, stored] :
         ReadSectionEntries(path, L"JoystickPresetBindings")) {
        if (presetId.empty()) continue;
        std::erase_if(data.joystickShortcuts, [&](const JoystickShortcut& shortcut) {
            return shortcut.presetId == presetId;
        });
        const auto normalized = Lower(stored);
        if (normalized == "none" || normalized == "disabled" || normalized == "unbound") {
            continue;
        }
        if (!JoystickBindingPolicy::ValidToken(stored)) continue;
        std::erase_if(data.joystickShortcuts, [&](const JoystickShortcut& shortcut) {
            return shortcut.token == stored;
        });
        data.joystickShortcuts.push_back({presetId, stored});
    }
}

void ApplyFile(ConfigurationData& data, const std::filesystem::path& path,
               const FileSource& source,
               std::vector<ConfigurationRecordSource>& presetSources,
               std::vector<ConfigurationRecordSource>& ruleSources) {
    if (const auto value = ReadValue(path, L"General", L"bEnableLog"))
        data.enableLog = ParseBool(*value, data.enableLog);
    if (const auto value = ReadValue(path, L"General", L"bAutomationEnabled"))
        data.automationEnabled = ParseBool(*value, data.automationEnabled);
    if (const auto value = ReadValue(path, L"General", L"sStartupPreset")) {
        const auto normalized = Lower(*value);
        if (normalized == "none" || normalized == "disabled" || normalized.empty()) {
            data.startupPreset.clear();
        } else {
            data.startupPreset = *value;
        }
    }

    const auto hasAnyValue = [&](const std::wstring& section,
                                 std::initializer_list<const wchar_t*> keys) {
        return std::ranges::any_of(keys, [&](const auto* key) {
            return ReadValue(path, section, key).has_value();
        });
    };
    for (const auto& section : ReadSections(path)) {
        if (section.starts_with("Preset.")) {
            const auto id = std::string_view(section).substr(
                std::string_view("Preset.").size());
            const auto wideSection = Widen(section);
            const auto deleted = ReadValue(path, wideSection, L"Deleted");
            const bool recognized = hasAnyValue(
                wideSection, {L"Deleted", L"Name", L"Order", L"Weapon0",
                              L"Weapon1", L"Weapon2", L"Engine", L"Shield",
                              L"GravDrive"});
            if (source.kind == ConfigurationSourceKind::Custom && !recognized) continue;
            ApplyPresetSection(data, path, section);
            RecordApplied(presetSources, id, source,
                          deleted && ParseBool(*deleted, false));
        } else if (section.starts_with("Rule.")) {
            const auto id = std::string_view(section).substr(
                std::string_view("Rule.").size());
            const auto wideSection = Widen(section);
            const auto deleted = ReadValue(path, wideSection, L"Deleted");
            const bool recognized = hasAnyValue(
                wideSection, {L"Deleted", L"Name", L"Enabled", L"Trigger",
                              L"Source", L"Target", L"TargetPips",
                              L"ThresholdPercent", L"HysteresisPercent",
                              L"HoldMilliseconds", L"Priority"});
            if (source.kind == ConfigurationSourceKind::Custom && !recognized) continue;
            ApplyRuleSection(data, path, section);
            RecordApplied(ruleSources, id, source,
                          deleted && ParseBool(*deleted, false));
        }
    }
    ApplyKeyboardShortcutSection(data, path);
    ApplyJoystickShortcutSection(data, path);
}

bool ValidIniKey(std::string_view value) {
    if (value.empty() || value.size() >= 64) return false;
    return std::ranges::all_of(value, [](const unsigned char character) {
        return character <= 0x7F && character != '=' && character != '[' &&
               character != ']' && character != '\r' && character != '\n';
    });
}

bool ValidIniValue(std::string_view value, std::size_t capacity) {
    return !value.empty() && value.size() < capacity &&
           !value.contains('\r') && !value.contains('\n');
}

template <class Record>
const Record* FindRecord(const std::vector<Record>& records, std::string_view id) {
    const auto normalized = Lower(id);
    const auto found = std::ranges::find_if(records, [&](const auto& record) {
        return Lower(record.id) == normalized;
    });
    return found == records.end() ? nullptr : &*found;
}

bool SamePlan(const TierPlan& left, const TierPlan& right) noexcept {
    return left.green == right.green && left.yellow == right.yellow &&
           left.red == right.red;
}

bool SamePreset(const Preset& left, const Preset& right) noexcept {
    return left.id == right.id && left.displayName == right.displayName &&
           std::ranges::equal(left.systems, right.systems, SamePlan) &&
           left.tieBreakOrder == right.tieBreakOrder;
}

bool SameRule(const AutomationRule& left, const AutomationRule& right) noexcept {
    return left.id == right.id && left.displayName == right.displayName &&
           left.enabled == right.enabled && left.trigger == right.trigger &&
           left.sourceSystem == right.sourceSystem &&
           left.targetSystem == right.targetSystem &&
           left.targetPips == right.targetPips &&
           left.thresholdPercent == right.thresholdPercent &&
           left.hysteresisPercent == right.hysteresisPercent &&
           left.holdMilliseconds == right.holdMilliseconds &&
           left.priority == right.priority;
}

template <class Record, class Same>
bool SameRecordSet(const std::vector<Record>& left,
                   const std::vector<Record>& right, Same same) {
    if (left.size() != right.size()) return false;
    return std::ranges::all_of(left, [&](const auto& record) {
        const auto* other = FindRecord(right, record.id);
        return other && same(record, *other);
    });
}

bool SameConfiguration(const ConfigurationData& left,
                       const ConfigurationData& right) {
    auto leftShortcuts = left.keyboardShortcuts;
    auto rightShortcuts = right.keyboardShortcuts;
    std::ranges::sort(leftShortcuts, {}, &PresetShortcut::presetId);
    std::ranges::sort(rightShortcuts, {}, &PresetShortcut::presetId);
    auto leftJoyShortcuts = left.joystickShortcuts;
    auto rightJoyShortcuts = right.joystickShortcuts;
    std::ranges::sort(leftJoyShortcuts, {}, &JoystickShortcut::presetId);
    std::ranges::sort(rightJoyShortcuts, {}, &JoystickShortcut::presetId);
    return left.enableLog == right.enableLog &&
           left.automationEnabled == right.automationEnabled &&
           left.startupPreset == right.startupPreset &&
           SameRecordSet(left.presets, right.presets, SamePreset) &&
           SameRecordSet(left.rules, right.rules, SameRule) &&
           leftShortcuts == rightShortcuts &&
           leftJoyShortcuts == rightJoyShortcuts;
}

bool ValidateDraft(const ConfigurationData& data, std::string& detail) {
    if (data.presets.empty() || data.presets.size() > 256 || data.rules.size() > 256) {
        detail = "A configuration requires 1-256 presets and no more than 256 rules.";
        return false;
    }
    std::set<std::string> identifiers;
    for (const auto& preset : data.presets) {
        std::array<bool, kSystemCount> orderSeen{};
        const bool validOrder = std::ranges::all_of(
            preset.tieBreakOrder, [&](const auto system) {
                const auto index = ToIndex(system);
                if (index >= kSystemCount || orderSeen[index]) return false;
                orderSeen[index] = true;
                return true;
            });
        if (!ValidIniKey(preset.id) || !ValidIniValue(preset.displayName, 96) ||
            !identifiers.insert("preset:" + Lower(preset.id)).second ||
            !validOrder) {
            detail = "A preset has an invalid or duplicate ID, label, or tie-break order.";
            return false;
        }
        for (const auto& plan : preset.systems) {
            const auto total = static_cast<std::uint32_t>(plan.green) + plan.yellow +
                               plan.red;
            if (total > 32) {
                detail = "A preset system requests more than the supported 32 pips.";
                return false;
            }
        }
    }
    if (!data.startupPreset.empty() && !FindRecord(data.presets, data.startupPreset)) {
        detail = "The startup preset does not exist in the submitted draft.";
        return false;
    }
    for (const auto& rule : data.rules) {
        if (!ValidIniKey(rule.id) || !ValidIniValue(rule.displayName, 96) ||
            !identifiers.insert("rule:" + Lower(rule.id)).second ||
            ToIndex(rule.targetSystem) >= kSystemCount ||
            (rule.sourceSystem != SystemId::Invalid &&
             ToIndex(rule.sourceSystem) >= kSystemCount) ||
            rule.thresholdPercent > 100 || rule.hysteresisPercent > 100 ||
            (rule.targetPips != std::numeric_limits<std::uint16_t>::max() &&
             rule.targetPips > 32)) {
            detail = "An automation rule has an invalid ID, label, system, or bounded value.";
            return false;
        }
    }
    for (std::size_t index = 0; index < data.keyboardShortcuts.size(); ++index) {
        const auto& shortcut = data.keyboardShortcuts[index];
        if (!FindRecord(data.presets, shortcut.presetId) ||
            !KeyboardShortcutPolicy::Valid(shortcut.chord) ||
            std::ranges::any_of(data.keyboardShortcuts.begin(),
                                data.keyboardShortcuts.begin() + index,
                                [&](const auto& previous) {
                                    return Lower(previous.presetId) ==
                                               Lower(shortcut.presetId) ||
                                           previous.chord == shortcut.chord;
                                })) {
            detail = "A keyboard binding is invalid, duplicated, or refers to a missing preset.";
            return false;
        }
    }
    for (std::size_t index = 0; index < data.joystickShortcuts.size(); ++index) {
        const auto& shortcut = data.joystickShortcuts[index];
        if (!FindRecord(data.presets, shortcut.presetId) ||
            !JoystickBindingPolicy::ValidToken(shortcut.token) ||
            std::ranges::any_of(data.joystickShortcuts.begin(),
                                data.joystickShortcuts.begin() + index,
                                [&](const auto& previous) {
                                    return Lower(previous.presetId) ==
                                               Lower(shortcut.presetId) ||
                                           previous.token == shortcut.token;
                                })) {
            detail = "A joystick binding is invalid, duplicated, or refers to a missing preset.";
            return false;
        }
    }
    return true;
}

std::string SystemName(SystemId system, bool allowAny = false) {
    if (allowAny && system == SystemId::Invalid) return "Any";
    return std::string(SystemKey(system));
}

std::string TierPlanValue(const TierPlan& plan) {
    return std::format("{},{},{}", plan.green, plan.yellow, plan.red);
}

std::string OrderValue(const Preset& preset) {
    std::string result;
    for (const auto system : preset.tieBreakOrder) {
        if (!result.empty()) result += ',';
        result += SystemKey(system);
    }
    return result;
}

std::string TriggerValue(TriggerKind trigger) {
    switch (trigger) {
    case TriggerKind::WeaponFired: return "WeaponFired";
    case TriggerKind::IncomingDamage: return "IncomingDamage";
    case TriggerKind::ThrottleAbove: return "ThrottleAbove";
    case TriggerKind::Manual: return "Manual";
    }
    return "Manual";
}

bool WriteValue(const std::filesystem::path& path, std::string_view section,
                std::string_view key, const std::optional<std::string>& value) {
    const auto wideSection = Widen(section);
    const auto wideKey = Widen(key);
    const auto wideValue = value ? Widen(*value) : std::wstring{};
    return WritePrivateProfileStringW(wideSection.c_str(), wideKey.c_str(),
                                      value ? wideValue.c_str() : nullptr,
                                      path.c_str()) != FALSE;
}

bool WritePresetOverlay(const std::filesystem::path& path,
                        const Preset* inherited, const Preset* desired,
                        std::string_view id) {
    const auto section = "Preset." + std::string(id);
    if (!desired) return WriteValue(path, section, "Deleted", std::string("true"));
    bool ok = WriteValue(path, section, "Deleted", std::nullopt);
    const auto writeDiff = [&](std::string_view key, const std::string& value,
                               const std::optional<std::string>& baseline) {
        return WriteValue(path, section, key,
                          baseline && *baseline == value
                              ? std::optional<std::string>{}
                              : std::optional(value));
    };
    ok = ok && writeDiff("Name", desired->displayName,
                         inherited ? std::optional(inherited->displayName) : std::nullopt);
    ok = ok && writeDiff("Order", OrderValue(*desired),
                         inherited ? std::optional(OrderValue(*inherited)) : std::nullopt);
    for (const auto system : kCockpitOrder) {
        ok = ok && writeDiff(
            SystemKey(system), TierPlanValue(desired->systems[ToIndex(system)]),
            inherited ? std::optional(TierPlanValue(inherited->systems[ToIndex(system)]))
                      : std::nullopt);
    }
    return ok;
}

bool WriteRuleOverlay(const std::filesystem::path& path,
                      const AutomationRule* inherited,
                      const AutomationRule* desired, std::string_view id) {
    const auto section = "Rule." + std::string(id);
    if (!desired) return WriteValue(path, section, "Deleted", std::string("true"));
    bool ok = WriteValue(path, section, "Deleted", std::nullopt);
    const auto writeDiff = [&](std::string_view key, const std::string& value,
                               const std::optional<std::string>& baseline) {
        return WriteValue(path, section, key,
                          baseline && *baseline == value
                              ? std::optional<std::string>{}
                              : std::optional(value));
    };
    const auto boolValue = [](bool value) { return value ? "true" : "false"; };
    const auto targetPips = [](const AutomationRule& rule) {
        return rule.targetPips == std::numeric_limits<std::uint16_t>::max()
                   ? std::string("Max")
                   : std::to_string(rule.targetPips);
    };
#define AP_WRITE_RULE(KEY, VALUE)                                                     \
    ok = ok && writeDiff(KEY, (VALUE)(*desired),                                     \
        inherited ? std::optional<std::string>((VALUE)(*inherited)) : std::nullopt)
    AP_WRITE_RULE("Name", [](const auto& rule) { return rule.displayName; });
    AP_WRITE_RULE("Enabled", [&](const auto& rule) { return std::string(boolValue(rule.enabled)); });
    AP_WRITE_RULE("Trigger", [](const auto& rule) { return TriggerValue(rule.trigger); });
    AP_WRITE_RULE("Source", [](const auto& rule) { return SystemName(rule.sourceSystem, true); });
    AP_WRITE_RULE("Target", [](const auto& rule) { return SystemName(rule.targetSystem); });
    AP_WRITE_RULE("TargetPips", targetPips);
    AP_WRITE_RULE("ThresholdPercent", [](const auto& rule) { return std::to_string(rule.thresholdPercent); });
    AP_WRITE_RULE("HysteresisPercent", [](const auto& rule) { return std::to_string(rule.hysteresisPercent); });
    AP_WRITE_RULE("HoldMilliseconds", [](const auto& rule) { return std::to_string(rule.holdMilliseconds); });
    AP_WRITE_RULE("Priority", [](const auto& rule) { return std::to_string(rule.priority); });
#undef AP_WRITE_RULE
    return ok;
}

const PresetShortcut* FindShortcut(const std::vector<PresetShortcut>& shortcuts,
                                   std::string_view presetId) {
    const auto normalized = Lower(presetId);
    const auto found = std::ranges::find_if(shortcuts, [&](const auto& shortcut) {
        return Lower(shortcut.presetId) == normalized;
    });
    return found == shortcuts.end() ? nullptr : &*found;
}

const JoystickShortcut* FindJoystickShortcut(
    const std::vector<JoystickShortcut>& shortcuts,
    std::string_view presetId) {
    const auto normalized = Lower(presetId);
    const auto found = std::ranges::find_if(shortcuts, [&](const auto& shortcut) {
        return Lower(shortcut.presetId) == normalized;
    });
    return found == shortcuts.end() ? nullptr : &*found;
}

bool WriteKeyboardOverlay(const std::filesystem::path& path,
                          const ConfigurationData& inherited,
                          const ConfigurationData& desired) {
    std::set<std::string> presetIds;
    for (const auto& shortcut : inherited.keyboardShortcuts) {
        presetIds.insert(Lower(shortcut.presetId));
    }
    for (const auto& shortcut : desired.keyboardShortcuts) {
        presetIds.insert(Lower(shortcut.presetId));
    }
    for (const auto& [presetId, value] :
         ReadSectionEntries(path, L"KeyboardPresetBindings")) {
        (void)value;
        presetIds.insert(Lower(presetId));
    }
    bool ok = true;
    for (const auto& normalized : presetIds) {
        const auto* base = FindShortcut(inherited.keyboardShortcuts, normalized);
        const auto* draft = FindShortcut(desired.keyboardShortcuts, normalized);
        const std::string id = draft ? draft->presetId
                               : base ? base->presetId : normalized;
        std::optional<std::string> value;
        if (draft) {
            if (!base || !(base->chord == draft->chord)) {
                value = KeyboardShortcutPolicy::StorageName(draft->chord);
            }
        } else if (base) {
            value = "None";
        }
        ok = ok && WriteValue(path, "KeyboardPresetBindings", id, value);
    }
    return ok;
}

bool WriteJoystickOverlay(const std::filesystem::path& path,
                          const ConfigurationData& inherited,
                          const ConfigurationData& desired) {
    std::set<std::string> presetIds;
    for (const auto& shortcut : inherited.joystickShortcuts) {
        presetIds.insert(Lower(shortcut.presetId));
    }
    for (const auto& shortcut : desired.joystickShortcuts) {
        presetIds.insert(Lower(shortcut.presetId));
    }
    for (const auto& [presetId, value] :
         ReadSectionEntries(path, L"JoystickPresetBindings")) {
        (void)value;
        presetIds.insert(Lower(presetId));
    }
    bool ok = true;
    for (const auto& normalized : presetIds) {
        const auto* base = FindJoystickShortcut(inherited.joystickShortcuts, normalized);
        const auto* draft = FindJoystickShortcut(desired.joystickShortcuts, normalized);
        const std::string id = draft ? draft->presetId
                               : base ? base->presetId : normalized;
        std::optional<std::string> value;
        if (draft) {
            if (!base || !(base->token == draft->token)) {
                value = draft->token;
            }
        } else if (base) {
            value = "None";
        }
        ok = ok && WriteValue(path, "JoystickPresetBindings", id, value);
    }
    return ok;
}

} // namespace

LoadedConfiguration LoadDetailed(const std::filesystem::path& defaultsPath,
                                 const std::filesystem::path& importsDirectory,
                                 const std::filesystem::path& customPath) {
    LoadedConfiguration loaded;
    auto& data = loaded.effective;
    if (std::filesystem::exists(defaultsPath)) {
        ApplyFile(data, defaultsPath,
                  {ConfigurationSourceKind::Defaults, "Shipped defaults", defaultsPath},
                  loaded.presetSources, loaded.ruleSources);
    }

    // Import packs are ordinary sparse overlays. Sorting by filename makes
    // their precedence deterministic across loose files and virtual mod filesystems.
    std::vector<std::filesystem::path> imports;
    std::error_code error;
    if (std::filesystem::is_directory(importsDirectory, error)) {
        for (std::filesystem::directory_iterator iterator(importsDirectory, error), end;
             !error && iterator != end; iterator.increment(error)) {
            if (!iterator->is_regular_file(error)) continue;
            if (Lower(iterator->path().extension().string()) == ".ini")
                imports.push_back(iterator->path());
        }
    }
    std::ranges::sort(imports, [](const auto& left, const auto& right) {
        return Lower(left.filename().string()) < Lower(right.filename().string());
    });
    for (const auto& import : imports) {
        ApplyFile(data, import,
                  {ConfigurationSourceKind::Import,
                   import.stem().string(), import},
                  loaded.presetSources, loaded.ruleSources);
    }
    if (data.presets.empty()) {
        Preset balanced{.id = "Balanced", .displayName = "Balanced"};
        for (auto& system : balanced.systems) system.green = 1;
        data.presets.push_back(std::move(balanced));
        loaded.presetSources.push_back({
            .recordId = "Balanced",
            .baseKind = ConfigurationSourceKind::BuiltIn,
            .baseLabel = "Built-in fallback",
        });
    }
    loaded.inherited = data;

    if (std::filesystem::exists(customPath)) {
        ApplyFile(data, customPath,
                  {ConfigurationSourceKind::Custom, "User configuration", customPath},
                  loaded.presetSources, loaded.ruleSources);
    }
    if (data.presets.empty()) {
        // The runtime must always remain operable even if a hand-written file
        // tombstones every inherited preset. The transactional editor rejects
        // such a draft before it can reach this fail-safe.
        Preset balanced{.id = "Balanced", .displayName = "Balanced"};
        for (auto& system : balanced.systems) system.green = 1;
        data.presets.push_back(std::move(balanced));
        loaded.presetSources.push_back({
            .recordId = "Balanced",
            .baseKind = ConfigurationSourceKind::BuiltIn,
            .baseLabel = "Built-in fallback",
            .userOverride = true,
        });
    }
    std::erase_if(data.keyboardShortcuts, [&](const PresetShortcut& shortcut) {
        return std::ranges::find(data.presets, shortcut.presetId, &Preset::id) ==
               data.presets.end();
    });
    std::ranges::sort(data.keyboardShortcuts, {}, &PresetShortcut::presetId);
    std::erase_if(data.joystickShortcuts, [&](const JoystickShortcut& shortcut) {
        return std::ranges::find(data.presets, shortcut.presetId, &Preset::id) ==
               data.presets.end();
    });
    std::ranges::sort(data.joystickShortcuts, {}, &JoystickShortcut::presetId);
    return loaded;
}

std::string AllocatePresetId(std::span<const Preset> presets,
                             std::span<const Preset> reserved) {
    for (std::uint32_t suffix = 1; suffix <= 1000000; ++suffix) {
        const auto candidate = std::format("Custom{}", suffix);
        const auto availableIn = [&](const auto& records) {
            return std::ranges::none_of(records, [&](const auto& preset) {
                return Lower(preset.id) == Lower(candidate);
            });
        };
        if (availableIn(presets) && availableIn(reserved)) {
            return candidate;
        }
    }
    return {};
}

std::string AllocateRuleId(std::span<const AutomationRule> rules,
                           std::span<const AutomationRule> reserved) {
    for (std::uint32_t suffix = 1; suffix <= 1000000; ++suffix) {
        const auto candidate = std::format("CustomRule{}", suffix);
        const auto availableIn = [&](const auto& records) {
            return std::ranges::none_of(records, [&](const auto& rule) {
                return Lower(rule.id) == Lower(candidate);
            });
        };
        if (availableIn(rules) && availableIn(reserved)) {
            return candidate;
        }
    }
    return {};
}

ConfigurationData Load(const std::filesystem::path& defaultsPath,
                       const std::filesystem::path& importsDirectory,
                       const std::filesystem::path& customPath) {
    return LoadDetailed(defaultsPath, importsDirectory, customPath).effective;
}

SaveConfigurationReport Save(const std::filesystem::path& defaultsPath,
                             const std::filesystem::path& importsDirectory,
                             const std::filesystem::path& customPath,
                             const ConfigurationData& inherited,
                             const ConfigurationData& desired) {
    SaveConfigurationReport report;
    if (!ValidateDraft(desired, report.detail)) {
        report.result = SaveConfigurationResult::InvalidDraft;
        return report;
    }

    std::error_code error;
    if (!customPath.parent_path().empty()) {
        std::filesystem::create_directories(customPath.parent_path(), error);
        if (error) {
            report.result = SaveConfigurationResult::WriteFailure;
            report.detail = "Could not create the user configuration directory.";
            return report;
        }
    }
    auto temporary = customPath;
    temporary += L".transaction.tmp";
    std::filesystem::remove(temporary, error);
    error.clear();
    const bool existed = std::filesystem::exists(customPath, error);
    if (error) {
        report.result = SaveConfigurationResult::WriteFailure;
        report.detail = "Could not inspect the existing user configuration.";
        return report;
    }
    if (existed) {
        std::filesystem::copy_file(customPath, temporary,
                                   std::filesystem::copy_options::overwrite_existing,
                                   error);
        if (error) {
            report.result = SaveConfigurationResult::WriteFailure;
            report.detail = "Could not prepare the atomic user configuration update.";
            return report;
        }
    } else {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) {
            report.result = SaveConfigurationResult::WriteFailure;
            report.detail = "Could not create the atomic user configuration update.";
            return report;
        }
    }

    bool wrote = true;
    wrote = wrote && WriteValue(
        temporary, "General", "sStartupPreset",
        desired.startupPreset == inherited.startupPreset
            ? std::optional<std::string>{}
            : std::optional(desired.startupPreset.empty() ? std::string("None") : desired.startupPreset));
    wrote = wrote && WriteValue(
        temporary, "General", "bAutomationEnabled",
        desired.automationEnabled == inherited.automationEnabled
            ? std::optional<std::string>{}
            : std::optional(std::string(desired.automationEnabled ? "true" : "false")));

    std::set<std::string> presetIds;
    for (const auto& preset : inherited.presets) presetIds.insert(Lower(preset.id));
    for (const auto& preset : desired.presets) presetIds.insert(Lower(preset.id));
    for (const auto& section : ReadSections(temporary)) {
        if (section.starts_with("Preset.") && section.size() > 7) {
            presetIds.insert(Lower(std::string_view(section).substr(7)));
        }
    }
    for (const auto& normalized : presetIds) {
        const auto base = std::ranges::find_if(inherited.presets, [&](const auto& preset) {
            return Lower(preset.id) == normalized;
        });
        const auto draft = std::ranges::find_if(desired.presets, [&](const auto& preset) {
            return Lower(preset.id) == normalized;
        });
        const Preset* baseRecord = base == inherited.presets.end() ? nullptr : &*base;
        const Preset* draftRecord = draft == desired.presets.end() ? nullptr : &*draft;
        const auto id = draftRecord ? draftRecord->id
                                    : baseRecord ? baseRecord->id : normalized;
        wrote = wrote && WritePresetOverlay(temporary, baseRecord, draftRecord, id);
    }

    std::set<std::string> ruleIds;
    for (const auto& rule : inherited.rules) ruleIds.insert(Lower(rule.id));
    for (const auto& rule : desired.rules) ruleIds.insert(Lower(rule.id));
    for (const auto& section : ReadSections(temporary)) {
        if (section.starts_with("Rule.") && section.size() > 5) {
            ruleIds.insert(Lower(std::string_view(section).substr(5)));
        }
    }
    for (const auto& normalized : ruleIds) {
        const auto base = std::ranges::find_if(inherited.rules, [&](const auto& rule) {
            return Lower(rule.id) == normalized;
        });
        const auto draft = std::ranges::find_if(desired.rules, [&](const auto& rule) {
            return Lower(rule.id) == normalized;
        });
        const AutomationRule* baseRecord = base == inherited.rules.end() ? nullptr : &*base;
        const AutomationRule* draftRecord = draft == desired.rules.end() ? nullptr : &*draft;
        const auto id = draftRecord ? draftRecord->id
                                    : baseRecord ? baseRecord->id : normalized;
        wrote = wrote && WriteRuleOverlay(temporary, baseRecord, draftRecord, id);
    }
    wrote = wrote && WriteKeyboardOverlay(temporary, inherited, desired);
    wrote = wrote && WriteJoystickOverlay(temporary, inherited, desired);
    if (wrote) WritePrivateProfileStringW(nullptr, nullptr, nullptr, temporary.c_str());
    if (!wrote) {
        std::filesystem::remove(temporary, error);
        report.result = SaveConfigurationResult::WriteFailure;
        report.detail = "Windows rejected one or more sparse configuration updates.";
        return report;
    }

    const auto candidate = LoadDetailed(defaultsPath, importsDirectory, temporary);
    if (!SameConfiguration(candidate.effective, desired)) {
        std::filesystem::remove(temporary, error);
        report.result = SaveConfigurationResult::VerificationMismatch;
        report.detail = "The prepared overlay did not read back as the submitted draft.";
        return report;
    }

    bool replaced{};
    DWORD replaceError{ERROR_SUCCESS};
    DWORD moveError{ERROR_SUCCESS};
    if (existed) {
        replaced = ReplaceFileW(customPath.c_str(), temporary.c_str(), nullptr,
                                REPLACEFILE_WRITE_THROUGH, nullptr, nullptr) != FALSE;
        if (!replaced) {
            replaceError = GetLastError();
            // Some virtualized/mod-manager directories reject ReplaceFileW even
            // though a verified same-directory rename is supported. Keep the
            // stronger metadata-preserving operation first, then use the same
            // write-through atomic replacement used for a newly-created overlay.
            replaced = MoveFileExW(
                temporary.c_str(), customPath.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
            if (!replaced) moveError = GetLastError();
        }
    } else {
        replaced = MoveFileExW(
            temporary.c_str(), customPath.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
        if (!replaced) moveError = GetLastError();
    }
    if (!replaced) {
        std::filesystem::remove(temporary, error);
        report.result = SaveConfigurationResult::WriteFailure;
        report.detail = std::format(
            "The verified overlay could not replace the user configuration atomically "
            "(ReplaceFile error {}, MoveFileEx error {}).",
            replaceError, moveError);
        return report;
    }

    report.configuration = LoadDetailed(defaultsPath, importsDirectory, customPath);
    if (!SameConfiguration(report.configuration.effective, desired)) {
        report.result = SaveConfigurationResult::ReloadFailure;
        report.detail = "The committed overlay could not be reloaded as the submitted draft.";
        return report;
    }
    report.result = SaveConfigurationResult::Ok;
    report.detail = "Power configuration saved, reloaded, and verified.";
    return report;
}

bool WriteKeyboardShortcut(const std::filesystem::path& customPath,
                           std::string_view presetId,
                           std::optional<KeyboardChord> chord) {
    const KeyboardShortcutEdit edit{std::string(presetId), chord};
    return WriteKeyboardShortcuts(customPath, std::span(&edit, 1));
}

bool WriteKeyboardShortcuts(const std::filesystem::path& customPath,
                            std::span<const KeyboardShortcutEdit> edits) {
    for (std::size_t index = 0; index < edits.size(); ++index) {
        const auto& edit = edits[index];
        if (!ValidIniKey(edit.presetId) ||
            (edit.chord && !KeyboardShortcutPolicy::Valid(*edit.chord)) ||
            std::ranges::any_of(edits.first(index), [&](const auto& previous) {
                return previous.presetId == edit.presetId;
            })) {
            return false;
        }
    }
    if (edits.empty()) return true;

    std::error_code error;
    if (!customPath.parent_path().empty()) {
        std::filesystem::create_directories(customPath.parent_path(), error);
        if (error) return false;
    }
    auto temporary = customPath;
    temporary += L".keyboard.tmp";
    std::filesystem::remove(temporary, error);
    error.clear();

    const bool existed = std::filesystem::exists(customPath, error);
    if (error) return false;
    if (existed) {
        std::filesystem::copy_file(customPath, temporary,
                                   std::filesystem::copy_options::overwrite_existing,
                                   error);
        if (error) return false;
    } else {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) return false;
    }

    bool wrote = true;
    for (const auto& edit : edits) {
        const std::wstring key = Widen(edit.presetId);
        const std::wstring value = edit.chord
                                       ? Widen(KeyboardShortcutPolicy::StorageName(*edit.chord))
                                       : L"None";
        wrote = wrote && WritePrivateProfileStringW(
                             L"KeyboardPresetBindings", key.c_str(), value.c_str(),
                             temporary.c_str()) != FALSE;
        if (!wrote) break;
    }
    if (wrote) {
        WritePrivateProfileStringW(nullptr, nullptr, nullptr, temporary.c_str());
    }

    bool replaced{};
    if (wrote) {
        if (existed) {
            replaced = ReplaceFileW(customPath.c_str(), temporary.c_str(), nullptr,
                                    REPLACEFILE_WRITE_THROUGH, nullptr, nullptr) != FALSE;
            if (!replaced) {
                replaced = MoveFileExW(
                    temporary.c_str(), customPath.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
            }
        } else {
            replaced = MoveFileExW(
                temporary.c_str(), customPath.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
        }
    }
    if (!replaced) {
        std::filesystem::remove(temporary, error);
    }
    return replaced;
}

bool WriteJoystickShortcut(const std::filesystem::path& customPath,
                           std::string_view presetId,
                           std::optional<std::string_view> token) {
    const JoystickShortcutEdit edit{
        std::string(presetId),
        token ? std::optional(std::string(*token)) : std::nullopt};
    return WriteJoystickShortcuts(customPath, std::span(&edit, 1));
}

bool WriteJoystickShortcuts(const std::filesystem::path& customPath,
                            std::span<const JoystickShortcutEdit> edits) {
    for (std::size_t index = 0; index < edits.size(); ++index) {
        const auto& edit = edits[index];
        if (!ValidIniKey(edit.presetId) ||
            (edit.token && !JoystickBindingPolicy::ValidToken(*edit.token)) ||
            std::ranges::any_of(edits.first(index), [&](const auto& previous) {
                return previous.presetId == edit.presetId;
            })) {
            return false;
        }
    }
    if (edits.empty()) return true;

    std::error_code error;
    if (!customPath.parent_path().empty()) {
        std::filesystem::create_directories(customPath.parent_path(), error);
        if (error) return false;
    }
    auto temporary = customPath;
    temporary += L".joystick.tmp";
    std::filesystem::remove(temporary, error);
    error.clear();

    const bool existed = std::filesystem::exists(customPath, error);
    if (error) return false;
    if (existed) {
        std::filesystem::copy_file(customPath, temporary,
                                   std::filesystem::copy_options::overwrite_existing,
                                   error);
        if (error) return false;
    } else {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) return false;
    }

    bool wrote = true;
    for (const auto& edit : edits) {
        const std::wstring key = Widen(edit.presetId);
        const std::wstring value = edit.token ? Widen(*edit.token) : L"None";
        wrote = wrote && WritePrivateProfileStringW(
                             L"JoystickPresetBindings", key.c_str(), value.c_str(),
                             temporary.c_str()) != FALSE;
        if (!wrote) break;
    }
    if (wrote) {
        WritePrivateProfileStringW(nullptr, nullptr, nullptr, temporary.c_str());
    }

    bool replaced{};
    if (wrote) {
        if (existed) {
            replaced = ReplaceFileW(customPath.c_str(), temporary.c_str(), nullptr,
                                    REPLACEFILE_WRITE_THROUGH, nullptr, nullptr) != FALSE;
            if (!replaced) {
                replaced = MoveFileExW(
                    temporary.c_str(), customPath.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
            }
        } else {
            replaced = MoveFileExW(
                temporary.c_str(), customPath.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
        }
    }
    if (!replaced) {
        std::filesystem::remove(temporary, error);
    }
    return replaced;
}

bool WriteAutomationEnabled(const std::filesystem::path& customPath, bool enabled) {
    std::error_code error;
    if (!customPath.parent_path().empty()) {
        std::filesystem::create_directories(customPath.parent_path(), error);
        if (error) return false;
    }
    auto temporary = customPath;
    temporary += L".general.tmp";
    std::filesystem::remove(temporary, error);
    error.clear();

    const bool existed = std::filesystem::exists(customPath, error);
    if (error) return false;
    if (existed) {
        std::filesystem::copy_file(customPath, temporary,
                                   std::filesystem::copy_options::overwrite_existing,
                                   error);
        if (error) return false;
    } else {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) return false;
    }

    const bool wrote = WritePrivateProfileStringW(
                           L"General", L"bAutomationEnabled", enabled ? L"true" : L"false",
                           temporary.c_str()) != FALSE;
    if (wrote) WritePrivateProfileStringW(nullptr, nullptr, nullptr, temporary.c_str());
    bool replaced{};
    if (wrote) {
        if (existed) {
            replaced = ReplaceFileW(customPath.c_str(), temporary.c_str(), nullptr,
                                    REPLACEFILE_WRITE_THROUGH, nullptr, nullptr) != FALSE;
            if (!replaced) {
                replaced = MoveFileExW(
                    temporary.c_str(), customPath.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
            }
        } else {
            replaced = MoveFileExW(
                temporary.c_str(), customPath.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
        }
    }
    if (!replaced) std::filesystem::remove(temporary, error);
    return replaced;
}

} // namespace AbsolutePower::Configuration
