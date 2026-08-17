#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace AbsolutePower {

struct KeyboardChord {
    std::uint8_t virtualKey{};
    bool control{};
    bool alt{};
    bool shift{};

    bool operator==(const KeyboardChord&) const = default;
};

struct PresetShortcut {
    std::string presetId;
    KeyboardChord chord;

    bool operator==(const PresetShortcut&) const = default;
};

namespace KeyboardShortcutPolicy {
[[nodiscard]] bool Valid(const KeyboardChord& chord) noexcept;
[[nodiscard]] std::optional<KeyboardChord> Parse(std::string_view text);
[[nodiscard]] std::string StorageName(const KeyboardChord& chord);
[[nodiscard]] bool Matches(const KeyboardChord& chord, bool keyDown, bool controlDown,
                           bool altDown, bool shiftDown) noexcept;
[[nodiscard]] bool ConsumePressEdge(bool down, bool suppressed,
                                    bool& previousDown) noexcept;
} // namespace KeyboardShortcutPolicy

} // namespace AbsolutePower
