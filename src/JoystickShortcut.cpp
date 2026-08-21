#include "PCH.h"

#include "JoystickShortcut.h"

#include <algorithm>
#include <charconv>
#include <format>
#include <string_view>

namespace AbsolutePower::JoystickBindingPolicy {
namespace {

std::string ToLower(std::string_view value) {
    std::string result(value);
    std::ranges::transform(result, result.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return result;
}

std::optional<std::uint32_t> ParseNumber(std::string_view text) noexcept {
    if (text.empty()) return std::nullopt;
    std::uint32_t value{};
    const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec == std::errc{} && ptr == text.data() + text.size()) {
        return value;
    }
    return std::nullopt;
}

} // namespace

bool ValidToken(std::string_view token) noexcept {
    return Parse(token).has_value();
}

std::optional<JoystickBinding> Parse(std::string_view token) noexcept {
    if (token.empty() || token.size() > 256) return std::nullopt;
    const auto atPos = token.find('@');
    if (atPos == std::string_view::npos || atPos == 0 || atPos + 1 >= token.size()) {
        return std::nullopt;
    }

    const auto guidPart = token.substr(0, atPos);
    const auto controlPart = token.substr(atPos + 1);

    // Validate GUID has braces or reasonable identifier form (at least 8 chars, no spaces)
    if (guidPart.size() < 4 || guidPart.contains(' ') || guidPart.contains('\r') ||
        guidPart.contains('\n')) {
        return std::nullopt;
    }

    const auto controlLower = ToLower(controlPart);
    std::uint32_t channelIndex = 0;

    if (controlLower.starts_with("button:")) {
        const auto numStr = controlPart.substr(std::string_view("button:").size());
        const auto num = ParseNumber(numStr);
        if (!num || *num < 1 || *num > 128) return std::nullopt;
        channelIndex = *num - 1; // 0..127
    } else if (controlLower.starts_with("pov:")) {
        const auto rest = controlPart.substr(std::string_view("pov:").size());
        const auto colonPos = rest.find(':');
        if (colonPos == std::string_view::npos) {
            // Raw POV code 129..144 or 0..15
            const auto num = ParseNumber(rest);
            if (!num) return std::nullopt;
            if (*num >= 129 && *num <= 144) {
                channelIndex = *num - 1; // 128..143
            } else if (*num < 16) {
                channelIndex = 128 + *num;
            } else {
                return std::nullopt;
            }
        } else {
            // Format: pov:<povIndex>:<dir> e.g. pov:0:up, pov:1:N
            const auto povIdxStr = rest.substr(0, colonPos);
            const auto dirStr = ToLower(rest.substr(colonPos + 1));
            const auto povIdx = ParseNumber(povIdxStr);
            if (!povIdx) return std::nullopt;

            std::uint32_t povNumber = *povIdx;
            if (povNumber >= 1 && povNumber <= 4) {
                povNumber -= 1; // normalize 1..4 to 0..3
            } else if (povNumber > 3) {
                return std::nullopt;
            }

            std::uint32_t dirOffset = 0;
            if (dirStr == "up" || dirStr == "north" || dirStr == "n" || dirStr == "0") dirOffset = 0;
            else if (dirStr == "right" || dirStr == "east" || dirStr == "e" || dirStr == "1") dirOffset = 1;
            else if (dirStr == "down" || dirStr == "south" || dirStr == "s" || dirStr == "2") dirOffset = 2;
            else if (dirStr == "left" || dirStr == "west" || dirStr == "w" || dirStr == "3") dirOffset = 3;
            else return std::nullopt;

            channelIndex = 128 + (povNumber * 4) + dirOffset;
        }
    } else {
        return std::nullopt;
    }

    return JoystickBinding{
        .persistentId = std::string(guidPart),
        .channelIndex = channelIndex,
        .originalToken = std::string(token),
    };
}

std::string FormatDisplay(std::string_view token,
                           std::string_view deviceProductName) noexcept {
    const auto parsed = Parse(token);
    if (!parsed) return std::string(token);

    std::string controlLabel;
    if (parsed->channelIndex < 128) {
        controlLabel = std::format("Button {}", parsed->channelIndex + 1);
    } else if (parsed->channelIndex < 144) {
        const auto povOffset = parsed->channelIndex - 128;
        const auto povIndex = povOffset / 4;
        const auto dirIndex = povOffset % 4;
        constexpr std::array<const char*, 4> dirs{"Up", "Right", "Down", "Left"};
        controlLabel = std::format("POV {} {}", povIndex + 1, dirs[dirIndex]);
    } else {
        controlLabel = std::format("Channel {}", parsed->channelIndex);
    }

    if (!deviceProductName.empty()) {
        return std::format("{} - {}", deviceProductName, controlLabel);
    }

    // If persistentId is a standard GUID like {12345678-...}, format cleanly
    return std::format("Joy {}", controlLabel);
}

} // namespace AbsolutePower::JoystickBindingPolicy
