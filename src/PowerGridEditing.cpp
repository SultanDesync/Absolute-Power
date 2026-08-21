#include "PowerGridEditing.h"

#include <algorithm>
#include <limits>

namespace AbsolutePower::PowerGridEditing {

SegmentTier TierAt(const TierPlan& plan, std::size_t position) noexcept {
    const auto greenEnd = static_cast<std::size_t>(plan.green);
    const auto yellowEnd = greenEnd + plan.yellow;
    const auto redEnd = yellowEnd + plan.red;
    if (position < greenEnd) return SegmentTier::Green;
    if (position < yellowEnd) return SegmentTier::Yellow;
    if (position < redEnd) return SegmentTier::Red;
    return SegmentTier::Hollow;
}

bool SetSegmentTier(TierPlan& plan, std::size_t position,
                    SegmentTier targetTier,
                    std::uint16_t maximumSegments) noexcept {
    if (maximumSegments == 0 || position >= maximumSegments) return false;

    std::size_t greenEnd = plan.green;
    std::size_t yellowEnd = greenEnd + plan.yellow;
    std::size_t redEnd = yellowEnd + plan.red;
    if (redEnd > maximumSegments) return false;

    switch (targetTier) {
    case SegmentTier::Hollow:
        redEnd = (std::min)(redEnd, position);
        yellowEnd = (std::min)(yellowEnd, redEnd);
        greenEnd = (std::min)(greenEnd, yellowEnd);
        break;
    case SegmentTier::Green:
        greenEnd = (std::max)(greenEnd, position + 1);
        yellowEnd = (std::max)(yellowEnd, greenEnd);
        redEnd = (std::max)(redEnd, yellowEnd);
        break;
    case SegmentTier::Yellow:
        greenEnd = (std::min)(greenEnd, position);
        yellowEnd = (std::max)(yellowEnd, position + 1);
        redEnd = (std::max)(redEnd, yellowEnd);
        break;
    case SegmentTier::Red:
        yellowEnd = (std::min)(yellowEnd, position);
        greenEnd = (std::min)(greenEnd, yellowEnd);
        redEnd = (std::max)(redEnd, position + 1);
        break;
    }

    if (redEnd > maximumSegments ||
        redEnd > (std::numeric_limits<std::uint16_t>::max)()) return false;
    plan.green = static_cast<std::uint16_t>(greenEnd);
    plan.yellow = static_cast<std::uint16_t>(yellowEnd - greenEnd);
    plan.red = static_cast<std::uint16_t>(redEnd - yellowEnd);
    return TierAt(plan, position) == targetTier;
}

} // namespace AbsolutePower::PowerGridEditing
