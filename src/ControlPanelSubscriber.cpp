#include "PCH.h"

#include "ControlPanelSubscriber.h"

#include "AbsoluteControlPanelLiveComponentsAPI.h"
#include "Configuration.h"
#include "GameTaskScheduler.h"
#include "KeyboardShortcut.h"
#include "PowerRuntime.h"
#include "PowerAllocator.h"
#include "Plugin.h"
#include "RuntimePaths.h"
#include "SuiteHost.h"

#include <numeric>

namespace {
using namespace AbsoluteControlPanelApi;
using AbsolutePower::BackendResult;
using AbsolutePower::KeyboardChord;
using AbsolutePower::PowerRuntime;
using AbsolutePower::PresetShortcut;
namespace Live = AbsoluteControlPanelExperimental;

using AbsoluteControlPanelQueryApi = const ApiV1* (__cdecl*)(
    std::uint32_t requestedAbiVersion) noexcept;
using AbsoluteControlPanelQueryLiveApi = const Live::ExperimentalApiV1* (__cdecl*)(
    std::uint32_t requestedAbiVersion) noexcept;

constexpr std::string_view kPowerModuleId = "absolute.power";
constexpr std::uint32_t kKeyboardChord =
    kBindingKeyboard | kBindingModifiers | kBindingClearable;

std::atomic<const ApiV1*> g_hostApi{};
std::atomic<const Live::ExperimentalApiV1*> g_liveHostApi{};
std::atomic<bool> g_registered{};
std::mutex g_registrationMutex;

struct PresetControl {
    enum class Kind : std::uint8_t {
        Selector,
        ActivationStatus,
        PreviewSummary,
        OrderSummary,
        Previous,
        Next,
        PreviousOrderSystem,
        NextOrderSystem,
        MoveEarlier,
        MoveLater,
        Create,
        Duplicate,
        DeleteOrHide,
        RevertOverride,
        SetStartup,
        ActivateSelected,
        SaveAndActivate,
        Name,
        Binding,
        TierValue,
    };
    std::string controlId;
    Kind kind{Kind::Selector};
    std::size_t systemIndex{};
    AbsolutePower::PriorityTier tier{AbsolutePower::PriorityTier::Green};
};

struct PresetPageState {
    std::mutex mutex;
    bool editing{};
    std::uint64_t baseGeneration{};
    AbsolutePower::ConfigurationData opening;
    AbsolutePower::ConfigurationData inherited;
    AbsolutePower::ConfigurationData draft;
    std::vector<AbsolutePower::ConfigurationRecordSource> sources;
    std::string selectedPresetId;
    AbsolutePower::SystemId selectedOrderSystem{AbsolutePower::SystemId::Weapon0};
    std::vector<PresetControl> records;
    std::vector<ControlDescriptorV1> controls;
};

struct AutomationPageState {
    std::mutex mutex;
    bool editing{};
    std::uint64_t baseGeneration{};
    AbsolutePower::ConfigurationData opening;
    AbsolutePower::ConfigurationData inherited;
    AbsolutePower::ConfigurationData draft;
    std::vector<AbsolutePower::ConfigurationRecordSource> sources;
    std::string selectedRuleId;
    struct Control {
        enum class Kind : std::uint8_t {
            Warning,
            EventSourceStatus,
            GlobalSummary,
            GlobalEnabled,
            DisableAllNow,
            RuleSelector,
            RuleSummary,
            RulePlainSummary,
            Name,
            Previous,
            Next,
            Create,
            Duplicate,
            DeleteOrHide,
            RevertOverride,
            Enabled,
            Trigger,
            Source,
            Target,
            TargetPips,
            Threshold,
            Hysteresis,
            HoldMilliseconds,
            Priority,
        };
        std::string controlId;
        Kind kind{Kind::Warning};
    };
    std::vector<Control> records;
    std::vector<ControlDescriptorV1> controls;
};

PresetPageState g_presets;
AutomationPageState g_automation;
constexpr std::uint32_t kAutomationPreviewControlCount = 3;

class LiveFrameMailbox {
public:
    void Publish(Live::LiveFrameV1 frame) noexcept {
        const auto active = active_.load(std::memory_order_acquire);
        for (std::uint32_t candidate = 0; candidate < slots_.size(); ++candidate) {
            if (candidate == active ||
                slots_[candidate].readers.load(std::memory_order_acquire) != 0) continue;
            frame.sequence = nextSequence_.fetch_add(1, std::memory_order_relaxed) + 1;
            const auto now = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());
            const auto previous = lastTimestamp_.load(std::memory_order_relaxed);
            frame.monotonicTimestampUs = (std::max)(now, previous + 1);
            lastTimestamp_.store(frame.monotonicTimestampUs, std::memory_order_relaxed);
            slots_[candidate].frame = frame;
            active_.store(candidate, std::memory_order_release);
            return;
        }
    }

    Live::Result Read(Live::LiveFrameV1* output) noexcept {
        if (!output) return Live::Result::InvalidArgument;
        for (std::uint32_t attempt = 0; attempt < slots_.size(); ++attempt) {
            const auto candidate = active_.load(std::memory_order_acquire);
            if (candidate >= slots_.size()) return Live::Result::NotReady;
            slots_[candidate].readers.fetch_add(1, std::memory_order_acq_rel);
            if (candidate != active_.load(std::memory_order_acquire)) {
                slots_[candidate].readers.fetch_sub(1, std::memory_order_release);
                continue;
            }
            *output = slots_[candidate].frame;
            slots_[candidate].readers.fetch_sub(1, std::memory_order_release);
            return Live::Result::Ok;
        }
        return Live::Result::NotReady;
    }

private:
    struct Slot {
        std::atomic<std::uint32_t> readers{};
        Live::LiveFrameV1 frame{};
    };
    std::array<Slot, 3> slots_{};
    std::atomic<std::uint32_t> active_{std::numeric_limits<std::uint32_t>::max()};
    std::atomic<std::uint64_t> nextSequence_{};
    std::atomic<std::uint64_t> lastTimestamp_{};
};

LiveFrameMailbox g_powerFrames;
std::atomic<std::uint64_t> g_lastFramePublishUs{};
std::mutex g_framePublishMutex;

void PublishPowerFrame(bool force);

template <std::size_t N>
void Copy(char (&target)[N], std::string_view source) noexcept {
    const auto count = (std::min)(source.size(), N - 1);
    std::memcpy(target, source.data(), count);
    target[count] = '\0';
}

ValueV1 Boolean(bool value) noexcept {
    ValueV1 result;
    result.kind = ValueKind::Boolean;
    result.booleanValue = value ? 1U : 0U;
    return result;
}

ValueV1 String(std::string_view value) noexcept {
    ValueV1 result;
    result.kind = ValueKind::String;
    Copy(result.stringValue, value);
    return result;
}

ValueV1 Integer(std::int64_t value) noexcept {
    ValueV1 result;
    result.kind = ValueKind::Integer;
    result.integerValue = value;
    return result;
}

std::string DuplicateDisplayName(std::string_view source) {
    constexpr std::string_view suffix = " Copy";
    constexpr std::size_t maximum = 95;
    const auto kept = (std::min)(source.size(), maximum - suffix.size());
    return std::string(source.substr(0, kept)) + std::string(suffix);
}

ControlDescriptorV1 Control(ControlKind kind, std::uint32_t flags,
                            std::string_view id, std::string_view label,
                            std::string_view description) {
    ControlDescriptorV1 control;
    control.kind = kind;
    control.flags = flags;
    Copy(control.controlId, id);
    Copy(control.label, label);
    Copy(control.description, description);
    return control;
}

const PresetControl* FindPresetControl(std::string_view controlId) noexcept {
    const auto found = std::ranges::find(g_presets.records, controlId,
                                         &PresetControl::controlId);
    return found == g_presets.records.end() ? nullptr : &*found;
}

AbsolutePower::Preset* FindSelected(AbsolutePower::ConfigurationData& data) noexcept {
    const auto found = std::ranges::find(
        data.presets, g_presets.selectedPresetId, &AbsolutePower::Preset::id);
    return found == data.presets.end() ? nullptr : &*found;
}

const AbsolutePower::Preset* FindSelected(
    const AbsolutePower::ConfigurationData& data) noexcept {
    const auto found = std::ranges::find(
        data.presets, g_presets.selectedPresetId, &AbsolutePower::Preset::id);
    return found == data.presets.end() ? nullptr : &*found;
}

void EnsureSelection(AbsolutePower::ConfigurationData& data) {
    if (FindSelected(data)) return;
    const auto startup = std::ranges::find(
        data.presets, data.startupPreset, &AbsolutePower::Preset::id);
    if (startup != data.presets.end()) g_presets.selectedPresetId = startup->id;
    else if (!data.presets.empty()) g_presets.selectedPresetId = data.presets.front().id;
    else g_presets.selectedPresetId.clear();
}

void BeginPresetDraft(const AbsolutePower::ConfigurationView& view) {
    if (g_presets.editing) return;
    g_presets.opening = view.data;
    g_presets.inherited = view.inherited;
    g_presets.draft = view.data;
    g_presets.sources = view.presetSources;
    g_presets.baseGeneration = view.generation;
    g_presets.editing = true;
    EnsureSelection(g_presets.draft);
}

std::string RecordSourceLabel(
    std::string_view recordId,
    const std::vector<AbsolutePower::ConfigurationRecordSource>& sources) {
    std::string source = "User";
    const auto found = std::ranges::find(
        sources, recordId, &AbsolutePower::ConfigurationRecordSource::recordId);
    if (found != sources.end()) {
        switch (found->baseKind) {
        case AbsolutePower::ConfigurationSourceKind::BuiltIn:
            source = "Built-in";
            break;
        case AbsolutePower::ConfigurationSourceKind::Defaults:
            source = "Defaults";
            break;
        case AbsolutePower::ConfigurationSourceKind::Import:
            source = found->baseLabel.empty() ? "Imported pack" :
                "Pack: " + found->baseLabel;
            break;
        case AbsolutePower::ConfigurationSourceKind::Custom:
            source = "User";
            break;
        }
        if (found->userOverride &&
            found->baseKind != AbsolutePower::ConfigurationSourceKind::Custom) {
            source = "User override of " + source;
        }
    }
    return source;
}

const AutomationPageState::Control* FindAutomationControl(
    std::string_view controlId) noexcept {
    const auto found = std::ranges::find(
        g_automation.records, controlId, &AutomationPageState::Control::controlId);
    return found == g_automation.records.end() ? nullptr : &*found;
}

AbsolutePower::AutomationRule* FindSelectedRule(
    AbsolutePower::ConfigurationData& data) noexcept {
    const auto found = std::ranges::find(
        data.rules, g_automation.selectedRuleId, &AbsolutePower::AutomationRule::id);
    return found == data.rules.end() ? nullptr : &*found;
}

const AbsolutePower::AutomationRule* FindSelectedRule(
    const AbsolutePower::ConfigurationData& data) noexcept {
    const auto found = std::ranges::find(
        data.rules, g_automation.selectedRuleId, &AbsolutePower::AutomationRule::id);
    return found == data.rules.end() ? nullptr : &*found;
}

