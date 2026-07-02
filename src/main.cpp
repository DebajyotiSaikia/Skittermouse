// Skittermouse -- entry point and app lifecycle (spec Section 2.2).
//
// On Windows this launches the systray application shell (Section 10): a
// message-only window hosting the tray icon, the global hotkey, and the clipboard
// listener. Capture/injection (Section 3), pairing (Section 7), and the mesh
// network (Section 5) are wired in behind it as the remaining build-order steps
// land. On macOS the Cocoa shell is not built yet, so main() exercises the
// pure-logic core as a smoke check.

#ifdef _WIN32

#include "platform/tray_app.h"

#include <windows.h>

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    return sm::platform::runTrayApp();
}

#else

#include "core/ownership_state.h"

#include <cstdio>

int main() {
    sm::core::OwnershipState state{"this-machine"};
    std::printf("Skittermouse %s\n", "0.1.0");
    std::printf("Local input owner: %s (owner id: %s)\n",
                state.isLocalOwner() ? "yes" : "no",
                state.owner().c_str());
    return 0;
}

#endif

