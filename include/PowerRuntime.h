#pragma once

#include "Automation.h"
#include "Configuration.h"
#include "KeyboardShortcut.h"
#include "NativePowerBackend.h"
#include "PowerService.h"

#include <cstdint>
#include <chrono>
#include <array>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace AbsolutePower {

enum class RuntimeState : std::uint8_t {
    Uninitialized,
    WorkbenchMissing, // ABI v1 legacy value; standalone runtime no longer enters it.
    AwaitingNativeSnapshotSeam,
    Ready,
};

struct CommandInfo {
    std::string id;
    std::string label;
    std::string category;
};

enum class ShortcutUpdateResult : std::uint8_t {
    Ok,
    InvalidArgument,
    PresetNotFound,
    Conflict,
    WriteFailure,
};

struct ConfigurationView {
    std::uint64_t generation{};
    ConfigurationData data;
    ConfigurationData inherited;
    std::vector<ConfigurationRecordSource> presetSources;
    std::vector<ConfigurationRecordSource> ruleSources;
};

enum class ActivationState : std::uint8_t {
    Idle,
    Queued,
    Waiting,
    Settling,
    Converged,
    Failed,
};

struct ActivationStatus {
    std::uint64_t sequence{};
    ActivationState state{ActivationState::Idle};
    BackendResult backend{BackendResult::Ok};
    std::string requestedPresetId;
    std::string activePresetId;
    std::size_t totalChanges{};
    std::size_t completedChanges{};
    std::size_t remainingChanges{};
};

enum class ConfigurationCommitResult : std::uint8_t {
    Ok,
    StaleGeneration,
    InvalidDraft,
    WriteFailure,
    ReloadFailure,
    VerificationMismatch,
};

struct ConfigurationCommitReport {
    ConfigurationCommitResult result{ConfigurationCommitResult::InvalidDraft};
    std::uint64_t generation{};
    std::string detail;
};

enum class WeaponFireOrigin : std::uint8_t {
    NativeInputListener,
    AbsoluteHotasBridge,
};

struct AutomationRuntimeStatus {
    bool nativeWeaponInputReady{};
    bool hotasWeaponBridgeSeen{};
    bool settlementActive{};
    bool restoringBasePreset{};
    std::size_t activeDemandCount{};
    std::array<std::uint64_t, 3> weaponEventCounts{};
    BackendResult lastSettlementResult{BackendResult::Ok};
};

class PowerRuntime {
public:
    static PowerRuntime& Get();

    // Initialize the standalone Power engine. Presentation/input hosts are
    // optional adapters and never gate configuration or native execution.
    void Initialize();
    void SetGameThreadAvailable(bool available);
    void SetNativeWeaponInputReady(bool ready);
    void TickGameThread();
    void ReloadConfiguration();
    void RecordWeaponFire(SystemId weapon, WeaponFireOrigin origin) noexcept;

    [[nodiscard]] RuntimeState State() const;
    [[nodiscard]] bool AutomationEnabled() const;
    [[nodiscard]] std::string ActivePresetId() const;
    BackendResult Capture(Snapshot& snapshot);
    ApplyResult ActivatePreset(std::string_view presetId);
    bool SetAutomationEnabled(bool enabled);

    [[nodiscard]] std::vector<Preset> Presets() const;
    [[nodiscard]] std::vector<AutomationRule> Rules() const;
    [[nodiscard]] ConfigurationView ConfigurationSnapshot() const;
    [[nodiscard]] ActivationStatus ActivationSnapshot() const;
    [[nodiscard]] AutomationRuntimeStatus AutomationSnapshot() const;
    [[nodiscard]] ConfigurationCommitReport SaveConfiguration(
        std::uint64_t baseGeneration, ConfigurationData desired,
        bool preserveKeyboardBindings = true);
    [[nodiscard]] std::vector<CommandInfo> Commands() const;
    [[nodiscard]] std::optional<PresetShortcut> KeyboardShortcut(
        std::string_view presetId) const;
    [[nodiscard]] std::vector<PresetShortcut> KeyboardShortcuts() const;
    ShortcutUpdateResult SetKeyboardShortcut(std::string_view presetId,
                                             const KeyboardChord& chord);
    ShortcutUpdateResult ClearKeyboardShortcut(std::string_view presetId);
    ShortcutUpdateResult ReplaceKeyboardShortcuts(
        const std::vector<PresetShortcut>& shortcuts);
    [[nodiscard]] std::optional<AbsolutePower::JoystickShortcut> JoystickShortcut(
        std::string_view presetId) const;
    [[nodiscard]] std::vector<AbsolutePower::JoystickShortcut> JoystickShortcuts() const;
    ShortcutUpdateResult SetJoystickShortcut(std::string_view presetId,
                                             std::string_view token);
    ShortcutUpdateResult ClearJoystickShortcut(std::string_view presetId);
    ShortcutUpdateResult ReplaceJoystickShortcuts(
        const std::vector<AbsolutePower::JoystickShortcut>& shortcuts);
    BackendResult InvokeCommand(std::string_view commandId);

private:
    PowerRuntime();
    const Preset* FindPreset(std::string_view id) const;
    void ProcessKeyboardShortcuts();
    void ProcessJoystickShortcuts();
    void ProcessAutomationSettlement(std::chrono::steady_clock::time_point now);
    void SeedKeyboardShortcutEdgesLocked();

    mutable std::mutex mutex_;
    NativePowerBackend backend_;
    PowerService service_;
    AutomationEngine automation_;
    ConfigurationData configuration_;
    ConfigurationData inheritedConfiguration_;
    std::vector<ConfigurationRecordSource> presetSources_;
    std::vector<ConfigurationRecordSource> ruleSources_;
    std::uint64_t configurationGeneration_{};
    RuntimeState state_{RuntimeState::Uninitialized};
    std::string activePresetId_;
    std::string pendingPresetId_;
    Snapshot cachedSnapshot_{};
    BackendResult cachedSnapshotResult_{BackendResult::SnapshotSeamUnavailable};
    bool gameThreadAvailable_{};
    bool nativeWeaponInputReady_{};
    bool hotasWeaponBridgeSeen_{};
    bool automationSettlementActive_{};
    bool automationRestoringBase_{};
    std::size_t activeAutomationDemandCount_{};
    std::array<std::uint64_t, 3> weaponEventCounts_{};
    std::chrono::steady_clock::time_point lastSnapshotRefresh_{};
    std::chrono::steady_clock::time_point lastApplyAttempt_{};
    std::chrono::steady_clock::time_point lastAutomationApplyAttempt_{};
    BackendResult lastApplyResult_{BackendResult::Ok};
    BackendResult lastAutomationResult_{BackendResult::Ok};
    std::string lastApplyResultPreset_;
    ActivationStatus activation_{};
    std::unordered_map<std::string, bool> keyboardShortcutDown_;
};

} // namespace AbsolutePower
