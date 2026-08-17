#include "PowerRuntime.h"

#include "RuntimePaths.h"
#include "SuiteHost.h"

#include <algorithm>

namespace AbsolutePower {
namespace {

std::string_view BackendResultName(BackendResult result) {
    switch (result) {
    case BackendResult::Ok: return "Ok";
    case BackendResult::WorkbenchMissing: return "HostMissing";
    case BackendResult::UnsupportedRuntime: return "UnsupportedRuntime";
    case BackendResult::SnapshotSeamUnavailable: return "SnapshotSeamUnavailable";
    case BackendResult::PilotNotReady: return "PilotNotReady";
    case BackendResult::SystemUnavailable: return "SystemUnavailable";
    case BackendResult::InvalidRequest: return "InvalidRequest";
    case BackendResult::SetterRejected: return "SetterRejected";
    }
    return "Unknown";
}

bool KeyDown(std::uint8_t virtualKey) noexcept {
    return virtualKey != 0 && (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
}

bool ChordDown(const KeyboardChord& chord) noexcept {
    return KeyboardShortcutPolicy::Matches(
        chord, KeyDown(chord.virtualKey), KeyDown(VK_CONTROL), KeyDown(VK_MENU),
        KeyDown(VK_SHIFT));
}

} // namespace

PowerRuntime& PowerRuntime::Get() {
    static PowerRuntime instance;
    return instance;
}

PowerRuntime::PowerRuntime() : service_(backend_) {}

void PowerRuntime::Initialize() {
    std::scoped_lock lock(mutex_);
    auto loaded = Configuration::LoadDetailed(RuntimePaths::DefaultsPath(),
                                               RuntimePaths::ImportsDirectory(),
                                               RuntimePaths::CustomPath());
    configuration_ = std::move(loaded.effective);
    inheritedConfiguration_ = std::move(loaded.inherited);
    presetSources_ = std::move(loaded.presetSources);
    ruleSources_ = std::move(loaded.ruleSources);
    ++configurationGeneration_;
    automation_.SetRules(configuration_.rules);
    automation_.SetEnabled(configuration_.automationEnabled);
    SeedKeyboardShortcutEdgesLocked();
    activePresetId_.clear();
    pendingPresetId_.clear();
    activation_ = {};
    automationSettlementActive_ = false;
    automationRestoringBase_ = false;
    activeAutomationDemandCount_ = 0;
    weaponEventCounts_.fill(0);
    hotasWeaponBridgeSeen_ = false;
    lastAutomationApplyAttempt_ = {};
    lastAutomationResult_ = BackendResult::Ok;
    backend_.Initialize();
    state_ = RuntimeState::AwaitingNativeSnapshotSeam;
    if (gameThreadAvailable_ && FindPreset(configuration_.startupPreset)) {
        pendingPresetId_ = configuration_.startupPreset;
        activation_.sequence = 1;
        activation_.state = ActivationState::Queued;
        activation_.requestedPresetId = pendingPresetId_;
    }
}

void PowerRuntime::SetGameThreadAvailable(bool available) {
    std::scoped_lock lock(mutex_);
    gameThreadAvailable_ = available;
}

void PowerRuntime::SetNativeWeaponInputReady(bool ready) {
    std::scoped_lock lock(mutex_);
    nativeWeaponInputReady_ = ready;
}

void PowerRuntime::RecordWeaponFire(SystemId weapon, WeaponFireOrigin origin) noexcept {
    const auto index = ToIndex(weapon);
    if (index >= weaponEventCounts_.size()) return;
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now().time_since_epoch())
                         .count();
    bool firstForSource{};
    {
        std::scoped_lock lock(mutex_);
        if (state_ == RuntimeState::Uninitialized) return;
        firstForSource = weaponEventCounts_[index] == 0;
        ++weaponEventCounts_[index];
        if (origin == WeaponFireOrigin::AbsoluteHotasBridge) {
            hotasWeaponBridgeSeen_ = true;
        }
        automation_.RecordWeaponFire(weapon, now);
    }
    if (firstForSource) {
        RuntimePaths::Log(
            "Automation",
            std::format("Observed {} fire through the {} event source.",
                        SystemLabel(weapon),
                        origin == WeaponFireOrigin::AbsoluteHotasBridge
                            ? "AbsoluteHOTAS native bridge"
                            : "native WeaponGroup listener"),
            true);
    }
}

void PowerRuntime::TickGameThread() {
    static std::atomic<bool> firstTickLogged{};
    if (!firstTickLogged.exchange(true, std::memory_order_acq_rel)) {
        RuntimePaths::Log("Executor", "First Power executor tick is running.", true);
    }
    ProcessKeyboardShortcuts();
    std::string pending;
    const auto now = std::chrono::steady_clock::now();
    {
        std::scoped_lock lock(mutex_);
        if (!gameThreadAvailable_ || state_ == RuntimeState::Uninitialized) {
            return;
        }
        if (!pendingPresetId_.empty() &&
            (lastApplyAttempt_.time_since_epoch().count() == 0 ||
             now - lastApplyAttempt_ >= std::chrono::milliseconds(100))) {
            pending = pendingPresetId_;
            lastApplyAttempt_ = now;
        }
    }

    if (!pending.empty()) {
        const auto result = ActivatePreset(pending);
        const bool terminalFailure = result.backend != BackendResult::Ok &&
                                     result.backend != BackendResult::PilotNotReady &&
                                     result.backend != BackendResult::SnapshotSeamUnavailable;
        bool resultChanged{};
        const bool converged = result.backend == BackendResult::Ok && result.totalChanges == 0;
        {
            std::scoped_lock lock(mutex_);
            resultChanged = lastApplyResult_ != result.backend ||
                            lastApplyResultPreset_ != pending;
            lastApplyResult_ = result.backend;
            lastApplyResultPreset_ = pending;
            activation_.backend = result.backend;
            activation_.requestedPresetId = pending;
            if (converged) {
                if (pendingPresetId_ == pending) pendingPresetId_.clear();
                lastSnapshotRefresh_ = {};
                activation_.state = ActivationState::Converged;
                activation_.activePresetId = pending;
                activation_.remainingChanges = 0;
                activation_.totalChanges = activation_.completedChanges;
            } else if (terminalFailure && pendingPresetId_ == pending) {
                pendingPresetId_.clear();
                activation_.state = ActivationState::Failed;
                activation_.remainingChanges = result.totalChanges;
                activation_.totalChanges = activation_.completedChanges +
                                           activation_.remainingChanges;
            } else if (result.backend == BackendResult::Ok) {
                activation_.state = ActivationState::Settling;
                activation_.completedChanges += result.completedChanges;
                activation_.remainingChanges = result.totalChanges >=
                                                       result.completedChanges
                                                   ? result.totalChanges -
                                                         result.completedChanges
                                                   : 0;
                activation_.totalChanges = activation_.completedChanges +
                                           activation_.remainingChanges;
            } else {
                activation_.state = ActivationState::Waiting;
            }
        }
        if (converged) {
            RuntimePaths::Log(
                "Activation",
                std::format(
                    "Committed preset '{}' after a later native snapshot confirmed the full allocation.",
                    pending));
        } else if (result.backend == BackendResult::Ok) {
            RuntimePaths::Log(
                "Activation",
                std::format(
                    "Preset '{}' accepted one native pip step; awaiting the next-frame snapshot ({} pip{} remained before this step).",
                    pending, result.totalChanges, result.totalChanges == 1 ? "" : "s"));
        } else if (terminalFailure) {
            RuntimePaths::Log(
                "Activation",
                std::format("Preset '{}' was rejected by the native backend ({}).", pending,
                            BackendResultName(result.backend)),
                true);
        } else if (resultChanged) {
            RuntimePaths::Log(
                "Activation",
                std::format("Preset '{}' is waiting in the validated game-update context ({}).",
                            pending, BackendResultName(result.backend)),
                true);
        }
    }

    if (pending.empty()) ProcessAutomationSettlement(now);

    {
        std::scoped_lock lock(mutex_);
        if (lastSnapshotRefresh_.time_since_epoch().count() != 0 &&
            now - lastSnapshotRefresh_ < std::chrono::milliseconds(250)) {
            return;
        }
        lastSnapshotRefresh_ = now;
    }

    Snapshot snapshot{};
    const auto result = service_.Capture(snapshot);
    std::scoped_lock lock(mutex_);
    cachedSnapshot_ = snapshot;
    cachedSnapshotResult_ = result;
    if (result == BackendResult::Ok) state_ = RuntimeState::Ready;
}

void PowerRuntime::ProcessAutomationSettlement(
    std::chrono::steady_clock::time_point now) {
    Preset basePreset;
    std::uint64_t generation{};
    std::string basePresetId;
    {
        std::scoped_lock lock(mutex_);
        if (!gameThreadAvailable_ || state_ == RuntimeState::Uninitialized ||
            !pendingPresetId_.empty() || activePresetId_.empty()) {
            return;
        }
        if (lastAutomationApplyAttempt_.time_since_epoch().count() != 0 &&
            now - lastAutomationApplyAttempt_ < std::chrono::milliseconds(100)) {
            return;
        }
        const auto* found = FindPreset(activePresetId_);
        if (!found) {
            automationSettlementActive_ = false;
            automationRestoringBase_ = false;
            activeAutomationDemandCount_ = 0;
            return;
        }
        basePreset = *found;
        basePresetId = activePresetId_;
        generation = configurationGeneration_;
        lastAutomationApplyAttempt_ = now;
    }

    Snapshot snapshot{};
    const auto captureResult = service_.Capture(snapshot);
    std::vector<Demand> demands;
    bool started{};
    bool startedRestoring{};
    bool shouldApply{};
    {
        std::scoped_lock lock(mutex_);
        if (generation != configurationGeneration_ || activePresetId_ != basePresetId ||
            !pendingPresetId_.empty()) {
            return;
        }
        if (captureResult == BackendResult::Ok) {
            const auto nowMilliseconds =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    now.time_since_epoch())
                    .count();
            demands = automation_.ActiveDemands(snapshot, nowMilliseconds);
        }
        activeAutomationDemandCount_ = demands.size();
        if (!demands.empty()) {
            started = !automationSettlementActive_;
            automationSettlementActive_ = true;
            automationRestoringBase_ = false;
        } else if (automationSettlementActive_ && !automationRestoringBase_) {
            automationRestoringBase_ = true;
            startedRestoring = true;
        }
        shouldApply = automationSettlementActive_;
        if (!shouldApply) lastAutomationResult_ = captureResult;
    }

