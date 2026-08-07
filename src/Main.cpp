#include "PCH.h"

#include "PowerRuntime.h"
#include "RuntimePaths.h"
#include "WorkbenchHost.h"

namespace {
std::atomic<bool> g_initialized{};

void SelectHostAndInitialize() {
    if (g_initialized.exchange(true)) return;
    const auto selection = WorkbenchHost::Select();
    const bool active = selection == WorkbenchHost::Selection::Active;
    AbsolutePower::PowerRuntime::Get().Initialize(active);
    RuntimePaths::Log(
        "Dependency",
        active ? "Positive Absolute Workbench ABI handshake accepted. Absolute Power API and "
                 "runtime initialized; native snapshot seam remains fail-closed in this alpha."
               : selection == WorkbenchHost::Selection::Suppressed
                     ? "Absolute Workbench graphics are suppressed. Absolute Power remains inert."
                     : selection == WorkbenchHost::Selection::Unavailable
                           ? "Absolute Workbench is unavailable. Absolute Power remains inert."
                           : "Absolute Workbench is incompatible. Absolute Power remains inert.",
        true);
}

void OnSfseMessage(SFSE::MessagingInterface::Message* message) {
    if (message && message->type == SFSE::MessagingInterface::kPostPostLoad) {
        SelectHostAndInitialize();
    }
}
} // namespace

SFSE_PLUGIN_LOAD(const SFSE::LoadInterface* loadInterface) {
    RuntimePaths::InitializeLogging();
    RuntimePaths::Log("Main",
                      std::format("{} {} startup.", Plugin::FriendlyName, Plugin::VersionString),
                      true);
    if (!loadInterface) {
        RuntimePaths::Log("Dependency",
                          "SFSE load interface is null; Workbench handshake cannot be selected.",
                          true);
        AbsolutePower::PowerRuntime::Get().Initialize(false);
        return true;
    }
    const auto* messaging = loadInterface->GetMessagingInterface();
    if (!messaging || messaging->Version() < 1 ||
        !messaging->RegisterListener(loadInterface->GetPluginHandle(), &OnSfseMessage)) {
        RuntimePaths::Log(
            "Dependency",
            "Could not subscribe to SFSE post-post-load messaging; Absolute Power remains inert "
            "instead of making a load-order-dependent dependency decision.",
            true);
        AbsolutePower::PowerRuntime::Get().Initialize(false);
        return true;
    }
    RuntimePaths::Log("Main", "Host selection deferred until all SFSE plugins are loaded.");
    return true;
}
