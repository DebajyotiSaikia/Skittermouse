// macOS global hotkey (spec 4.1). Registers the "open the picker" combo with
// Carbon's RegisterEventHotKey -- the still-supported, low-overhead path for a
// system-wide hotkey that does not require an Accessibility-permission event tap.
// Native Carbon/HIToolbox, zero third-party.
//
// The core/hotkey parser emits Win32-style modifier flags and Windows virtual-key
// codes (so the one parser serves both platforms); here we translate those into
// Carbon modifier masks and macOS virtual key codes. Per spec 4.5 the physical key
// slots map across keyboards: the "Alt" slot is Option, the "Win" slot is Command.

#include "platform/global_hotkey.h"

#import <Carbon/Carbon.h>

#include <functional>

namespace {

std::function<void()> g_callback;
EventHotKeyRef g_hotkeyRef = nullptr;
EventHandlerRef g_handlerRef = nullptr;

OSStatus hotkeyEventHandler(EventHandlerCallRef, EventRef, void*) {
    if (g_callback) g_callback();
    return noErr;
}

UInt32 toCarbonModifiers(uint32_t mods) {
    using namespace sm::core::hotkey_mod;
    UInt32 c = 0;
    if (mods & Control) c |= controlKey;
    if (mods & Alt) c |= optionKey; // Alt occupies Option's physical slot (spec 4.5)
    if (mods & Shift) c |= shiftKey;
    if (mods & Win) c |= cmdKey;     // Win occupies Command's physical slot (spec 4.5)
    return c;
}

// Windows virtual-key code -> macOS virtual key code. Covers everything the
// core/hotkey parser can emit (letters, digits, space/enter/tab/esc, editing and
// arrow keys, F1..F20). Returns 0xFFFF for anything unmapped so registration fails
// cleanly rather than binding the wrong key.
UInt32 toMacKeyCode(uint16_t vk) {
    switch (vk) {
        // Letters (VK 'A'..'Z')
        case 'A': return 0x00; case 'B': return 0x0B; case 'C': return 0x08;
        case 'D': return 0x02; case 'E': return 0x0E; case 'F': return 0x03;
        case 'G': return 0x05; case 'H': return 0x04; case 'I': return 0x22;
        case 'J': return 0x26; case 'K': return 0x28; case 'L': return 0x25;
        case 'M': return 0x2E; case 'N': return 0x2D; case 'O': return 0x1F;
        case 'P': return 0x23; case 'Q': return 0x0C; case 'R': return 0x0F;
        case 'S': return 0x01; case 'T': return 0x11; case 'U': return 0x20;
        case 'V': return 0x09; case 'W': return 0x0D; case 'X': return 0x07;
        case 'Y': return 0x10; case 'Z': return 0x06;
        // Digits (VK '0'..'9')
        case '0': return 0x1D; case '1': return 0x12; case '2': return 0x13;
        case '3': return 0x14; case '4': return 0x15; case '5': return 0x17;
        case '6': return 0x16; case '7': return 0x1A; case '8': return 0x1C;
        case '9': return 0x19;
        // Named keys
        case 0x20: return 0x31; // Space
        case 0x0D: return 0x24; // Return
        case 0x09: return 0x30; // Tab
        case 0x1B: return 0x35; // Escape
        case 0x08: return 0x33; // Backspace (Delete-left)
        case 0x2E: return 0x75; // Delete (forward)
        case 0x2D: return 0x72; // Insert -> Help
        case 0x24: return 0x73; // Home
        case 0x23: return 0x77; // End
        case 0x21: return 0x74; // Page Up
        case 0x22: return 0x79; // Page Down
        case 0x25: return 0x7B; // Left
        case 0x26: return 0x7E; // Up
        case 0x27: return 0x7C; // Right
        case 0x28: return 0x7D; // Down
        // Function keys (VK_F1..VK_F20 = 0x70..0x83)
        case 0x70: return 0x7A; case 0x71: return 0x78; case 0x72: return 0x63;
        case 0x73: return 0x76; case 0x74: return 0x60; case 0x75: return 0x61;
        case 0x76: return 0x62; case 0x77: return 0x64; case 0x78: return 0x65;
        case 0x79: return 0x6D; case 0x7A: return 0x67; case 0x7B: return 0x6F;
        case 0x7C: return 0x69; case 0x7D: return 0x6B; case 0x7E: return 0x71;
        case 0x7F: return 0x6A; case 0x80: return 0x40; case 0x81: return 0x4F;
        case 0x82: return 0x50; case 0x83: return 0x5A;
        default: return 0xFFFF;
    }
}

} // namespace

namespace sm::platform {

bool registerGlobalHotkey(const sm::core::Hotkey& hk, std::function<void()> onPressed) {
    if (!hk.valid) return false;
    UInt32 code = toMacKeyCode(hk.key);
    if (code == 0xFFFF) return false;

    g_callback = std::move(onPressed);

    if (!g_handlerRef) {
        EventTypeSpec spec{kEventClassKeyboard, kEventHotKeyPressed};
        InstallApplicationEventHandler(&hotkeyEventHandler, 1, &spec, nullptr,
                                       &g_handlerRef);
    }
    if (g_hotkeyRef) {
        UnregisterEventHotKey(g_hotkeyRef);
        g_hotkeyRef = nullptr;
    }

    EventHotKeyID hotkeyId;
    hotkeyId.signature = 'sktm';
    hotkeyId.id = 1;
    OSStatus st = RegisterEventHotKey(code, toCarbonModifiers(hk.modifiers), hotkeyId,
                                      GetApplicationEventTarget(), 0, &g_hotkeyRef);
    return st == noErr;
}

void unregisterGlobalHotkey() {
    if (g_hotkeyRef) {
        UnregisterEventHotKey(g_hotkeyRef);
        g_hotkeyRef = nullptr;
    }
    if (g_handlerRef) {
        RemoveEventHandler(g_handlerRef);
        g_handlerRef = nullptr;
    }
    g_callback = nullptr;
}

} // namespace sm::platform
