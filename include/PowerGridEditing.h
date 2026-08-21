#pragma once

#include "PowerTypes.h"

#include <cstddef>
#include <cstdint>

namespace AbsolutePower::PowerGridEditing {

enum class SegmentTier : std::uint8_t {
    Hollow,
    Green,
    Yellow,
    Red,
};

// Rebuilds the three aggregate tier counts around one visual pip. The selected
// position is guaranteed to acquire targetTier while all positions remain in
// canonical Green -> Yellow -> Red -> Hollow order.
[[nodiscard]] bool SetSegmentTier(TierPlan& plan, std::size_t position,
                                  SegmentTier targetTier,
                                  std::uint16_t maximumSegments) noexcept;

[[nodiscard]] SegmentTier TierAt(const TierPlan& plan,
                                 std::size_t position) noexcept;

} // namespace AbsolutePower::PowerGridEditing