    if (started) {
        RuntimePaths::Log(
            "Automation",
            std::format("Started {} emergency demand{} against base preset '{}'.",
                        demands.size(), demands.size() == 1 ? "" : "s", basePresetId),
            true);
    }
    if (startedRestoring) {
        RuntimePaths::Log(
            "Automation",
            std::format("Emergency demands expired; restoring base preset '{}'.",
                        basePresetId),
            true);
    }
    if (!shouldApply) return;

    if (captureResult != BackendResult::Ok) {
        bool changed{};
        {
            std::scoped_lock lock(mutex_);
            changed = lastAutomationResult_ != captureResult;
            lastAutomationResult_ = captureResult;
        }
        if (changed) {
            RuntimePaths::Log(
                "Automation",
                std::format("Automation settlement is waiting for the native backend ({}).",
                            BackendResultName(captureResult)),
                true);
        }
        return;
    }

    const auto result = service_.ApplyPreset(basePreset, demands);
    bool restored{};
    bool resultChanged{};
    {
        std::scoped_lock lock(mutex_);
        resultChanged = lastAutomationResult_ != result.backend;
        lastAutomationResult_ = result.backend;
        if (generation == configurationGeneration_ && activePresetId_ == basePresetId &&
            demands.empty() && result.backend == BackendResult::Ok &&
            result.totalChanges == 0) {
            automationSettlementActive_ = false;
            automationRestoringBase_ = false;
            activeAutomationDemandCount_ = 0;
            restored = true;
        }
    }
    if (restored) {
        RuntimePaths::Log(
            "Automation",
            std::format("Base preset '{}' restored after automation settlement.",
                        basePresetId),
            true);
    } else if (resultChanged && result.backend != BackendResult::Ok) {
        RuntimePaths::Log(
            "Automation",
            std::format("Automation settlement is waiting for the native backend ({}).",
                        BackendResultName(result.backend)),
            true);
    }
}

