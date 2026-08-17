#include "KeyboardShortcut.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <format>

namespace {
constexpr std::uint8_t kBackspace = 0x08;
constexpr std::uint8_t kTab = 0x09;
constexpr std::uint8_t kEnter = 0x0D;
constexpr std::uint8_t kPause = 0x13;
constexpr std::uint8_t kCapsLock = 0x14;
constexpr std::uint8_t kSpace = 0x20;
constexpr std::uint8_t kPageUp = 0x21;
constexpr std::uint8_t kPageDown = 0x22;
constexpr std::uint8_t kEnd = 0x23;
constexpr std::uint8_t kHome = 0x24;
constexpr std::uint8_t kLeft = 0x25;
constexpr std::uint8_t kUp = 0x26;
constexpr std::uint8_t kRight = 0x27;
constexpr std::uint8_t kDown = 0x28;
constexpr std::uint8_t kPrintScreen = 0x2C;
constexpr std::uint8_t kInsert = 0x2D;
constexpr std::uint8_t kDelete = 0x2E;
constexpr std::uint8_t kNumpad0 = 0x60;
constexpr std::uint8_t kNumpad9 = 0x69;
constexpr std::uint8_t kMultiply = 0x6A;
constexpr std::uint8_t kAdd = 0x6B;
constexpr std::uint8_t kSubtract = 0x6D;
constexpr std::uint8_t kDecimal = 0x6E;
constexpr std::uint8_t kDivide = 0x6F;
constexpr std::uint8_t kF1 = 0x70;
constexpr std::uint8_t kF24 = 0x87;

std::string Normalize(std::string_view text) {
    std::string normalized;
    normalized.reserve(text.size());
    for (const unsigned char character : text) {
        if (std::isspace(character) == 0 && character != '_' && character != '-') {
            normalized.push_back(static_cast<char>(std::toupper(character)));
        }
    }
    return normalized;
}

bool ModifierVirtualKey(std::uint8_t key) {
    return key == 0x10 || key == 0x11 || key == 0x12 || (key >= 0x5B && key <= 0x5C) ||
           (key >= 0xA0 && key <= 0xA5);
}

std::optional<std::uint8_t> NamedKey(std::string_view token) {
    const std::string name = Normalize(token);
    if (name.size() == 1 && ((name[0] >= 'A' && name[0] <= 'Z') ||
                            (name[0] >= '0' && name[0] <= '9'))) {
        return static_cast<std::uint8_t>(name[0]);
    }
    if (name.starts_with('F') && name.size() <= 3) {
        int number{};
        const auto parsed = std::from_chars(name.data() + 1, name.data() + name.size(), number);
        if (parsed.ec == std::errc{} && parsed.ptr == name.data() + name.size() &&
            number >= 1 && number <= 24) {
            return static_cast<std::uint8_t>(kF1 + number - 1);
        }
    }
    if (name.starts_with("NUMPAD") && name.size() == 7 && name.back() >= '0' &&
        name.back() <= '9') {
        return static_cast<std::uint8_t>(kNumpad0 + name.back() - '0');
    }
    if (name.starts_with("VK") && name.size() == 4) {
        unsigned int value{};
        const auto parsed = std::from_chars(name.data() + 2, name.data() + 4, value, 16);
        if (parsed.ec == std::errc{} && parsed.ptr == name.data() + 4 && value > 0 &&
            value <= 0xFF) {
            return static_cast<std::uint8_t>(value);
        }
    }
    static constexpr std::array<std::pair<std::string_view, std::uint8_t>, 25> names{{
        {"BACKSPACE", kBackspace}, {"TAB", kTab},           {"ENTER", kEnter},
        {"PAUSE", kPause},         {"CAPSLOCK", kCapsLock}, {"SPACE", kSpace},
        {"PAGEUP", kPageUp},       {"PAGEDOWN", kPageDown}, {"END", kEnd},
        {"HOME", kHome},           {"LEFT", kLeft},         {"UP", kUp},
        {"RIGHT", kRight},         {"DOWN", kDown},         {"PRINTSCREEN", kPrintScreen},
        {"INSERT", kInsert},       {"DELETE", kDelete},     {"NUMPADMULTIPLY", kMultiply},
        {"NUMPADADD", kAdd},       {"NUMPADPLUS", kAdd},    {"NUMPADSUBTRACT", kSubtract},
        {"NUMPADMINUS", kSubtract},{"NUMPADDECIMAL", kDecimal},
        {"NUMPADDIVIDE", kDivide}, {"NUMPADSLASH", kDivide},
    }};
    const auto found = std::ranges::find(
        names, name, &std::pair<std::string_view, std::uint8_t>::first);
    return found == names.end() ? std::nullopt : std::optional(found->second);
}

std::optional<AbsolutePower::KeyboardChord> ParseControlPanelCapture(
    std::string_view text) {
    constexpr std::string_view prefix = "keyboard:0x";
    if (!text.starts_with(prefix)) return std::nullopt;
    text.remove_prefix(prefix.size());
    const auto keyEnd = text.find(';');
    if (keyEnd == std::string_view::npos || keyEnd == 0) return std::nullopt;
    unsigned int key{};
    const auto keyText = text.substr(0, keyEnd);
    const auto parsed = std::from_chars(keyText.data(), keyText.data() + keyText.size(), key, 16);
    if (parsed.ec != std::errc{} || parsed.ptr != keyText.data() + keyText.size() ||
        key == 0 || key > 0xFF) {
        return std::nullopt;
    }
    text.remove_prefix(keyEnd + 1);

    const auto takeBoolean = [&](std::string_view name, bool& output) {
        if (!text.starts_with(name)) return false;
        text.remove_prefix(name.size());
        if (text.empty() || (text.front() != '0' && text.front() != '1')) return false;
        output = text.front() == '1';
        text.remove_prefix(1);
        return true;
    };
    AbsolutePower::KeyboardChord chord{.virtualKey = static_cast<std::uint8_t>(key)};
    if (!takeBoolean("ctrl=", chord.control) || text.empty() || text.front() != ';') {
        return std::nullopt;
    }
    text.remove_prefix(1);
    if (!takeBoolean("alt=", chord.alt) || text.empty() || text.front() != ';') {
        return std::nullopt;
    }
    text.remove_prefix(1);
    if (!takeBoolean("shift=", chord.shift) || !text.empty()) return std::nullopt;
    return chord;
}

std::string StorageKey(std::uint8_t key) {
    if ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9')) {
        return std::string(1, static_cast<char>(key));
    }
    if (key >= kF1 && key <= kF24) return std::format("F{}", key - kF1 + 1);
    if (key >= kNumpad0 && key <= kNumpad9) {
        return std::format("Numpad{}", key - kNumpad0);
    }
    switch (key) {
    case kBackspace: return "Backspace";
    case kTab: return "Tab";
    case kEnter: return "Enter";
    case kPause: return "Pause";
    case kCapsLock: return "CapsLock";
    case kSpace: return "Space";
    case kPageUp: return "PageUp";
    case kPageDown: return "PageDown";
    case kEnd: return "End";
    case kHome: return "Home";
    case kLeft: return "Left";
    case kUp: return "Up";
    case kRight: return "Right";
    case kDown: return "Down";
    case kPrintScreen: return "PrintScreen";
    case kInsert: return "Insert";
    case kDelete: return "Delete";
    case kMultiply: return "NumpadMultiply";
    case kAdd: return "NumpadAdd";
    case kSubtract: return "NumpadSubtract";
    case kDecimal: return "NumpadDecimal";
    case kDivide: return "NumpadDivide";
    default: return std::format("VK{:02X}", key);
    }
}

