#include "PowerAllocator.h"

#include <cassert>
#include <span>

using namespace AbsolutePower;

namespace {

Snapshot MakeSnapshot(std::uint16_t totalPower) {
    Snapshot snapshot{};
    snapshot.pilotReady = true;
    snapshot.totalPower = totalPower;
    snapshot.available = totalPower;
    snapshot.systems[ToIndex(SystemId::Weapon0)] = {true, 0, 4};
    snapshot.systems[ToIndex(SystemId::Engine)] = {true, 0, 4};
    snapshot.systems[ToIndex(SystemId::Shield)] = {true, 0, 6};
    return snapshot;
}

Preset MakePreset() {
    Preset preset{.id = "Test", .displayName = "Test"};
    preset.tieBreakOrder = {SystemId::Shield, SystemId::Engine, SystemId::Weapon0,
                            SystemId::Weapon1, SystemId::Weapon2, SystemId::GravDrive};
    preset.systems[ToIndex(SystemId::Shield)].green = 2;
    preset.systems[ToIndex(SystemId::Engine)].green = 2;
    preset.systems[ToIndex(SystemId::Weapon0)].yellow = 3;
    return preset;
}

void StrictTierBarrierAndRoundRobin() {
    const auto preset = MakePreset();
    const auto allocation = PowerAllocator::Allocate(MakeSnapshot(5), preset);
    assert(allocation.status == AllocationStatus::Ok);
    assert(allocation.target[ToIndex(SystemId::Shield)] == 2);
    assert(allocation.target[ToIndex(SystemId::Engine)] == 2);
    assert(allocation.target[ToIndex(SystemId::Weapon0)] == 1);

    const auto partialGreen = PowerAllocator::Allocate(MakeSnapshot(3), preset);
    assert(partialGreen.target[ToIndex(SystemId::Shield)] == 2);
    assert(partialGreen.target[ToIndex(SystemId::Engine)] == 1);
    assert(partialGreen.target[ToIndex(SystemId::Weapon0)] == 0);
}

void AutomationDemandsPrecedeGreen() {
    const auto snapshot = MakeSnapshot(5);
    const auto preset = MakePreset();
    const Demand demand{"weapon-response", SystemId::Weapon0, 3, 200};
    const auto allocation =
        PowerAllocator::Allocate(snapshot, preset, std::span<const Demand>(&demand, 1));
    assert(allocation.target[ToIndex(SystemId::Weapon0)] == 3);
    assert(allocation.target[ToIndex(SystemId::Shield)] == 1);
    assert(allocation.target[ToIndex(SystemId::Engine)] == 1);
    assert(allocation.unsatisfiedDemandPips == 0);
}

void MissingSystemsAreClipped() {
    auto preset = MakePreset();
    preset.systems[ToIndex(SystemId::Weapon2)].green = 4;
    const auto allocation = PowerAllocator::Allocate(MakeSnapshot(5), preset);
    assert(allocation.target[ToIndex(SystemId::Weapon2)] == 0);
    assert(allocation.clippedPresetPips == 4);
}

void ReleasesArePlannedBeforeAssignments() {
    auto snapshot = MakeSnapshot(4);
    snapshot.available = 1;
    snapshot.systems[ToIndex(SystemId::Weapon0)].current = 3;
    Allocation allocation{};
    allocation.target[ToIndex(SystemId::Shield)] = 4;
    const auto changes = PowerAllocator::PlanChanges(snapshot, allocation);
    assert(changes.size() == 2);
    assert(changes[0].phase == ChangePhase::Release);
    assert(changes[0].system == SystemId::Weapon0);
    assert(changes[1].phase == ChangePhase::Assign);
    assert(changes[1].system == SystemId::Shield);
}

} // namespace

int main() {
    StrictTierBarrierAndRoundRobin();
    AutomationDemandsPrecedeGreen();
    MissingSystemsAreClipped();
    ReleasesArePlannedBeforeAssignments();
    return 0;
}