void EnsureAutomationSelection(const AbsolutePower::ConfigurationData& data) {
    if (FindSelectedRule(data)) return;
    if (!data.rules.empty()) g_automation.selectedRuleId = data.rules.front().id;
    else g_automation.selectedRuleId.clear();
}

void BeginAutomationDraft(const AbsolutePower::ConfigurationView& view) {
    if (g_automation.editing) return;
    g_automation.opening = view.data;
    g_automation.inherited = view.inherited;
    g_automation.draft = view.data;
    g_automation.sources = view.ruleSources;
    g_automation.baseGeneration = view.generation;
    g_automation.editing = true;
    EnsureAutomationSelection(g_automation.draft);
}

std::string_view TriggerLabel(AbsolutePower::TriggerKind trigger) noexcept {
    switch (trigger) {
    case AbsolutePower::TriggerKind::WeaponFired: return "Weapon fired";
    case AbsolutePower::TriggerKind::IncomingDamage: return "Incoming damage";
    case AbsolutePower::TriggerKind::ThrottleAbove: return "Throttle above";
    case AbsolutePower::TriggerKind::Manual: return "Manual";
    }
    return "Unknown";
}

std::string RuleSourceLabel(AbsolutePower::SystemId source) {
    return source == AbsolutePower::SystemId::Invalid ? "any weapon" :
        std::string(AbsolutePower::SystemLabel(source));
}

std::string RuleRecordSummary(
    const AbsolutePower::ConfigurationData& data,
    const std::vector<AbsolutePower::ConfigurationRecordSource>& sources) {
    const auto* rule = FindSelectedRule(data);
    if (!rule) return "No automation rule";
    const auto provenance = RecordSourceLabel(rule->id, sources);
    const auto position = std::ranges::find(
        data.rules, rule->id, &AbsolutePower::AutomationRule::id);
    const auto ordinal = position == data.rules.end() ? 0U :
        static_cast<std::size_t>(std::distance(data.rules.begin(), position)) + 1;
    return std::format("Rule {}/{} | {} | {}", ordinal, data.rules.size(),
        rule->displayName, provenance);
}

std::string RulePlainSummary(const AbsolutePower::AutomationRule& rule) {
    const auto pips = rule.targetPips == std::numeric_limits<std::uint16_t>::max() ?
        std::string("max") : std::to_string(rule.targetPips);
    std::string trigger;
    switch (rule.trigger) {
    case AbsolutePower::TriggerKind::WeaponFired:
        trigger = "After " + RuleSourceLabel(rule.sourceSystem) + " fires";
        break;
    case AbsolutePower::TriggerKind::IncomingDamage:
        trigger = "After incoming damage";
        break;
    case AbsolutePower::TriggerKind::ThrottleAbove:
        trigger = std::format("Above {}% throttle ({}% hysteresis)",
            rule.thresholdPercent, rule.hysteresisPercent);
        break;
    case AbsolutePower::TriggerKind::Manual:
        trigger = "While manually held";
        break;
    }
    return std::format("{}: demand {} {} pips for {} ms at priority {}", trigger,
        AbsolutePower::SystemLabel(rule.targetSystem), pips,
        rule.holdMilliseconds, rule.priority);
}

std::string ActivationSummary(const AbsolutePower::ActivationStatus& status) {
    switch (status.state) {
    case AbsolutePower::ActivationState::Idle: return "No activation requested";
    case AbsolutePower::ActivationState::Queued:
        return "Queued " + status.requestedPresetId;
    case AbsolutePower::ActivationState::Waiting:
        return "Waiting for pilot / native snapshot";
    case AbsolutePower::ActivationState::Settling:
        return std::format("Settling {}: {}/{} pips",
            status.requestedPresetId, status.completedChanges, status.totalChanges);
    case AbsolutePower::ActivationState::Converged:
        return "Active " + status.activePresetId;
    case AbsolutePower::ActivationState::Failed:
        return "Activation failed for " + status.requestedPresetId;
    }
    return "Activation state unavailable";
}

std::string_view BackendResultLabel(BackendResult result) noexcept {
    switch (result) {
    case BackendResult::Ok: return "Ok";
    case BackendResult::WorkbenchMissing: return "HostMissing";
    case BackendResult::UnsupportedRuntime: return "UnsupportedRuntime";
    case BackendResult::SnapshotSeamUnavailable: return "NativeSeamUnavailable";
    case BackendResult::PilotNotReady: return "PilotNotReady";
    case BackendResult::SystemUnavailable: return "SystemUnavailable";
    case BackendResult::InvalidRequest: return "InvalidRequest";
    case BackendResult::SetterRejected: return "SetterRejected";
    }
    return "Unknown";
}

std::string_view RuntimeStateLabel(AbsolutePower::RuntimeState state) noexcept {
    switch (state) {
    case AbsolutePower::RuntimeState::Uninitialized: return "Uninitialized";
    case AbsolutePower::RuntimeState::WorkbenchMissing: return "LegacyHostMissing";
    case AbsolutePower::RuntimeState::AwaitingNativeSnapshotSeam:
        return "AwaitingNativeSnapshot";
    case AbsolutePower::RuntimeState::Ready: return "Ready";
    }
    return "Unknown";
}

std::string_view HostSelectionLabel(SuiteHost::Selection selection) noexcept {
    switch (selection) {
    case SuiteHost::Selection::Workbench: return "Legacy Workbench";
    case SuiteHost::Selection::Hotas: return "AbsoluteHOTAS bridge";
    case SuiteHost::Selection::Suppressed: return "Legacy Workbench suppressed";
    case SuiteHost::Selection::Unavailable: return "Standalone Power";
    case SuiteHost::Selection::Incompatible: return "Optional host incompatible";
    }
    return "Unknown";
}

std::string_view ShortSystemLabel(AbsolutePower::SystemId system) noexcept {
    constexpr std::array<std::string_view, AbsolutePower::kSystemCount> labels{
        "W0", "W1", "W2", "ENG", "SHD", "GRV"};
    const auto index = AbsolutePower::ToIndex(system);
    return index < labels.size() ? labels[index] : "?";
}

std::string OrderSummary(const AbsolutePower::Preset& preset) {
    std::string result = "Focus ";
    result += ShortSystemLabel(g_presets.selectedOrderSystem);
    result += " | ";
    for (std::size_t index = 0; index < preset.tieBreakOrder.size(); ++index) {
        if (index != 0) result += " > ";
        result += ShortSystemLabel(preset.tieBreakOrder[index]);
    }
    return result;
}

std::string PreviewSummary(const AbsolutePower::Preset& preset) {
    AbsolutePower::Snapshot snapshot{};
    const auto snapshotResult = PowerRuntime::Get().Capture(snapshot);
    if (snapshotResult != BackendResult::Ok) {
        return "Live preview unavailable; editing and saving remain available";
    }
    const auto allocation = AbsolutePower::PowerAllocator::Allocate(snapshot, preset);
    if (allocation.status != AbsolutePower::AllocationStatus::Ok) {
        return "Preview rejected the current draft allocation";
    }
    const auto assigned = std::accumulate(
        allocation.target.begin(), allocation.target.end(), std::uint32_t{});
    return std::format("{} assigned | {} unassigned | {} clipped",
        assigned, allocation.unassigned, allocation.clippedPresetPips);
}

std::uint16_t& TierValue(AbsolutePower::TierPlan& plan,
                         AbsolutePower::PriorityTier tier) noexcept {
    if (tier == AbsolutePower::PriorityTier::Yellow) return plan.yellow;
    if (tier == AbsolutePower::PriorityTier::Red) return plan.red;
    return plan.green;
}

std::uint16_t TierValue(const AbsolutePower::TierPlan& plan,
                        AbsolutePower::PriorityTier tier) noexcept {
    if (tier == AbsolutePower::PriorityTier::Yellow) return plan.yellow;
    if (tier == AbsolutePower::PriorityTier::Red) return plan.red;
    return plan.green;
}

std::optional<KeyboardChord> FindBinding(
    const std::vector<PresetShortcut>& shortcuts, std::string_view presetId) {
    const auto found = std::ranges::find(shortcuts, presetId, &PresetShortcut::presetId);
    return found == shortcuts.end() ? std::nullopt : std::optional(found->chord);
}

void SetBinding(std::vector<PresetShortcut>& shortcuts, std::string_view presetId,
                std::optional<KeyboardChord> chord) {
    std::erase_if(shortcuts, [&](const auto& shortcut) {
        return shortcut.presetId == presetId;
    });
    if (chord) shortcuts.push_back({std::string(presetId), *chord});
    std::ranges::sort(shortcuts, {}, &PresetShortcut::presetId);
}

Result MapCommand(BackendResult result) noexcept {
    switch (result) {
    case BackendResult::Ok: return Result::Ok;
    case BackendResult::InvalidRequest: return Result::InvalidArgument;
    case BackendResult::SnapshotSeamUnavailable:
    case BackendResult::PilotNotReady: return Result::NotReady;
    default: return Result::Rejected;
    }
}

Result __cdecl ReadPresets(void*, const char* rawId, ValueV1* output) noexcept {
    if (!rawId || !output || output->structSize < sizeof(ValueV1)) {
        return Result::InvalidArgument;
    }
    try {
        const auto* control = FindPresetControl(rawId);
        if (!control) return Result::NotFound;
        if (control->kind == PresetControl::Kind::ActivationStatus) {
            *output = String(ActivationSummary(
                PowerRuntime::Get().ActivationSnapshot()));
            return Result::Ok;
        }
        auto view = PowerRuntime::Get().ConfigurationSnapshot();
        AbsolutePower::Preset previewPreset;
        bool previewRequested{};
        {
            std::scoped_lock lock(g_presets.mutex);
            auto& data = g_presets.editing ? g_presets.draft : view.data;
            EnsureSelection(data);
            const auto* preset = FindSelected(data);
            if (!preset) return Result::NotReady;
            if (control->kind == PresetControl::Kind::Selector) {
                const auto selected = std::ranges::find(
                    data.presets, g_presets.selectedPresetId,
                    &AbsolutePower::Preset::id);
                if (selected == data.presets.end()) return Result::NotReady;
                *output = Integer(std::distance(data.presets.begin(), selected));
            } else if (control->kind == PresetControl::Kind::PreviewSummary) {
                previewPreset = *preset;
                previewRequested = true;
            } else if (control->kind == PresetControl::Kind::OrderSummary) {
                *output = String(OrderSummary(*preset));
            } else if (control->kind == PresetControl::Kind::Name) {
                *output = String(preset->displayName);
            } else if (control->kind == PresetControl::Kind::Binding) {
                const auto binding = FindBinding(data.keyboardShortcuts, preset->id);
                *output = String(binding ?
                    AbsolutePower::KeyboardShortcutPolicy::StorageName(*binding) :
                    "Unbound");
            } else if (control->kind == PresetControl::Kind::TierValue &&
                       control->systemIndex < AbsolutePower::kSystemCount) {
                *output = Integer(TierValue(
                    preset->systems[control->systemIndex], control->tier));
            } else {
                return Result::NotFound;
            }
        }
        if (previewRequested) *output = String(PreviewSummary(previewPreset));
        return Result::Ok;
    } catch (...) {
        return Result::Rejected;
    }
}

