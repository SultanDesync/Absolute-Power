#pragma once

#include "AbsoluteControlPanelAPI.h"

namespace ControlPanelSubscriber {

// Safe to call more than once. NotReady remains retryable; host absence and
// rejection never prevent the Power runtime from initializing.
[[nodiscard]] AbsoluteControlPanelApi::Result RegisterDiscoveredHost() noexcept;
[[nodiscard]] bool IsHosted() noexcept;
[[nodiscard]] bool IsMenuOpen() noexcept;
[[nodiscard]] bool IsInputCaptureActive() noexcept;
// Rebuilds the immutable low-rate Power grid frame outside the UI read
// callback. Safe to call from the validated game-update/task paths.
void PublishLiveState() noexcept;

} // namespace ControlPanelSubscriber
