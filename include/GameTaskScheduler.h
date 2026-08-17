#pragma once

namespace GameTaskScheduler {

// Queue at most one one-shot SFSE game task. Safe to call from the render/UI
// thread and AbsoluteHOTAS's DirectInput polling thread.
void Request() noexcept;

} // namespace GameTaskScheduler