void PowerRuntime::ReloadConfiguration() {
    std::scoped_lock lock(mutex_);
    auto loaded = Configuration::LoadDetailed(RuntimePaths::DefaultsPath(),
                                               RuntimePaths::ImportsDirectory(),
                                               RuntimePaths::CustomPath());
    configuration_ = std::move(loaded.effective);
    inheritedConfiguration_ = std::move(loaded.inherited);
    presetSources_ = std::move(loaded.presetSources);
    ruleSources_ = std::move(loaded.ruleSources);
    ++configurationGeneration_;
    automation_.SetRules(configuration_.rules);
    automation_.SetEnabled(configuration_.automationEnabled);
    SeedKeyboardShortcutEdgesLocked();
    if (FindPreset(activePresetId_) == nullptr) activePresetId_.clear();
    if (FindPreset(pendingPresetId_) == nullptr) {
        pendingPresetId_.clear();
        if (activation_.state == ActivationState::Queued ||
            activation_.state == ActivationState::Waiting ||
            activation_.state == ActivationState::Settling) {
            activation_.state = ActivationState::Failed;
            activation_.backend = BackendResult::InvalidRequest;
        }
    }
    RuntimePaths::Log("Configuration", "Defaults and user overlay reloaded.");
}

RuntimeState PowerRuntime::State() const {
    std::scoped_lock lock(mutex_);
    return state_;
}

