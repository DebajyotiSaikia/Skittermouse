#pragma once

// Windows tray application entry (spec 10). Runs the message loop that hosts the
// tray icon, the global hotkey, and the clipboard listener. Declared here so the
// cross-platform main.cpp can call it under _WIN32 without pulling in <windows.h>.

namespace sm::platform {

int runTrayApp();

} // namespace sm::platform
