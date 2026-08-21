#include "PCH.h"

#include "PowerRuntime.h"
#include "ControlPanelSubscriber.h"
#include "GameTaskScheduler.h"
#include "GameUpdateHook.h"
#include "InputBusClient.h"
#include "RuntimePaths.h"
#include "SuiteHost.h"
#include "WeaponFireHook.h"

namespace {
std::atomic<bool> g_initialized{};
std::atomic<bool> g_gameTaskQueued{};
const SFSE::TaskInterface* g_tasks{};
bool g_taskAvailable{};

class PowerGameTask final : public SFSE::ITaskDelegate {
public:
    void Run() override {
        g_gameTaskQueued.store(false, std::memory_order_release);
        if (!ControlPanelSubscriber::IsHosted()) {
            (void)ControlPanelSubscriber::RegisterDiscoveredHost();
        }
        AbsolutePower::PowerRuntime::Get().TickGameThread();
        ControlPanelSubscriber::PublishLiveState();
    }
    void Destroy() override { delete this; }
};

void QueuePowerGameTask() noexcept {
    if (!g_tasks || g_gameTaskQueued.exchange(true, std::memory_order_acq_rel)) return;
    g_tasks->AddTask(new PowerGameTask());
}

void SelectHostAndInitialize() {
    if (g_initialized.exchange(true)) return;
    AbsolutePower::InputBusClient::Get().Discover();
    const auto selection = SuiteHost::Select();
    // HOTAS owns the selected-handler slot when present and supplies the neutral
    // game-thread callback through the Power API. In every other configuration,
    // including fully standalone operation, Power owns its exact-gated hook.
    const bool hotasBridge = selection == SuiteHost::Selection::Hotas;
    const bool shipUpdateAvailable = hotasBridge || GameUpdateHook::Install();
    const bool nativeWeaponInputReady = WeaponFireHook::Install();
    AbsolutePower::PowerRuntime::Get().SetGameThreadAvailable(
        shipUpdateAvailable);
    AbsolutePower::PowerRuntime::Get().SetNativeWeaponInputReady(
        nativeWeaponInputReady);
    AbsolutePower::PowerRuntime::Get().Initialize();
    GameTaskScheduler::Request();
    RuntimePaths::Log(
        "Dependency",
        selection == SuiteHost::Selection::Workbench
            ? "Standalone Power runtime initialized with the optional Absolute Workbench keyboard/mouse interface."
            : selection == SuiteHost::Selection::Hotas
                  ? "Standalone Power runtime initialized with the optional AbsoluteHOTAS interface and selected-handler executor."
                  : selection == SuiteHost::Selection::Suppressed
                        ? "Absolute Workbench is suppressed; Absolute Power is running from its standalone configuration."
                        : selection == SuiteHost::Selection::Unavailable
                              ? "No interface host detected; Absolute Power is running from its standalone configuration."
                              : "An installed interface host is ABI-incompatible; Absolute Power remains available in standalone configuration mode.",
        selection == SuiteHost::Selection::Incompatible || !shipUpdateAvailable);
    if (!shipUpdateAvailable) {
        RuntimePaths::Log(
            "Executor",
            "No validated game-update executor is available; configurations remain loaded but cannot be applied.",
            true);
    }
}

void OnSfseMessage(SFSE::MessagingInterface::Message* message) {
    if (message && message->type == SFSE::MessagingInterface::kPostPostLoad) {
        SelectHostAndInitialize();
    } else if (message && message->type == SFSE::MessagingInterface::kPostDataLoad) {
        const auto result = ControlPanelSubscriber::RegisterDiscoveredHost();
        if (result != AbsoluteControlPanelApi::Result::Ok &&
            result != AbsoluteControlPanelApi::Result::Duplicate &&
            result != AbsoluteControlPanelApi::Result::NotFound &&
            result != AbsoluteControlPanelApi::Result::NotReady) {
            RuntimePaths::Log(
                "ControlPanel",
                std::format("Absolute Control rejected Power registration (result {}).",
                            static_cast<std::uint32_t>(result)),
                true);
        }
        GameTaskScheduler::Request();
    }
}
} // namespace

namespace GameTaskScheduler {
void Request() noexcept { QueuePowerGameTask(); }
} // namespace GameTaskScheduler

SFSE_PLUGIN_LOAD(const SFSE::LoadInterface* loadInterface) {
    RuntimePaths::InitializeLogging();
    RuntimePaths::Log("Main",
                      std::format("{} {} startup.", Plugin::FriendlyName, Plugin::VersionString),
                      true);
    if (!loadInterface) {
        RuntimePaths::Log("Dependency",
                          "SFSE load interface is null; standalone configuration loaded without an executor.",
                          true);
        AbsolutePower::PowerRuntime::Get().Initialize();
        return true;
    }
    g_tasks = loadInterface->GetTaskInterface();
    g_taskAvailable = g_tasks && g_tasks->Version() >= 1;
    if (g_taskAvailable) {
        RuntimePaths::Log("Main", "SFSE one-shot game-task scheduler is available.");
    } else {
        RuntimePaths::Log(
            "Main",
            "SFSE task interface is unavailable; Power plans can be edited but cannot be applied.",
            true);
    }
    AbsolutePower::PowerRuntime::Get().SetGameThreadAvailable(false);
    const auto* messaging = loadInterface->GetMessagingInterface();
    if (!messaging || messaging->Version() < 1 ||
        !messaging->RegisterListener(loadInterface->GetPluginHandle(), &OnSfseMessage)) {
        RuntimePaths::Log(
            "Dependency",
            "Could not subscribe to SFSE post-post-load messaging; standalone configuration "
            "loaded without making a load-order-dependent executor decision.",
            true);
        AbsolutePower::PowerRuntime::Get().Initialize();
        return true;
    }
    RuntimePaths::Log("Main", "Host selection deferred until all SFSE plugins are loaded.");
    return true;
}
