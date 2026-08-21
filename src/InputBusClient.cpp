#include "PCH.h"

#include "InputBusClient.h"
#include "RuntimePaths.h"

#include <algorithm>
#include <cstring>
#include <span>

namespace AbsolutePower {
namespace {

using QueryInputBus = const AbsoluteInputBusApi::ApiV1* (__cdecl*)(std::uint32_t) noexcept;

template <std::size_t N>
void CopyBuffer(char (&target)[N], std::string_view source) noexcept {
    const auto count = (std::min)(source.size(), N - 1);
    std::memcpy(target, source.data(), count);
    target[count] = '\0';
}

std::string ToLower(std::string_view value) {
    std::string result(value);
    std::ranges::transform(result, result.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return result;
}

} // namespace

InputBusClient& InputBusClient::Get() noexcept {
    static InputBusClient instance;
    return instance;
}

bool InputBusClient::Discover() noexcept {
    if (api_.load(std::memory_order_acquire) != nullptr) return true;
    const HMODULE hotas = GetModuleHandleW(L"AbsoluteHOTAS.dll");
    if (!hotas) return false;

    const FARPROC address = GetProcAddress(hotas, "AbsoluteHOTAS_QueryInputBusApi");
    if (!address) return false;

    const auto query = reinterpret_cast<QueryInputBus>(address);
    const auto* api = query(AbsoluteInputBusApi::kAbiVersion);
    if (!api) return false;

    constexpr std::size_t required =
        offsetof(AbsoluteInputBusApi::ApiV1, cancelCapture) +
        sizeof(api->cancelCapture);
    if (api->structSize < required ||
        api->abiVersion != AbsoluteInputBusApi::kAbiVersion ||
        !api->providerId ||
        std::string_view(api->providerId) != "absolute.hotas.input-bus" ||
        !api->getDeviceCount || !api->getDevice || !api->getSnapshot ||
        !api->beginCapture || !api->pollCapture || !api->cancelCapture) {
        return false;
    }

    api_.store(api, std::memory_order_release);
    RuntimePaths::Log("InputBus", "Connected to AbsoluteHOTAS Input Bus (ABI v1).", true);
    return true;
}

bool InputBusClient::IsAvailable() const noexcept {
    return api_.load(std::memory_order_acquire) != nullptr;
}

AbsoluteControlPanelApi::Result InputBusClient::BeginCapture(const char* controlId) noexcept {
    if (!Discover()) return AbsoluteControlPanelApi::Result::NotReady;
    const auto* api = api_.load(std::memory_order_acquire);
    if (!api) return AbsoluteControlPanelApi::Result::NotReady;

    std::scoped_lock lock(captureMutex_);
    if (activeSessionId_ != 0) {
        api->cancelCapture(activeSessionId_);
        activeSessionId_ = 0;
        activeControlId_.clear();
    }

    AbsoluteInputBusApi::CaptureRequestV1 request{};
    request.allowedControls = AbsoluteInputBusApi::kCaptureDigital; // buttons & POVs for presets
    request.settleMilliseconds = 50;
    request.timeoutMilliseconds = 8000;
    CopyBuffer(request.consumerId, "absolute.power");

    std::uint64_t session = 0;
    const auto res = api->beginCapture(&request, &session);
    if (res != AbsoluteInputBusApi::Result::Ok) {
        if (res == AbsoluteInputBusApi::Result::Busy) return AbsoluteControlPanelApi::Result::Rejected;
        if (res == AbsoluteInputBusApi::Result::NotReady) return AbsoluteControlPanelApi::Result::NotReady;
        return AbsoluteControlPanelApi::Result::InvalidArgument;
    }

    activeSessionId_ = session;
    activeControlId_ = controlId ? controlId : "";
    RuntimePaths::Log("InputBus", std::format("Controller capture session {} started for Absolute Power.", session), true);
    return AbsoluteControlPanelApi::Result::Ok;
}

AbsoluteControlPanelApi::Result InputBusClient::PollCapture(
    const char* controlId,
    AbsoluteControlPanelApi::BindingCaptureV1* output) noexcept {
    if (!output || output->structSize < sizeof(AbsoluteControlPanelApi::BindingCaptureV1)) {
        return AbsoluteControlPanelApi::Result::InvalidArgument;
    }
    const auto* api = api_.load(std::memory_order_acquire);
    if (!api) {
        *output = {};
        output->state = AbsoluteControlPanelApi::BindingCaptureState::Error;
        CopyBuffer(output->detail, "AbsoluteHOTAS Input Bus unavailable");
        return AbsoluteControlPanelApi::Result::NotReady;
    }

    std::scoped_lock lock(captureMutex_);
    if (activeSessionId_ == 0 || activeControlId_ != (controlId ? controlId : "")) {
        *output = {};
        output->state = AbsoluteControlPanelApi::BindingCaptureState::Idle;
        return AbsoluteControlPanelApi::Result::Ok;
    }

    AbsoluteInputBusApi::CaptureResultV1 result{};
    const auto pollRes = api->pollCapture(activeSessionId_, &result);
    if (pollRes != AbsoluteInputBusApi::Result::Ok) {
        if (pollRes == AbsoluteInputBusApi::Result::StaleSession) {
            activeSessionId_ = 0;
            activeControlId_.clear();
            *output = {};
            output->state = AbsoluteControlPanelApi::BindingCaptureState::Cancelled;
            CopyBuffer(output->detail, "Capture session expired");
            return AbsoluteControlPanelApi::Result::Ok;
        }
        return AbsoluteControlPanelApi::Result::Rejected;
    }

    *output = {};
    switch (result.state) {
    case AbsoluteInputBusApi::CaptureState::Idle:
        output->state = AbsoluteControlPanelApi::BindingCaptureState::Idle;
        break;
    case AbsoluteInputBusApi::CaptureState::Capturing:
        output->state = AbsoluteControlPanelApi::BindingCaptureState::Capturing;
        CopyBuffer(output->detail, result.detail);
        break;
    case AbsoluteInputBusApi::CaptureState::Captured:
        output->state = AbsoluteControlPanelApi::BindingCaptureState::Captured;
        CopyBuffer(output->binding, result.binding.bindingText);
        CopyBuffer(output->detail, result.detail);
        activeSessionId_ = 0;
        activeControlId_.clear();
        RuntimePaths::Log("InputBus", std::format("Controller capture completed with {}", result.binding.bindingText), true);
        break;
    case AbsoluteInputBusApi::CaptureState::Cancelled:
        output->state = AbsoluteControlPanelApi::BindingCaptureState::Cancelled;
        CopyBuffer(output->detail, result.detail);
        activeSessionId_ = 0;
        activeControlId_.clear();
        break;
    case AbsoluteInputBusApi::CaptureState::TimedOut:
        output->state = AbsoluteControlPanelApi::BindingCaptureState::TimedOut;
        CopyBuffer(output->detail, result.detail);
        activeSessionId_ = 0;
        activeControlId_.clear();
        break;
    case AbsoluteInputBusApi::CaptureState::Error:
        output->state = AbsoluteControlPanelApi::BindingCaptureState::Error;
        CopyBuffer(output->detail, result.detail);
        activeSessionId_ = 0;
        activeControlId_.clear();
        break;
    }
    return AbsoluteControlPanelApi::Result::Ok;
}

AbsoluteControlPanelApi::Result InputBusClient::CancelCapture(const char*) noexcept {
    const auto* api = api_.load(std::memory_order_acquire);
    std::scoped_lock lock(captureMutex_);
    if (activeSessionId_ != 0 && api) {
        api->cancelCapture(activeSessionId_);
    }
    activeSessionId_ = 0;
    activeControlId_.clear();
    return AbsoluteControlPanelApi::Result::Ok;
}

std::vector<InputBusDeviceInfo> InputBusClient::GetDevices() noexcept {
    std::vector<InputBusDeviceInfo> result;
    const auto* api = api_.load(std::memory_order_acquire);
    if (!api) return result;

    const auto count = api->getDeviceCount();
    result.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        AbsoluteInputBusApi::DeviceInfoV1 info{};
        if (api->getDevice(i, &info) == AbsoluteInputBusApi::Result::Ok) {
            result.push_back({
                .deviceIndex = info.deviceIndex,
                .persistentId = std::string(info.persistentId),
                .productName = std::string(info.productName),
                .buttonCount = info.buttonCount,
                .povCount = info.povCount,
            });
        }
    }
    return result;
}

std::string InputBusClient::GetDeviceProductName(std::string_view persistentId) noexcept {
    const auto* api = api_.load(std::memory_order_acquire);
    if (!api) return {};

    const auto count = api->getDeviceCount();
    const auto target = ToLower(persistentId);
    for (std::uint32_t i = 0; i < count; ++i) {
        AbsoluteInputBusApi::DeviceInfoV1 info{};
        if (api->getDevice(i, &info) == AbsoluteInputBusApi::Result::Ok) {
            if (ToLower(info.persistentId) == target) {
                return std::string(info.productName);
            }
        }
    }
    return {};
}

std::string InputBusClient::FormatBinding(std::string_view token) noexcept {
    const auto parsed = JoystickBindingPolicy::Parse(token);
    if (!parsed) return std::string(token);
    const auto productName = GetDeviceProductName(parsed->persistentId);
    return JoystickBindingPolicy::FormatDisplay(token, productName);
}

std::vector<std::string> InputBusClient::PollPressedPresets(
    const std::vector<JoystickShortcut>& shortcuts) noexcept {
    std::vector<std::string> triggered;
    if (shortcuts.empty()) return triggered;

    const auto* api = api_.load(std::memory_order_acquire);
    if (!api) return triggered;

    // Parse active shortcut bindings
    struct ActiveTarget {
        std::string presetId;
        std::string persistentIdLower;
        std::uint32_t channelIndex{};
    };
    std::vector<ActiveTarget> targets;
    targets.reserve(shortcuts.size());
    for (const auto& s : shortcuts) {
        const auto parsed = JoystickBindingPolicy::Parse(s.token);
        if (parsed) {
            targets.push_back({
                .presetId = s.presetId,
                .persistentIdLower = ToLower(parsed->persistentId),
                .channelIndex = parsed->channelIndex,
            });
        }
    }
    if (targets.empty()) return triggered;

    std::scoped_lock lock(trackingMutex_);
    const auto deviceCount = api->getDeviceCount();

    for (std::uint32_t devIdx = 0; devIdx < deviceCount; ++devIdx) {
        AbsoluteInputBusApi::DeviceInfoV1 info{};
        if (api->getDevice(devIdx, &info) != AbsoluteInputBusApi::Result::Ok) continue;

        const auto devKey = ToLower(info.persistentId);
        AbsoluteInputBusApi::DeviceSnapshotV1 snapshot{};
        if (api->getSnapshot(devIdx, &snapshot) != AbsoluteInputBusApi::Result::Ok ||
            !snapshot.connected) {
            continue;
        }

        auto& tracking = trackingByPersistentId_[devKey];
        if (!tracking.initialized ||
            tracking.producerGeneration != snapshot.producerGeneration) {
            // Rebase baseline on first frame or producer generation change
            tracking.producerGeneration = snapshot.producerGeneration;
            std::ranges::copy(snapshot.pressCount, tracking.pressCountBaseline.begin());
            tracking.initialized = true;
            continue;
        }

        // Check for any target matching this device
        for (const auto& target : targets) {
            if (target.persistentIdLower != devKey ||
                target.channelIndex >= AbsoluteInputBusApi::kDigitalControlCount) {
                continue;
            }

            const auto currentCount = snapshot.pressCount[target.channelIndex];
            const auto baselineCount = tracking.pressCountBaseline[target.channelIndex];
            if (currentCount != baselineCount) {
                triggered.push_back(target.presetId);
            }
        }

        // Update tracking baseline to snapshot
        std::ranges::copy(snapshot.pressCount, tracking.pressCountBaseline.begin());
    }

    return triggered;
}

InputBusRuntimeContextInfo InputBusClient::GetRuntimeContext() noexcept {
    InputBusRuntimeContextInfo info{};
    const auto* api = api_.load(std::memory_order_acquire);
    if (!api || !api->getRuntimeContext) return info;

    AbsoluteInputBusApi::RuntimeContextV1 context{};
    if (api->getRuntimeContext(&context) != AbsoluteInputBusApi::Result::Ok) {
        return info;
    }

    info.available = true;
    info.automaticPilotSignal =
        (context.sourceFlags & AbsoluteInputBusApi::kContextSourceAutomaticPilot) != 0;
    info.outputAgeMs = context.selectedOutputAgeMilliseconds;

    if (context.context != AbsoluteInputBusApi::RuntimeContext::Suspended) {
        if ((context.validSignals & AbsoluteInputBusApi::kContextSignalIsPilot) != 0) {
            info.isPilot = (context.activeSignals & AbsoluteInputBusApi::kContextSignalIsPilot) != 0;
        }
        if ((context.validSignals & AbsoluteInputBusApi::kContextSignalGameplayActive) != 0) {
            info.gameplayActive = (context.activeSignals & AbsoluteInputBusApi::kContextSignalGameplayActive) != 0;
        }
        if ((context.validSignals & AbsoluteInputBusApi::kContextSignalTargetingMode) != 0) {
            info.targetingMode = (context.activeSignals & AbsoluteInputBusApi::kContextSignalTargetingMode) != 0;
        }
    }
    return info;
}

} // namespace AbsolutePower
