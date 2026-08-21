#include "JoystickShortcut.h"
#include "InputBusClient.h"

#include <cassert>
#include <string>

using namespace AbsolutePower;

int main() {
    // 1. Valid button tokens
    const auto btn1 = JoystickBindingPolicy::Parse("{01234567-89AB-CDEF-0123-456789ABCDEF}@button:1");
    assert(btn1.has_value());
    assert(btn1->persistentId == "{01234567-89AB-CDEF-0123-456789ABCDEF}");
    assert(btn1->channelIndex == 0); // 0-indexed channel for button 1
    assert(JoystickBindingPolicy::ValidToken("{01234567-89AB-CDEF-0123-456789ABCDEF}@button:1"));

    const auto btn128 = JoystickBindingPolicy::Parse("{01234567-89ab-cdef-0123-456789abcdef}@button:128");
    assert(btn128.has_value());
    assert(btn128->channelIndex == 127); // 0-indexed channel for button 128
    assert(btn128->persistentId == "{01234567-89AB-CDEF-0123-456789ABCDEF}");

    // Invalid button index (0, 129)
    assert(!JoystickBindingPolicy::Parse("{01234567-89AB-CDEF-0123-456789ABCDEF}@button:0"));
    assert(!JoystickBindingPolicy::Parse("{01234567-89AB-CDEF-0123-456789ABCDEF}@button:129"));

    // 2. Valid POV tokens
    const auto povN = JoystickBindingPolicy::Parse("{01234567-89AB-CDEF-0123-456789ABCDEF}@pov:1:N");
    assert(povN.has_value());
    assert(povN->channelIndex == 128); // 128 = POV 1 Up

    const auto povSW = JoystickBindingPolicy::Parse("{01234567-89AB-CDEF-0123-456789ABCDEF}@pov:4:SW");
    assert(povSW.has_value());
    // POV 4 = channels 128 + 3*4 = 140 base; SW is direction 5 (0:N, 1:NE, 2:E, 3:SE, 4:S, 5:SW, 6:W, 7:NW) -> index 140 + 5/2 = 142/143
    assert(povSW->channelIndex >= 128 && povSW->channelIndex < 144);

    // Numeric POV index (0..3 maps to pov 1..4 North)
    const auto povNum = JoystickBindingPolicy::Parse("{01234567-89AB-CDEF-0123-456789ABCDEF}@pov:0");
    assert(povNum.has_value());
    assert(povNum->channelIndex == 128);

    // Invalid POV index / direction
    assert(!JoystickBindingPolicy::Parse("{01234567-89AB-CDEF-0123-456789ABCDEF}@pov:5:N"));
    assert(!JoystickBindingPolicy::Parse("{01234567-89AB-CDEF-0123-456789ABCDEF}@pov:1:INVALID"));

    // 3. User-facing labels
    assert(JoystickBindingPolicy::FormatDisplay("{01234567-89AB-CDEF-0123-456789ABCDEF}@button:1") == "Button 1");
    assert(JoystickBindingPolicy::FormatDisplay("{01234567-89AB-CDEF-0123-456789ABCDEF}@pov:1:N") == "POV 1 Up");
    assert(JoystickBindingPolicy::FormatDisplay("{01234567-89AB-CDEF-0123-456789ABCDEF}@pov:4:SW") == "POV 4 Down-Left");

    // 4. Fallback formatting with product name
    assert(JoystickBindingPolicy::FormatDisplay("{01234567-89AB-CDEF-0123-456789ABCDEF}@button:7", "VKB Gladiator") == "VKB Gladiator Button 7");
    assert(InputBusClient::Get().FormatBinding("{01234567-89AB-CDEF-0123-456789ABCDEF}@button:7") == "Button 7");
    assert(InputBusClient::Get().FormatBinding("{01234567-89AB-CDEF-0123-456789ABCDEF}@pov:2:E") == "POV 2 Right");

    return 0;
}
