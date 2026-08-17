#include "PCH.h"

#include "GameUpdateHook.h"

#include "ControlPanelSubscriber.h"
#include "PowerRuntime.h"
#include "RuntimePaths.h"

namespace {

using ThrusterOutputFunction = void (*)(void*, const float*, float*);

constexpr std::uintptr_t kThrusterOutputSlotRva = 0x4C41BB0;
constexpr std::uintptr_t kThrusterOutputRva = 0x12BA5E0;

ThrusterOutputFunction g_original{};
std::atomic<bool> g_installed{};

#pragma warning(push)
#pragma warning(disable : 4733)
bool SafeRead(std::uintptr_t address, std::uintptr_t& value) {
    if (!address) return false;
    __try {
        value = *reinterpret_cast<volatile std::uintptr_t*>(address);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
#pragma warning(pop)

void HookedThrusterOutput(void* handler, const float* input, float* output) {
    g_original(handler, input, output);
    if (handler && input && output) {
        AbsolutePower::PowerRuntime::Get().TickGameThread();
        ControlPanelSubscriber::PublishLiveState();
    }
}

} // namespace

namespace GameUpdateHook {

bool Install() noexcept {
    if (g_installed.load(std::memory_order_acquire)) return true;
    try {
        const auto module = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        if (!module) return false;
        auto* slot = reinterpret_cast<std::uintptr_t*>(module + kThrusterOutputSlotRva);
        std::uintptr_t current{};
        if (!SafeRead(reinterpret_cast<std::uintptr_t>(slot), current) ||
            current != module + kThrusterOutputRva) {
            RuntimePaths::Log(
                "Executor",
                "Validated ship-update hook was not installed because the exact vtable target is unavailable or already owned.",
                true);
            return false;
        }

        DWORD oldProtection{};
        if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &oldProtection)) {
            RuntimePaths::Log("Executor", "Ship-update vtable protection change failed.", true);
            return false;
        }
        g_original = reinterpret_cast<ThrusterOutputFunction>(current);
        *slot = reinterpret_cast<std::uintptr_t>(&HookedThrusterOutput);
        FlushInstructionCache(GetCurrentProcess(), slot, sizeof(*slot));
        DWORD ignored{};
        VirtualProtect(slot, sizeof(*slot), oldProtection, &ignored);
        g_installed.store(true, std::memory_order_release);
        RuntimePaths::Log(
            "Executor",
            "Validated Starfield ship-update Power executor installed at the flight-handler output vtable.",
            true);
        return true;
    } catch (...) {
        RuntimePaths::Log("Executor", "Ship-update hook installation raised an exception.", true);
        return false;
    }
}

} // namespace GameUpdateHook
