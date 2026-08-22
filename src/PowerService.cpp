#include "PowerService.h"

#include <numeric>

namespace AbsolutePower {

BackendResult PowerService::Capture(Snapshot& snapshot) {
    std::scoped_lock lock(mutex_);
    return backend_.Capture(snapshot);
}

ApplyResult PowerService::ApplyPreset(const Preset& preset, std::span<const Demand> demands,
                                      CrewBonusMode crewBonusMode) {
    std::scoped_lock lock(mutex_);
    ApplyResult result{};
    Snapshot snapshot{};
    result.backend = backend_.Capture(snapshot);
    if (result.backend != BackendResult::Ok) {
        return result;
    }

    result.allocation = PowerAllocator::Allocate(
        snapshot, preset, demands, crewBonusMode);
    if (result.allocation.status != AllocationStatus::Ok) {
        result.backend = BackendResult::InvalidRequest;
        return result;
    }

    const auto changes = PowerAllocator::PlanChanges(snapshot, result.allocation);
    result.totalChanges = std::accumulate(
        changes.begin(), changes.end(), std::size_t{},
        [](std::size_t total, const PowerChange& change) {
            const auto delta = change.to > change.from
                                   ? change.to - change.from
                                   : change.from - change.to;
            return total + delta;
        });
    if (changes.empty()) return result;

    // Only a one-pip absolute mutation has a causal in-game/HUD validation.
    // Apply one such step per game-update interval, then let the next call
    // re-capture native state before deciding whether to continue. This also
    // preserves the releases-before-assignments ordering produced above.
    const auto& change = changes.front();
    const auto next = static_cast<std::uint16_t>(
        change.phase == ChangePhase::Release ? change.from - 1 : change.from + 1);
    result.backend = backend_.SetPower(change.system, next);
    if (result.backend == BackendResult::Ok) result.completedChanges = 1;
    return result;
}

} // namespace AbsolutePower
