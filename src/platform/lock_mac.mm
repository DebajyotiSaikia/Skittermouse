// macOS screen lock (spec 14). Opt-in, lock-only. Uses the CGSession suspend path
// (the same mechanism the Fast-User-Switching menu uses) to lock to the login
// window. Unlock is never scripted -- the user switches over and types normally.

#include "platform/lock_screen.h"

#include <cstdlib>

namespace sm::platform {

bool lockScreen() {
    // The CGSession helper suspends (locks) the current session.
    int rc = std::system(
        "'/System/Library/CoreServices/Menu Extras/User.menu/Contents/Resources/CGSession' -suspend");
    return rc == 0;
}

} // namespace sm::platform