Result __cdecl WritePresets(void*, const char* rawId, const ValueV1* value) noexcept {
    if (!rawId || !value || value->structSize < sizeof(ValueV1)) {
        return Result::InvalidArgument;
    }
    try {
        const auto* control = FindPresetControl(rawId);
        if (!control) return Result::NotFound;
        const auto current = PowerRuntime::Get().ConfigurationSnapshot();
        if (control->kind == PresetControl::Kind::Selector) {
            if (value->kind != ValueKind::Integer || value->integerValue < 0) {
                return Result::InvalidArgument;
            }
            {
                std::scoped_lock lock(g_presets.mutex);
                auto effective = current.data;
                auto& data = g_presets.editing ? g_presets.draft : effective;
                EnsureSelection(data);
                const auto index = static_cast<std::size_t>(value->integerValue);
                if (index >= data.presets.size()) return Result::InvalidArgument;
                g_presets.selectedPresetId = data.presets[index].id;
            }
            PublishPowerFrame(true);
            return Result::Ok;
        }
        {
            std::scoped_lock lock(g_presets.mutex);
            BeginPresetDraft(current);
            auto* preset = FindSelected(g_presets.draft);
            if (!preset) return Result::NotReady;
            if (control->kind == PresetControl::Kind::Name) {
                if (value->kind != ValueKind::String ||
                    !std::memchr(value->stringValue, '\0', kStringValueCapacity)) {
                    return Result::InvalidArgument;
                }
                const std::string_view name{value->stringValue};
                if (name.empty() || name.size() >= 96 ||
                    name.contains('\r') || name.contains('\n')) {
                    return Result::InvalidArgument;
                }
                preset->displayName = name;
            } else if (control->kind == PresetControl::Kind::Binding) {
                if (value->kind != ValueKind::String ||
                    !std::memchr(value->stringValue, '\0', kStringValueCapacity)) {
                    return Result::InvalidArgument;
                }
                const std::string_view encoded{value->stringValue};
                std::optional<KeyboardChord> proposed;
                if (!encoded.empty()) {
                    proposed = AbsolutePower::KeyboardShortcutPolicy::Parse(encoded);
                    if (!proposed) return Result::InvalidArgument;
                }
                if (proposed && std::ranges::any_of(
                        g_presets.draft.keyboardShortcuts,
                        [&](const auto& shortcut) {
                            return shortcut.presetId != preset->id &&
                                   shortcut.chord == *proposed;
                        })) {
                    return Result::Rejected;
                }
                SetBinding(g_presets.draft.keyboardShortcuts, preset->id, proposed);
            } else if (control->kind == PresetControl::Kind::TierValue &&
                       control->systemIndex < AbsolutePower::kSystemCount) {
                if (value->kind != ValueKind::Integer || value->integerValue < 0 ||
                    value->integerValue > 32) {
                    return Result::InvalidArgument;
                }
                auto& plan = preset->systems[control->systemIndex];
                auto& target = TierValue(plan, control->tier);
                const auto previous = target;
                target = static_cast<std::uint16_t>(value->integerValue);
                const auto total = static_cast<std::uint32_t>(plan.green) +
                                   plan.yellow + plan.red;
                if (total > 32) {
                    target = previous;
                    return Result::InvalidArgument;
                }
            } else {
                return Result::NotFound;
            }
        }
        PublishPowerFrame(true);
        return Result::Ok;
    } catch (...) {
        return Result::Rejected;
    }
}

Result __cdecl ReadPresetChoiceOptions(void*, const char* rawId,
                                       ChoiceOptionV1* options,
                                       std::uint32_t capacity,
                                       std::uint32_t* count) noexcept {
    if (!rawId || !options || !count ||
        std::string_view(rawId) != "preset-selector") {
        return Result::InvalidArgument;
    }
    try {
        const auto view = PowerRuntime::Get().ConfigurationSnapshot();
        std::scoped_lock lock(g_presets.mutex);
        auto effective = view.data;
        auto& data = g_presets.editing ? g_presets.draft : effective;
        const auto& sources = g_presets.editing ? g_presets.sources :
                                                   view.presetSources;
        EnsureSelection(data);
        if (data.presets.empty()) return Result::NotReady;
        if (capacity < data.presets.size()) return Result::CapacityExceeded;
        for (std::size_t index = 0; index < data.presets.size(); ++index) {
            if (options[index].structSize < sizeof(ChoiceOptionV1)) {
                return Result::InvalidArgument;
            }
            options[index].value = static_cast<std::int64_t>(index);
            auto label = data.presets[index].displayName + "  -  " +
                RecordSourceLabel(data.presets[index].id, sources);
            if (data.startupPreset == data.presets[index].id) {
                label += "  -  STARTUP";
            }
            Copy(options[index].label, label);
        }
        *count = static_cast<std::uint32_t>(data.presets.size());
        return Result::Ok;
    } catch (...) {
        return Result::Rejected;
    }
}

Result __cdecl ReadAutomationChoiceOptions(void*, const char* rawId,
                                           ChoiceOptionV1* options,
                                           std::uint32_t capacity,
                                           std::uint32_t* count) noexcept {
    if (!rawId || !options || !count) return Result::InvalidArgument;
    std::span<const std::string_view> labels;
    constexpr std::array triggerLabels{
        std::string_view{"Weapon fired"}, std::string_view{"Player ship damaged"},
        std::string_view{"Throttle above"}, std::string_view{"Manual"}};
    constexpr std::array sourceLabels{
        std::string_view{"Weapon 0"}, std::string_view{"Weapon 1"},
        std::string_view{"Weapon 2"}, std::string_view{"Any weapon"}};
    constexpr std::array targetLabels{
        std::string_view{"Weapon 0"}, std::string_view{"Weapon 1"},
        std::string_view{"Weapon 2"}, std::string_view{"Engines"},
        std::string_view{"Shields"}, std::string_view{"Grav drive"}};
    const std::string_view id{rawId};
    if (id == "rule-selector") {
        try {
            const auto view = PowerRuntime::Get().ConfigurationSnapshot();
            std::scoped_lock lock(g_automation.mutex);
            auto effective = view.data;
            auto& data = g_automation.editing ? g_automation.draft : effective;
            const auto& sources = g_automation.editing ? g_automation.sources :
                                                          view.ruleSources;
            EnsureAutomationSelection(data);
            if (data.rules.empty()) return Result::NotReady;
            if (capacity < data.rules.size()) return Result::CapacityExceeded;
            for (std::size_t index = 0; index < data.rules.size(); ++index) {
                if (options[index].structSize < sizeof(ChoiceOptionV1)) {
                    return Result::InvalidArgument;
                }
                options[index].value = static_cast<std::int64_t>(index);
                const auto& rule = data.rules[index];
                const auto label = rule.displayName + "  -  " +
                    RecordSourceLabel(rule.id, sources) + "  -  " +
                    (rule.enabled ? "ON" : "OFF");
                Copy(options[index].label, label);
            }
            *count = static_cast<std::uint32_t>(data.rules.size());
            return Result::Ok;
        } catch (...) {
            return Result::Rejected;
        }
    }
    if (id == "rule-trigger") labels = triggerLabels;
    else if (id == "rule-source") labels = sourceLabels;
    else if (id == "rule-target") labels = targetLabels;
    else return Result::NotFound;
    if (capacity < labels.size()) return Result::CapacityExceeded;
    for (std::size_t index = 0; index < labels.size(); ++index) {
        if (options[index].structSize < sizeof(ChoiceOptionV1)) {
            return Result::InvalidArgument;
        }
        options[index].value = static_cast<std::int64_t>(index);
        Copy(options[index].label, labels[index]);
    }
    *count = static_cast<std::uint32_t>(labels.size());
    return Result::Ok;
}

Result __cdecl InvokePreset(void*, const char* rawId) noexcept {
    if (!rawId) return Result::InvalidArgument;
    try {
        const auto* control = FindPresetControl(rawId);
        if (!control) return Result::NotFound;
        auto view = PowerRuntime::Get().ConfigurationSnapshot();
        std::string activate;
        {
            std::scoped_lock lock(g_presets.mutex);
            auto& current = g_presets.editing ? g_presets.draft : view.data;
            EnsureSelection(current);
            if (control->kind == PresetControl::Kind::Previous ||
                control->kind == PresetControl::Kind::Next) {
                if (current.presets.empty()) return Result::NotReady;
                const auto selected = std::ranges::find(
                    current.presets, g_presets.selectedPresetId,
                    &AbsolutePower::Preset::id);
                const auto position = selected == current.presets.end() ? 0U :
                    static_cast<std::size_t>(
                        std::distance(current.presets.begin(), selected));
                const auto next = control->kind == PresetControl::Kind::Previous ?
                    (position + current.presets.size() - 1) % current.presets.size() :
                    (position + 1) % current.presets.size();
                g_presets.selectedPresetId = current.presets[next].id;
            } else if (control->kind == PresetControl::Kind::PreviousOrderSystem ||
                       control->kind == PresetControl::Kind::NextOrderSystem) {
                const auto currentSystem = AbsolutePower::ToIndex(
                    g_presets.selectedOrderSystem);
                const auto next = control->kind ==
                                          PresetControl::Kind::PreviousOrderSystem
                                      ? (currentSystem + AbsolutePower::kSystemCount - 1) %
                                            AbsolutePower::kSystemCount
                                      : (currentSystem + 1) %
                                            AbsolutePower::kSystemCount;
                g_presets.selectedOrderSystem =
                    static_cast<AbsolutePower::SystemId>(next);
            } else if (control->kind == PresetControl::Kind::ActivateSelected ||
                       control->kind == PresetControl::Kind::SaveAndActivate) {
                if (g_presets.editing) return Result::Rejected;
                activate = g_presets.selectedPresetId;
            } else {
                BeginPresetDraft(view);
                auto* selected = FindSelected(g_presets.draft);
                if (!selected) return Result::NotReady;
                if (control->kind == PresetControl::Kind::Create ||
                    control->kind == PresetControl::Kind::Duplicate) {
                    if (g_presets.draft.presets.size() >= 256) {
                        return Result::CapacityExceeded;
                    }
                    const auto id = AbsolutePower::Configuration::AllocatePresetId(
                        g_presets.draft.presets, g_presets.inherited.presets);
                    if (id.empty()) return Result::CapacityExceeded;
                    AbsolutePower::Preset created =
                        control->kind == PresetControl::Kind::Duplicate ?
                            *selected : AbsolutePower::Preset{};
                    created.id = id;
                    created.displayName =
                        control->kind == PresetControl::Kind::Duplicate ?
                            DuplicateDisplayName(selected->displayName) :
                            "New Preset " + id;
                    g_presets.draft.presets.push_back(std::move(created));
                    g_presets.selectedPresetId = id;
                    g_presets.sources.push_back({
                        .recordId = id,
                        .baseKind = AbsolutePower::ConfigurationSourceKind::Custom,
                        .baseLabel = "User",
                        .userOverride = true,
                    });
                } else if (control->kind == PresetControl::Kind::DeleteOrHide) {
                    if (g_presets.draft.presets.size() <= 1) {
                        return Result::Rejected;
                    }
                    const auto removedId = selected->id;
                    const auto position = static_cast<std::size_t>(
                        selected - g_presets.draft.presets.data());
                    g_presets.draft.presets.erase(
                        g_presets.draft.presets.begin() + position);
                    std::erase_if(g_presets.draft.keyboardShortcuts,
                        [&](const auto& shortcut) {
                            return shortcut.presetId == removedId;
                        });
                    if (g_presets.draft.startupPreset == removedId) {
                        g_presets.draft.startupPreset =
                            g_presets.draft.presets.front().id;
                    }
                    const auto next = (std::min)(
                        position, g_presets.draft.presets.size() - 1);
                    g_presets.selectedPresetId =
                        g_presets.draft.presets[next].id;
                } else if (control->kind == PresetControl::Kind::RevertOverride) {
                    const auto source = std::ranges::find(
                        g_presets.sources, selected->id,
                        &AbsolutePower::ConfigurationRecordSource::recordId);
                    const auto inherited = std::ranges::find(
                        g_presets.inherited.presets, selected->id,
                        &AbsolutePower::Preset::id);
                    if (source == g_presets.sources.end() ||
                        !source->userOverride ||
                        source->baseKind ==
                            AbsolutePower::ConfigurationSourceKind::Custom ||
                        inherited == g_presets.inherited.presets.end()) {
                        return Result::NotFound;
                    }
                    *selected = *inherited;
                    source->userOverride = false;
                } else if (control->kind == PresetControl::Kind::SetStartup) {
                    g_presets.draft.startupPreset = selected->id;
                } else if (control->kind == PresetControl::Kind::MoveEarlier ||
                           control->kind == PresetControl::Kind::MoveLater) {
                    auto position = std::ranges::find(
                        selected->tieBreakOrder, g_presets.selectedOrderSystem);
                    if (position == selected->tieBreakOrder.end()) {
                        return Result::Rejected;
                    }
                    const auto index = static_cast<std::size_t>(
                        std::distance(selected->tieBreakOrder.begin(), position));
                    if (control->kind == PresetControl::Kind::MoveEarlier) {
                        if (index == 0) return Result::Rejected;
                        std::iter_swap(position, std::prev(position));
                    } else {
                        if (index + 1 >= selected->tieBreakOrder.size()) {
                            return Result::Rejected;
                        }
                        std::iter_swap(position, std::next(position));
                    }
                } else {
                    return Result::NotFound;
                }
            }
        }
        if (!activate.empty()) {
            const auto result = PowerRuntime::Get().InvokeCommand(
                "preset:" + activate);
            if (result == BackendResult::Ok) GameTaskScheduler::Request();
            return MapCommand(result);
        }
        PublishPowerFrame(true);
        if (const auto* api = g_hostApi.load(std::memory_order_acquire)) {
            (void)api->requestRefresh(kPowerModuleId.data(), "power-presets");
        }
        return Result::Ok;
    } catch (...) {
        return Result::Rejected;
    }
}

