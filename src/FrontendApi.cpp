#include "AbsolutePowerFrontendAPI.h"

#include "Plugin.h"
#include "PowerRuntime.h"
#include "RuntimePaths.h"

#include <windows.h>

#include <limits>

namespace {
using namespace AbsolutePowerFrontendApi;
using AbsolutePower::AutomationRule;
using AbsolutePower::ConfigurationCommitResult;
using AbsolutePower::ConfigurationRecordSource;
using AbsolutePower::ConfigurationSourceKind;
using AbsolutePower::PowerRuntime;
using AbsolutePower::Preset;

template <std::size_t Size>
void Copy(char (&destination)[Size], std::string_view value) noexcept {
    std::ranges::fill(destination, '\0');
    const auto count = (std::min)(value.size(), Size - 1);
    std::memcpy(destination, value.data(), count);
}

std::string Utf8(const std::filesystem::path& path) {
    const auto& value = path.native();
    if (value.empty()) return {};
    const auto count = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                           static_cast<int>(value.size()), nullptr, 0,
                                           nullptr, nullptr);
    if (count <= 0) return {};
    std::string result(static_cast<std::size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                        result.data(), count, nullptr, nullptr);
    return result;
}

bool Terminated(const char* value, std::size_t capacity) noexcept {
    return value && std::memchr(value, '\0', capacity) != nullptr;
}

template <std::size_t Size>
std::string Read(const char (&value)[Size]) {
    return std::string(value, strnlen_s(value, Size));
}

void CopyPreset(const Preset& source, AbsolutePowerApi::PresetV1& target) noexcept {
    target = {};
    Copy(target.id, source.id);
    Copy(target.label, source.displayName);
    for (std::size_t index = 0; index < AbsolutePower::kSystemCount; ++index) {
        target.systems[index] = {source.systems[index].green,
                                 source.systems[index].yellow,
                                 source.systems[index].red};
        target.tieBreakOrder[index] =
            static_cast<std::uint8_t>(source.tieBreakOrder[index]);
    }
}

Preset ReadPreset(const AbsolutePowerApi::PresetV1& source) {
    Preset target;
    target.id = Read(source.id);
    target.displayName = Read(source.label);
    for (std::size_t index = 0; index < AbsolutePower::kSystemCount; ++index) {
        target.systems[index] = {source.systems[index].green,
                                 source.systems[index].yellow,
                                 source.systems[index].red};
        target.tieBreakOrder[index] =
            static_cast<AbsolutePower::SystemId>(source.tieBreakOrder[index]);
    }
    return target;
}

void CopyRule(const AutomationRule& source, AbsolutePowerApi::RuleV1& target) noexcept {
    target = {};
    Copy(target.id, source.id);
    Copy(target.label, source.displayName);
    target.enabled = source.enabled ? 1 : 0;
    target.trigger = static_cast<std::uint8_t>(source.trigger);
    target.sourceSystem = static_cast<std::uint8_t>(source.sourceSystem);
    target.targetSystem = static_cast<std::uint8_t>(source.targetSystem);
    target.targetPips = source.targetPips;
    target.thresholdPercent = source.thresholdPercent;
    target.hysteresisPercent = source.hysteresisPercent;
    target.holdMilliseconds = source.holdMilliseconds;
    target.priority = source.priority;
}

AutomationRule ReadRule(const AbsolutePowerApi::RuleV1& source) {
    return {
        .id = Read(source.id),
        .displayName = Read(source.label),
        .enabled = source.enabled != 0,
        .trigger = static_cast<AbsolutePower::TriggerKind>(source.trigger),
        .sourceSystem = static_cast<AbsolutePower::SystemId>(source.sourceSystem),
        .targetSystem = static_cast<AbsolutePower::SystemId>(source.targetSystem),
        .targetPips = source.targetPips,
        .thresholdPercent = source.thresholdPercent,
        .hysteresisPercent = source.hysteresisPercent,
        .holdMilliseconds = source.holdMilliseconds,
        .priority = source.priority,
    };
}

SourceKind MapSource(ConfigurationSourceKind source) noexcept {
    switch (source) {
    case ConfigurationSourceKind::BuiltIn: return SourceKind::BuiltIn;
    case ConfigurationSourceKind::Defaults: return SourceKind::Defaults;
    case ConfigurationSourceKind::Import: return SourceKind::Import;
    case ConfigurationSourceKind::Custom: return SourceKind::Custom;
    }
    return SourceKind::BuiltIn;
}

