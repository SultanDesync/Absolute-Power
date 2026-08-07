#pragma once

#include <cstddef>
#include <cstdint>

namespace AbsoluteWorkbenchApi {
inline constexpr std::uint32_t kAbiVersion = 1;
inline constexpr const char* kModuleId = "absolute.workbench";
enum class Result : std::uint32_t {
    Ok,
    NotReady,
    InvalidArgument,
    NotFound,
    Rejected,
    InternalError,
};
enum class HostMode : std::uint32_t { Active, Suppressed, Unavailable, Incompatible };
struct HostApiV1 {
    std::uint32_t structSize{};
    std::uint32_t abiVersion{};
    const char* moduleId{};
    const char* version{};
    HostMode(__cdecl* getHostMode)() noexcept{};
    Result(__cdecl* requestOpen)(const char*, const char*) noexcept{};
    std::uint8_t(__cdecl* isOpen)() noexcept{};
    std::uint8_t(__cdecl* isInputCaptureActive)() noexcept{};
    const void* reserved[8]{};
};
using QueryHostApi = const HostApiV1*(__cdecl*)(std::uint32_t) noexcept;
} // namespace AbsoluteWorkbenchApi