Result __cdecl ApplyPresets(void*) noexcept {
    try {
        std::uint64_t baseGeneration{};
        AbsolutePower::ConfigurationData draft;
        {
            std::scoped_lock lock(g_presets.mutex);
            if (!g_presets.editing) return Result::Ok;
            baseGeneration = g_presets.baseGeneration;
            draft = g_presets.draft;
        }
        const auto saved = PowerRuntime::Get().SaveConfiguration(
            baseGeneration, std::move(draft), false);
        if (saved.result != AbsolutePower::ConfigurationCommitResult::Ok) {
            RuntimePaths::Log(
                "ControlPanel",
                std::format(
                    "Preset draft apply rejected (result={}, base generation={}, current generation={}): {}",
                    static_cast<unsigned>(saved.result), baseGeneration,
                    saved.generation, saved.detail));
        }
        const auto result = saved.result == AbsolutePower::ConfigurationCommitResult::Ok
                                ? Result::Ok
                            : saved.result == AbsolutePower::ConfigurationCommitResult::WriteFailure
                                ? Result::WriteFailure
                                : Result::Rejected;
        if (result == Result::Ok) {
            std::scoped_lock lock(g_presets.mutex);
            g_presets.editing = false;
            g_presets.baseGeneration = 0;
            g_presets.opening = {};
            g_presets.inherited = {};
            g_presets.draft = {};
            g_presets.sources.clear();
        }
        PublishPowerFrame(true);
        return result;
    } catch (...) {
        return Result::Rejected;
    }
}

void __cdecl CancelPresets(void*) noexcept {
    {
        std::scoped_lock lock(g_presets.mutex);
        g_presets.editing = false;
        g_presets.baseGeneration = 0;
        g_presets.opening = {};
        g_presets.inherited = {};
        g_presets.draft = {};
        g_presets.sources.clear();
    }
    PublishPowerFrame(true);
}

std::uint8_t TierIndex(const AbsolutePower::TierPlan& plan,
                       std::uint32_t pip) noexcept {
    if (pip < plan.green) return 1;
    if (pip < static_cast<std::uint32_t>(plan.green) + plan.yellow) return 2;
    if (pip < static_cast<std::uint32_t>(plan.green) + plan.yellow + plan.red) {
        return 3;
    }
    return 0;
}

std::optional<std::size_t> SystemIndex(std::string_view id) noexcept {
    for (std::size_t index = 0; index < AbsolutePower::kSystemCount; ++index) {
        if (AbsolutePower::SystemKey(AbsolutePower::kCockpitOrder[index]) == id) {
            return index;
        }
    }
    return std::nullopt;
}

Live::LiveFrameV1 BuildPowerFrame() {
    Live::LiveFrameV1 frame;
    frame.kind = Live::ComponentKind::SegmentedAllocationGrid;
    frame.segmentedGrid.columnCount =
        static_cast<std::uint32_t>(AbsolutePower::kSystemCount);

    const auto view = PowerRuntime::Get().ConfigurationSnapshot();
    AbsolutePower::Preset preset;
    bool hasPreset{};
    {
        std::scoped_lock lock(g_presets.mutex);
        const auto& data = g_presets.editing ? g_presets.draft : view.data;
        auto found = std::ranges::find(data.presets, g_presets.selectedPresetId,
                                       &AbsolutePower::Preset::id);
        if (found == data.presets.end()) {
            found = std::ranges::find(data.presets, data.startupPreset,
                                      &AbsolutePower::Preset::id);
        }
        if (found == data.presets.end() && !data.presets.empty()) {
            found = data.presets.begin();
        }
        if (found != data.presets.end()) {
            preset = *found;
            g_presets.selectedPresetId = found->id;
            hasPreset = true;
        }
    }

    AbsolutePower::Snapshot snapshot{};
    const auto snapshotResult = PowerRuntime::Get().Capture(snapshot);
    AbsolutePower::Allocation allocation{};
    if (snapshotResult == BackendResult::Ok && hasPreset) {
        allocation = AbsolutePower::PowerAllocator::Allocate(snapshot, preset);
    } else {
        frame.flags |= Live::kFrameUnavailable;
    }
    for (std::size_t system = 0; system < AbsolutePower::kSystemCount; ++system) {
        auto& column = frame.segmentedGrid.columns[system];
        column.segmentCount = 32;
        column.currentCount = snapshotResult == BackendResult::Ok
                                  ? snapshot.systems[system].current : 0;
        column.maximumCount = snapshotResult == BackendResult::Ok
                                  ? snapshot.systems[system].maximum : 0;
        column.targetCount = allocation.status == AbsolutePower::AllocationStatus::Ok &&
                                     snapshotResult == BackendResult::Ok
                                 ? allocation.target[system] : 0;
        const auto plan = hasPreset ? preset.systems[system]
                                    : AbsolutePower::TierPlan{};
        for (std::uint32_t pip = 0; pip < column.segmentCount; ++pip) {
            column.segments[pip].tierIndex = TierIndex(plan, pip);
            column.segments[pip].live = pip < column.currentCount ? 1 : 0;
            column.segments[pip].preview = pip < column.targetCount ? 1 : 0;
            column.segments[pip].interactive = hasPreset ? 1 : 0;
        }
    }
    return frame;
}

void PublishPowerFrame(bool force) {
    std::unique_lock publishLock(g_framePublishMutex, std::defer_lock);
    if (force) publishLock.lock();
    else if (!publishLock.try_lock()) return;
    const auto now = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    auto previous = g_lastFramePublishUs.load(std::memory_order_acquire);
    if (!force && previous != 0 && now - previous < 100000) return;
    g_lastFramePublishUs.store(now, std::memory_order_release);
    g_powerFrames.Publish(BuildPowerFrame());
    if (const auto* live = g_liveHostApi.load(std::memory_order_acquire)) {
        (void)live->requestImmediateRefresh(
            kPowerModuleId.data(), "power-presets", "preset-grid");
    }
}

Live::Result __cdecl ReadPowerFrame(void*, Live::LiveFrameV1* output) noexcept {
    return g_powerFrames.Read(output);
}

Live::Result __cdecl ApplyPowerOperation(
    void*, const Live::CompoundOperationV1* operation,
    Live::CompoundSnapshotV1* replacement) noexcept {
    if (!operation || !replacement ||
        operation->structSize < sizeof(*operation) ||
        replacement->structSize < sizeof(*replacement)) {
        return Live::Result::InvalidArgument;
    }
    try {
        const auto system = SystemIndex(operation->columnId);
        if (!system || operation->kind == Live::CompoundOperationKind::SetTier) {
            return Live::Result::InvalidArgument;
        }
        const auto current = PowerRuntime::Get().ConfigurationSnapshot();
        {
            std::scoped_lock lock(g_presets.mutex);
            BeginPresetDraft(current);
            auto preset = std::ranges::find(
                g_presets.draft.presets, g_presets.selectedPresetId,
                &AbsolutePower::Preset::id);
            if (preset == g_presets.draft.presets.end()) {
                return Live::Result::NotFound;
            }
            auto& plan = preset->systems[*system];
            if (operation->kind == Live::CompoundOperationKind::TrimColumn) {
                auto total = static_cast<std::uint32_t>(plan.green) + plan.yellow +
                             plan.red;
                if (operation->count > total) return Live::Result::InvalidArgument;
                auto trim = total - operation->count;
                const auto red = (std::min)(trim, static_cast<std::uint32_t>(plan.red));
                plan.red = static_cast<std::uint16_t>(plan.red - red);
                trim -= red;
                const auto yellow =
                    (std::min)(trim, static_cast<std::uint32_t>(plan.yellow));
                plan.yellow = static_cast<std::uint16_t>(plan.yellow - yellow);
                trim -= yellow;
                plan.green = static_cast<std::uint16_t>(plan.green - trim);
            } else {
                std::uint16_t* tier{};
                const std::string_view tierId{operation->tierId};
                if (tierId == "green") tier = &plan.green;
                else if (tierId == "yellow") tier = &plan.yellow;
                else if (tierId == "red") tier = &plan.red;
                if (!tier) return Live::Result::InvalidArgument;
                const auto previous = *tier;
                *tier = static_cast<std::uint16_t>(operation->count);
                const auto total = static_cast<std::uint32_t>(plan.green) +
                                   plan.yellow + plan.red;
                if (total > 32) {
                    *tier = previous;
                    return Live::Result::InvalidArgument;
                }
            }
        }
        PublishPowerFrame(true);
        Live::LiveFrameV1 published;
        if (g_powerFrames.Read(&published) != Live::Result::Ok) {
            return Live::Result::NotReady;
        }
        *replacement = {};
        replacement->revision = published.sequence;
        replacement->segmentedGrid = published.segmentedGrid;
        return Live::Result::Ok;
    } catch (...) {
        return Live::Result::Rejected;
    }
}