void CopySource(const std::vector<ConfigurationRecordSource>& sources,
                std::string_view id, RecordSourceV1& target) noexcept {
    target = {};
    const auto found = std::ranges::find(sources, id,
                                         &ConfigurationRecordSource::recordId);
    if (found == sources.end()) return;
    target.baseKind = MapSource(found->baseKind);
    target.userOverride = found->userOverride ? 1 : 0;
    Copy(target.baseLabel, found->baseLabel);
    Copy(target.basePath, Utf8(found->basePath));
}

Result MapCommit(ConfigurationCommitResult result) noexcept {
    switch (result) {
    case ConfigurationCommitResult::Ok: return Result::Ok;
    case ConfigurationCommitResult::StaleGeneration: return Result::StaleGeneration;
    case ConfigurationCommitResult::InvalidDraft: return Result::InvalidDraft;
    case ConfigurationCommitResult::WriteFailure: return Result::WriteFailure;
    case ConfigurationCommitResult::ReloadFailure: return Result::ReloadFailure;
    case ConfigurationCommitResult::VerificationMismatch:
        return Result::VerificationMismatch;
    }
    return Result::Rejected;
}

AbsolutePowerApi::Result MapBackend(AbsolutePower::BackendResult result) noexcept {
    switch (result) {
    case AbsolutePower::BackendResult::Ok: return AbsolutePowerApi::Result::Ok;
    case AbsolutePower::BackendResult::WorkbenchMissing:
        return AbsolutePowerApi::Result::WorkbenchMissing;
    case AbsolutePower::BackendResult::UnsupportedRuntime:
        return AbsolutePowerApi::Result::UnsupportedRuntime;
    case AbsolutePower::BackendResult::SnapshotSeamUnavailable:
        return AbsolutePowerApi::Result::NativeSeamUnavailable;
    case AbsolutePower::BackendResult::PilotNotReady:
        return AbsolutePowerApi::Result::PilotNotReady;
    case AbsolutePower::BackendResult::InvalidRequest:
        return AbsolutePowerApi::Result::InvalidArgument;
    case AbsolutePower::BackendResult::SystemUnavailable:
    case AbsolutePower::BackendResult::SetterRejected:
        return AbsolutePowerApi::Result::Rejected;
    }
    return AbsolutePowerApi::Result::Rejected;
}

static_assert(static_cast<std::uint32_t>(ActivationState::Idle) ==
              static_cast<std::uint32_t>(AbsolutePower::ActivationState::Idle));
static_assert(static_cast<std::uint32_t>(ActivationState::Failed) ==
              static_cast<std::uint32_t>(AbsolutePower::ActivationState::Failed));

Result __cdecl GetActivationStatus(ActivationStatusV1* output) noexcept {
    if (!output || output->structSize < sizeof(*output)) return Result::InvalidArgument;
    try {
        const auto source = PowerRuntime::Get().ActivationSnapshot();
        *output = {};
        output->state = static_cast<ActivationState>(source.state);
        output->lastResult = MapBackend(source.backend);
        output->totalChanges = static_cast<std::uint32_t>((std::min)(
            source.totalChanges,
            static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())));
        output->completedChanges = static_cast<std::uint32_t>((std::min)(
            source.completedChanges,
            static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())));
        output->remainingChanges = static_cast<std::uint32_t>((std::min)(
            source.remainingChanges,
            static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())));
        output->sequence = source.sequence;
        Copy(output->requestedPreset, source.requestedPresetId);
        Copy(output->activePreset, source.activePresetId);
        return Result::Ok;
    } catch (...) {
        return Result::Rejected;
    }
}

Result __cdecl GetConfigurationInfo(ConfigurationInfoV1* output) noexcept {
    if (!output || output->structSize < sizeof(*output)) return Result::InvalidArgument;
    try {
        const auto view = PowerRuntime::Get().ConfigurationSnapshot();
        if (view.data.presets.size() > kMaximumRecords ||
            view.data.rules.size() > kMaximumRecords) {
            return Result::CapacityExceeded;
        }
        *output = {};
        output->generation = view.generation;
        output->presetCount = static_cast<std::uint32_t>(view.data.presets.size());
        output->ruleCount = static_cast<std::uint32_t>(view.data.rules.size());
        output->automationEnabled = view.data.automationEnabled ? 1 : 0;
        Copy(output->startupPreset, view.data.startupPreset);
        Copy(output->defaultsPath, Utf8(RuntimePaths::DefaultsPath()));
        Copy(output->importsPath, Utf8(RuntimePaths::ImportsDirectory()));
        Copy(output->customPath, Utf8(RuntimePaths::CustomPath()));
        return Result::Ok;
    } catch (...) {
        return Result::Rejected;
    }
}

