#pragma once

namespace SuiteHost {

enum class Selection {
    Workbench,
    Hotas,
    Suppressed,
    Unavailable,
    Incompatible,
};

// Accept either optional frontend/input provider without gating the Power runtime.
[[nodiscard]] Selection Select() noexcept;

// Power continues to own keyboard shortcut execution. An active Control host
// suppresses and reseeds those edges while its menu or capture UI owns input.
[[nodiscard]] bool KeyboardInputSuppressed() noexcept;

} // namespace SuiteHost
