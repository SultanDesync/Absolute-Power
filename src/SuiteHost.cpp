#include "PCH.h"

#include "SuiteHost.h"

#include "AbsoluteHOTASAPI.h"
#include "AbsoluteWorkbenchAPI.h"
#include "ControlPanelSubscriber.h"

namespace {

enum class Probe { Active, Suppressed, Unavailable, Incompatible };

std::atomic<const AbsoluteWorkbenchApi::HostApiV1*> g_workbenchApi{};

bool ValidWorkbench(const AbsoluteWorkbenchApi::HostApiV1* api) {
    constexpr std::size_t minimum =
        offsetof(AbsoluteWorkbenchApi::HostApiV1, isInputCaptureActive) +
        sizeof(api->isInputCaptureActive);
    return api && api->structSize >= minimum &&
           api->abiVersion == AbsoluteWorkbenchApi::kAbiVersion && api->moduleId &&
           std::string_view(api->moduleId) == AbsoluteWorkbenchApi::kModuleId &&
           api->getHostMode && api->requestOpen && api->isOpen && api->isInputCaptureActive;
}

Probe ProbeWorkbench() {
    const HMODULE module = GetModuleHandleW(L"AbsoluteWorkbench.dll");
    if (!module) {
        g_workbenchApi.store(nullptr, std::memory_order_release);
        return Probe::Unavailable;
    }
    const FARPROC address = GetProcAddress(module, "AbsoluteWorkbench_QueryHostApi");
    if (!address) return Probe::Incompatible;
    const auto query = reinterpret_cast<AbsoluteWorkbenchApi::QueryHostApi>(address);
    const auto* api = query(AbsoluteWorkbenchApi::kAbiVersion);
    if (!ValidWorkbench(api)) {
        g_workbenchApi.store(nullptr, std::memory_order_release);
        return Probe::Incompatible;
    }
    g_workbenchApi.store(api, std::memory_order_release);
    switch (api->getHostMode()) {
    case AbsoluteWorkbenchApi::HostMode::Active: return Probe::Active;
    case AbsoluteWorkbenchApi::HostMode::Suppressed: return Probe::Suppressed;
    case AbsoluteWorkbenchApi::HostMode::Unavailable: return Probe::Unavailable;
    case AbsoluteWorkbenchApi::HostMode::Incompatible: return Probe::Incompatible;
    }
    return Probe::Incompatible;
}

Probe ProbeHotas() {
    const HMODULE module = GetModuleHandleW(L"AbsoluteHOTAS.dll");
    if (!module) return Probe::Unavailable;
    const FARPROC address = GetProcAddress(module, "AbsoluteHOTAS_QueryApi");
    if (!address) return Probe::Incompatible;
    const auto query = reinterpret_cast<AbsoluteHOTASApi::QueryApi>(address);
    const auto* api = query(AbsoluteHOTASApi::kAbiVersion);
    constexpr std::size_t minimum = offsetof(AbsoluteHOTASApi::ApiV1, cancelButtonCapture) +
                                    sizeof(api->cancelButtonCapture);
    const bool valid = api && api->structSize >= minimum &&
                       api->abiVersion == AbsoluteHOTASApi::kAbiVersion && api->moduleId &&
                       std::string_view(api->moduleId) == AbsoluteHOTASApi::kModuleId &&
                       api->getCommandBinding && api->clearCommandBinding &&
                       api->beginButtonCapture && api->pollButtonCapture &&
                       api->cancelButtonCapture;
    return valid ? Probe::Active : Probe::Incompatible;
}

} // namespace

namespace SuiteHost {

Selection Select() noexcept {
    try {
        const auto workbench = ProbeWorkbench();
        const auto hotas = ProbeHotas();
        if (hotas == Probe::Active) return Selection::Hotas;
        if (workbench == Probe::Active) return Selection::Workbench;

        if (workbench == Probe::Suppressed) return Selection::Suppressed;
        if (workbench == Probe::Incompatible || hotas == Probe::Incompatible)
            return Selection::Incompatible;
        return Selection::Unavailable;
    } catch (...) {
        return Selection::Incompatible;
    }
}

bool KeyboardInputSuppressed() noexcept {
    try {
        if (ControlPanelSubscriber::IsMenuOpen() ||
            ControlPanelSubscriber::IsInputCaptureActive()) {
            return true;
        }
        const auto* api = g_workbenchApi.load(std::memory_order_acquire);
        return api && (api->isOpen() || api->isInputCaptureActive());
    } catch (...) {
        return true;
    }
}

} // namespace SuiteHost
