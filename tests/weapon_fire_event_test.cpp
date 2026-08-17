#include "WeaponFireEvent.h"

#include <cassert>
#include <limits>

using namespace AbsolutePower;

int main() {
    assert(DecodeWeaponFireEvent(0, 1.0F) == SystemId::Weapon0);
    assert(DecodeWeaponFireEvent(1, 0.25F) == SystemId::Weapon1);
    assert(DecodeWeaponFireEvent(2, 1.0F) == SystemId::Weapon2);
    assert(!DecodeWeaponFireEvent(3, 1.0F));
    assert(!DecodeWeaponFireEvent(0, 0.0F));
    assert(!DecodeWeaponFireEvent(0, -1.0F));
    assert(!DecodeWeaponFireEvent(
        0, std::numeric_limits<float>::quiet_NaN()));
    return 0;
}
