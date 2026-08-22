#pragma once

#include "PowerBackend.h"

#include <cstdint>

namespace AbsolutePower {

// Production seam boundary. Bootstrap validates the recovered 1.16.244.0
// setter family but deliberately does not call it until the registry lookup,
// equipment ownership, and game-thread lifetime sequence is promoted intact.
class NativePowerBackend final : public IPowerBackend {
public:
    bool Initialize();
    [[nodiscard]] bool SetterSignaturesReady() const noexcept;

    BackendResult Capture(Snapshot& snapshot) override;
    BackendResult SetPower(SystemId system, std::uint16_t targetPips) override;

private:
    bool setterSignaturesReady_{};
    std::uintptr_t module_{};
};

namespace ResearchLayout {
inline constexpr std::uintptr_t EquipmentDescriptorGlobalRva = 0x5F2FAF8;
// Observed research-session value only. The native descriptor field is
// dynamically assigned and must be read live for each game process.
inline constexpr std::uint32_t PowerEquipmentTypeId = 260072;
inline constexpr std::uintptr_t SetAbsolutePowerRva = 0x21573A0;
inline constexpr std::uintptr_t AddOnePowerRva = 0x2157440;
inline constexpr std::uintptr_t RemoveOnePowerRva = 0x2157633;
inline constexpr std::uintptr_t EffectivePowerRva = 0x21507A0;
// Recovered from the native pool-selector branches. Their adjacent failure
// strings identify selector 1 as engines, 2 as shields, and 3 as grav drive.
inline constexpr std::uintptr_t EngineIdentityGateRva = 0x1F2FAC4;
inline constexpr std::uintptr_t ShieldIdentityGateRva = 0x1F2FA82;
inline constexpr std::uintptr_t GravDriveIdentityGateRva = 0x1F2FA40;
inline constexpr std::size_t PartMaximumPowerOffset = 0x60;
inline constexpr std::size_t PartCurrentPowerOffset = 0x64;
inline constexpr std::uint32_t WorkbenchReason = 1;

// Installed weapon parts retain canonical W0/W1/W2 order. Keep the translation
// explicit at the native boundary so it cannot be confused with Control's
// one-based display labels (Weapon 1/2/3).
[[nodiscard]] constexpr SystemId WeaponSystemFromPartOrder(
    std::uint32_t partOrder) noexcept {
    constexpr std::array mapping{
        SystemId::Weapon0, SystemId::Weapon1, SystemId::Weapon2};
    return partOrder < mapping.size() ? mapping[partOrder] : SystemId::Invalid;
}

// Native selector 0 enters the weapon path, where partOrder selects the
// weapon group. The remaining selectors directly identify non-weapon pools.
[[nodiscard]] constexpr SystemId SystemFromNativePoolSelector(
    std::uint32_t selector, std::uint32_t weaponPartOrder = 0) noexcept {
    switch (selector) {
    case 0: return WeaponSystemFromPartOrder(weaponPartOrder);
    case 1: return SystemId::Engine;
    case 2: return SystemId::Shield;
    case 3: return SystemId::GravDrive;
    default: return SystemId::Invalid;
    }
}

[[nodiscard]] constexpr std::uintptr_t IdentityGateRva(
    SystemId system) noexcept {
    switch (system) {
    case SystemId::Engine: return EngineIdentityGateRva;
    case SystemId::Shield: return ShieldIdentityGateRva;
    case SystemId::GravDrive: return GravDriveIdentityGateRva;
    default: return 0;
    }
}

[[nodiscard]] constexpr std::uint32_t BaseTargetFromEffective(
    std::uint32_t effectiveTarget, std::uint32_t bonus) noexcept {
    return effectiveTarget > bonus ? effectiveTarget - bonus : 0u;
}
} // namespace ResearchLayout

} // namespace AbsolutePower
