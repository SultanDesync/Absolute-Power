#include "RuntimePaths.h"

#include <windows.h>

#include <array>
#include <atomic>
#include <cwctype>
#include <fstream>
#include <mutex>

namespace {
std::atomic<bool> g_loggingEnabled{true};
std::mutex g_logMutex;

std::filesystem::path ModulePath() {
    HMODULE module{};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&ModulePath), &module);
    std::array<wchar_t, 32768> path{};
    if (module && GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()))) {
        return path.data();
    }
    return std::filesystem::current_path() / L"Data" / L"SFSE" / L"Plugins" /
           L"AbsolutePower.dll";
}

bool ReadBool(const std::filesystem::path& path, bool fallback) {
    std::array<wchar_t, 32> value{};
    GetPrivateProfileStringW(L"General", L"bEnableLog", fallback ? L"true" : L"false",
                             value.data(), static_cast<DWORD>(value.size()), path.c_str());
    std::wstring normalized(value.data());
    std::ranges::transform(normalized, normalized.begin(),
                           [](wchar_t character) { return std::towlower(character); });
    if (normalized == L"true" || normalized == L"1" || normalized == L"yes" ||
        normalized == L"on") return true;
    if (normalized == L"false" || normalized == L"0" || normalized == L"no" ||
        normalized == L"off") return false;
    return fallback;
}
} // namespace

namespace RuntimePaths {
std::filesystem::path PluginDirectory() { return ModulePath().parent_path(); }
std::filesystem::path DefaultsPath() { return PluginDirectory() / L"AbsolutePower.ini"; }
std::filesystem::path CustomPath() { return PluginDirectory() / L"AbsolutePower_Custom.ini"; }
std::filesystem::path WorkbenchPath() { return PluginDirectory() / L"AbsoluteWorkbench.dll"; }
std::filesystem::path LogPath() { return PluginDirectory() / L"AbsolutePower.log"; }

void InitializeLogging() {
    bool enabled = ReadBool(DefaultsPath(), true);
    if (std::filesystem::exists(CustomPath())) enabled = ReadBool(CustomPath(), enabled);
    g_loggingEnabled.store(enabled, std::memory_order_release);
}

bool IsLoggingEnabled() { return g_loggingEnabled.load(std::memory_order_acquire); }

void Log(std::string_view area, std::string_view message, bool force) {
    if (!force && !IsLoggingEnabled()) return;
    std::scoped_lock lock(g_logMutex);
    std::ofstream stream(LogPath(), std::ios::app);
    if (stream) stream << '[' << area << "] " << message << '\n';
}
} // namespace RuntimePaths
