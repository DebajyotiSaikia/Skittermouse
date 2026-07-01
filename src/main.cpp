// Skittermouse -- entry point, app lifecycle, config load (spec Section 2.2).
//
// This is a minimal smoke entry for the first build-order milestone (Section 17,
// step 1): it exercises the pure-logic core so the app target links and runs.
// Platform subsystems -- capture/injection (Section 3), hotkey/picker/tray
// (Sections 4, 10), networking (Section 5), pairing (Section 7) -- are wired in
// as later build-order steps land.

#include "core/ownership_state.h"

#include <cstdio>

int main() {
    // TODO(config, Section 2.2): derive a stable machine id from the persisted
    // flat-file config instead of this placeholder.
    sm::core::OwnershipState state{"this-machine"};

    std::printf("Skittermouse %s\n", "0.1.0");
    std::printf("Local input owner: %s (owner id: %s)\n",
                state.isLocalOwner() ? "yes" : "no",
                state.owner().c_str());
    return 0;
}
