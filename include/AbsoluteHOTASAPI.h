#pragma once

// Minimal copy of AbsoluteHOTAS's stable suite-command binding ABI. Absolute
// Power only uses this for a positive compatible-host handshake; no C++ object
// or device state crosses the DLL boundary.

#include <cstddef>
#include <cstdint>

namespace AbsoluteHOTASApi {

inline constexpr std::uint32_t kAbiVersion = 1;
inline constexpr const char* kModuleId = "absolute.hotas";

enum class Result : std::uint32_t {
    Ok,
    NotReady,
    InvalidArgument,
    NotFound,
    Busy,
    WriteFailure,
};

struct CommandBindingV1;
struct CaptureV1;

struct ApiV1 {
    std::uint32_t structSize{sizeof(ApiV1)};
    std::uint32_t abiVersion{kAbiVersion};
    const char* moduleId{};
    const char* displayName{};
    const char* version{};

    Result(__cdecl* getCommandBinding)(const char*, const char*, CommandBindingV1*) noexcept{};
    Result(__cdecl* clearCommandBinding)(const char*, const char*) noexcept{};
    Result(__cdecl* beginButtonCapture)(const char*, const char*) noexcept{};
    Result(__cdecl* pollButtonCapture)(CaptureV1*) noexcept{};
    Result(__cdecl* cancelButtonCapture)() noexcept{};
};

using QueryApi = const ApiV1*(__cdecl*)(std::uint32_t) noexcept;

} // namespace AbsoluteHOTASApi
