#include "KeyboardShortcut.h"

#include <cassert>

using namespace AbsolutePower;

int main() {
    const auto chord = KeyboardShortcutPolicy::Parse("Ctrl+Shift+F8");
    assert(chord);
    assert(chord->virtualKey == 0x77);
    assert(chord->control && chord->shift && !chord->alt);
    assert(KeyboardShortcutPolicy::StorageName(*chord) == "Ctrl+Shift+F8");
    assert(KeyboardShortcutPolicy::Matches(*chord, true, true, false, true));
    assert(!KeyboardShortcutPolicy::Matches(*chord, true, true, true, true));

    const auto numpad = KeyboardShortcutPolicy::Parse("Alt+NumpadAdd");
    assert(numpad && numpad->virtualKey == 0x6B && numpad->alt);
    assert(KeyboardShortcutPolicy::Parse(
               KeyboardShortcutPolicy::StorageName(*numpad)) == numpad);

    const auto captured = KeyboardShortcutPolicy::Parse(
        "keyboard:0x77;ctrl=1;alt=0;shift=1");
    assert(captured == chord);
    assert(!KeyboardShortcutPolicy::Parse(
        "keyboard:0x77;ctrl=1;alt=0;shift=2"));
    assert(!KeyboardShortcutPolicy::Parse(
        "keyboard:0x00;ctrl=0;alt=0;shift=0"));

    assert(!KeyboardShortcutPolicy::Parse("Ctrl+Alt"));
    assert(!KeyboardShortcutPolicy::Parse("Ctrl++A"));
    assert(!KeyboardShortcutPolicy::Parse("A+B"));
    assert(!KeyboardShortcutPolicy::Parse("Ctrl+Ctrl+A"));

    bool previous{};
    assert(KeyboardShortcutPolicy::ConsumePressEdge(true, false, previous));
    assert(!KeyboardShortcutPolicy::ConsumePressEdge(true, false, previous));
    assert(!KeyboardShortcutPolicy::ConsumePressEdge(false, false, previous));
    assert(!KeyboardShortcutPolicy::ConsumePressEdge(true, true, previous));
    assert(!KeyboardShortcutPolicy::ConsumePressEdge(true, false, previous));
    assert(!KeyboardShortcutPolicy::ConsumePressEdge(false, false, previous));
    assert(KeyboardShortcutPolicy::ConsumePressEdge(true, false, previous));
    return 0;
}