bool PowerRuntime::AutomationEnabled() const {
    std::scoped_lock lock(mutex_);
    return automation_.Enabled();
}

std::string PowerRuntime::ActivePresetId() const {
    std::scoped_lock lock(mutex_);
    return activePresetId_;
}

BackendResult PowerRuntime::Capture(Snapshot& snapshot) {
    std::scoped_lock lock(mutex_);
    snapshot = cachedSnapshot_;
    return cachedSnapshotResult_;
}

ApplyResult PowerRuntime::ActivatePreset(std::string_view presetId) {
    Preset preset;
    std::vector<Demand> demands;
    {
        std::scoped_lock lock(mutex_);
        const auto* found = FindPreset(presetId);
        if (!found) {
            ApplyResult result{};
            result.backend = BackendResult::InvalidRequest;
            return result;
        }
        preset = *found;
    }

    Snapshot snapshot{};
    if (service_.Capture(snapshot) == BackendResult::Ok) {
        const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now().time_since_epoch())
                             .count();
        std::scoped_lock lock(mutex_);
        demands = automation_.ActiveDemands(snapshot, now);
    }
    auto result = service_.ApplyPreset(preset, demands);
    if (result.backend == BackendResult::Ok && result.totalChanges == 0) {
        std::scoped_lock lock(mutex_);
        activePresetId_ = preset.id;
        state_ = RuntimeState::Ready;
    }
    return result;
}

bool PowerRuntime::SetAutomationEnabled(bool enabled) {
    std::scoped_lock lock(mutex_);
    automation_.SetEnabled(enabled);
    configuration_.automationEnabled = enabled;
    ++configurationGeneration_;
    return true;
}

std::vector<Preset> PowerRuntime::Presets() const {
    std::scoped_lock lock(mutex_);
    return configuration_.presets;
}

std::vector<AutomationRule> PowerRuntime::Rules() const {
    std::scoped_lock lock(mutex_);
    return configuration_.rules;
}

ConfigurationView PowerRuntime::ConfigurationSnapshot() const {
    std::scoped_lock lock(mutex_);
    return {
        .generation = configurationGeneration_,
        .data = configuration_,
        .inherited = inheritedConfiguration_,
        .presetSources = presetSources_,
        .ruleSources = ruleSources_,
    };
}

ActivationStatus PowerRuntime::ActivationSnapshot() const {
    std::scoped_lock lock(mutex_);
    auto result = activation_;
    result.activePresetId = activePresetId_;
    return result;
}