Result __cdecl GetPresetRecord(std::uint64_t generation, std::uint32_t index,
                               PresetRecordV1* output) noexcept {
    if (!output || output->structSize < sizeof(*output)) return Result::InvalidArgument;
    try {
        const auto view = PowerRuntime::Get().ConfigurationSnapshot();
        if (view.generation != generation) return Result::StaleGeneration;
        if (index >= view.data.presets.size()) return Result::NotFound;
        *output = {};
        const auto& preset = view.data.presets[index];
        CopyPreset(preset, output->preset);
        CopySource(view.presetSources, preset.id, output->source);
        const auto binding = std::ranges::find(
            view.data.keyboardShortcuts, preset.id,
            &AbsolutePower::PresetShortcut::presetId);
        if (binding != view.data.keyboardShortcuts.end()) {
            output->hasKeyboardBinding = 1;
            Copy(output->keyboardBinding.presetId, binding->presetId);
            output->keyboardBinding.virtualKey = binding->chord.virtualKey;
            output->keyboardBinding.modifiers = static_cast<std::uint8_t>(
                (binding->chord.control ? AbsolutePowerApi::kKeyboardModifierControl : 0) |
                (binding->chord.alt ? AbsolutePowerApi::kKeyboardModifierAlt : 0) |
                (binding->chord.shift ? AbsolutePowerApi::kKeyboardModifierShift : 0));
        }
        return Result::Ok;
    } catch (...) {
        return Result::Rejected;
    }
}

Result __cdecl GetRuleRecord(std::uint64_t generation, std::uint32_t index,
                             RuleRecordV1* output) noexcept {
    if (!output || output->structSize < sizeof(*output)) return Result::InvalidArgument;
    try {
        const auto view = PowerRuntime::Get().ConfigurationSnapshot();
        if (view.generation != generation) return Result::StaleGeneration;
        if (index >= view.data.rules.size()) return Result::NotFound;
        *output = {};
        const auto& rule = view.data.rules[index];
        CopyRule(rule, output->rule);
        CopySource(view.ruleSources, rule.id, output->source);
        return Result::Ok;
    } catch (...) {
        return Result::Rejected;
    }
}

Result __cdecl SaveConfiguration(const ConfigurationDraftV1* input,
                                 SaveReportV1* output) noexcept {
    if (!input || input->structSize < sizeof(*input) || !output ||
        output->structSize < sizeof(*output) ||
        input->presetCount > kMaximumRecords || input->ruleCount > kMaximumRecords ||
        (input->presetCount && !input->presets) ||
        (input->ruleCount && !input->rules) ||
        !Terminated(input->startupPreset, sizeof(input->startupPreset))) {
        return Result::InvalidArgument;
    }
    try {
        auto desired = PowerRuntime::Get().ConfigurationSnapshot().data;
        desired.startupPreset = Read(input->startupPreset);
        desired.automationEnabled = input->automationEnabled != 0;
        desired.presets.clear();
        desired.rules.clear();
        desired.presets.reserve(input->presetCount);
        desired.rules.reserve(input->ruleCount);
        for (std::uint32_t index = 0; index < input->presetCount; ++index) {
            const auto& record = input->presets[index];
            if (record.structSize < sizeof(record) ||
                !Terminated(record.id, sizeof(record.id)) ||
                !Terminated(record.label, sizeof(record.label))) {
                return Result::InvalidArgument;
            }
            desired.presets.push_back(ReadPreset(record));
        }
        for (std::uint32_t index = 0; index < input->ruleCount; ++index) {
            const auto& record = input->rules[index];
            if (record.structSize < sizeof(record) ||
                !Terminated(record.id, sizeof(record.id)) ||
                !Terminated(record.label, sizeof(record.label))) {
                return Result::InvalidArgument;
            }
            desired.rules.push_back(ReadRule(record));
        }
        const auto committed = PowerRuntime::Get().SaveConfiguration(
            input->baseGeneration, std::move(desired));
        *output = {};
        output->result = MapCommit(committed.result);
        output->generation = committed.generation;
        Copy(output->detail, committed.detail);
        return output->result;
    } catch (...) {
        return Result::Rejected;
    }
}

const ApiV1 kApi{
    .moduleId = "absolute.power",
    .version = Plugin::VersionString.data(),
    .getConfigurationInfo = &GetConfigurationInfo,
    .getPresetRecord = &GetPresetRecord,
    .getRuleRecord = &GetRuleRecord,
    .saveConfiguration = &SaveConfiguration,
    .getActivationStatus = &GetActivationStatus,
};
} // namespace

extern "C" __declspec(dllexport) const AbsolutePowerFrontendApi::ApiV1*
AbsolutePower_QueryFrontendApi(std::uint32_t requestedAbiVersion) noexcept {
    return requestedAbiVersion == AbsolutePowerFrontendApi::kAbiVersion ? &kApi : nullptr;
}
