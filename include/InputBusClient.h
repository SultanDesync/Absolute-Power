#pragma once

#include "AbsoluteControlPanelAPI.h"
#include "AbsoluteInputBusAPI.h"
#include "JoystickShortcut.h"

#include <array>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace AbsolutePower {

struct InputBusDeviceInfo {
    std::uint32_t deviceIndex{};
    std::string persistentId;
    std::string productName;
    std::uint32_t buttonCount{};
    std::uint32_t povCount{};
};

struct InputBusRuntimeContextInfo {
    bool available{};
    bool isPilot{};
    bool gameplayActive{};
    bool targetingMode{};
    bool automaticPilotSignal{};
    std::int64_t outputAgeMs{-1};
};

class InputBusClient {
public:
    static InputBusClient& Get() noexcept;

    // Discover AbsoluteHOTAS_QueryInputBusApi(1) dynamically. Safe to call multiple times.
    bool Discover() noexcept;

    // Checks if the API is discovered and valid.
    [[nodiscard]] bool IsAvailable() const noexcept;

    // Binding capture bridge for Absolute Control Panel
    AbsoluteControlPanelApi::Result BeginCapture(const char* controlId) noexcept;
    AbsoluteControlPanelApi::Result PollCapture(
        const char* controlId,
        AbsoluteControlPanelApi::BindingCaptureV1* output) noexcept;
    AbsoluteControlPanelApi::Result CancelCapture(const char* controlId) noexcept;

    // Query connected devices
    std::vector<InputBusDeviceInfo> GetDevices() noexcept;
    std::string GetDeviceProductName(std::string_view persistentId) noexcept;

    // Format a joystick binding token using live device metadata if available
    std::string FormatBinding(std::string_view token) noexcept;

    // Polls snapshots for all devices and returns the preset IDs whose bound
    // joystick controls experienced a rising press edge on this tick.
    std::vector<std::string> PollPressedPresets(
        const std::vector<JoystickShortcut>& shortcuts) noexcept;

    // Context query
    InputBusRuntimeContextInfo GetRuntimeContext() noexcept;

private:
    InputBusClient() = default;

    struct DeviceTrackingState {
        std::uint64_t producerGeneration{};
        std::array<std::uint32_t, AbsoluteInputBusApi::kDigitalControlCount> pressCountBaseline{};
        bool initialized{};
    };

    std::atomic<const AbsoluteInputBusApi::ApiV1*> api_{};
    std::mutex captureMutex_;
    std::uint64_t activeSessionId_{};
    std::string activeControlId_;

    std::mutex trackingMutex_;
    std::unordered_map<std::string, DeviceTrackingState> trackingByPersistentId_;
};

} // namespace AbsolutePower
