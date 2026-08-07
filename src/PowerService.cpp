#include "PowerService.h"

namespace AbsolutePower {

BackendResult PowerService::Capture(Snapshot& snapshot) {
    std::scoped_lock lock(mutex_);
    return backend_.Capture(snapshot);
}

ApplyResult PowerService::ApplyPreset(const Preset& preset, std::span<const Demand> demands) {
    std::scoped_lock lock(mutex_);
    ApplyResult result{};
    Snapshot snapshot{};
    result.backend = backend_.Capture(snapshot);
    if (result.backend != BackendResult::Ok) {
        return result;
    }

    result.allocation = PowerAllocator::Allocate(snapshot, preset, demands);
    if (result.allocation.status != AllocationStatus::Ok) {
        result.backend = BackendResult::InvalidRequest;
        return result;
    }

    const auto changes = PowerAllocator::PlanChanges(snapshot, result.allocation);
    result.totalChanges = changes.size();
    for (const auto& change : changes) {
        result.backend = backend_.SetPower(change.system, change.to);
        if (result.backend != BackendResult::Ok) {
            return result;
        }
        ++result.completedChanges;
    }
    return result;
}

} // namespace AbsolutePower