Result __cdecl ReadAutomation(void*, const char* rawId, ValueV1* output) noexcept {
    if (!rawId || !output || output->structSize < sizeof(ValueV1)) {
        return Result::InvalidArgument;
    }
    try {
        const auto* control = FindAutomationControl(rawId);
        if (!control) return Result::NotFound;
        const auto view = PowerRuntime::Get().ConfigurationSnapshot();
        const auto runtimeEnabled = PowerRuntime::Get().AutomationEnabled();
        const auto automationStatus = PowerRuntime::Get().AutomationSnapshot();
        std::scoped_lock lock(g_automation.mutex);
        auto& data = g_automation.editing ? g_automation.draft : view.data;
        EnsureAutomationSelection(data);
        const auto& sources = g_automation.editing ? g_automation.sources : view.ruleSources;
        const auto* rule = FindSelectedRule(data);
        using Kind = AutomationPageState::Control::Kind;
        switch (control->kind) {
        case Kind::Warning:
            *output = String("COMING SOON | Automation configuration is withheld from the early Absolute Control release.");
            break;
        case Kind::EventSourceStatus:
            *output = String("NOT RELEASE-QUALIFIED | Cross-weapon identity, demand settlement, and the final On-Demand Power policy remain under redesign.");
            break;
        case Kind::GlobalSummary:
            {
            const auto activationState = !data.automationEnabled
                ? std::string_view{"BLOCKED: turn on the global gate"}
                : !rule
                    ? std::string_view{"BLOCKED: select a rule"}
                    : !rule->enabled
                        ? std::string_view{"BLOCKED: turn on the selected-rule gate"}
                        : g_automation.editing
                            ? std::string_view{"PENDING: choose Apply to arm these changes"}
                            : !runtimeEnabled
                                ? std::string_view{"BLOCKED: live engine is off"}
                                : std::string_view{"ARMED: selected rule can run"};
            *output = String(std::format("{} | Global {} | Selected rule {} | live engine {} | {} demand{} | {}",
                activationState,
                data.automationEnabled ? "ON" : "OFF",
                rule && rule->enabled ? "ON" : "OFF",
                runtimeEnabled ? "ON" : "OFF",
                automationStatus.activeDemandCount,
                automationStatus.activeDemandCount == 1 ? "" : "s",
                automationStatus.restoringBasePreset
                    ? "restoring base preset"
                    : automationStatus.settlementActive ? "settling" : "idle"));
            break;
            }
        case Kind::GlobalEnabled:
            *output = Boolean(data.automationEnabled);
            break;
        case Kind::RuleSelector: {
            const auto selected = std::ranges::find(
                data.rules, g_automation.selectedRuleId,
                &AbsolutePower::AutomationRule::id);
            if (selected == data.rules.end()) return Result::NotReady;
            *output = Integer(static_cast<std::int64_t>(
                std::distance(data.rules.begin(), selected)));
            break;
        }
        case Kind::RuleSummary:
            *output = String(RuleRecordSummary(data, sources));
            break;
        case Kind::RulePlainSummary:
            if (!rule) return Result::NotReady;
            *output = String(RulePlainSummary(*rule));
            break;
        case Kind::Name:
            if (!rule) return Result::NotReady;
            *output = String(rule->displayName);
            break;
        case Kind::Enabled:
            if (!rule) return Result::NotReady;
            *output = Boolean(rule->enabled);
            break;
        case Kind::Trigger:
            if (!rule) return Result::NotReady;
            *output = Integer(static_cast<std::uint8_t>(rule->trigger));
            break;
        case Kind::Source:
            if (!rule) return Result::NotReady;
            *output = Integer(rule->sourceSystem == AbsolutePower::SystemId::Invalid ? 3 :
                static_cast<std::int64_t>(AbsolutePower::ToIndex(rule->sourceSystem)));
            break;
        case Kind::Target:
            if (!rule) return Result::NotReady;
            *output = Integer(AbsolutePower::ToIndex(rule->targetSystem));
            break;
        case Kind::TargetPips:
            if (!rule) return Result::NotReady;
            *output = Integer(rule->targetPips ==
                std::numeric_limits<std::uint16_t>::max() ? 33 : rule->targetPips);
            break;
        case Kind::Threshold:
            if (!rule) return Result::NotReady;
            *output = Integer(rule->thresholdPercent);
            break;
        case Kind::Hysteresis:
            if (!rule) return Result::NotReady;
            *output = Integer(rule->hysteresisPercent);
            break;
        case Kind::HoldMilliseconds:
            if (!rule) return Result::NotReady;
            *output = Integer(rule->holdMilliseconds);
            break;
        case Kind::Priority:
            if (!rule) return Result::NotReady;
            *output = Integer(rule->priority);
            break;
        default:
            return Result::NotFound;
        }
        return Result::Ok;
    } catch (...) {
        return Result::Rejected;
    }
}

Result __cdecl WriteAutomation(void*, const char* rawId,
                               const ValueV1* value) noexcept {
    if (!rawId || !value || value->structSize < sizeof(ValueV1)) {
        return Result::InvalidArgument;
    }
    try {
        const auto* control = FindAutomationControl(rawId);
        if (!control) return Result::NotFound;
        const auto current = PowerRuntime::Get().ConfigurationSnapshot();
        std::scoped_lock lock(g_automation.mutex);
        using Kind = AutomationPageState::Control::Kind;
        if (control->kind == Kind::RuleSelector) {
            if (value->kind != ValueKind::Integer || value->integerValue < 0) {
                return Result::InvalidArgument;
            }
            auto effective = current.data;
            auto& data = g_automation.editing ? g_automation.draft : effective;
            EnsureAutomationSelection(data);
            const auto index = static_cast<std::size_t>(value->integerValue);
            if (index >= data.rules.size()) return Result::InvalidArgument;
            g_automation.selectedRuleId = data.rules[index].id;
            return Result::Ok;
        }
        BeginAutomationDraft(current);
        if (control->kind == Kind::GlobalEnabled) {
            if (value->kind != ValueKind::Boolean || value->booleanValue > 1) {
                return Result::InvalidArgument;
            }
            g_automation.draft.automationEnabled = value->booleanValue != 0;
            return Result::Ok;
        }
        auto* rule = FindSelectedRule(g_automation.draft);
        if (!rule) return Result::NotReady;
        if (control->kind == Kind::Name) {
            if (value->kind != ValueKind::String ||
                !std::memchr(value->stringValue, '\0', kStringValueCapacity)) {
                return Result::InvalidArgument;
            }
            const std::string_view name{value->stringValue};
            if (name.empty() || name.size() >= 96 ||
                name.contains('\r') || name.contains('\n')) {
                return Result::InvalidArgument;
            }
            rule->displayName = name;
            return Result::Ok;
        }
        if (control->kind == Kind::Enabled) {
            if (value->kind != ValueKind::Boolean || value->booleanValue > 1) {
                return Result::InvalidArgument;
            }
            rule->enabled = value->booleanValue != 0;
            return Result::Ok;
        }
        if (value->kind != ValueKind::Integer) return Result::InvalidArgument;
        const auto integer = value->integerValue;
        switch (control->kind) {
        case Kind::Trigger:
            if (integer < 0 || integer > 3) return Result::InvalidArgument;
            rule->trigger = static_cast<AbsolutePower::TriggerKind>(integer);
            break;
        case Kind::Source:
            if (integer < 0 || integer > 3) return Result::InvalidArgument;
            rule->sourceSystem = integer == 3 ? AbsolutePower::SystemId::Invalid :
                static_cast<AbsolutePower::SystemId>(integer);
            break;
        case Kind::Target:
            if (integer < 0 || integer >=
                static_cast<std::int64_t>(AbsolutePower::kSystemCount)) {
                return Result::InvalidArgument;
            }
            rule->targetSystem = static_cast<AbsolutePower::SystemId>(integer);
            break;
        case Kind::TargetPips:
            if (integer < 0 || integer > 33) return Result::InvalidArgument;
            rule->targetPips = integer == 33 ?
                std::numeric_limits<std::uint16_t>::max() :
                static_cast<std::uint16_t>(integer);
            break;
        case Kind::Threshold:
            if (integer < 0 || integer > 100) return Result::InvalidArgument;
            rule->thresholdPercent = static_cast<std::uint8_t>(integer);
            break;
        case Kind::Hysteresis:
            if (integer < 0 || integer > 100) return Result::InvalidArgument;
            rule->hysteresisPercent = static_cast<std::uint8_t>(integer);
            break;
        case Kind::HoldMilliseconds:
            if (integer < 0 || integer > 600000) return Result::InvalidArgument;
            rule->holdMilliseconds = static_cast<std::uint32_t>(integer);
            break;
        case Kind::Priority:
            if (integer < 0 || integer > 65535) return Result::InvalidArgument;
            rule->priority = static_cast<std::uint16_t>(integer);
            break;
        default:
            return Result::NotFound;
        }
        return Result::Ok;
    } catch (...) {
        return Result::Rejected;
    }
}

