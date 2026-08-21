#include "PowerGridEditing.h"

#include <cassert>

using AbsolutePower::PowerGridEditing::SegmentTier;

namespace {

void ExpectTier(const AbsolutePower::TierPlan& plan, std::size_t position,
                SegmentTier tier) {
    assert(AbsolutePower::PowerGridEditing::TierAt(plan, position) == tier);
}

} // namespace

int main() {
    using AbsolutePower::PowerGridEditing::SetSegmentTier;

    AbsolutePower::TierPlan interiorGreen{2, 0, 0};
    assert(SetSegmentTier(interiorGreen, 0, SegmentTier::Yellow, 12));
    assert(interiorGreen.green == 0 && interiorGreen.yellow == 2 &&
           interiorGreen.red == 0);
    ExpectTier(interiorGreen, 0, SegmentTier::Yellow);

    AbsolutePower::TierPlan interiorYellow{1, 2, 0};
    assert(SetSegmentTier(interiorYellow, 1, SegmentTier::Red, 12));
    assert(interiorYellow.green == 1 && interiorYellow.yellow == 0 &&
           interiorYellow.red == 2);
    ExpectTier(interiorYellow, 1, SegmentTier::Red);

    AbsolutePower::TierPlan interiorRed{1, 1, 2};
    assert(SetSegmentTier(interiorRed, 2, SegmentTier::Hollow, 12));
    assert(interiorRed.green == 1 && interiorRed.yellow == 1 &&
           interiorRed.red == 0);
    ExpectTier(interiorRed, 2, SegmentTier::Hollow);

    AbsolutePower::TierPlan distantHollow{1, 1, 1};
    assert(SetSegmentTier(distantHollow, 7, SegmentTier::Green, 12));
    assert(distantHollow.green == 8 && distantHollow.yellow == 0 &&
           distantHollow.red == 0);
    ExpectTier(distantHollow, 7, SegmentTier::Green);

    AbsolutePower::TierPlan full{4, 4, 4};
    assert(SetSegmentTier(full, 11, SegmentTier::Hollow, 12));
    assert(full.green == 4 && full.yellow == 4 && full.red == 3);
    assert(!SetSegmentTier(full, 12, SegmentTier::Green, 12));

    AbsolutePower::TierPlan invalid{12, 1, 0};
    assert(!SetSegmentTier(invalid, 0, SegmentTier::Yellow, 12));
    return 0;
}
