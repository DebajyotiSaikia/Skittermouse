#pragma once

// Numeric-comparison pairing dialog (spec 7.1). Shows the 6-digit code and asks the
// human to confirm it matches the code on the other machine. Returns true only on
// explicit confirm -- this visual match is the actual MITM defense, not a
// formality. Implemented per-OS (Win32 MessageBox; macOS NSAlert).

#include <string>

namespace sm::platform {

bool confirmPairingCode(const std::string& code, const std::string& peerName);

} // namespace sm::platform
