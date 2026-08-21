#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace AbsolutePower {

struct JoystickBinding {
    std::string persistentId; // e.g. "{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}"
    std::uint32_t channelIndex{}; // 0..127 = button 1..128, 128..143 = POV 129..144
    std::string originalToken; // e.g. "{GUID}@button:7"

    bool operator==(const JoystickBinding&) const = default;
};

struct JoystickShortcut {
    std::string presetId;
    std::string token; // serialized token, e.g. "{GUID}@button:7"

    bool operator==(const JoystickShortcut&) const = default;
};

struct JoystickShortcutEdit {
    std::string presetId;
    std::optional<std::string> token;
};

namespace JoystickBindingPolicy {
[[nodiscard]] bool ValidToken(std::string_view token) noexcept;
[[nodiscard]] std::optional<JoystickBinding> Parse(std::string_view token) noexcept;
[[nodiscard]] std::string FormatDisplay(std::string_view token,
                                        std::string_view deviceProductName = {}) noexcept;
} // namespace JoystickBindingPolicy

} // namespace AbsolutePower
