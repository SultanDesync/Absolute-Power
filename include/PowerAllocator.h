#pragma once

#include "PowerTypes.h"

#include <span>
#include <vector>

namespace AbsolutePower::PowerAllocator {

[[nodiscard]] bool ValidateSnapshot(const Snapshot& snapshot) noexcept;
[[nodiscard]] bool ValidatePreset(const Preset& preset) noexcept;

// Emergency demands are allocated before Green. Base preset tiers are then
// filled in strict Green -> Yellow -> Red barriers. A tier may be partial when
// reactor output is insufficient; the next tier never receives power in that case.
[[nodiscard]] Allocation Allocate(const Snapshot& snapshot, const Preset& preset,
                                  std::span<const Demand> demands = {});

// Native absolute setters are planned releases-first so reallocations never
// transiently request more reactor output than the ship owns.
[[nodiscard]] std::vector<PowerChange> PlanChanges(const Snapshot& snapshot,
                                                   const Allocation& allocation);

} // namespace AbsolutePower::PowerAllocator
