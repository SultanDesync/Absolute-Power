#pragma once

namespace GameUpdateHook {

// Install the exact Starfield 1.16.244.0 flight-handler output hook used by the
// accepted Power research. Queued plans are drained after the native output
// method while the live ship component is owned by a game-update thread.
bool Install() noexcept;

} // namespace GameUpdateHook
