#pragma once

// Native settings window (spec 10/16). Replaces "open the config in a text editor"
// with a real GUI: edit the hotkey and toggle the boolean settings, then Save. PURE
// INTERFACE -- implemented per-OS (Win32 dialog; macOS Cocoa panel).

#include "core/config.h"

namespace sm::ui {

// Show a modal settings window seeded from `config`. On Save, writes the edited
// values back into `config.settings` and returns true (the caller persists the file
// and applies side effects such as run-on-startup). Returns false if cancelled.
bool showSettingsWindow(sm::core::Config& config);

} // namespace sm::ui