Result __cdecl InvokeAutomation(void*, const char* rawId) noexcept {
    if (!rawId) return Result::InvalidArgument;
    try {
        const auto* control = FindAutomationControl(rawId);
        if (!control) return Result::NotFound;
        const auto view = PowerRuntime::Get().ConfigurationSnapshot();
        using Kind = AutomationPageState::Control::Kind;
        if (control->kind == Kind::DisableAllNow) {
            auto desired = view.data;
            desired.automationEnabled = false;
            const auto saved = PowerRuntime::Get().SaveConfiguration(
                view.generation, std::move(desired));
            if (saved.result != AbsolutePower::ConfigurationCommitResult::Ok) {
                return saved.result == AbsolutePower::ConfigurationCommitResult::WriteFailure ?
                    Result::WriteFailure : Result::Rejected;
            }
            RuntimePaths::Log("Automation",
                "Absolute Control immediately persisted automation as disabled and cleared runtime latches.");
        } else {
            std::scoped_lock lock(g_automation.mutex);
            auto effective = view.data;
            auto& current = g_automation.editing ? g_automation.draft : effective;
            EnsureAutomationSelection(current);
            if (control->kind == Kind::Previous || control->kind == Kind::Next) {
                if (current.rules.empty()) return Result::NotReady;
                const auto selected = std::ranges::find(
                    current.rules, g_automation.selectedRuleId,
                    &AbsolutePower::AutomationRule::id);
                const auto position = selected == current.rules.end() ? 0U :
                    static_cast<std::size_t>(std::distance(current.rules.begin(), selected));
                const auto next = control->kind == Kind::Previous ?
                    (position + current.rules.size() - 1) % current.rules.size() :
                    (position + 1) % current.rules.size();
                g_automation.selectedRuleId = current.rules[next].id;
            } else {
                BeginAutomationDraft(view);
                auto* selected = FindSelectedRule(g_automation.draft);
                if (control->kind == Kind::Create || control->kind == Kind::Duplicate) {
                    if (g_automation.draft.rules.size() >= 256) {
                        return Result::CapacityExceeded;
                    }
                    if (control->kind == Kind::Duplicate && !selected) {
                        return Result::NotReady;
                    }
                    const auto id = AbsolutePower::Configuration::AllocateRuleId(
                        g_automation.draft.rules, g_automation.inherited.rules);
                    if (id.empty()) return Result::CapacityExceeded;
                    AbsolutePower::AutomationRule created;
                    if (control->kind == Kind::Duplicate) created = *selected;
                    created.id = id;
                    created.displayName = control->kind == Kind::Duplicate ?
                        DuplicateDisplayName(selected->displayName) :
                        "New automation rule " + id;
                    if (control->kind == Kind::Create) {
                        created.trigger = AbsolutePower::TriggerKind::Manual;
                        created.sourceSystem = AbsolutePower::SystemId::Invalid;
                        created.targetSystem = AbsolutePower::SystemId::Shield;
                        created.targetPips = std::numeric_limits<std::uint16_t>::max();
                        created.priority = 100;
                    }
                    g_automation.draft.rules.push_back(std::move(created));
                    g_automation.selectedRuleId = id;
                    g_automation.sources.push_back({
                        .recordId = id,
                        .baseKind = AbsolutePower::ConfigurationSourceKind::Custom,
                        .baseLabel = "User",
                        .userOverride = true,
                    });
                } else if (control->kind == Kind::DeleteOrHide) {
                    if (!selected) return Result::NotReady;
                    const auto position = static_cast<std::size_t>(
                        selected - g_automation.draft.rules.data());
                    g_automation.draft.rules.erase(
                        g_automation.draft.rules.begin() + position);
                    if (g_automation.draft.rules.empty()) {
                        g_automation.selectedRuleId.clear();
                    } else {
                        const auto next = (std::min)(
                            position, g_automation.draft.rules.size() - 1);
                        g_automation.selectedRuleId = g_automation.draft.rules[next].id;
                    }
                } else if (control->kind == Kind::RevertOverride) {
                    if (!selected) return Result::NotReady;
                    const auto source = std::ranges::find(
                        g_automation.sources, selected->id,
                        &AbsolutePower::ConfigurationRecordSource::recordId);
                    const auto inherited = std::ranges::find(
                        g_automation.inherited.rules, selected->id,
                        &AbsolutePower::AutomationRule::id);
                    if (source == g_automation.sources.end() || !source->userOverride ||
                        source->baseKind == AbsolutePower::ConfigurationSourceKind::Custom ||
                        inherited == g_automation.inherited.rules.end()) {
                        return Result::NotFound;
                    }
                    *selected = *inherited;
                    source->userOverride = false;
                } else {
                    return Result::NotFound;
                }
            }
        }
        if (const auto* api = g_hostApi.load(std::memory_order_acquire)) {
            (void)api->requestRefresh(kPowerModuleId.data(), "power-automation");
        }
        return Result::Ok;
    } catch (...) {
        return Result::Rejected;
    }
}

Result __cdecl ApplyAutomation(void*) noexcept {
    try {
        std::uint64_t baseGeneration{};
        AbsolutePower::ConfigurationData draft;
        {
            std::scoped_lock lock(g_automation.mutex);
            if (!g_automation.editing) return Result::Ok;
            baseGeneration = g_automation.baseGeneration;
            draft = g_automation.draft;
        }
        const auto saved = PowerRuntime::Get().SaveConfiguration(
            baseGeneration, std::move(draft));
        if (saved.result != AbsolutePower::ConfigurationCommitResult::Ok) {
            return saved.result == AbsolutePower::ConfigurationCommitResult::WriteFailure
                       ? Result::WriteFailure
                       : Result::Rejected;
        }
        {
            std::scoped_lock lock(g_automation.mutex);
            g_automation.editing = false;
            g_automation.baseGeneration = 0;
            g_automation.opening = {};
            g_automation.inherited = {};
            g_automation.draft = {};
            g_automation.sources.clear();
        }
        RuntimePaths::Log(
            "Automation",
            "Absolute Control saved the automation rule library and global permission.");
        return Result::Ok;
    } catch (...) {
        return Result::Rejected;
    }
}

void __cdecl CancelAutomation(void*) noexcept {
    std::scoped_lock lock(g_automation.mutex);
    g_automation.editing = false;
    g_automation.baseGeneration = 0;
    g_automation.opening = {};
    g_automation.inherited = {};
    g_automation.draft = {};
    g_automation.sources.clear();
}

Result __cdecl ReadDiagnostics(void*, const char* rawId, ValueV1* output) noexcept {
    if (!rawId || !output || output->structSize < sizeof(ValueV1)) {
        return Result::InvalidArgument;
    }
    try {
        const std::string_view id{rawId};
        const auto state = PowerRuntime::Get().State();
        AbsolutePower::Snapshot snapshot{};
        const auto snapshotResult = PowerRuntime::Get().Capture(snapshot);
        const auto configuration = PowerRuntime::Get().ConfigurationSnapshot();
        const auto activation = PowerRuntime::Get().ActivationSnapshot();
        const auto automationStatus = PowerRuntime::Get().AutomationSnapshot();
        if (id == "compatibility-summary") {
            *output = String(std::format(
                "Absolute Power {} | ABI 1 | exact Starfield 1.16.244 gate | {}",
                Plugin::VersionString, RuntimeStateLabel(state)));
        } else if (id == "executor-summary") {
            *output = String(std::format("Executor {} | snapshot {} | activation sequence {}",
                RuntimeStateLabel(state), BackendResultLabel(snapshotResult),
                activation.sequence));
        } else if (id == "live-ship-summary") {
            *output = String(snapshotResult == BackendResult::Ok ?
                std::format("Pilot ready | reactor {} total | {} currently unassigned",
                    snapshot.totalPower, snapshot.available) :
                std::format("Live ship unavailable: {}", BackendResultLabel(snapshotResult)));
        } else if (id.starts_with("system-")) {
            const auto key = id.substr(std::string_view{"system-"}.size());
            std::optional<std::size_t> system;
            for (std::size_t index{}; index < AbsolutePower::kSystemCount; ++index) {
                if (AbsolutePower::SystemKey(static_cast<AbsolutePower::SystemId>(index)) == key) {
                    system = index;
                    break;
                }
            }
            if (!system) return Result::NotFound;
            const auto& value = snapshot.systems[*system];
            *output = String(snapshotResult != BackendResult::Ok ? "Snapshot unavailable" :
                !value.present ? "Not installed" :
                std::format("{} / {} pips", value.current, value.maximum));
        } else if (id == "configuration-summary") {
            *output = String(std::format(
                "Generation {} | {} presets | {} rules | automation {}",
                configuration.generation, configuration.data.presets.size(),
                configuration.data.rules.size(),
                configuration.data.automationEnabled ? "ON" : "OFF"));
        } else if (id == "automation-runtime-summary") {
            const auto totalWeaponEvents =
                automationStatus.weaponEventCounts[0] +
                automationStatus.weaponEventCounts[1] +
                automationStatus.weaponEventCounts[2];
            *output = String(std::format(
                "Weapon listener {} | HOTAS bridge {} | {} demand{} | settlement {} | {} events",
                automationStatus.nativeWeaponInputReady ? "READY" : "OFF",
                automationStatus.hotasWeaponBridgeSeen ? "OBSERVED" : "not observed",
                automationStatus.activeDemandCount,
                automationStatus.activeDemandCount == 1 ? "" : "s",
                automationStatus.restoringBasePreset
                    ? "RESTORING"
                    : automationStatus.settlementActive ? "ACTIVE" : "IDLE",
                totalWeaponEvents));
        } else if (id == "defaults-path") {
            *output = String(RuntimePaths::DefaultsPath().string());
        } else if (id == "imports-path") {
            *output = String(RuntimePaths::ImportsDirectory().string());
        } else if (id == "custom-path") {
            *output = String(RuntimePaths::CustomPath().string());
        } else if (id == "log-path") {
            *output = String(RuntimePaths::LogPath().string());
        } else if (id == "activation-summary") {
            *output = String(std::format("{} | result {} | {}/{} changes complete",
                ActivationSummary(activation), BackendResultLabel(activation.backend),
                activation.completedChanges, activation.totalChanges));
        } else if (id == "frontends-summary") {
            *output = String(std::format(
                "Absolute Control {} | {} | keyboard shortcuts Power-owned",
                g_hostApi.load(std::memory_order_acquire) ? "connected" : "absent",
                HostSelectionLabel(SuiteHost::Select())));
        } else if (id == "support-summary") {
            *output = String(std::format(
                "Power {} | {} | snapshot {} | config gen {} | activation {}",
                Plugin::VersionString, RuntimeStateLabel(state),
                BackendResultLabel(snapshotResult), configuration.generation,
                BackendResultLabel(activation.backend)));
        } else return Result::NotFound;
        return Result::Ok;
    } catch (...) {
        return Result::Rejected;
    }
}

const ModuleDescriptorV1 g_module = [] {
    ModuleDescriptorV1 module;
    Copy(module.moduleId, kPowerModuleId);
    Copy(module.displayName, "Absolute Power");
    Copy(module.description,
         "Priority ship-power presets, bindings, automation, and runtime status.");
    return module;
}();

const std::array g_diagnosticControls{
    Control(ControlKind::InputBinding, kControlReadOnly, "compatibility-summary",
            "Compatibility", "Power version, public ABI, exact runtime gate, and runtime state."),
    Control(ControlKind::InputBinding, kControlReadOnly, "executor-summary",
            "Executor", "Exact-gated executor, snapshot result, and activation sequence."),
    Control(ControlKind::InputBinding, kControlReadOnly, "live-ship-summary",
            "Live ship", "Pilot context, reactor output, and unassigned live power."),
    Control(ControlKind::InputBinding, kControlReadOnly, "system-Weapon0", "Weapon 0", "Current and installed maximum pips."),
    Control(ControlKind::InputBinding, kControlReadOnly, "system-Weapon1", "Weapon 1", "Current and installed maximum pips."),
    Control(ControlKind::InputBinding, kControlReadOnly, "system-Weapon2", "Weapon 2", "Current and installed maximum pips."),
    Control(ControlKind::InputBinding, kControlReadOnly, "system-Engine", "Engines", "Current and installed maximum pips."),
    Control(ControlKind::InputBinding, kControlReadOnly, "system-Shield", "Shields", "Current and installed maximum pips."),
    Control(ControlKind::InputBinding, kControlReadOnly, "system-GravDrive", "Grav drive", "Current and installed maximum pips."),
    Control(ControlKind::InputBinding, kControlReadOnly, "configuration-summary",
            "Configuration", "Generation, effective record counts, and saved automation permission."),
    Control(ControlKind::InputBinding, kControlReadOnly, "automation-runtime-summary",
            "Automation runtime", "Weapon-source readiness, demand settlement, restoration, and observed events."),
    Control(ControlKind::InputBinding, kControlReadOnly, "activation-summary",
            "Activation", "Requested/active state, backend result, and settlement progress."),
    Control(ControlKind::InputBinding, kControlReadOnly, "frontends-summary",
            "Frontends", "Absolute Control connection, optional executor host, and keyboard ownership."),
    Control(ControlKind::InputBinding, kControlReadOnly, "support-summary",
            "Support summary", "Compact copy-friendly version, runtime, snapshot, config, and activation state."),
    Control(ControlKind::InputBinding, kControlReadOnly, "defaults-path",
            "Defaults path", "Power-owned shipped configuration path."),
    Control(ControlKind::InputBinding, kControlReadOnly, "imports-path",
            "Imports path", "Power-owned filename-ordered import-pack directory."),
    Control(ControlKind::InputBinding, kControlReadOnly, "custom-path",
            "Custom path", "Power-owned sparse user overlay path."),
    Control(ControlKind::InputBinding, kControlReadOnly, "log-path",
            "Log path", "Power runtime log path."),
};

