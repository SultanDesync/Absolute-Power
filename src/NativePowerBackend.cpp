#include "NativePowerBackend.h"

#include "RuntimePaths.h"

#include <windows.h>

#include <array>
#include <cstring>

namespace AbsolutePower {
namespace {

#pragma warning(push)
#pragma warning(disable : 4733)
template <std::size_t Size>
bool BytesMatch(std::uintptr_t address, const std::array<std::uint8_t, Size>& expected) {
    if (!address) return false;
    std::array<std::uint8_t, Size> actual{};
    __try {
        std::memcpy(actual.data(), reinterpret_cast<const void*>(address), actual.size());
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return actual == expected;
}
#pragma warning(pop)

} // namespace

bool NativePowerBackend::Initialize() {
    const auto module = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    constexpr std::array<std::uint8_t, 5> setBytes{0x48, 0x89, 0x5C, 0x24, 0x08};
    constexpr std::array<std::uint8_t, 5> addBytes{0x48, 0x89, 0x5C, 0x24, 0x10};
    constexpr std::array<std::uint8_t, 5> removeBytes{0x49, 0x89, 0x5B, 0x20, 0x55};
    setterSignaturesReady_ =
        module && BytesMatch(module + ResearchLayout::SetAbsolutePowerRva, setBytes) &&
        BytesMatch(module + ResearchLayout::AddOnePowerRva, addBytes) &&
        BytesMatch(module + ResearchLayout::RemoveOnePowerRva, removeBytes);

    if (setterSignaturesReady_) {
        RuntimePaths::Log("NativePower",
                          "Starfield 1.16.244.0 power setter family validated. The snapshot "
                          "ownership seam remains fail-closed in this bootstrap.");
    } else {
        RuntimePaths::Log("NativePower",
                          "Native power signatures did not match; runtime operations disabled.",
                          true);
    }
    return setterSignaturesReady_;
}

bool NativePowerBackend::SetterSignaturesReady() const noexcept {
    return setterSignaturesReady_;
}

BackendResult NativePowerBackend::Capture(Snapshot& snapshot) {
    snapshot = {};
    return setterSignaturesReady_ ? BackendResult::SnapshotSeamUnavailable
                                  : BackendResult::UnsupportedRuntime;
}

BackendResult NativePowerBackend::SetPower(SystemId system, std::uint16_t targetPips) {
    static_cast<void>(system);
    static_cast<void>(targetPips);
    return setterSignaturesReady_ ? BackendResult::SnapshotSeamUnavailable
                                  : BackendResult::UnsupportedRuntime;
}

} // namespace AbsolutePower
