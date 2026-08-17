#include "Configuration.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string_view>

using namespace AbsolutePower;

int main() {
    const std::array allocatedIds{
        Preset{.id = "Custom1"},
        Preset{.id = "cUsToM2"},
        Preset{.id = "Balanced"},
    };
    assert(Configuration::AllocatePresetId(allocatedIds) == "Custom3");
    const std::array reservedIds{Preset{.id = "CUSTOM3"}};
    assert(Configuration::AllocatePresetId(allocatedIds, reservedIds) == "Custom4");
    const std::array allocatedRuleIds{
        AutomationRule{.id = "CustomRule1"},
        AutomationRule{.id = "customrule2"},
    };
    const std::array reservedRuleIds{AutomationRule{.id = "CUSTOMRULE3"}};
    assert(Configuration::AllocateRuleId(allocatedRuleIds, reservedRuleIds) ==
           "CustomRule4");

    const auto configuration = Configuration::Load(
        std::filesystem::path("config") / "AbsolutePower.ini",
        std::filesystem::path("tests") / "fixtures" / "imports",
        std::filesystem::path("tests") / "missing-custom.ini");
    assert(configuration.presets.size() == 5);
    assert(configuration.rules.size() == 3);
    assert(configuration.startupPreset == "Balanced");
    assert(!configuration.automationEnabled);

    const auto& balanced = configuration.presets.front();
    assert(balanced.id == "Balanced");
    const auto weapon = balanced.systems[ToIndex(SystemId::Weapon0)];
    assert(weapon.green == 1 && weapon.yellow == 2 && weapon.red == 2);
    assert(balanced.tieBreakOrder.front() == SystemId::Shield);

    const auto stealth = std::ranges::find(
        configuration.presets, "Stealth", &Preset::id);
    assert(stealth != configuration.presets.end());
    assert(stealth->systems[ToIndex(SystemId::Engine)].green == 1);
    assert(stealth->systems[ToIndex(SystemId::Shield)].green == 1);
    const auto emptyPlan = [](const TierPlan& plan) {
        return plan.green == 0 && plan.yellow == 0 && plan.red == 0;
    };
    assert(emptyPlan(stealth->systems[ToIndex(SystemId::Weapon0)]));
    assert(emptyPlan(stealth->systems[ToIndex(SystemId::Weapon1)]));
    assert(emptyPlan(stealth->systems[ToIndex(SystemId::Weapon2)]));
    assert(emptyPlan(stealth->systems[ToIndex(SystemId::GravDrive)]));

    const auto& incomingFire = configuration.rules[1];
    assert(incomingFire.id == "IncomingFire");
    assert(incomingFire.trigger == TriggerKind::IncomingDamage);
    assert(incomingFire.targetSystem == SystemId::Shield);
    assert(incomingFire.targetPips == std::numeric_limits<std::uint16_t>::max());
    assert(!incomingFire.enabled);

    const auto& imported = configuration.presets.back();
    assert(imported.id == "ImportedTravel");
    assert(imported.displayName == "Imported Travel");
    assert(imported.systems[ToIndex(SystemId::Engine)].green == 4);
    assert(configuration.keyboardShortcuts.size() == 5);
    const auto importedBinding = std::ranges::find(
        configuration.keyboardShortcuts, "ImportedTravel", &PresetShortcut::presetId);
    assert(importedBinding != configuration.keyboardShortcuts.end());
    assert(importedBinding->chord.virtualKey == 0x77);
    assert(importedBinding->chord.control);
    assert(importedBinding->chord.shift);
    assert(!importedBinding->chord.alt);
    constexpr std::array defaultBindings{
        std::pair{std::string_view{"Balanced"}, std::uint8_t{'1'}},
        std::pair{std::string_view{"Combat"}, std::uint8_t{'2'}},
        std::pair{std::string_view{"Travel"}, std::uint8_t{'3'}},
        std::pair{std::string_view{"Stealth"}, std::uint8_t{'4'}},
    };
    for (const auto& [presetId, virtualKey] : defaultBindings) {
        const auto binding = std::ranges::find(
            configuration.keyboardShortcuts, presetId, &PresetShortcut::presetId);
        assert(binding != configuration.keyboardShortcuts.end());
        assert(binding->chord.virtualKey == virtualKey);
        assert(!binding->chord.control && !binding->chord.alt && !binding->chord.shift);
    }

    const auto customPath =
        std::filesystem::temp_directory_path() / "AbsolutePower-configuration-test.ini";
    std::error_code error;
    std::filesystem::remove(customPath, error);
    {
        std::ofstream stream(customPath);
        stream << "[Preserved]\nValue=Yes\n";
    }
    const KeyboardChord savedChord{.virtualKey = 0x78, .alt = true};
    assert(Configuration::WriteKeyboardShortcut(customPath, "Balanced", savedChord));
    const auto saved = Configuration::Load(
        std::filesystem::path("tests") / "missing-default.ini",
        std::filesystem::path("tests") / "missing-imports", customPath);
    assert(saved.keyboardShortcuts.size() == 1);
    assert(saved.keyboardShortcuts.front().presetId == "Balanced");
    assert(saved.keyboardShortcuts.front().chord == savedChord);
    assert(Configuration::WriteKeyboardShortcut(customPath, "Balanced", std::nullopt));
    const auto cleared = Configuration::Load(
        std::filesystem::path("tests") / "missing-default.ini",
        std::filesystem::path("tests") / "missing-imports", customPath);
    assert(cleared.keyboardShortcuts.empty());
    const std::array batch{
        KeyboardShortcutEdit{"Balanced", KeyboardChord{.virtualKey = 0x70}},
        KeyboardShortcutEdit{"Travel", KeyboardChord{.virtualKey = 0x71, .control = true}},
    };
    assert(Configuration::WriteKeyboardShortcuts(customPath, batch));
    assert(Configuration::WriteAutomationEnabled(customPath, true));
    const auto batched = Configuration::Load(
        std::filesystem::path("config") / "AbsolutePower.ini",
        std::filesystem::path("tests") / "missing-imports", customPath);
    assert(batched.keyboardShortcuts.size() == 4);
    assert(batched.automationEnabled);
    const auto batchedBalanced = std::ranges::find(
        batched.keyboardShortcuts, "Balanced", &PresetShortcut::presetId);
    const auto batchedTravel = std::ranges::find(
        batched.keyboardShortcuts, "Travel", &PresetShortcut::presetId);
    assert(batchedBalanced != batched.keyboardShortcuts.end());
    assert(batchedBalanced->chord.virtualKey == 0x70);
    assert(batchedTravel != batched.keyboardShortcuts.end());
    assert(batchedTravel->chord.virtualKey == 0x71 && batchedTravel->chord.control);
    std::ifstream preserved(customPath);
    const std::string preservedText((std::istreambuf_iterator<char>(preserved)), {});
    assert(preservedText.contains("[Preserved]"));
    preserved.close();
    std::filesystem::remove(customPath, error);

    const auto transactionRoot =
        std::filesystem::temp_directory_path() / "AbsolutePower-transaction-test";
    std::filesystem::remove_all(transactionRoot, error);
    std::filesystem::create_directories(transactionRoot / "imports");
    const auto defaultsPath = transactionRoot / "defaults.ini";
    const auto importsPath = transactionRoot / "imports";
    const auto importPath = importsPath / "10-pack.ini";
    const auto transactionCustomPath = transactionRoot / "custom.ini";
    std::filesystem::copy_file(std::filesystem::path("config") / "AbsolutePower.ini",
                               defaultsPath);
    {
        std::ofstream stream(importPath);
        stream << "[Preset.PackTravel]\nName=Pack Travel\n"
                  "Order=Engine,Shield,GravDrive,Weapon0,Weapon1,Weapon2\n"
                  "Weapon0=0,0,0\nWeapon1=0,0,0\nWeapon2=0,0,0\n"
                  "Engine=4,0,0\nShield=2,0,0\nGravDrive=0,0,0\n";
    }
    {
        std::ofstream stream(transactionCustomPath);
        stream << "[Preserved]\nValue=Yes\n\n"
                  "[General]\nbEnableLog=false\n\n"
                  "[Preset.Balanced]\nName=User Balanced\nUnknownPresetKey=Keep\n\n"
                  "[Preset.UserOnly]\nName=User Only\n"
                  "Order=Shield,Engine,Weapon0,Weapon1,Weapon2,GravDrive\n"
                  "Weapon0=1,0,0\nWeapon1=1,0,0\nWeapon2=1,0,0\n"
                  "Engine=1,0,0\nShield=1,0,0\nGravDrive=0,0,0\n\n"
                  "[KeyboardPresetBindings]\nBalanced=F8\n";
    }

    const auto sourced = Configuration::LoadDetailed(
        defaultsPath, importsPath, transactionCustomPath);
    assert(sourced.effective.presets.size() == 6);
    const auto balancedSource = std::ranges::find(
        sourced.presetSources, "Balanced", &ConfigurationRecordSource::recordId);
    assert(balancedSource != sourced.presetSources.end());
    assert(balancedSource->baseKind == ConfigurationSourceKind::Defaults);
    assert(balancedSource->userOverride);
    const auto packSource = std::ranges::find(
        sourced.presetSources, "PackTravel", &ConfigurationRecordSource::recordId);
    assert(packSource != sourced.presetSources.end());
    assert(packSource->baseKind == ConfigurationSourceKind::Import);
    assert(!packSource->userOverride);
    const auto userSource = std::ranges::find(
        sourced.presetSources, "UserOnly", &ConfigurationRecordSource::recordId);
    assert(userSource != sourced.presetSources.end());
    assert(userSource->baseKind == ConfigurationSourceKind::Custom);
    assert(userSource->userOverride);

    auto desired = sourced.effective;
    *std::ranges::find(desired.presets, "Balanced", &Preset::id) =
        *std::ranges::find(sourced.inherited.presets, "Balanced", &Preset::id);
    auto pack = std::ranges::find(desired.presets, "PackTravel", &Preset::id);
    pack->systems[ToIndex(SystemId::Engine)].green = 5;
    std::erase_if(desired.presets,
                  [](const Preset& preset) {
                      return preset.id == "Combat" || preset.id == "UserOnly";
                  });
    Preset created{.id = "Created", .displayName = "Created in Control"};
    created.systems[ToIndex(SystemId::Shield)].green = 3;
    desired.presets.push_back(created);
    desired.startupPreset = "Created";
    desired.automationEnabled = true;
    desired.keyboardShortcuts = {
        PresetShortcut{"Balanced", KeyboardChord{.virtualKey = 0x31}},
    };

    const auto savedTransaction = Configuration::Save(
        defaultsPath, importsPath, transactionCustomPath,
        sourced.inherited, desired);
    assert(savedTransaction.result == SaveConfigurationResult::Ok);
    assert(savedTransaction.configuration.effective.startupPreset == "Created");
    assert(savedTransaction.configuration.effective.automationEnabled);
    assert(std::ranges::find(savedTransaction.configuration.effective.presets,
                             "Combat", &Preset::id) ==
           savedTransaction.configuration.effective.presets.end());
    assert(std::ranges::find(savedTransaction.configuration.effective.presets,
                             "UserOnly", &Preset::id) ==
           savedTransaction.configuration.effective.presets.end());
    assert(savedTransaction.configuration.effective.keyboardShortcuts.size() == 1);
    assert(savedTransaction.configuration.effective.keyboardShortcuts.front().presetId ==
           "Balanced");
    assert(savedTransaction.configuration.effective.keyboardShortcuts.front().chord ==
           KeyboardChord{.virtualKey = 0x31});
    const auto savedBalancedSource = std::ranges::find(
        savedTransaction.configuration.presetSources, "Balanced",
        &ConfigurationRecordSource::recordId);
    assert(savedBalancedSource != savedTransaction.configuration.presetSources.end());
    assert(!savedBalancedSource->userOverride);
    std::ifstream transactionFile(transactionCustomPath);
    const std::string transactionText((std::istreambuf_iterator<char>(transactionFile)), {});
    assert(transactionText.contains("[Preserved]"));
    assert(transactionText.contains("UnknownPresetKey=Keep"));
    assert(transactionText.contains("[KeyboardPresetBindings]"));
    assert(transactionText.contains("[Preset.Combat]"));
    assert(transactionText.contains("Deleted=true"));
    assert(!transactionText.contains("Name=User Balanced"));

    auto invalid = desired;
    invalid.startupPreset = "Missing";
    assert(Configuration::Save(defaultsPath, importsPath, transactionCustomPath,
                               sourced.inherited, invalid)
               .result == SaveConfigurationResult::InvalidDraft);
    std::filesystem::remove_all(transactionRoot, error);
    return 0;
}
