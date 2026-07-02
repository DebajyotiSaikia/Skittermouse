#pragma once

// System-wide hotkey registration (spec 4.1). PURE INTERFACE -- implemented per OS
// (Windows registers inline in tray_win.cpp via RegisterHotKey; macOS in
// hotkey_mac.mm via Carbon RegisterEventHotKey). Only one active registration at a
// time, matching the single "open the picker" combo.

#include <functional>

#include "core/hotkey.h"

namespace sm::platform {

// Registers `hk` as a global hotkey; `onPressed` fires on the main run loop each
// time the combo is pressed. Returns false if hk is invalid or the OS rejects the
// combo (e.g. already owned by another app) -- the caller then tries a fallback
// combo, same policy as the Windows tray.
bool registerGlobalHotkey(const sm::core::Hotkey& hk, std::function<void()> onPressed);

// Removes the active registration (if any).
void unregisterGlobalHotkey();

} // namespace sm::platform