PageDescriptorV1 Page(std::string_view id, std::string_view label,
                      std::string_view description,
                      const ControlDescriptorV1* controls, std::uint32_t count) {
    PageDescriptorV1 page;
    Copy(page.moduleId, kPowerModuleId);
    Copy(page.pageId, id);
    Copy(page.displayName, label);
    Copy(page.description, description);
    page.controlCount = count;
    page.controls = controls;
    return page;
}

std::array<PageDescriptorV1, 3> BuildPages(bool labeledChoices) {
    g_presets.records.clear();
    g_presets.controls.clear();
    g_presets.records.reserve(36);
    g_presets.controls.reserve(36);

    const auto add = [](PresetControl record, ControlDescriptorV1 control) {
        g_presets.records.push_back(std::move(record));
        g_presets.controls.push_back(std::move(control));
    };
    const auto addAction = [&](std::string_view id, PresetControl::Kind kind,
                               std::string_view label,
                               std::string_view description,
                               bool mutatesDraft = false) {
        add({std::string(id), kind},
            Control(ControlKind::Action,
                    mutatesDraft ? kControlMutatesDraft : kControlNone,
                    id, label, description));
    };

    {
        const auto view = PowerRuntime::Get().ConfigurationSnapshot();
        std::scoped_lock lock(g_presets.mutex);
        auto effective = view.data;
        EnsureSelection(effective);
    }

    if (labeledChoices) {
        auto selector = Control(ControlKind::Choice, kControlTransientChoice,
            "preset-selector", "Power profile",
            "The authoritative profile selection. Its menu also identifies the profile source and startup profile without creating a pending transaction.");
        selector.minimumValue = 0.0;
        selector.maximumValue = 255.0;
        selector.stepValue = 1.0;
        add({"preset-selector", PresetControl::Kind::Selector}, std::move(selector));
    }
    {
        auto name = Control(ControlKind::TextInput, kControlNone,
            "preset-name", "Rename selected profile",
            "Change the selected profile's display label. This edits the current profile; it does not select a second profile.");
        name.minimumValue = 0.0;
        name.maximumValue = 95.0;
        name.stepValue = 1.0;
        add({"preset-name", PresetControl::Kind::Name}, std::move(name));
    }
    if (!labeledChoices) {
        addAction("preset-previous", PresetControl::Kind::Previous,
                  "Previous preset",
                  "Select the previous effective preset without changing the transaction.");
        addAction("preset-next", PresetControl::Kind::Next,
                  "Next preset",
                  "Select the next effective preset without changing the transaction.");
    }
    addAction("preset-create", PresetControl::Kind::Create,
              "Create preset",
              "Create a new user-owned preset with a backend-allocated stable identifier.", true);
    addAction("preset-duplicate", PresetControl::Kind::Duplicate,
              "Duplicate preset",
              "Create a user-owned copy of the selected effective preset.", true);
    addAction("preset-delete", PresetControl::Kind::DeleteOrHide,
              "Delete / hide preset",
              "Delete a user preset or hide an inherited preset from the effective configuration.", true);
    addAction("preset-revert", PresetControl::Kind::RevertOverride,
              "Revert user override",
              "Restore the selected inherited preset from its defaults or import-pack source.", true);
    addAction("preset-startup", PresetControl::Kind::SetStartup,
              "Use at startup",
              "Make the selected preset Absolute Power's startup preset.", true);

    add({"preset-binding", PresetControl::Kind::Binding},
        Control(ControlKind::InputBinding, kKeyboardChord,
                "preset-binding", "Selected preset shortcut",
                "Record a Power-owned keyboard key or Ctrl, Alt, and Shift chord."));
    add({"activation-status", PresetControl::Kind::ActivationStatus},
        Control(ControlKind::InputBinding, kControlReadOnly,
                "activation-status", "Activation status",
                "Live provider-owned queue, readiness, settlement, and convergence status."));
    add({"preview-summary", PresetControl::Kind::PreviewSummary},
        Control(ControlKind::InputBinding, kControlReadOnly,
                "preview-summary", "Allocation preview",
                "Allocator-backed assigned, unassigned, and clipped pip totals for the draft."));
    addAction("activate-selected", PresetControl::Kind::ActivateSelected,
              "Activate saved preset",
              "Queue the last saved version for native settlement. Apply pending changes first.");
    add({"save-activate-selected", PresetControl::Kind::SaveAndActivate},
        Control(ControlKind::Action, kControlAppliesDraftBeforeInvoke,
                "save-activate-selected", "Save & activate",
                "Atomically save pending preset changes, then queue that saved preset for native settlement."));

    add({"order-summary", PresetControl::Kind::OrderSummary},
        Control(ControlKind::InputBinding, kControlReadOnly,
                "order-summary", "Within-tier tie break",
                "Earlier systems win only when requests compete inside the same priority tier."));
    addAction("order-system-previous", PresetControl::Kind::PreviousOrderSystem,
              "Previous tie-break system",
              "Move the tie-break focus to the previous cockpit system.");
    addAction("order-system-next", PresetControl::Kind::NextOrderSystem,
              "Next tie-break system",
              "Move the tie-break focus to the next cockpit system.");
    addAction("order-move-earlier", PresetControl::Kind::MoveEarlier,
              "Move focused system earlier",
              "Give the focused system an earlier tie-break position inside each tier.", true);
    addAction("order-move-later", PresetControl::Kind::MoveLater,
              "Move focused system later",
              "Give the focused system a later tie-break position inside each tier.", true);

    constexpr std::array tiers{
        std::pair{AbsolutePower::PriorityTier::Green, std::string_view{"green"}},
        std::pair{AbsolutePower::PriorityTier::Yellow, std::string_view{"yellow"}},
        std::pair{AbsolutePower::PriorityTier::Red, std::string_view{"red"}},
    };
    for (std::size_t system = 0; system < AbsolutePower::kSystemCount; ++system) {
        const auto systemId = AbsolutePower::SystemKey(
            static_cast<AbsolutePower::SystemId>(system));
        const auto systemLabel = AbsolutePower::SystemLabel(
            static_cast<AbsolutePower::SystemId>(system));
        for (const auto& [tier, tierId] : tiers) {
            const auto controlId = std::format("tier-{}-{}", systemId, tierId);
            auto control = Control(
                ControlKind::IntegerSlider, kControlAdvanced, controlId,
                std::format("{} {} pips", systemLabel, tierId),
                "Set the exact priority-tier pip count. A system may request at most 32 total pips.");
            control.minimumValue = 0.0;
            control.maximumValue = 32.0;
            control.stepValue = 1.0;
            add({controlId, PresetControl::Kind::TierValue, system, tier},
                std::move(control));
        }
    }

    g_automation.records.clear();
    g_automation.controls.clear();
    g_automation.records.reserve(23);
    g_automation.controls.reserve(23);
    using AutomationKind = AutomationPageState::Control::Kind;
    const auto addAutomation = [](std::string_view id, AutomationKind kind,
                                  ControlDescriptorV1 control) {
        g_automation.records.push_back({std::string(id), kind});
        g_automation.controls.push_back(std::move(control));
    };
    const auto automationAction = [&](std::string_view id, AutomationKind kind,
                                      std::string_view label,
                                      std::string_view description,
                                      std::uint32_t flags = kControlNone) {
        addAutomation(id, kind,
            Control(ControlKind::Action, flags, id, label, description));
    };
    const auto automationInteger = [&](std::string_view id, AutomationKind kind,
                                       ControlKind controlKind,
                                       std::string_view label,
                                       std::string_view description,
                                       double minimum, double maximum, double step) {
        auto control = Control(controlKind, kControlAdvanced, id, label, description);
        control.minimumValue = minimum;
        control.maximumValue = maximum;
        control.stepValue = step;
        addAutomation(id, kind, std::move(control));
    };
    addAutomation("automation-warning", AutomationKind::Warning,
        Control(ControlKind::InputBinding, kControlReadOnly,
            "automation-warning", "Changes game balance",
            "Automatic reassignment is optional and disabled by default."));
    addAutomation("automation-event-sources", AutomationKind::EventSourceStatus,
        Control(ControlKind::InputBinding, kControlReadOnly,
            "automation-event-sources", "Release readiness",
            "The experimental backend remains disabled by default and is not part of the early supported menu surface."));
    automationAction("automation-disable-now", AutomationKind::DisableAllNow,
        "Disable all automation now",
        "Atomically persist the global permission as OFF and clear every runtime latch.");
    addAutomation("automation-summary", AutomationKind::GlobalSummary,
        Control(ControlKind::InputBinding, kControlReadOnly,
            "automation-summary", "Activation checklist",
            "Shows exactly which gate blocks the selected rule and whether Apply is required."));
    addAutomation("automation-enabled", AutomationKind::GlobalEnabled,
        Control(ControlKind::Toggle, kControlNone, "automation-enabled",
            "1. Global automation gate",
            "Required for every rule. Turn this ON, enable the selected rule below, then choose Apply."));
    if (labeledChoices) {
        auto selector = Control(ControlKind::Choice, kControlTransientChoice,
            "rule-selector", "Automation rule",
            "The authoritative rule selection. Options identify each rule's source and current enabled state without creating a pending transaction.");
        selector.minimumValue = 0.0;
        selector.maximumValue = 255.0;
        selector.stepValue = 1.0;
        addAutomation("rule-selector", AutomationKind::RuleSelector,
            std::move(selector));
    } else {
        addAutomation("rule-summary", AutomationKind::RuleSummary,
            Control(ControlKind::InputBinding, kControlReadOnly,
                "rule-summary", "Selected rule",
                "Provider-owned record position, display name, and source provenance."));
    }
    addAutomation("rule-plain-summary", AutomationKind::RulePlainSummary,
        Control(ControlKind::InputBinding, kControlReadOnly,
            "rule-plain-summary", "Rule meaning",
            "Plain-language summary of the selected rule and its precedence."));
    addAutomation("rule-enabled", AutomationKind::Enabled,
        Control(ControlKind::Toggle, kControlNone, "rule-enabled",
            "2. Selected rule gate",
            "Required for this rule. Turn this ON with the global gate above, then choose Apply."));
    {
        auto name = Control(ControlKind::TextInput, kControlNone,
            "rule-name", "Rename selected rule",
            "Change the selected rule's display label without changing its stable backend ID or selecting another rule.");
        name.minimumValue = 0.0;
        name.maximumValue = 95.0;
        name.stepValue = 1.0;
        addAutomation("rule-name", AutomationKind::Name, std::move(name));
    }
    if (!labeledChoices) {
        automationAction("rule-previous", AutomationKind::Previous,
            "Previous rule", "Select the previous effective automation rule.");
        automationAction("rule-next", AutomationKind::Next,
            "Next rule", "Select the next effective automation rule.");
    }
    automationAction("rule-create", AutomationKind::Create,
        "Create rule", "Create a disabled user-owned Manual rule with a backend-allocated stable ID.",
        kControlMutatesDraft);
    automationAction("rule-duplicate", AutomationKind::Duplicate,
        "Duplicate rule", "Create a user-owned copy of the selected rule.",
        kControlMutatesDraft);
    automationAction("rule-delete", AutomationKind::DeleteOrHide,
        "Delete / hide rule", "Delete a user rule or hide an inherited rule from the effective configuration.",
        kControlMutatesDraft);
    automationAction("rule-revert", AutomationKind::RevertOverride,
        "Revert user override", "Restore the selected inherited rule from its defaults or import-pack source.",
        kControlMutatesDraft);
    automationInteger("rule-trigger", AutomationKind::Trigger, ControlKind::Choice,
        "Trigger (0 fired, 1 damage, 2 throttle, 3 manual)",
        "Select the rule trigger; the plain-language summary resolves the numeric code.",
        0, 3, 1);
    automationInteger("rule-source", AutomationKind::Source, ControlKind::Choice,
        "Weapon source (0 W0, 1 W1, 2 W2, 3 any)",
        "Used by Weapon Fired rules; other trigger kinds ignore this field.",
        0, 3, 1);
    automationInteger("rule-target", AutomationKind::Target, ControlKind::Choice,
        "Target (0 W0, 1 W1, 2 W2, 3 ENG, 4 SHD, 5 GRV)",
        "Ship system that receives the emergency demand.", 0, 5, 1);
    automationInteger("rule-target-pips", AutomationKind::TargetPips,
        ControlKind::IntegerSlider, "Target pips (33 = installed maximum)",
        "Demand an exact pip count or use 33 as the portable installed-system maximum.",
        0, 33, 1);
    automationInteger("rule-threshold", AutomationKind::Threshold,
        ControlKind::IntegerSlider, "Throttle threshold percent",
        "Used only by Throttle Above rules.", 0, 100, 1);
    automationInteger("rule-hysteresis", AutomationKind::Hysteresis,
        ControlKind::IntegerSlider, "Throttle hysteresis percent",
        "Prevents repeated throttle activation near the threshold.", 0, 100, 1);
    automationInteger("rule-hold-ms", AutomationKind::HoldMilliseconds,
        ControlKind::IntegerSlider, "Hold duration milliseconds",
        "Duration for event-triggered demands; Throttle and Manual use their live level.",
        0, 600000, 100);
    automationInteger("rule-priority", AutomationKind::Priority,
        ControlKind::IntegerSlider, "Emergency priority",
        "Larger values win when enabled automation demands compete before the Green tier.",
        0, 65535, 10);

    auto presetsPage = Page(
        "power-presets", "Presets",
        "Edit tiered allocations, activate saved presets, and assign Power-owned shortcuts.",
        g_presets.controls.data(),
        static_cast<std::uint32_t>(g_presets.controls.size()));
    presetsPage.readValue = &ReadPresets;
    presetsPage.writeDraft = &WritePresets;
    presetsPage.invokeAction = &InvokePreset;
    presetsPage.apply = &ApplyPresets;
    presetsPage.cancel = &CancelPresets;
    if (labeledChoices) {
        presetsPage.readChoiceOptions = &ReadPresetChoiceOptions;
    }

    auto automationPage = Page(
        "power-automation", "Automation / Cheats (Coming Soon)",
        "Preview only; the early release exposes status and an emergency disable action.",
        g_automation.controls.data(),
        kAutomationPreviewControlCount);
    automationPage.readValue = &ReadAutomation;
    automationPage.writeDraft = &WriteAutomation;
    automationPage.invokeAction = &InvokeAutomation;
    automationPage.apply = &ApplyAutomation;
    automationPage.cancel = &CancelAutomation;
    if (labeledChoices) {
        automationPage.readChoiceOptions = &ReadAutomationChoiceOptions;
    }

    auto diagnosticsPage = Page(
        "power-diagnostics", "Diagnostics",
        "Provider-owned readiness signals; OFF is a visible fail-closed state.",
        g_diagnosticControls.data(),
        static_cast<std::uint32_t>(g_diagnosticControls.size()));
    diagnosticsPage.readValue = &ReadDiagnostics;

    return {presetsPage, automationPage, diagnosticsPage};
}

