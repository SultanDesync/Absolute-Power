#include "PowerService.h"

#include <cassert>
#include <cstdlib>
#include <utility>
#include <vector>

using namespace AbsolutePower;

namespace {

class SettlingBackend final : public IPowerBackend {
public:
    SettlingBackend() {
        snapshot.pilotReady = true;
        snapshot.totalPower = 4;
        snapshot.available = 0;
        snapshot.systems[ToIndex(SystemId::Weapon0)] = {true, 3, 4};
        snapshot.systems[ToIndex(SystemId::Shield)] = {true, 1, 4};
    }

    BackendResult Capture(Snapshot& output) override {
        output = snapshot;
        return BackendResult::Ok;
    }

    BackendResult SetPower(SystemId system, std::uint16_t target) override {
        auto& state = snapshot.systems[ToIndex(system)];
        const auto delta = std::abs(static_cast<int>(target) - static_cast<int>(state.current));
        assert(delta == 1);
        if (target < state.current) {
            ++snapshot.available;
        } else {
            assert(snapshot.available > 0);
            --snapshot.available;
        }
        state.current = target;
        calls.emplace_back(system, target);
        return BackendResult::Ok;
    }

    Snapshot snapshot{};
    std::vector<std::pair<SystemId, std::uint16_t>> calls;
};

Preset ShieldPreset() {
    Preset preset{.id = "Shield", .displayName = "Shield"};
    preset.systems[ToIndex(SystemId::Shield)].green = 4;
    preset.tieBreakOrder = {SystemId::Shield, SystemId::Weapon0, SystemId::Weapon1,
                            SystemId::Weapon2, SystemId::Engine, SystemId::GravDrive};
    return preset;
}

void Settle(PowerService& service, const Preset& preset,
            std::span<const Demand> demands = {}) {
    for (std::size_t guard = 0; guard < 32; ++guard) {
        const auto step = service.ApplyPreset(preset, demands);
        assert(step.backend == BackendResult::Ok);
        if (step.totalChanges == 0) return;
        assert(step.completedChanges == 1);
    }
    assert(false && "settlement did not converge");
}

} // namespace

int main() {
    SettlingBackend backend;
    PowerService service(backend);
    const auto preset = ShieldPreset();

    for (std::size_t remaining = 6; remaining > 0; --remaining) {
        const auto step = service.ApplyPreset(preset);
        assert(step.backend == BackendResult::Ok);
        assert(step.totalChanges == remaining);
        assert(step.completedChanges == 1);
        assert(backend.calls.size() == 7 - remaining);
    }

    const auto settled = service.ApplyPreset(preset);
    assert(settled.backend == BackendResult::Ok);
    assert(settled.totalChanges == 0);
    assert(settled.completedChanges == 0);
    assert(backend.calls.size() == 6);
    assert(backend.snapshot.available == 0);
    assert(backend.snapshot.systems[ToIndex(SystemId::Weapon0)].current == 0);
    assert(backend.snapshot.systems[ToIndex(SystemId::Shield)].current == 4);

    const Demand demand{"weapon-response", SystemId::Weapon0, 3, 200};
    Settle(service, preset, std::span<const Demand>(&demand, 1));
    assert(backend.snapshot.systems[ToIndex(SystemId::Weapon0)].current == 3);
    assert(backend.snapshot.systems[ToIndex(SystemId::Shield)].current == 1);

    // Expiring the overlay demand must converge back to the active base preset
    // rather than leaving an emergency allocation permanently latched.
    Settle(service, preset);
    assert(backend.snapshot.systems[ToIndex(SystemId::Weapon0)].current == 0);
    assert(backend.snapshot.systems[ToIndex(SystemId::Shield)].current == 4);
    return 0;
}
