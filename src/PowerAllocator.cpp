#include "PowerAllocator.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <vector>

namespace AbsolutePower::PowerAllocator {
namespace {

std::array<std::uint16_t, kSystemCount>
BuildTierTargets(const Snapshot& snapshot, const Preset& preset, PriorityTier tier) {
    std::array<std::uint16_t, kSystemCount> targets{};
    for (std::size_t index = 0; index < kSystemCount; ++index) {
        const auto maximum = snapshot.systems[index].present
                                 ? static_cast<std::uint32_t>(snapshot.systems[index].maximum)
                                 : 0u;
        const auto& plan = preset.systems[index];
        const std::array<std::uint16_t, kTierCount> requested{plan.green, plan.yellow,
                                                              plan.red};
        std::uint32_t cumulative{};
        std::uint32_t accepted{};
        for (std::size_t tierIndex = 0; tierIndex <= static_cast<std::size_t>(tier);
             ++tierIndex) {
            cumulative += requested[tierIndex];
            accepted = std::min<std::uint32_t>(cumulative, maximum);
        }
        targets[index] = static_cast<std::uint16_t>(accepted);

    }
    return targets;
}

} // namespace

bool ValidateSnapshot(const Snapshot& snapshot) noexcept {
    std::uint32_t allocated{};
    for (const auto& system : snapshot.systems) {
        if (!system.present && (system.current != 0 || system.maximum != 0)) {
            return false;
        }
        if (system.current > system.maximum) {
            return false;
        }
        allocated += system.current;
    }
    return allocated + snapshot.available == snapshot.totalPower;
}

bool ValidatePreset(const Preset& preset) noexcept {
    if (preset.id.empty()) {
        return false;
    }
    std::array<bool, kSystemCount> seen{};
    for (const auto system : preset.tieBreakOrder) {
        const auto index = ToIndex(system);
        if (index >= kSystemCount || seen[index]) {
            return false;
        }
        seen[index] = true;
    }
    return true;
}

Allocation Allocate(const Snapshot& snapshot, const Preset& preset,
                    std::span<const Demand> demands) {
    Allocation result{};
    if (!ValidateSnapshot(snapshot)) {
        result.status = AllocationStatus::InvalidSnapshot;
        return result;
    }
    if (!ValidatePreset(preset)) {
        result.status = AllocationStatus::InvalidPreset;
        return result;
    }

    std::uint32_t remaining = snapshot.totalPower;
    std::uint32_t clippedPresetPips{};
    for (std::size_t index = 0; index < kSystemCount; ++index) {
        const auto& plan = preset.systems[index];
        const auto requested =
            static_cast<std::uint32_t>(plan.green) + plan.yellow + plan.red;
        const auto maximum = snapshot.systems[index].present
                                 ? static_cast<std::uint32_t>(snapshot.systems[index].maximum)
                                 : 0u;
        if (requested > maximum) clippedPresetPips += requested - maximum;
    }
    result.clippedPresetPips = static_cast<std::uint16_t>(std::min<std::uint32_t>(
        clippedPresetPips, std::numeric_limits<std::uint16_t>::max()));

    std::vector<Demand> orderedDemands(demands.begin(), demands.end());
    std::stable_sort(orderedDemands.begin(), orderedDemands.end(),
                     [](const Demand& left, const Demand& right) {
                         if (left.priority != right.priority) {
                             return left.priority > right.priority;
                         }
                         return ToIndex(left.system) < ToIndex(right.system);
                     });

    std::uint32_t unsatisfied{};
    for (const auto& demand : orderedDemands) {
        const auto index = ToIndex(demand.system);
        if (index >= kSystemCount || !snapshot.systems[index].present) {
            unsatisfied += demand.minimum;
            continue;
        }
        const auto maximum = snapshot.systems[index].maximum;
        const auto desired = std::min<std::uint16_t>(demand.minimum, maximum);
        if (demand.minimum > maximum) {
            unsatisfied += demand.minimum - maximum;
        }
        const auto already = result.target[index];
        if (desired <= already) {
            continue;
        }
        const auto needed = static_cast<std::uint32_t>(desired - already);
        const auto assigned = std::min(needed, remaining);
        result.target[index] = static_cast<std::uint16_t>(already + assigned);
        remaining -= assigned;
        unsatisfied += needed - assigned;
    }

    for (const auto tier : {PriorityTier::Green, PriorityTier::Yellow, PriorityTier::Red}) {
        auto desired = BuildTierTargets(snapshot, preset, tier);
        bool needsMore = true;
        while (remaining > 0 && needsMore) {
            needsMore = false;
            for (const auto system : preset.tieBreakOrder) {
                const auto index = ToIndex(system);
                if (result.target[index] < desired[index]) {
                    ++result.target[index];
                    --remaining;
                    needsMore = true;
                    if (remaining == 0) {
                        break;
                    }
                }
            }
        }

        const bool tierComplete = std::ranges::equal(
            result.target, desired, [](std::uint16_t actual, std::uint16_t wanted) {
                return actual >= wanted;
            });
        if (!tierComplete) {
            break;
        }
    }

    result.unassigned = static_cast<std::uint16_t>(remaining);
    result.unsatisfiedDemandPips = static_cast<std::uint16_t>(
        std::min<std::uint32_t>(unsatisfied, std::numeric_limits<std::uint16_t>::max()));
    return result;
}

std::vector<PowerChange> PlanChanges(const Snapshot& snapshot, const Allocation& allocation) {
    std::vector<PowerChange> changes;
    if (allocation.status != AllocationStatus::Ok || !ValidateSnapshot(snapshot)) {
        return changes;
    }
    changes.reserve(kSystemCount);
    for (const auto system : kCockpitOrder) {
        const auto index = ToIndex(system);
        const auto current = snapshot.systems[index].current;
        const auto target = allocation.target[index];
        if (target < current) {
            changes.push_back({system, current, target, ChangePhase::Release});
        }
    }
    for (const auto system : kCockpitOrder) {
        const auto index = ToIndex(system);
        const auto current = snapshot.systems[index].current;
        const auto target = allocation.target[index];
        if (target > current) {
            changes.push_back({system, current, target, ChangePhase::Assign});
        }
    }
    return changes;
}

} // namespace AbsolutePower::PowerAllocator