Live::LiveChannelDescriptorV1 PowerGridChannel() {
    Live::LiveChannelDescriptorV1 channel;
    Copy(channel.moduleId, kPowerModuleId);
    Copy(channel.pageId, "power-presets");
    Copy(channel.channelId, "preset-grid");
    Copy(channel.title, "Power allocation and priority editor");
    channel.kind = Live::ComponentKind::SegmentedAllocationGrid;
    channel.readLiveFrame = &ReadPowerFrame;
    channel.applyCompoundOperation = &ApplyPowerOperation;
    auto& grid = channel.segmentedGrid;
    Copy(grid.controlId, "allocation");
    grid.columnCount = static_cast<std::uint32_t>(AbsolutePower::kSystemCount);
    grid.tierCount = 4;
    for (std::size_t index = 0; index < AbsolutePower::kSystemCount; ++index) {
        const auto system = AbsolutePower::kCockpitOrder[index];
        Copy(grid.columns[index].columnId, AbsolutePower::SystemKey(system));
        Copy(grid.columns[index].label, AbsolutePower::SystemLabel(system));
        grid.columns[index].maximumSegments = 32;
    }
    constexpr std::array<std::string_view, 4> tierIds{
        "hollow", "green", "yellow", "red"};
    constexpr std::array<std::string_view, 4> tierLabels{
        "Unassigned", "Green / first", "Yellow / second", "Red / last"};
    constexpr std::array<Live::VisualRole, 4> tierRoles{
        Live::VisualRole::Neutral, Live::VisualRole::Tier1,
        Live::VisualRole::Tier2, Live::VisualRole::Tier3};
    for (std::size_t index = 0; index < tierIds.size(); ++index) {
        Copy(grid.tiers[index].tierId, tierIds[index]);
        Copy(grid.tiers[index].label, tierLabels[index]);
        grid.tiers[index].visualRole = tierRoles[index];
    }
    return channel;
}

bool Valid(const ApiV1* api) noexcept {
    constexpr auto required = offsetof(ApiV1, isInputCaptureActive) +
                              sizeof(api->isInputCaptureActive);
    return api && api->structSize >= required && api->abiVersion == kAbiVersion &&
           api->moduleId && std::string_view(api->moduleId) == kModuleId &&
           api->registerPage && api->unregisterModule && api->requestRefresh &&
           api->registerModule && api->isOpen && api->isInputCaptureActive;
}

bool SupportsLabeledChoices(const ApiV1* api) noexcept {
    constexpr auto required = offsetof(ApiV1, capabilities) +
                              sizeof(api->capabilities);
    return api && api->structSize >= required &&
        (api->capabilities & kCapabilityLabeledChoices) != 0;
}
} // namespace

namespace ControlPanelSubscriber {

AbsoluteControlPanelApi::Result RegisterDiscoveredHost() noexcept {
    using AbsoluteControlPanelApi::Result;
    if (g_registered.load(std::memory_order_acquire)) return Result::Ok;
    try {
        std::scoped_lock registrationLock(g_registrationMutex);
        if (g_registered.load(std::memory_order_relaxed)) return Result::Ok;
        for (const wchar_t* moduleName : {
                 L"AbsoluteControlPanel.dll",
                 L"AbsoluteControlPanelResearchDev.dll",
             }) {
            const auto module = GetModuleHandleW(moduleName);
            if (!module) continue;
            const auto address = GetProcAddress(module, "AbsoluteControlPanel_QueryApi");
            if (!address) return Result::NotFound;
            const auto query = reinterpret_cast<AbsoluteControlPanelQueryApi>(address);
            const auto* api = query(AbsoluteControlPanelApi::kAbiVersion);
            if (!Valid(api)) return Result::Rejected;

            const auto moduleResult = api->registerModule(&g_module);
            if (moduleResult != Result::Ok && moduleResult != Result::Duplicate) {
                return moduleResult;
            }
            const auto pages = BuildPages(SupportsLabeledChoices(api));
            for (const auto& page : pages) {
                const auto result = api->registerPage(&page);
                if (result != Result::Ok && result != Result::Duplicate) return result;
            }
            PublishPowerFrame(true);
            const auto liveAddress = GetProcAddress(
                module, "AbsoluteControlPanel_QueryLiveComponentsExperimental");
            if (liveAddress) {
                const auto queryLive =
                    reinterpret_cast<AbsoluteControlPanelQueryLiveApi>(liveAddress);
                const auto* live = queryLive(Live::kAbiVersion);
                if (live && live->structSize >= sizeof(Live::ExperimentalApiV1) &&
                    live->abiVersion == Live::kAbiVersion &&
                    live->registerLiveChannel && live->requestImmediateRefresh) {
                    const auto channel = PowerGridChannel();
                    const auto liveResult = live->registerLiveChannel(&channel);
                    if (liveResult == Live::Result::Ok ||
                        liveResult == Live::Result::Duplicate) {
                        g_liveHostApi.store(live, std::memory_order_release);
                    } else {
                        RuntimePaths::Log(
                            "ControlPanel",
                            std::format("Absolute Control rejected the Power grid channel (result {}).",
                                        static_cast<std::uint32_t>(liveResult)),
                            true);
                    }
                }
            }
            g_hostApi.store(api, std::memory_order_release);
            g_registered.store(true, std::memory_order_release);
            RuntimePaths::Log(
                "ControlPanel",
                std::format(
                    "Registered Presets, Automation / Cheats (Coming Soon), and Diagnostics pages with Absolute Control ({} preset, {} automation-preview, {} diagnostic controls; grid={}).",
                    g_presets.controls.size(), kAutomationPreviewControlCount,
                    g_diagnosticControls.size(),
                    g_liveHostApi.load(std::memory_order_acquire) ? "ready" : "unavailable"),
                true);
            return Result::Ok;
        }
        return Result::NotFound;
    } catch (...) {
        return Result::Rejected;
    }
}

bool IsHosted() noexcept {
    return g_registered.load(std::memory_order_acquire);
}

bool IsMenuOpen() noexcept {
    try {
        const auto* api = g_hostApi.load(std::memory_order_acquire);
        return api && api->isOpen && api->isOpen() != 0;
    } catch (...) {
        return true;
    }
}

bool IsInputCaptureActive() noexcept {
    try {
        const auto* api = g_hostApi.load(std::memory_order_acquire);
        return api && api->isInputCaptureActive && api->isInputCaptureActive() != 0;
    } catch (...) {
        return true;
    }
}

void PublishLiveState() noexcept {
    try {
        PublishPowerFrame(false);
    } catch (...) {
        // Live presentation is fail-optional. Configuration and command paths
        // remain available even if a frame cannot be prepared.
    }
}

} // namespace ControlPanelSubscriber
