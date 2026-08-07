#include "PowerRuntime.h"

#include "RuntimePaths.h"

#include <algorithm>

namespace AbsolutePower {

PowerRuntime& PowerRuntime::Get() {
    static PowerRuntime instance;
    return instance;
}

PowerRuntime::PowerRuntime() : service_(backend_) {}

void PowerRuntime::Initialize(bool workbenchPresent) {
    std::scoped_lock lock(mutex_);
    configuration_ = Configuration::Load(RuntimePaths::DefaultsPath(), RuntimePaths::CustomPath());
    automation_.SetRules(configuration_.rules);
    automation_.SetEnabled(configuration_.automationEnabled);
    activePresetId_ = configuration_.startupPreset;
    if (!workbenchPresent) {
        state_ = RuntimeState::WorkbenchMissing;
        return;
    }
    backend_.Initialize();
    state_ = RuntimeState::AwaitingNativeSnapshotSeam;
}

void PowerRuntime::ReloadConfiguration() {
    std::scoped_lock lock(mutex_);
    configuration_ = Configuration::Load(RuntimePaths::DefaultsPath(), RuntimePaths::CustomPath());
    automation_.SetRules(configuration_.rules);
    automation_.SetEnabled(configuration_.automationEnabled);
    if (FindPreset(activePresetId_) == nullptr) activePresetId_ = configuration_.startupPreset;
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
    {
        std::scoped_lock lock(mutex_);
        if (state_ == RuntimeState::WorkbenchMissing) return BackendResult::WorkbenchMissing;
    }
    const auto result = service_.Capture(snapshot);
    if (result == BackendResult::Ok) {
        std::scoped_lock lock(mutex_);
        state_ = RuntimeState::Ready;
    }
    return result;
}

ApplyResult PowerRuntime::ActivatePreset(std::string_view presetId) {
    Preset preset;
    std::vector<Demand> demands;
    {
        std::scoped_lock lock(mutex_);
        if (state_ == RuntimeState::WorkbenchMissing) {
            ApplyResult result{};
            result.backend = BackendResult::WorkbenchMissing;
            return result;
        }
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
    if (result.backend == BackendResult::Ok) {
        std::scoped_lock lock(mutex_);
        activePresetId_ = preset.id;
        state_ = RuntimeState::Ready;
    }
    return result;
}

bool PowerRuntime::SetAutomationEnabled(bool enabled) {
    std::scoped_lock lock(mutex_);
    if (state_ == RuntimeState::WorkbenchMissing) return false;
    automation_.SetEnabled(enabled);
    configuration_.automationEnabled = enabled;
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

BackendResult PowerRuntime::InvokeCommand(std::string_view commandId) {
    if (State() == RuntimeState::WorkbenchMissing) return BackendResult::WorkbenchMissing;
    constexpr std::string_view prefix = "preset:";
    if (commandId.starts_with(prefix)) {
        return ActivatePreset(commandId.substr(prefix.size())).backend;
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
