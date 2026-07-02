// Skittermouse -- entry point and app lifecycle (spec Section 2.2).
//
// Both platforms run the systray application shell (Section 10) via
// sm::platform::runTrayApp(): Windows hosts a message-only window + Shell_NotifyIcon
// tray icon (wired to the MeshNode); macOS hosts an NSStatusBar item. Capture/
// injection (Section 3), pairing (Section 7), and the mesh network (Section 5) are
// wired in behind it as the remaining build-order steps land.

#include "platform/tray_app.h"

#ifdef _WIN32

#include <windows.h>

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    return sm::platform::runTrayApp();
}

#else

int main() {
    return sm::platform::runTrayApp();
}

#endif