AutomationRuntimeStatus PowerRuntime::AutomationSnapshot() const {
    std::scoped_lock lock(mutex_);
    return {
        .nativeWeaponInputReady = nativeWeaponInputReady_,
        .hotasWeaponBridgeSeen = hotasWeaponBridgeSeen_,
        .settlementActive = automationSettlementActive_,
        .restoringBasePreset = automationRestoringBase_,
        .activeDemandCount = activeAutomationDemandCount_,
        .weaponEventCounts = weaponEventCounts_,
        .lastSettlementResult = lastAutomationResult_,
    };
}

ConfigurationCommitReport PowerRuntime::SaveConfiguration(
    std::uint64_t baseGeneration, ConfigurationData desired,
    bool preserveKeyboardBindings) {
    std::scoped_lock lock(mutex_);
    if (baseGeneration != configurationGeneration_) {
        return {
            .result = ConfigurationCommitResult::StaleGeneration,
            .generation = configurationGeneration_,
            .detail = "Power configuration changed after this editor transaction opened.",
        };
    }

    // Logging and keyboard bindings are Power-owned but edited through their
    // own bounded controls. A preset/rule transaction carries them forward,
    // except that deleting a preset necessarily removes its orphan binding.
    desired.enableLog = configuration_.enableLog;
    if (preserveKeyboardBindings) {
        desired.keyboardShortcuts = configuration_.keyboardShortcuts;
    }
    std::erase_if(desired.keyboardShortcuts, [&](const auto& shortcut) {
        return std::ranges::find(desired.presets, shortcut.presetId,
                                 &Preset::id) == desired.presets.end();
    });
    auto saved = Configuration::Save(
        RuntimePaths::DefaultsPath(), RuntimePaths::ImportsDirectory(),
        RuntimePaths::CustomPath(), inheritedConfiguration_, desired);
    ConfigurationCommitResult mapped{};
    switch (saved.result) {
    case SaveConfigurationResult::Ok: mapped = ConfigurationCommitResult::Ok; break;
    case SaveConfigurationResult::InvalidDraft:
        mapped = ConfigurationCommitResult::InvalidDraft;
        break;
    case SaveConfigurationResult::WriteFailure:
        mapped = ConfigurationCommitResult::WriteFailure;
        break;
    case SaveConfigurationResult::ReloadFailure:
        mapped = ConfigurationCommitResult::ReloadFailure;
        break;
    case SaveConfigurationResult::VerificationMismatch:
        mapped = ConfigurationCommitResult::VerificationMismatch;
        break;
    }
    if (mapped != ConfigurationCommitResult::Ok) {
        return {
            .result = mapped,
            .generation = configurationGeneration_,
            .detail = std::move(saved.detail),
        };
    }

    configuration_ = std::move(saved.configuration.effective);
    inheritedConfiguration_ = std::move(saved.configuration.inherited);
    presetSources_ = std::move(saved.configuration.presetSources);
    ruleSources_ = std::move(saved.configuration.ruleSources);
    automation_.SetRules(configuration_.rules);
    automation_.SetEnabled(configuration_.automationEnabled);
    SeedKeyboardShortcutEdgesLocked();
    if (FindPreset(activePresetId_) == nullptr) activePresetId_.clear();
    if (FindPreset(pendingPresetId_) == nullptr) {
        pendingPresetId_.clear();
        if (activation_.state == ActivationState::Queued ||
            activation_.state == ActivationState::Waiting ||
            activation_.state == ActivationState::Settling) {
            activation_.state = ActivationState::Failed;
            activation_.backend = BackendResult::InvalidRequest;
        }
    }
    ++configurationGeneration_;
    RuntimePaths::Log(
        "Configuration",
        std::format("Committed source-aware configuration generation {}.",
                    configurationGeneration_));
    return {
        .result = ConfigurationCommitResult::Ok,
        .generation = configurationGeneration_,
        .detail = std::move(saved.detail),
    };
}

std::vector<CommandInfo> PowerRuntime::Commands() const {
    std::scoped_lock lock(mutex_);
    std::vector<CommandInfo> commands;
    commands.reserve(configuration_.presets.size() + 1);
    for (const auto& preset : configuration_.presets) {
        commands.push_back({"preset:" + preset.id, "Activate " + preset.displayName,
                            "Power Presets"});
    }
    commands.push_back(
        {"automation:toggle", "Toggle Power Automation (Cheat)", "Automation / Cheats"});
    return commands;
}

