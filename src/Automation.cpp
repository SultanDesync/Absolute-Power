#include "Automation.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace AbsolutePower {

void AutomationEngine::SetRules(std::vector<AutomationRule> rules) {
    rules_ = std::move(rules);
    states_.assign(rules_.size(), RuleState{});
    lastWeaponFireMilliseconds_.fill(-1);
    lastIncomingDamageMilliseconds_ = -1;
    throttlePercent_ = 0.0F;
}

void AutomationEngine::SetEnabled(bool enabled) noexcept {
    enabled_ = enabled;
    if (!enabled_) {
        for (auto& state : states_) {
            state = {};
        }
        lastWeaponFireMilliseconds_.fill(-1);
        lastIncomingDamageMilliseconds_ = -1;
        throttlePercent_ = 0.0F;
    }
}

bool AutomationEngine::Enabled() const noexcept { return enabled_; }

void AutomationEngine::RecordWeaponFire(SystemId weapon, std::int64_t nowMilliseconds) noexcept {
    const auto index = ToIndex(weapon);
    if (index < lastWeaponFireMilliseconds_.size()) {
        lastWeaponFireMilliseconds_[index] = nowMilliseconds;
    }
}

void AutomationEngine::RecordIncomingDamage(std::int64_t nowMilliseconds) noexcept {
    lastIncomingDamageMilliseconds_ = nowMilliseconds;
}

void AutomationEngine::SetThrottlePercent(float percent,
                                          std::int64_t nowMilliseconds) noexcept {
    static_cast<void>(nowMilliseconds);
    throttlePercent_ = std::clamp(percent, 0.0F, 100.0F);
    for (std::size_t index = 0; index < rules_.size(); ++index) {
        const auto& rule = rules_[index];
        if (!rule.enabled || rule.trigger != TriggerKind::ThrottleAbove) {
            states_[index].levelActive = false;
            continue;
        }
        const auto releaseThreshold = static_cast<float>(
            rule.thresholdPercent > rule.hysteresisPercent
                ? rule.thresholdPercent - rule.hysteresisPercent
                : 0);
        if (!states_[index].levelActive && throttlePercent_ >= rule.thresholdPercent) {
            states_[index].levelActive = true;
        } else if (states_[index].levelActive && throttlePercent_ <= releaseThreshold) {
            states_[index].levelActive = false;
        }
    }
}

void AutomationEngine::SetManualRule(std::string_view ruleId, bool active,
                                     std::int64_t nowMilliseconds) noexcept {
    for (std::size_t index = 0; index < rules_.size(); ++index) {
        if (rules_[index].id == ruleId) {
            states_[index].manualActive = active;
            states_[index].lastTriggeredMilliseconds = nowMilliseconds;
            return;
        }
    }
}

std::vector<Demand> AutomationEngine::ActiveDemands(const Snapshot& snapshot,
                                                    std::int64_t nowMilliseconds) {
    std::vector<Demand> demands;
    if (!enabled_) {
        return demands;
    }

    for (std::size_t index = 0; index < rules_.size(); ++index) {
        const auto& rule = rules_[index];
        if (!rule.enabled) {
            continue;
        }

        bool active{};
        switch (rule.trigger) {
        case TriggerKind::WeaponFired: {
            if (rule.sourceSystem == SystemId::Invalid) {
                for (const auto firedAt : lastWeaponFireMilliseconds_) {
                    active = active || (firedAt >= 0 && nowMilliseconds >= firedAt &&
                                        nowMilliseconds - firedAt <= rule.holdMilliseconds);
                }
            } else {
                const auto sourceIndex = ToIndex(rule.sourceSystem);
                if (sourceIndex < lastWeaponFireMilliseconds_.size()) {
                    const auto firedAt = lastWeaponFireMilliseconds_[sourceIndex];
                    active = firedAt >= 0 && nowMilliseconds >= firedAt &&
                             nowMilliseconds - firedAt <= rule.holdMilliseconds;
                }
            }
            break;
        }
        case TriggerKind::IncomingDamage:
            active = lastIncomingDamageMilliseconds_ >= 0 &&
                     nowMilliseconds >= lastIncomingDamageMilliseconds_ &&
                     nowMilliseconds - lastIncomingDamageMilliseconds_ <=
                         rule.holdMilliseconds;
            break;
        case TriggerKind::ThrottleAbove:
            active = states_[index].levelActive;
            break;
        case TriggerKind::Manual:
            active = states_[index].manualActive;
            break;
        }

        const auto targetIndex = ToIndex(rule.targetSystem);
        if (!active || targetIndex >= kSystemCount || !snapshot.systems[targetIndex].present) {
            continue;
        }
        const auto target = rule.targetPips == std::numeric_limits<std::uint16_t>::max()
                                ? snapshot.systems[targetIndex].maximum
                                : rule.targetPips;
        demands.push_back({rule.id, rule.targetSystem, target, rule.priority});
    }
    return demands;
}

} // namespace AbsolutePower
