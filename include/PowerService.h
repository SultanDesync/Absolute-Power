#pragma once

#include "PowerAllocator.h"
#include "PowerBackend.h"

#include <mutex>
#include <span>
#include <vector>

namespace AbsolutePower {

struct ApplyResult {
    BackendResult backend{BackendResult::Ok};
    Allocation allocation{};
    std::size_t completedChanges{};
    std::size_t totalChanges{};
};

class PowerService {
public:
    explicit PowerService(IPowerBackend& backend) : backend_(backend) {}

    BackendResult Capture(Snapshot& snapshot);
    ApplyResult ApplyPreset(
        const Preset& preset, std::span<const Demand> demands = {},
        CrewBonusMode crewBonusMode = CrewBonusMode::Additive);

private:
    IPowerBackend& backend_;
    std::mutex mutex_;
};

} // namespace AbsolutePower
