#include "NativePowerBackend.h"

#include "RuntimePaths.h"

#include <windows.h>

#include <array>
#include <atomic>
#include <cstring>
#include <limits>

namespace AbsolutePower {
namespace {

constexpr std::size_t kMaximumPowerParts = 32;
constexpr std::size_t kPowerComponentCaptureSize = 0xE4;
constexpr std::size_t kPowerPartCaptureSize = 0x98;

constexpr std::uintptr_t kRegistryLookupCallerGateRva = 0x155CBE4;
constexpr std::array<std::uint8_t, 19> kRegistryLookupCallerGateBytes{
    0x48, 0x8B, 0x05, 0x0D, 0x2F, 0x9D, 0x04, 0x8B, 0x50, 0x28,
    0x48, 0x8D, 0x4D, 0xE8, 0xE8, 0x09, 0xD8, 0xBF, 0x00};
constexpr std::uintptr_t kComponentLayoutGateRva = 0x155CC07;
constexpr std::array<std::uint8_t, 27> kComponentLayoutGateBytes{
    0x4C, 0x8B, 0x55, 0xE8, 0x45, 0x33, 0xF6, 0x41, 0x3B,
    0x42, 0x30, 0x0F, 0x83, 0x0D, 0x02, 0x00, 0x00, 0x8B,
    0xC8, 0x49, 0x8B, 0x42, 0x38, 0x48, 0x8B, 0x14, 0xC8};
constexpr std::uintptr_t kAllocationFieldGateRva = 0x155CC22;
constexpr std::array<std::uint8_t, 9> kAllocationFieldGateBytes{
    0x8B, 0x72, 0x64, 0x8B, 0x47, 0x08, 0x8D, 0x1C, 0x30};
constexpr std::uintptr_t kRegistryLookupRva = 0x215A400;
constexpr std::array<std::uint8_t, 10> kRegistryLookupGateBytes{
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18};
constexpr std::uintptr_t kSharedReleaseRva = 0x24047A0;
constexpr std::array<std::uint8_t, 10> kSharedReleaseGateBytes{
    0x48, 0x89, 0x5C, 0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20};

constexpr std::uintptr_t kEngineIdentityGateRva =
    ResearchLayout::IdentityGateRva(SystemId::Engine);
constexpr std::array<std::uint8_t, 7> kEngineIdentityGateBytes{
    0x48, 0x8B, 0x15, 0xDD, 0x0E, 0xF7, 0x03};
constexpr std::uintptr_t kShieldIdentityGateRva =
    ResearchLayout::IdentityGateRva(SystemId::Shield);
constexpr std::array<std::uint8_t, 7> kShieldIdentityGateBytes{
    0x48, 0x8B, 0x15, 0x97, 0x13, 0xF7, 0x03};
constexpr std::uintptr_t kGravIdentityGateRva =
    ResearchLayout::IdentityGateRva(SystemId::GravDrive);
constexpr std::array<std::uint8_t, 7> kGravIdentityGateBytes{
    0x48, 0x8B, 0x15, 0xE1, 0x14, 0xF7, 0x03};

constexpr std::uintptr_t kNativeSetterCallGateRva = 0x155CC41;
constexpr std::array<std::uint8_t, 14> kNativeSetterCallGateBytes{
    0x41, 0xB1, 0x01, 0x44, 0x8B, 0xC3, 0x49,
    0x8B, 0xCA, 0xE8, 0x51, 0xA7, 0xBF, 0x00};
constexpr std::uintptr_t kPowerSetterRva = 0x21573A0;
constexpr std::array<std::uint8_t, 10> kPowerSetterGateBytes{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10};
constexpr std::uintptr_t kPowerSetterFieldGateRva = 0x21573B8;
constexpr std::array<std::uint8_t, 10> kPowerSetterFieldGateBytes{
    0x44, 0x8B, 0x7A, 0x64, 0x41, 0x8B, 0xF8, 0x41, 0x2B, 0xFF};
constexpr std::uintptr_t kEffectivePowerRva = ResearchLayout::EffectivePowerRva;
constexpr std::array<std::uint8_t, 10> kEffectivePowerGateBytes{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20};

struct NativeSharedReference {
    std::uintptr_t object{};
    std::uintptr_t owner{};
};

enum class PartKind : std::uint8_t { Other, Weapon, Shield, Engine, GravDrive };

struct NativePart {
    std::uintptr_t address{};
    PartKind kind{PartKind::Other};
    std::uint32_t weaponIndex{};
    std::uint32_t maximum{};
    std::uint32_t current{};
    std::uint32_t bonus{};
};

struct NativeEquipment {
    NativeSharedReference reference{};
    std::int32_t available{};
    std::array<NativePart, kMaximumPowerParts> parts{};
    std::size_t partCount{};
};

enum class SnapshotDiagnostic : std::uint32_t {
    DescriptorGlobal = 1u << 0,
    DescriptorPointer = 1u << 1,
    DescriptorType = 1u << 2,
    ComponentCopy = 1u << 3,
    ComponentLayout = 1u << 4,
    Identities = 1u << 5,
    PartPointer = 1u << 6,
    PartCopy = 1u << 7,
    ReferenceRelease = 1u << 8,
    SnapshotReady = 1u << 9,
    LiveRegistryType = 1u << 10,
};

std::atomic<std::uint32_t> g_snapshotDiagnostics{};

void LogSnapshotDiagnosticOnce(SnapshotDiagnostic diagnostic, const char* message,
                               bool warning = true) {
    const auto bit = static_cast<std::uint32_t>(diagnostic);
    if ((g_snapshotDiagnostics.fetch_or(bit, std::memory_order_acq_rel) & bit) == 0)
        RuntimePaths::Log("NativePower", message, warning);
}

void LogPointerDiagnosticOnce(SnapshotDiagnostic diagnostic, const char* label,
                              std::uintptr_t first, std::uintptr_t second = 0,
                              std::uint32_t value = 0) {
    char buffer[256]{};
    sprintf_s(buffer, "%s (first=0x%llX, second=0x%llX, value=%u, thread=%lu).",
              label, static_cast<unsigned long long>(first),
              static_cast<unsigned long long>(second), value,
              static_cast<unsigned long>(GetCurrentThreadId()));
    LogSnapshotDiagnosticOnce(diagnostic, buffer);
}

#pragma warning(push)
#pragma warning(disable : 4733)
bool SafeCopy(void* destination, std::uintptr_t source, std::size_t size) {
    if (!destination || !source || !size) return false;
    __try {
        std::memcpy(destination, reinterpret_cast<const void*>(source), size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
#pragma warning(pop)

template <std::size_t Size>
bool BytesMatch(std::uintptr_t module, std::uintptr_t rva,
                const std::array<std::uint8_t, Size>& expected) {
    std::array<std::uint8_t, Size> actual{};
    return module && SafeCopy(actual.data(), module + rva, actual.size()) && actual == expected;
}

std::uintptr_t RipGlobal(std::uintptr_t module, std::uintptr_t instructionRva) {
    std::int32_t displacement{};
    if (!SafeCopy(&displacement, module + instructionRva + 3, sizeof(displacement))) return 0;
    return module + instructionRva + 7 + displacement;
}

bool AllRuntimeGatesMatch(std::uintptr_t module) {
    return BytesMatch(module, kRegistryLookupCallerGateRva, kRegistryLookupCallerGateBytes) &&
           BytesMatch(module, kComponentLayoutGateRva, kComponentLayoutGateBytes) &&
           BytesMatch(module, kAllocationFieldGateRva, kAllocationFieldGateBytes) &&
           BytesMatch(module, kRegistryLookupRva, kRegistryLookupGateBytes) &&
           BytesMatch(module, kSharedReleaseRva, kSharedReleaseGateBytes) &&
           BytesMatch(module, kShieldIdentityGateRva, kShieldIdentityGateBytes) &&
           BytesMatch(module, kEngineIdentityGateRva, kEngineIdentityGateBytes) &&
           BytesMatch(module, kGravIdentityGateRva, kGravIdentityGateBytes) &&
           BytesMatch(module, kNativeSetterCallGateRva, kNativeSetterCallGateBytes) &&
           BytesMatch(module, kPowerSetterRva, kPowerSetterGateBytes) &&
           BytesMatch(module, kPowerSetterFieldGateRva, kPowerSetterFieldGateBytes) &&
           BytesMatch(module, kEffectivePowerRva, kEffectivePowerGateBytes);
}

bool ReleaseReference(std::uintptr_t module, NativeSharedReference& reference) {
    if (!reference.owner) {
        reference = {};
        return true;
    }
    if (reference.owner < 0x20) {
        LogPointerDiagnosticOnce(
            SnapshotDiagnostic::ReferenceRelease,
            "Snapshot reference owner failed the balanced-release address gate",
            reference.object, reference.owner);
        return false;
    }
    auto* ownerHeader = reinterpret_cast<volatile LONG64*>(reference.owner - 0x20);
    constexpr LONG64 kReferenceDecrement = -0x100000001LL;
    constexpr LONG64 kLastReference = 0x100000001LL;
    const auto previous = InterlockedExchangeAdd64(ownerHeader, kReferenceDecrement);
    if (previous == kLastReference) {
        using FinalRelease = void (*)(void*);
        reinterpret_cast<FinalRelease>(module + kSharedReleaseRva)(
            const_cast<LONG64*>(ownerHeader));
    }
    reference = {};
    return true;
}

BackendResult ReleaseAndReturn(std::uintptr_t module, NativeEquipment& equipment,
                               BackendResult result) {
    return ReleaseReference(module, equipment.reference)
               ? result
               : BackendResult::SnapshotSeamUnavailable;
}

std::uintptr_t ReadIdentity(std::uintptr_t module, std::uintptr_t instructionRva) {
    const auto global = RipGlobal(module, instructionRva);
    std::uintptr_t identity{};
    std::uint8_t formType{};
    if (!global || !SafeCopy(&identity, global, sizeof(identity)) || !identity ||
        !SafeCopy(&formType, identity + 0x2E, sizeof(formType)) || formType != 0x6D) {
        return 0;
    }
    return identity;
}

BackendResult LookupEquipment(std::uintptr_t module, NativeEquipment& equipment) {
    equipment = {};
    // Mirror the native caller exactly. This registry key is assigned at runtime
    // and has been observed changing between otherwise identical game processes;
    // the research-session value is evidence of the field, not a stable ID.
    const auto descriptorGlobal = RipGlobal(module, kRegistryLookupCallerGateRva);
    std::uintptr_t descriptor{};
    std::uint32_t typeId{};
    if (!descriptorGlobal) {
        return BackendResult::SnapshotSeamUnavailable;
    }
    if (!SafeCopy(&descriptor, descriptorGlobal, sizeof(descriptor)) || !descriptor) {
        return BackendResult::PilotNotReady;
    }
    if (!SafeCopy(&typeId, descriptor + 0x28, sizeof(typeId)) || !typeId) {
        LogPointerDiagnosticOnce(
            SnapshotDiagnostic::DescriptorType,
            "Live Power equipment descriptor did not expose its runtime registry key",
            descriptorGlobal, descriptor, typeId);
        return BackendResult::SnapshotSeamUnavailable;
    }
    char typeBuffer[192]{};
    sprintf_s(typeBuffer,
              "Using live Power equipment registry key 0x%08X from the exact-gated native descriptor.",
              typeId);
    LogSnapshotDiagnosticOnce(SnapshotDiagnostic::LiveRegistryType, typeBuffer, false);

    using RegistryLookup = NativeSharedReference* (*)(NativeSharedReference*, std::uint32_t);
    reinterpret_cast<RegistryLookup>(module + kRegistryLookupRva)(&equipment.reference, typeId);
    if (!equipment.reference.object) {
        return ReleaseAndReturn(module, equipment, BackendResult::PilotNotReady);
    }

    std::array<std::uint8_t, kPowerComponentCaptureSize> componentBytes{};
    if (!SafeCopy(componentBytes.data(), equipment.reference.object, componentBytes.size())) {
        LogPointerDiagnosticOnce(
            SnapshotDiagnostic::ComponentCopy,
            "Power equipment registry reference could not be copied",
            equipment.reference.object, equipment.reference.owner);
        return ReleaseAndReturn(module, equipment, BackendResult::SnapshotSeamUnavailable);
    }

    std::uint32_t partCount{};
    std::uintptr_t partArray{};
    std::memcpy(&partCount, componentBytes.data() + 0x30, sizeof(partCount));
    std::memcpy(&partArray, componentBytes.data() + 0x38, sizeof(partArray));
    std::memcpy(&equipment.available, componentBytes.data() + 0xE0,
                sizeof(equipment.available));
    if (partCount == 0 || partCount > 64 || partCount > equipment.parts.size() || !partArray ||
        equipment.available < 0) {
        LogPointerDiagnosticOnce(
            SnapshotDiagnostic::ComponentLayout,
            "Power equipment component is not currently pilot-ready",
            equipment.reference.object, partArray, partCount);
        return ReleaseAndReturn(module, equipment, BackendResult::PilotNotReady);
    }

    const auto shieldIdentity = ReadIdentity(module, kShieldIdentityGateRva);
    const auto engineIdentity = ReadIdentity(module, kEngineIdentityGateRva);
    const auto gravIdentity = ReadIdentity(module, kGravIdentityGateRva);
    if (!shieldIdentity || !engineIdentity || !gravIdentity) {
        char buffer[256]{};
        sprintf_s(buffer,
                  "Power pool identities were not all readable (shield=0x%llX, engine=0x%llX, grav=0x%llX, thread=%lu).",
                  static_cast<unsigned long long>(shieldIdentity),
                  static_cast<unsigned long long>(engineIdentity),
                  static_cast<unsigned long long>(gravIdentity),
                  static_cast<unsigned long>(GetCurrentThreadId()));
        LogSnapshotDiagnosticOnce(SnapshotDiagnostic::Identities, buffer);
        return ReleaseAndReturn(module, equipment, BackendResult::SnapshotSeamUnavailable);
    }

    std::uint32_t weaponIndex{};
    for (std::size_t index = 0; index < partCount; ++index) {
        std::uintptr_t partAddress{};
        std::array<std::uint8_t, kPowerPartCaptureSize> partBytes{};
        if (!SafeCopy(&partAddress, partArray + index * sizeof(void*), sizeof(partAddress)) ||
            !partAddress) {
            LogPointerDiagnosticOnce(
                SnapshotDiagnostic::PartPointer,
                "Power equipment part table contained an unreadable entry",
                partArray, partAddress, static_cast<std::uint32_t>(index));
            return ReleaseAndReturn(module, equipment, BackendResult::SnapshotSeamUnavailable);
        }
        if (!SafeCopy(partBytes.data(), partAddress, partBytes.size())) {
            LogPointerDiagnosticOnce(
                SnapshotDiagnostic::PartCopy,
                "Power equipment part object could not be copied",
                partArray, partAddress, static_cast<std::uint32_t>(index));
            return ReleaseAndReturn(module, equipment, BackendResult::SnapshotSeamUnavailable);
        }

        auto& part = equipment.parts[equipment.partCount++];
        part.address = partAddress;
        std::uintptr_t weapon{};
        std::uintptr_t identity{};
        std::memcpy(&weapon, partBytes.data() + 0x18, sizeof(weapon));
        std::memcpy(&identity, partBytes.data() + 0x28, sizeof(identity));
        std::memcpy(&part.maximum, partBytes.data() + ResearchLayout::PartMaximumPowerOffset,
                    sizeof(part.maximum));
        std::memcpy(&part.current, partBytes.data() + ResearchLayout::PartCurrentPowerOffset,
                    sizeof(part.current));
        using EffectivePower = std::uint32_t (*)(void*);
        const auto effective = reinterpret_cast<EffectivePower>(
            module + kEffectivePowerRva)(reinterpret_cast<void*>(partAddress));
        if (effective < part.current || effective > part.maximum) {
            return ReleaseAndReturn(
                module, equipment, BackendResult::SnapshotSeamUnavailable);
        }
        part.bonus = effective - part.current;
        if (identity == shieldIdentity) {
            part.kind = PartKind::Shield;
        } else if (identity == engineIdentity) {
            part.kind = PartKind::Engine;
        } else if (identity == gravIdentity) {
            part.kind = PartKind::GravDrive;
        } else if (weapon) {
            part.kind = PartKind::Weapon;
            part.weaponIndex = weaponIndex++;
        }
    }
    return BackendResult::Ok;
}

SystemId ToSystem(const NativePart& part) {
    switch (part.kind) {
    case PartKind::Weapon:
        return ResearchLayout::WeaponSystemFromPartOrder(part.weaponIndex);
    case PartKind::Engine: return SystemId::Engine;
    case PartKind::Shield: return SystemId::Shield;
    case PartKind::GravDrive: return SystemId::GravDrive;
    case PartKind::Other: break;
    }
    return SystemId::Invalid;
}

} // namespace

bool NativePowerBackend::Initialize() {
    module_ = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    setterSignaturesReady_ = module_ && AllRuntimeGatesMatch(module_);
    RuntimePaths::Log(
        "NativePower",
        setterSignaturesReady_
            ? "Starfield 1.16.244.0 equipment lookup, ownership release, layout, identity, effective-power getter, and absolute setter gates validated."
            : "Native Power exact gates did not match; runtime operations remain fail-closed.",
        !setterSignaturesReady_);
    return setterSignaturesReady_;
}

bool NativePowerBackend::SetterSignaturesReady() const noexcept {
    return setterSignaturesReady_;
}

BackendResult NativePowerBackend::Capture(Snapshot& snapshot) {
    snapshot = {};
    if (!setterSignaturesReady_ || !module_) return BackendResult::UnsupportedRuntime;

    NativeEquipment equipment{};
    const auto lookup = LookupEquipment(module_, equipment);
    if (lookup != BackendResult::Ok) return lookup;

    snapshot.pilotReady = true;
    if (equipment.available > std::numeric_limits<std::uint16_t>::max()) {
        return ReleaseAndReturn(module_, equipment, BackendResult::InvalidRequest);
    }
    snapshot.available = static_cast<std::uint16_t>(equipment.available);
    std::uint32_t total = snapshot.available;
    for (std::size_t index = 0; index < equipment.partCount; ++index) {
        const auto& part = equipment.parts[index];
        const auto system = ToSystem(part);
        if (system == SystemId::Invalid) continue;
        if (part.maximum > std::numeric_limits<std::uint16_t>::max() ||
            part.current > part.maximum || part.current > std::numeric_limits<std::uint16_t>::max()) {
            return ReleaseAndReturn(module_, equipment, BackendResult::InvalidRequest);
        }
        auto& state = snapshot.systems[ToIndex(system)];
        if (state.present) continue;
        state.present = true;
        state.maximum = static_cast<std::uint16_t>(part.maximum);
        state.current = static_cast<std::uint16_t>(part.current + part.bonus);
        state.bonus = static_cast<std::uint16_t>(part.bonus);
        total += part.current + part.bonus;
    }
    snapshot.totalPower = static_cast<std::uint16_t>(
        std::min<std::uint32_t>(total, std::numeric_limits<std::uint16_t>::max()));
    LogSnapshotDiagnosticOnce(
        SnapshotDiagnostic::SnapshotReady,
        "Validated native Power snapshot is available.", false);
    return ReleaseAndReturn(module_, equipment, BackendResult::Ok);
}

BackendResult NativePowerBackend::SetPower(SystemId system, std::uint16_t targetPips) {
    if (!setterSignaturesReady_ || !module_) return BackendResult::UnsupportedRuntime;
    if (system == SystemId::Invalid || ToIndex(system) >= kSystemCount) {
        return BackendResult::InvalidRequest;
    }

    NativeEquipment equipment{};
    const auto lookup = LookupEquipment(module_, equipment);
    if (lookup != BackendResult::Ok) return lookup;

    NativePart* targetPart = nullptr;
    for (std::size_t index = 0; index < equipment.partCount; ++index) {
        if (ToSystem(equipment.parts[index]) == system) {
            targetPart = &equipment.parts[index];
            break;
        }
    }
    if (!targetPart) {
        return ReleaseAndReturn(module_, equipment, BackendResult::SystemUnavailable);
    }
    if (targetPips > targetPart->maximum) {
        return ReleaseAndReturn(module_, equipment, BackendResult::InvalidRequest);
    }
    const auto baseTarget = ResearchLayout::BaseTargetFromEffective(
        targetPips, targetPart->bonus);
    if (baseTarget == targetPart->current) {
        return ReleaseAndReturn(module_, equipment, BackendResult::Ok);
    }

    using PowerSetter = bool (*)(void*, void*, std::uint32_t, std::uint8_t);
    const bool accepted = reinterpret_cast<PowerSetter>(module_ + kPowerSetterRva)(
        reinterpret_cast<void*>(equipment.reference.object),
        reinterpret_cast<void*>(targetPart->address), baseTarget,
        static_cast<std::uint8_t>(ResearchLayout::WorkbenchReason));
    std::uint32_t observed{};
    const bool observedTarget =
        SafeCopy(&observed, targetPart->address + ResearchLayout::PartCurrentPowerOffset,
                 sizeof(observed)) &&
        observed == baseTarget;
    return ReleaseAndReturn(module_, equipment,
                            accepted && observedTarget ? BackendResult::Ok
                                                       : BackendResult::SetterRejected);
}

} // namespace AbsolutePower