void Append(std::string& result, std::string_view part) {
    if (!result.empty()) result += '+';
    result += part;
}
} // namespace

namespace AbsolutePower::KeyboardShortcutPolicy {

bool Valid(const KeyboardChord& chord) noexcept {
    return chord.virtualKey != 0 && !ModifierVirtualKey(chord.virtualKey);
}

std::optional<KeyboardChord> Parse(std::string_view text) {
    if (const auto captured = ParseControlPanelCapture(text)) {
        return Valid(*captured) ? captured : std::nullopt;
    }
    KeyboardChord result{};
    bool foundKey{};
    std::size_t start{};
    while (start <= text.size()) {
        const std::size_t delimiter = text.find('+', start);
        const std::string token = Normalize(text.substr(start, delimiter - start));
        if (token.empty()) return std::nullopt;
        if (token == "CTRL" || token == "CONTROL") {
            if (result.control || foundKey) return std::nullopt;
            result.control = true;
        } else if (token == "ALT") {
            if (result.alt || foundKey) return std::nullopt;
            result.alt = true;
        } else if (token == "SHIFT") {
            if (result.shift || foundKey) return std::nullopt;
            result.shift = true;
        } else {
            const auto key = NamedKey(token);
            if (foundKey || !key || ModifierVirtualKey(*key)) return std::nullopt;
            result.virtualKey = *key;
            foundKey = true;
        }
        if (delimiter == std::string_view::npos) break;
        start = delimiter + 1;
    }
    return foundKey ? std::optional(result) : std::nullopt;
}

std::string StorageName(const KeyboardChord& chord) {
    if (!Valid(chord)) return {};
    std::string result;
    if (chord.control) Append(result, "Ctrl");
    if (chord.alt) Append(result, "Alt");
    if (chord.shift) Append(result, "Shift");
    Append(result, StorageKey(chord.virtualKey));
    return result;
}

bool Matches(const KeyboardChord& chord, bool keyDown, bool controlDown, bool altDown,
             bool shiftDown) noexcept {
    return Valid(chord) && keyDown && controlDown == chord.control && altDown == chord.alt &&
           shiftDown == chord.shift;
}

bool ConsumePressEdge(bool down, bool suppressed, bool& previousDown) noexcept {
    const bool pressed = !suppressed && down && !previousDown;
    previousDown = down;
    return pressed;
}

} // namespace AbsolutePower::KeyboardShortcutPolicy