std::optional<PresetShortcut> PowerRuntime::KeyboardShortcut(
    std::string_view presetId) const {
    std::scoped_lock lock(mutex_);
    const auto found = std::ranges::find(configuration_.keyboardShortcuts, presetId,
                                         &PresetShortcut::presetId);
    return found == configuration_.keyboardShortcuts.end()
               ? std::nullopt
               : std::optional<PresetShortcut>(*found);
}

std::vector<PresetShortcut> PowerRuntime::KeyboardShortcuts() const {
    std::scoped_lock lock(mutex_);
    return configuration_.keyboardShortcuts;
}

ShortcutUpdateResult PowerRuntime::SetKeyboardShortcut(std::string_view presetId,
                                                       const KeyboardChord& chord) {
    std::scoped_lock lock(mutex_);
    if (presetId.empty() || !KeyboardShortcutPolicy::Valid(chord)) {
        return ShortcutUpdateResult::InvalidArgument;
    }
    if (!FindPreset(presetId)) return ShortcutUpdateResult::PresetNotFound;
    const auto conflict = std::ranges::find_if(
        configuration_.keyboardShortcuts, [&](const PresetShortcut& shortcut) {
            return shortcut.presetId != presetId && shortcut.chord == chord;
        });
    if (conflict != configuration_.keyboardShortcuts.end()) {
        return ShortcutUpdateResult::Conflict;
    }
    if (!Configuration::WriteKeyboardShortcut(RuntimePaths::CustomPath(), presetId, chord)) {
        return ShortcutUpdateResult::WriteFailure;
    }
    std::erase_if(configuration_.keyboardShortcuts, [&](const PresetShortcut& shortcut) {
        return shortcut.presetId == presetId;
    });
    configuration_.keyboardShortcuts.push_back({std::string(presetId), chord});
    std::ranges::sort(configuration_.keyboardShortcuts, {}, &PresetShortcut::presetId);
    keyboardShortcutDown_[std::string(presetId)] = ChordDown(chord);
    ++configurationGeneration_;
    RuntimePaths::Log(
        "PowerBindings",
        std::format("Preset '{}' bound to {} in AbsolutePower_Custom.ini.", presetId,
                    KeyboardShortcutPolicy::StorageName(chord)));
    return ShortcutUpdateResult::Ok;
}

ShortcutUpdateResult PowerRuntime::ClearKeyboardShortcut(std::string_view presetId) {
    std::scoped_lock lock(mutex_);
    if (presetId.empty()) return ShortcutUpdateResult::InvalidArgument;
    if (!Configuration::WriteKeyboardShortcut(RuntimePaths::CustomPath(), presetId,
                                               std::nullopt)) {
        return ShortcutUpdateResult::WriteFailure;
    }
    std::erase_if(configuration_.keyboardShortcuts, [&](const PresetShortcut& shortcut) {
        return shortcut.presetId == presetId;
    });
    keyboardShortcutDown_.erase(std::string(presetId));
    ++configurationGeneration_;
    RuntimePaths::Log("PowerBindings",
                      std::format("Keyboard shortcut cleared for preset '{}'.", presetId));
    return ShortcutUpdateResult::Ok;
}

