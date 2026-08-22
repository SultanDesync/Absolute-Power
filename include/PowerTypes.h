#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace AbsolutePower {

enum class SystemId : std::uint8_t {
    Weapon0,
    Weapon1,
    Weapon2,
    Engine,
    Shield,
    GravDrive,
    Count,
    Invalid = 0xFF,
};

inline constexpr std::size_t kSystemCount = static_cast<std::size_t>(SystemId::Count);

inline constexpr std::array<SystemId, kSystemCount> kCockpitOrder{
    SystemId::Weapon0, SystemId::Weapon1, SystemId::Weapon2,
    SystemId::Engine, SystemId::Shield, SystemId::GravDrive,
};

[[nodiscard]] constexpr std::size_t ToIndex(SystemId system) noexcept {
    return static_cast<std::size_t>(system);
}

// Config arrays, Control rows, snapshots, and the public API all index by this
// canonical order. Native layouts must translate into it at their boundary.
static_assert(ToIndex(SystemId::Weapon0) == 0);
static_assert(ToIndex(SystemId::Weapon1) == 1);
static_assert(ToIndex(SystemId::Weapon2) == 2);
static_assert(ToIndex(SystemId::Engine) == 3);
static_assert(ToIndex(SystemId::Shield) == 4);
static_assert(ToIndex(SystemId::GravDrive) == 5);

[[nodiscard]] constexpr std::string_view SystemKey(SystemId system) noexcept {
    constexpr std::array<std::string_view, kSystemCount> keys{
        "Weapon0", "Weapon1", "Weapon2", "Engine", "Shield", "GravDrive"};
    return ToIndex(system) < keys.size() ? keys[ToIndex(system)] : std::string_view{};
}

[[nodiscard]] constexpr std::string_view SystemLabel(SystemId system) noexcept {
    constexpr std::array<std::string_view, kSystemCount> labels{
        "Weapon 1", "Weapon 2", "Weapon 3", "Engines", "Shields", "Grav Drive"};
    return ToIndex(system) < labels.size() ? labels[ToIndex(system)] : std::string_view{};
}

enum class PriorityTier : std::uint8_t { Green, Yellow, Red, Count };
inline constexpr std::size_t kTierCount = static_cast<std::size_t>(PriorityTier::Count);

struct SystemState {
    bool present{};
    std::uint16_t current{};
    std::uint16_t maximum{};
    // Power supplied by crew/perks is included in current but cannot be
    // reassigned by the reactor allocator.
    std::uint16_t bonus{};
};

enum class CrewBonusMode : std::uint8_t {
    // Preserve vanilla behavior: the always-on bonus is added to the pips
    // requested by a preset, up to the installed system maximum.
    Additive,
    // Let the always-on bonus satisfy the preset's requested target.
    CountTowardPreset,
};

struct Snapshot {
    bool pilotReady{};
    std::uint16_t available{};
    std::uint16_t totalPower{};
    std::array<SystemState, kSystemCount> systems{};
};

struct TierPlan {
    std::uint16_t green{};
    std::uint16_t yellow{};
    std::uint16_t red{};
};

struct Preset {
    std::string id;
    std::string displayName;
    std::array<TierPlan, kSystemCount> systems{};
    std::array<SystemId, kSystemCount> tieBreakOrder{kCockpitOrder};
};

struct Demand {
    std::string sourceId;
    SystemId system{SystemId::Invalid};
    std::uint16_t minimum{};
    std::uint16_t priority{};
};

enum class AllocationStatus : std::uint8_t {
    Ok,
    InvalidSnapshot,
    InvalidPreset,
};

struct Allocation {
    AllocationStatus status{AllocationStatus::Ok};
    std::array<std::uint16_t, kSystemCount> target{};
    std::uint16_t unassigned{};
    std::uint16_t clippedPresetPips{};
    std::uint16_t unsatisfiedDemandPips{};
};

enum class ChangePhase : std::uint8_t { Release, Assign };

struct PowerChange {
    SystemId system{SystemId::Invalid};
    std::uint16_t from{};
    std::uint16_t to{};
    ChangePhase phase{ChangePhase::Release};
};

} // namespace AbsolutePower
