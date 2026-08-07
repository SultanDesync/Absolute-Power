#pragma once

#include "Automation.h"
#include "Configuration.h"
#include "NativePowerBackend.h"
#include "PowerService.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace AbsolutePower {

enum class RuntimeState : std::uint8_t {
    Uninitialized,
    WorkbenchMissing,
    AwaitingNativeSnapshotSeam,
    Ready,
};

struct CommandInfo {
    std::string id;
    std::string label;
    std::string category;
};

class PowerRuntime {
public:
    static PowerRuntime& Get();

    void Initialize(bool workbenchPresent);
    void ReloadConfiguration();

    [[nodiscard]] RuntimeState State() const;
    [[nodiscard]] bool AutomationEnabled() const;
    [[nodiscard]] std::string ActivePresetId() const;
    BackendResult Capture(Snapshot& snapshot);
    ApplyResult ActivatePreset(std::string_view presetId);
    bool SetAutomationEnabled(bool enabled);

    [[nodiscard]] std::vector<Preset> Presets() const;
    [[nodiscard]] std::vector<AutomationRule> Rules() const;
    [[nodiscard]] std::vector<CommandInfo> Commands() const;
    BackendResult InvokeCommand(std::string_view commandId);

private:
    PowerRuntime();
    const Preset* FindPreset(std::string_view id) const;

    mutable std::mutex mutex_;
    NativePowerBackend backend_;
    PowerService service_;
    AutomationEngine automation_;
    ConfigurationData configuration_;
    RuntimeState state_{RuntimeState::Uninitialized};
    std::string activePresetId_;
};

} // namespace AbsolutePower
