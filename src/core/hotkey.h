#pragma once

// Hotkey string parsing (spec 4.1). Turns a user-configurable combo like
// "Ctrl+Alt+Space" into Win32 RegisterHotKey modifier flags + a virtual-key code.
// PURE LOGIC (no OS includes) -- the modifier flag values intentionally match
// Win32's fsModifiers so platform/hotkey_win can pass them straight through.

#include <cstdint>
#include <string>

namespace sm::core {

namespace hotkey_mod {
inline constexpr uint32_t Alt = 0x0001;     // MOD_ALT
inline constexpr uint32_t Control = 0x0002; // MOD_CONTROL
inline constexpr uint32_t Shift = 0x0004;   // MOD_SHIFT
inline constexpr uint32_t Win = 0x0008;     // MOD_WIN
} // namespace hotkey_mod

struct Hotkey {
    uint32_t modifiers = 0; // OR of hotkey_mod::*
    uint16_t key = 0;       // virtual-key code
    bool valid = false;
};

// Parse a "+"-separated, case-insensitive combo. Requires exactly one non-modifier
// key. Returns valid=false for empty/garbage/no-key/two-keys input.
Hotkey parseHotkey(const std::string& s);

} // namespace sm::core
