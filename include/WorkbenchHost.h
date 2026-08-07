#pragma once

namespace WorkbenchHost {
enum class Selection { Active, Suppressed, Unavailable, Incompatible };
[[nodiscard]] Selection Select() noexcept;
} // namespace WorkbenchHost
