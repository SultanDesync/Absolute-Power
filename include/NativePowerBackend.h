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
};

namespace ResearchLayout {
inline constexpr std::uintptr_t EquipmentDescriptorGlobalRva = 0x5F2FAF8;
inline constexpr std::uint32_t PowerEquipmentTypeId = 260072;
inline constexpr std::uintptr_t SetAbsolutePowerRva = 0x21573A0;
inline constexpr std::uintptr_t AddOnePowerRva = 0x2157440;
inline constexpr std::uintptr_t RemoveOnePowerRva = 0x2157633;
inline constexpr std::size_t PartMaximumPowerOffset = 0x60;
inline constexpr std::size_t PartCurrentPowerOffset = 0x64;
inline constexpr std::uint32_t WorkbenchReason = 1;
} // namespace ResearchLayout

} // namespace AbsolutePower
