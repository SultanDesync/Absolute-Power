#include "Automation.h"

#include <cassert>
#include <limits>
#include <vector>

using namespace AbsolutePower;

namespace {

Snapshot MakeSnapshot() {
    Snapshot snapshot{};
    snapshot.pilotReady = true;
    snapshot.totalPower = 10;
    snapshot.available = 10;
    snapshot.systems[ToIndex(SystemId::Weapon0)] = {true, 0, 4};
    snapshot.systems[ToIndex(SystemId::Engine)] = {true, 0, 4};
    snapshot.systems[ToIndex(SystemId::Shield)] = {true, 0, 6};
    return snapshot;
}

std::vector<AutomationRule> MakeRules() {
    return {
        {.id = "weapon",
         .displayName = "Weapon",
         .enabled = true,
         .trigger = TriggerKind::WeaponFired,
         .sourceSystem = SystemId::Weapon0,
         .targetSystem = SystemId::Weapon0,
         .targetPips = std::numeric_limits<std::uint16_t>::max(),
         .holdMilliseconds = 1000,
         .priority = 200},
        {.id = "damage",
         .displayName = "Damage",
         .enabled = true,
         .trigger = TriggerKind::IncomingDamage,
         .targetSystem = SystemId::Shield,
         .targetPips = 5,
         .holdMilliseconds = 2000,
         .priority = 300},
        {.id = "throttle",
         .displayName = "Throttle",
         .enabled = true,
         .trigger = TriggerKind::ThrottleAbove,
         .targetSystem = SystemId::Engine,
         .targetPips = std::numeric_limits<std::uint16_t>::max(),
         .thresholdPercent = 50,
         .hysteresisPercent = 10,
         .priority = 150},
    };
}

bool HasDemand(const std::vector<Demand>& demands, std::string_view id,
               std::uint16_t minimum) {
    for (const auto& demand : demands) {
        if (demand.sourceId == id && demand.minimum == minimum) return true;
    }
    return false;
}

} // namespace

int main() {
    AutomationEngine engine;
    engine.SetRules(MakeRules());
    engine.SetEnabled(true);
    const auto snapshot = MakeSnapshot();

    engine.RecordWeaponFire(SystemId::Weapon0, 1000);
    assert(HasDemand(engine.ActiveDemands(snapshot, 1500), "weapon", 4));
    assert(!HasDemand(engine.ActiveDemands(snapshot, 2001), "weapon", 4));

    engine.RecordIncomingDamage(2000);
    assert(HasDemand(engine.ActiveDemands(snapshot, 3500), "damage", 5));

    engine.SetThrottlePercent(55.0F, 4000);
    assert(HasDemand(engine.ActiveDemands(snapshot, 4000), "throttle", 4));
    engine.SetThrottlePercent(45.0F, 4100);
    assert(HasDemand(engine.ActiveDemands(snapshot, 4100), "throttle", 4));
    engine.SetThrottlePercent(40.0F, 4200);
    assert(!HasDemand(engine.ActiveDemands(snapshot, 4200), "throttle", 4));

    engine.SetEnabled(false);
    assert(engine.ActiveDemands(snapshot, 4200).empty());
    engine.SetEnabled(true);
    assert(engine.ActiveDemands(snapshot, 4200).empty());
    return 0;
}
