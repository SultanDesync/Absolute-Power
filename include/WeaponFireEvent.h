#pragma once

#include "PowerTypes.h"

#include <cmath>
#include <cstdint>
#include <optional>

namespace AbsolutePower {

// The validated WeaponGroup listener stores its zero-based group selector on
// the listener object and receives a ButtonEvent level value. Keep the native
// memory read in the hook and the event classification here so the boundary is
// small, deterministic, and independently testable.
[[nodiscard]] inline std::optional<SystemId> DecodeWeaponFireEvent(
    std::uint32_t groupIndex, float value) noexcept {
    if (groupIndex >= 3 || !std::isfinite(value) || value <= 0.0F) {
        return std::nullopt;
    }
    return static_cast<SystemId>(groupIndex);
}

} // namespace AbsolutePower
