#include "AbsolutePowerAPI.h"

#include "Plugin.h"
#include "PowerAllocator.h"
#include "PowerRuntime.h"

#include <algorithm>
#include <cstring>
#include <string_view>

namespace {
using AbsolutePower::BackendResult;
using AbsolutePower::PowerRuntime;
using AbsolutePowerApi::Result;

template <std::size_t Size>
void CopyString(char (&destination)[Size], std::string_view source) noexcept {
    std::ranges::fill(destination, '\0');
    const auto count = std::min(source.size(), Size - 1);
    std::memcpy(destination, source.data(), count);
}

Result MapBackend(BackendResult result) noexcept {
    switch (result) {
    case BackendResult::Ok:
        return Result::Ok;
    case BackendResult::WorkbenchMissing:
        return Result::WorkbenchMissing;
    case BackendResult::UnsupportedRuntime:
        return Result::UnsupportedRuntime;
    case BackendResult::SnapshotSeamUnavailable:
        return Result::NativeSeamUnavailable;
    case BackendResult::PilotNotReady:
        return Result::PilotNotReady;
    case BackendResult::InvalidRequest:
        return Result::InvalidArgument;
    case BackendResult::SystemUnavailable:
        return Result::NotFound;
    case BackendResult::SetterRejected:
        return Result::Rejected;
    }
    return Result::Rejected;
}

Result __cdecl GetStatus(AbsolutePowerApi::StatusV1* output) noexcept {
    if (!output || output->structSize < sizeof(*output)) return Result::InvalidArgument;
    try {
        const auto state = PowerRuntime::Get().State();
        output->state = static_cast<AbsolutePowerApi::RuntimeState>(state);
        output->automationEnabled = PowerRuntime::Get().AutomationEnabled() ? 1 : 0;
        CopyString(output->activePreset, PowerRuntime::Get().ActivePresetId());
        return Result::Ok;
    } catch (...) {
        return Result::Rejected;
    }
}

Result __cdecl GetSnapshot(AbsolutePowerApi::SnapshotV1* output) noexcept {
    if (!output || output->structSize < sizeof(*output)) return Result::InvalidArgument;
    try {
        AbsolutePower::Snapshot snapshot{};
        const auto result = PowerRuntime::Get().Capture(snapshot);
        if (result != BackendResult::Ok) return MapBackend(result);
        output->pilotReady = snapshot.pilotReady ? 1 : 0;
        output->available = snapshot.available;
        output->totalPower = snapshot.totalPower;
        for (std::size_t index = 0; index < AbsolutePower::kSystemCount; ++index) {
            output->systems[index].present = snapshot.systems[index].present ? 1 : 0;
            output->systems[index].current = snapshot.systems[index].current;
            output->systems[index].maximum = snapshot.systems[index].maximum;
        }
        return Result::Ok;
    } catch (...) {
        return Result::Rejected;
    }
}

std::uint32_t __cdecl GetPresetCount() noexcept {
    try {
        return static_cast<std::uint32_t>(PowerRuntime::Get().Presets().size());
    } catch (...) {
        return 0;
    }
}

Result __cdecl GetPreset(std::uint32_t index, AbsolutePowerApi::PresetV1* output) noexcept {
    if (!output || output->structSize < sizeof(*output)) return Result::InvalidArgument;
    try {
        const auto presets = PowerRuntime::Get().Presets();
        if (index >= presets.size()) return Result::NotFound;
        const auto& preset = presets[index];
        CopyString(output->id, preset.id);
        CopyString(output->label, preset.displayName);
        for (std::size_t system = 0; system < AbsolutePower::kSystemCount; ++system) {
            output->systems[system] = {preset.systems[system].green,
                                       preset.systems[system].yellow,
                                       preset.systems[system].red};
            output->tieBreakOrder[system] =
                static_cast<std::uint8_t>(preset.tieBreakOrder[system]);
        }
        return Result::Ok;
    } catch (...) {
        return Result::Rejected;
    }
}

std::uint32_t __cdecl GetRuleCount() noexcept {
    try {
        return static_cast<std::uint32_t>(PowerRuntime::Get().Rules().size());
    } catch (...) {
        return 0;
    }
}

Result __cdecl GetRule(std::uint32_t index, AbsolutePowerApi::RuleV1* output) noexcept {
    if (!output || output->structSize < sizeof(*output)) return Result::InvalidArgument;
    try {
        const auto rules = PowerRuntime::Get().Rules();
        if (index >= rules.size()) return Result::NotFound;
        const auto& rule = rules[index];
        CopyString(output->id, rule.id);
        CopyString(output->label, rule.displayName);
        output->enabled = rule.enabled ? 1 : 0;
        output->trigger = static_cast<std::uint8_t>(rule.trigger);
        output->sourceSystem = static_cast<std::uint8_t>(rule.sourceSystem);
        output->targetSystem = static_cast<std::uint8_t>(rule.targetSystem);
        output->targetPips = rule.targetPips;
        output->thresholdPercent = rule.thresholdPercent;
        output->hysteresisPercent = rule.hysteresisPercent;
        output->holdMilliseconds = rule.holdMilliseconds;
        output->priority = rule.priority;
        return Result::Ok;
    } catch (...) {
        return Result::Rejected;
    }
}

std::uint32_t __cdecl GetCommandCount() noexcept {
    try {
        return static_cast<std::uint32_t>(PowerRuntime::Get().Commands().size());
    } catch (...) {
        return 0;
    }
}

Result __cdecl GetCommand(std::uint32_t index, AbsolutePowerApi::CommandV1* output) noexcept {
    if (!output || output->structSize < sizeof(*output)) return Result::InvalidArgument;
    try {
        const auto commands = PowerRuntime::Get().Commands();
        if (index >= commands.size()) return Result::NotFound;
        CopyString(output->id, commands[index].id);
        CopyString(output->label, commands[index].label);
        CopyString(output->category, commands[index].category);
        return Result::Ok;
    } catch (...) {
        return Result::Rejected;
    }
}

Result __cdecl InvokeCommand(const char* commandId) noexcept {
    if (!commandId || !*commandId) return Result::InvalidArgument;
    try {
        return MapBackend(PowerRuntime::Get().InvokeCommand(commandId));
    } catch (...) {
        return Result::Rejected;
    }
}

Result __cdecl SetAutomationEnabled(std::uint8_t enabled) noexcept {
    try {
        return PowerRuntime::Get().SetAutomationEnabled(enabled != 0) ? Result::Ok
                                                                      : Result::Rejected;
    } catch (...) {
        return Result::Rejected;
    }
}

Result __cdecl ReloadConfiguration() noexcept {
    try {
        PowerRuntime::Get().ReloadConfiguration();
        return Result::Ok;
    } catch (...) {
        return Result::Rejected;
    }
}

Result __cdecl PreviewPreset(const AbsolutePowerApi::PresetV1* input,
                             AbsolutePowerApi::PreviewV1* output) noexcept {
    if (!input || input->structSize < sizeof(*input) || !output ||
        output->structSize < sizeof(*output)) {
        return Result::InvalidArgument;
    }
    try {
        AbsolutePower::Preset preset;
        preset.id = std::string(input->id, strnlen_s(input->id, sizeof(input->id)));
        preset.displayName =
            std::string(input->label, strnlen_s(input->label, sizeof(input->label)));
        for (std::size_t index = 0; index < AbsolutePower::kSystemCount; ++index) {
            preset.systems[index] = {input->systems[index].green,
                                     input->systems[index].yellow,
                                     input->systems[index].red};
            preset.tieBreakOrder[index] =
                static_cast<AbsolutePower::SystemId>(input->tieBreakOrder[index]);
        }
        if (!AbsolutePower::PowerAllocator::ValidatePreset(preset)) {
            return Result::InvalidArgument;
        }
        AbsolutePower::Snapshot snapshot{};
        const auto capture = PowerRuntime::Get().Capture(snapshot);
        if (capture != BackendResult::Ok) return MapBackend(capture);
        const auto allocation = AbsolutePower::PowerAllocator::Allocate(snapshot, preset);
        if (allocation.status != AbsolutePower::AllocationStatus::Ok) {
            return Result::InvalidArgument;
        }
        output->unassigned = allocation.unassigned;
        output->totalClipped = allocation.clippedPresetPips;
        output->firstIncompleteTier = AbsolutePowerApi::PreviewTier::Complete;
        for (std::size_t index = 0; index < AbsolutePower::kSystemCount; ++index) {
            output->target[index] = allocation.target[index];
            const auto& plan = preset.systems[index];
            const auto requested = static_cast<std::uint32_t>(plan.green) + plan.yellow +
                                   plan.red;
            const auto maximum = snapshot.systems[index].present
                                     ? static_cast<std::uint32_t>(snapshot.systems[index].maximum)
                                     : 0u;
            output->clipped[index] = static_cast<std::uint16_t>(std::min<std::uint32_t>(
                requested > maximum ? requested - maximum : 0u,
                std::numeric_limits<std::uint16_t>::max()));
        }
        for (std::size_t tier = 0; tier < 3; ++tier) {
            bool complete = true;
            for (std::size_t index = 0; index < AbsolutePower::kSystemCount; ++index) {
                const auto& plan = preset.systems[index];
                std::uint32_t requested = plan.green;
                if (tier >= 1) requested += plan.yellow;
                if (tier >= 2) requested += plan.red;
                const auto maximum = snapshot.systems[index].present
                                         ? snapshot.systems[index].maximum
                                         : 0;
                const auto desired = std::min<std::uint32_t>(requested, maximum);
                complete &= allocation.target[index] >= desired;
            }
            if (!complete) {
                output->firstIncompleteTier =
                    static_cast<AbsolutePowerApi::PreviewTier>(tier);
                break;
            }
        }
        return Result::Ok;
    } catch (...) {
        return Result::Rejected;
    }
}

const AbsolutePowerApi::ApiV1 kApi{
    .moduleId = "absolute.power",
    .displayName = "Absolute Power",
    .version = Plugin::VersionString.data(),
    .getStatus = &GetStatus,
    .getSnapshot = &GetSnapshot,
    .getPresetCount = &GetPresetCount,
    .getPreset = &GetPreset,
    .getRuleCount = &GetRuleCount,
    .getRule = &GetRule,
    .getCommandCount = &GetCommandCount,
    .getCommand = &GetCommand,
    .invokeCommand = &InvokeCommand,
    .setAutomationEnabled = &SetAutomationEnabled,
    .reloadConfiguration = &ReloadConfiguration,
    .previewPreset = &PreviewPreset,
};

} // namespace

extern "C" __declspec(dllexport) const AbsolutePowerApi::ApiV1*
AbsolutePower_QueryApi(std::uint32_t requestedAbiVersion) noexcept {
    return requestedAbiVersion == AbsolutePowerApi::kAbiVersion ? &kApi : nullptr;
}
