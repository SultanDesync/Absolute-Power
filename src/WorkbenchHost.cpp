#include "PCH.h"

#include "WorkbenchHost.h"

#include "AbsoluteWorkbenchAPI.h"

namespace {
bool Valid(const AbsoluteWorkbenchApi::HostApiV1* api) {
    constexpr std::size_t minimum =
        offsetof(AbsoluteWorkbenchApi::HostApiV1, isInputCaptureActive) +
        sizeof(api->isInputCaptureActive);
    return api && api->structSize >= minimum &&
           api->abiVersion == AbsoluteWorkbenchApi::kAbiVersion && api->moduleId &&
           std::string_view(api->moduleId) == AbsoluteWorkbenchApi::kModuleId &&
           api->getHostMode && api->requestOpen && api->isOpen &&
           api->isInputCaptureActive;
}

WorkbenchHost::Selection Map(AbsoluteWorkbenchApi::HostMode mode) {
    switch (mode) {
    case AbsoluteWorkbenchApi::HostMode::Active:
        return WorkbenchHost::Selection::Active;
    case AbsoluteWorkbenchApi::HostMode::Suppressed:
        return WorkbenchHost::Selection::Suppressed;
    case AbsoluteWorkbenchApi::HostMode::Unavailable:
        return WorkbenchHost::Selection::Unavailable;
    case AbsoluteWorkbenchApi::HostMode::Incompatible:
        return WorkbenchHost::Selection::Incompatible;
    }
    return WorkbenchHost::Selection::Incompatible;
}
} // namespace

namespace WorkbenchHost {
Selection Select() noexcept {
    try {
        const HMODULE module = GetModuleHandleW(L"AbsoluteWorkbench.dll");
        if (!module) return Selection::Unavailable;
        const FARPROC address = GetProcAddress(module, "AbsoluteWorkbench_QueryHostApi");
        if (!address) return Selection::Incompatible;
        const auto query = reinterpret_cast<AbsoluteWorkbenchApi::QueryHostApi>(address);
        const auto* api = query(AbsoluteWorkbenchApi::kAbiVersion);
        return Valid(api) ? Map(api->getHostMode()) : Selection::Incompatible;
    } catch (...) {
        return Selection::Incompatible;
    }
}
} // namespace WorkbenchHost
