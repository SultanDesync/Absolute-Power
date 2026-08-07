#pragma once

#include "PowerTypes.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace AbsolutePower {

enum class TriggerKind : std::uint8_t {
    WeaponFired,
    IncomingDamage,
    ThrottleAbove,
    Manual,
};

struct AutomationRule {
    std::string id;
    std::string displayName;
    bool enabled{};
    TriggerKind trigger{TriggerKind::Manual};
    SystemId sourceSystem{SystemId::Invalid};
    SystemId targetSystem{SystemId::Invalid};
    std::uint16_t targetPips{}; // UINT16_MAX means the target system's maximum.
    std::uint8_t thresholdPercent{50};
    std::uint8_t hysteresisPercent{5};
    std::uint32_t holdMilliseconds{1000};
    std::uint16_t priority{100};
};

class AutomationEngine {
public:
    void SetRules(std::vector<AutomationRule> rules);
    void SetEnabled(bool enabled) noexcept;
    [[nodiscard]] bool Enabled() const noexcept;

    void RecordWeaponFire(SystemId weapon, std::int64_t nowMilliseconds) noexcept;
    void RecordIncomingDamage(std::int64_t nowMilliseconds) noexcept;
    void SetThrottlePercent(float percent, std::int64_t nowMilliseconds) noexcept;
    void SetManualRule(std::string_view ruleId, bool active,
                       std::int64_t nowMilliseconds) noexcept;

    [[nodiscard]] std::vector<Demand> ActiveDemands(const Snapshot& snapshot,
                                                    std::int64_t nowMilliseconds);

private:
    struct RuleState {
        bool levelActive{};
        bool manualActive{};
        std::int64_t lastTriggeredMilliseconds{-1};
    };

    std::vector<AutomationRule> rules_;
    std::vector<RuleState> states_;
    std::array<std::int64_t, 3> lastWeaponFireMilliseconds_{-1, -1, -1};
    std::int64_t lastIncomingDamageMilliseconds_{-1};
    float throttlePercent_{};
    bool enabled_{};
};

} // namespace AbsolutePower
