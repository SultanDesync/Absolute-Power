#include "PCH.h"

#include "WeaponFireHook.h"

#include "PowerRuntime.h"
#include "RuntimePaths.h"
#include "WeaponFireEvent.h"

namespace {

using WeaponButtonFunction = std::uintptr_t (*)(void*, void*);

constexpr std::uintptr_t kWeaponListenerVtableRva = 0x4C42408;
constexpr std::uintptr_t kWeaponButtonSlotOffset = 0x40;
constexpr std::uintptr_t kWeaponButtonTargetRva = 0x12BDD30;
constexpr std::size_t kGroupSelectorOffset = 0xD0;
constexpr std::size_t kButtonValueOffset = 0x48;

WeaponButtonFunction g_original{};
std::atomic<bool> g_installed{};

#pragma warning(push)
#pragma warning(disable : 4733)
template <class T>
bool SafeRead(std::uintptr_t address, T& value) noexcept {
    if (!address) return false;
    __try {
        value = *reinterpret_cast<volatile T*>(address);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
#pragma warning(pop)

std::uintptr_t HookedWeaponButton(void* listener, void* event) {
    std::uint32_t groupIndex{};
    float value{};
    if (listener && event &&
        SafeRead(reinterpret_cast<std::uintptr_t>(listener) + kGroupSelectorOffset,
                 groupIndex) &&
        SafeRead(reinterpret_cast<std::uintptr_t>(event) + kButtonValueOffset, value)) {
        if (const auto weapon = AbsolutePower::DecodeWeaponFireEvent(groupIndex, value)) {
            AbsolutePower::PowerRuntime::Get().RecordWeaponFire(
                *weapon, AbsolutePower::WeaponFireOrigin::NativeInputListener);
        }
    }
    return g_original(listener, event);
}

} // namespace

namespace WeaponFireHook {

bool Install() noexcept {
    if (g_installed.load(std::memory_order_acquire)) return true;
    try {
        const auto module = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        if (!module) return false;
        auto* slot = reinterpret_cast<void**>(
            module + kWeaponListenerVtableRva + kWeaponButtonSlotOffset);
        auto* const expected = reinterpret_cast<void*>(module + kWeaponButtonTargetRva);

        void* current{};
        if (!SafeRead(reinterpret_cast<std::uintptr_t>(slot), current) ||
            current != expected) {
            RuntimePaths::Log(
                "Automation",
                "Native WeaponGroup event source was not installed because the exact listener vtable target is unavailable or already owned.",
                true);
            return false;
        }

        DWORD oldProtection{};
        if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &oldProtection)) {
            RuntimePaths::Log(
                "Automation",
                "Native WeaponGroup event-source vtable protection change failed.", true);
            return false;
        }
        // Publish the original before replacing the slot. Another game thread
        // may enter the detour immediately after the interlocked exchange.
        g_original = reinterpret_cast<WeaponButtonFunction>(expected);
        void* const previous = InterlockedCompareExchangePointer(
            reinterpret_cast<PVOID volatile*>(slot),
            reinterpret_cast<void*>(&HookedWeaponButton), expected);
        DWORD ignored{};
        VirtualProtect(slot, sizeof(*slot), oldProtection, &ignored);
        if (previous != expected) {
            g_original = nullptr;
            RuntimePaths::Log(
                "Automation",
                "Native WeaponGroup event source lost its exact listener slot during installation and failed closed.",
                true);
            return false;
        }

        FlushInstructionCache(GetCurrentProcess(), slot, sizeof(*slot));
        g_installed.store(true, std::memory_order_release);
        RuntimePaths::Log(
            "Automation",
            "Validated native WeaponGroup ButtonEvent source installed for standalone keyboard, mouse, and controller fire.",
            true);
        return true;
    } catch (...) {
        RuntimePaths::Log(
            "Automation", "Native WeaponGroup event-source installation raised an exception.",
            true);
        return false;
    }
}

bool Ready() noexcept { return g_installed.load(std::memory_order_acquire); }

} // namespace WeaponFireHook
