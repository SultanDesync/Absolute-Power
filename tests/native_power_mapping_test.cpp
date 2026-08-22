#include "NativePowerBackend.h"

#include <cassert>

using namespace AbsolutePower;

int main() {
    // All six native power pools map to their canonical Absolute Power IDs.
    assert(ResearchLayout::SystemFromNativePoolSelector(0, 0) == SystemId::Weapon0);
    assert(ResearchLayout::SystemFromNativePoolSelector(0, 1) == SystemId::Weapon1);
    assert(ResearchLayout::SystemFromNativePoolSelector(0, 2) == SystemId::Weapon2);
    assert(ResearchLayout::SystemFromNativePoolSelector(1) == SystemId::Engine);
    assert(ResearchLayout::SystemFromNativePoolSelector(2) == SystemId::Shield);
    assert(ResearchLayout::SystemFromNativePoolSelector(3) == SystemId::GravDrive);
    assert(ResearchLayout::SystemFromNativePoolSelector(4) == SystemId::Invalid);

    assert(ResearchLayout::WeaponSystemFromPartOrder(0) == SystemId::Weapon0);
    assert(ResearchLayout::WeaponSystemFromPartOrder(1) == SystemId::Weapon1);
    assert(ResearchLayout::WeaponSystemFromPartOrder(2) == SystemId::Weapon2);
    assert(ResearchLayout::WeaponSystemFromPartOrder(3) == SystemId::Invalid);

    assert(ResearchLayout::EngineIdentityGateRva == 0x1F2FAC4);
    assert(ResearchLayout::ShieldIdentityGateRva == 0x1F2FA82);
    assert(ResearchLayout::GravDriveIdentityGateRva == 0x1F2FA40);
    assert(ResearchLayout::IdentityGateRva(SystemId::Engine) == 0x1F2FAC4);
    assert(ResearchLayout::IdentityGateRva(SystemId::Shield) == 0x1F2FA82);
    assert(ResearchLayout::IdentityGateRva(SystemId::GravDrive) == 0x1F2FA40);
    assert(ResearchLayout::IdentityGateRva(SystemId::Weapon0) == 0);
    assert(ResearchLayout::EngineIdentityGateRva !=
           ResearchLayout::ShieldIdentityGateRva);

    // The canonical IDs then drive every downstream config key and UI label.
    assert(SystemKey(ResearchLayout::SystemFromNativePoolSelector(0, 0)) == "Weapon0");
    assert(SystemKey(ResearchLayout::SystemFromNativePoolSelector(0, 1)) == "Weapon1");
    assert(SystemKey(ResearchLayout::SystemFromNativePoolSelector(0, 2)) == "Weapon2");
    assert(SystemLabel(ResearchLayout::SystemFromNativePoolSelector(1)) == "Engines");
    assert(SystemLabel(ResearchLayout::SystemFromNativePoolSelector(2)) == "Shields");
    assert(SystemLabel(ResearchLayout::SystemFromNativePoolSelector(3)) == "Grav Drive");
    assert(ResearchLayout::BaseTargetFromEffective(4, 1) == 3);
    assert(ResearchLayout::BaseTargetFromEffective(1, 1) == 0);
    assert(ResearchLayout::BaseTargetFromEffective(0, 1) == 0);
    return 0;
}
