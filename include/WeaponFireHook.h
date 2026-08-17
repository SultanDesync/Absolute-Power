#pragma once

namespace WeaponFireHook {

// Observe Starfield's exact-gated WeaponGroup ButtonEvent listener. This is a
// read-only source: it never injects input or invokes a weapon operation.
bool Install() noexcept;
[[nodiscard]] bool Ready() noexcept;

} // namespace WeaponFireHook
