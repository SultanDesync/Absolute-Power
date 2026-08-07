#include "Configuration.h"

#include <cassert>
#include <filesystem>
#include <limits>
#include <string_view>

using namespace AbsolutePower;

int main() {
    const auto configuration = Configuration::Load(
        std::filesystem::path("config") / "AbsolutePower.ini",
        std::filesystem::path("tests") / "missing-custom.ini");
    assert(configuration.presets.size() == 3);
    assert(configuration.rules.size() == 3);
    assert(configuration.startupPreset == "Balanced");
    assert(!configuration.automationEnabled);

    const auto& balanced = configuration.presets.front();
    assert(balanced.id == "Balanced");
    const auto weapon = balanced.systems[ToIndex(SystemId::Weapon0)];
    assert(weapon.green == 1 && weapon.yellow == 2 && weapon.red == 2);
    assert(balanced.tieBreakOrder.front() == SystemId::Shield);

    const auto& incomingFire = configuration.rules[1];
    assert(incomingFire.id == "IncomingFire");
    assert(incomingFire.trigger == TriggerKind::IncomingDamage);
    assert(incomingFire.targetSystem == SystemId::Shield);
    assert(incomingFire.targetPips == std::numeric_limits<std::uint16_t>::max());
    assert(!incomingFire.enabled);
    return 0;
}