ShortcutUpdateResult PowerRuntime::ReplaceKeyboardShortcuts(
    const std::vector<PresetShortcut>& shortcuts) {
    std::scoped_lock lock(mutex_);
    for (std::size_t index = 0; index < shortcuts.size(); ++index) {
        const auto& shortcut = shortcuts[index];
        if (shortcut.presetId.empty() || !FindPreset(shortcut.presetId) ||
            !KeyboardShortcutPolicy::Valid(shortcut.chord)) {
            return ShortcutUpdateResult::InvalidArgument;
        }
        if (std::ranges::any_of(shortcuts.begin(), shortcuts.begin() + index,
                [&](const auto& previous) {
                    return previous.presetId == shortcut.presetId;
                })) {
            return ShortcutUpdateResult::InvalidArgument;
        }
        if (std::ranges::any_of(shortcuts.begin(), shortcuts.begin() + index,
                [&](const auto& previous) {
                    return previous.chord == shortcut.chord;
                })) {
            return ShortcutUpdateResult::Conflict;
        }
    }

    std::vector<KeyboardShortcutEdit> edits;
    edits.reserve(configuration_.presets.size());
    for (const auto& preset : configuration_.presets) {
        const auto current = std::ranges::find(configuration_.keyboardShortcuts, preset.id,
                                                &PresetShortcut::presetId);
        const auto desired = std::ranges::find(shortcuts, preset.id,
                                                &PresetShortcut::presetId);
        const bool currentBound = current != configuration_.keyboardShortcuts.end();
        const bool desiredBound = desired != shortcuts.end();
        if (currentBound == desiredBound &&
            (!currentBound || current->chord == desired->chord)) {
            continue;
        }
        edits.push_back({preset.id,
                         desiredBound ? std::optional(desired->chord) : std::nullopt});
    }
    if (!Configuration::WriteKeyboardShortcuts(RuntimePaths::CustomPath(), edits)) {
        return ShortcutUpdateResult::WriteFailure;
    }
    configuration_.keyboardShortcuts = shortcuts;
    std::ranges::sort(configuration_.keyboardShortcuts, {}, &PresetShortcut::presetId);
    SeedKeyboardShortcutEdgesLocked();
    ++configurationGeneration_;
    RuntimePaths::Log(
        "PowerBindings",
        std::format("Committed {} keyboard preset binding{} from Absolute Control.",
                    configuration_.keyboardShortcuts.size(),
                    configuration_.keyboardShortcuts.size() == 1 ? "" : "s"));
    return ShortcutUpdateResult::Ok;
}

void PowerRuntime::ProcessKeyboardShortcuts() {
    const bool suppressed = SuiteHost::KeyboardInputSuppressed();
    std::vector<std::string> triggered;
    {
        std::scoped_lock lock(mutex_);
        for (const auto& shortcut : configuration_.keyboardShortcuts) {
            const bool down = ChordDown(shortcut.chord);
            bool& previous = keyboardShortcutDown_[shortcut.presetId];
            if (KeyboardShortcutPolicy::ConsumePressEdge(down, suppressed, previous)) {
                triggered.push_back(shortcut.presetId);
            }
        }
    }
    for (const auto& presetId : triggered) {
        (void)InvokeCommand("preset:" + presetId);
    }
}

void PowerRuntime::SeedKeyboardShortcutEdgesLocked() {
    keyboardShortcutDown_.clear();
    for (const auto& shortcut : configuration_.keyboardShortcuts) {
        keyboardShortcutDown_.emplace(shortcut.presetId, ChordDown(shortcut.chord));
    }
}

BackendResult PowerRuntime::InvokeCommand(std::string_view commandId) {
    constexpr std::string_view prefix = "preset:";
    if (commandId.starts_with(prefix)) {
        const auto presetId = commandId.substr(prefix.size());
        {
            std::scoped_lock lock(mutex_);
            if (!gameThreadAvailable_) return BackendResult::SnapshotSeamUnavailable;
            if (!FindPreset(presetId)) return BackendResult::InvalidRequest;
            pendingPresetId_ = presetId;
            lastApplyAttempt_ = {};
            ++activation_.sequence;
            activation_.state = ActivationState::Queued;
            activation_.backend = BackendResult::Ok;
            activation_.requestedPresetId = presetId;
            activation_.activePresetId = activePresetId_;
            activation_.totalChanges = 0;
            activation_.completedChanges = 0;
            activation_.remainingChanges = 0;
        }
        RuntimePaths::Log("Activation", std::format("Queued preset '{}' for the SFSE game task.",
                                                      presetId));
        return BackendResult::Ok;
    }
    if (commandId == "automation:toggle") {
        SetAutomationEnabled(!AutomationEnabled());
        return BackendResult::Ok;
    }
    return BackendResult::InvalidRequest;
}

const Preset* PowerRuntime::FindPreset(std::string_view id) const {
    const auto found = std::ranges::find(configuration_.presets, id, &Preset::id);
    return found == configuration_.presets.end() ? nullptr : &*found;
}

} // namespace AbsolutePower
