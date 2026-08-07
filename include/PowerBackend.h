#pragma once

#include "PowerTypes.h"

#include <cstdint>

namespace AbsolutePower {

enum class BackendResult : std::uint8_t {
    Ok,
    WorkbenchMissing,
    UnsupportedRuntime,
    SnapshotSeamUnavailable,
    PilotNotReady,
    SystemUnavailable,
    InvalidRequest,
    SetterRejected,
};

class IPowerBackend {
public:
    virtual ~IPowerBackend() = default;
    virtual BackendResult Capture(Snapshot& snapshot) = 0;
    virtual BackendResult SetPower(SystemId system, std::uint16_t targetPips) = 0;
};

} // namespace AbsolutePower
