#pragma once

// Cross-OS modifier key translation by PHYSICAL key position (spec Section 4.5).
//
// PURE LOGIC -- no OS includes. We define our own small, fixed set of modifier
// keycode constants rather than pulling in <Windows.h> / Carbon so core/ stays
// platform-free (Section 2.2). The numeric values match the real OS virtual-key
// (Windows) and CGKeyCode (macOS) numbers, so the injection layer can hand these
// straight to SendInput / CGEventPost without a second lookup.
//
// Only relevant when a Windows machine and a Mac are paired; same-OS pairs get an
// identity mapping (Section 4.5). Remap is by physical slot, not label, so muscle
// memory transfers:
//     Ctrl slot   <-> Ctrl slot
//     Win slot    <-> Option slot   (2nd physical modifier)
//     Alt slot    <-> Cmd slot      (3rd physical modifier)
//     Shift slot  <-> Shift slot
// Left/right handedness is preserved.

#include <cstdint>

namespace sm::core {

enum class Os : uint8_t { Windows = 0, MacOS = 1 };

// Windows virtual-key codes for the modifier keys (winuser.h VK_* values).
namespace win_vk {
inline constexpr uint8_t LControl = 0xA2; // VK_LCONTROL
inline constexpr uint8_t RControl = 0xA3; // VK_RCONTROL
inline constexpr uint8_t LWin     = 0x5B; // VK_LWIN
inline constexpr uint8_t RWin     = 0x5C; // VK_RWIN
inline constexpr uint8_t LAlt     = 0xA4; // VK_LMENU
inline constexpr uint8_t RAlt     = 0xA5; // VK_RMENU
inline constexpr uint8_t LShift   = 0xA0; // VK_LSHIFT
inline constexpr uint8_t RShift   = 0xA1; // VK_RSHIFT
} // namespace win_vk

// macOS CGKeyCode values for the modifier keys (Carbon HIToolbox kVK_* values).
namespace mac_vk {
inline constexpr uint8_t Control  = 0x3B; // kVK_Control
inline constexpr uint8_t RControl = 0x3E; // kVK_RightControl
inline constexpr uint8_t Option   = 0x3A; // kVK_Option
inline constexpr uint8_t ROption  = 0x3D; // kVK_RightOption
inline constexpr uint8_t Command  = 0x37; // kVK_Command
inline constexpr uint8_t RCommand = 0x36; // kVK_RightCommand
inline constexpr uint8_t Shift    = 0x38; // kVK_Shift
inline constexpr uint8_t RShift   = 0x3C; // kVK_RightShift
} // namespace mac_vk

// Translate a modifier keycode from the source OS's space to the target OS's
// space by physical position. Non-modifier codes and same-OS translation are
// returned unchanged. Windows<->macOS modifier mappings are exact inverses.
uint8_t translateModifier(Os from, Os to, uint8_t code);

} // namespace sm::core
