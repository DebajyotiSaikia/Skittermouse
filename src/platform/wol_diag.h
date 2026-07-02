#pragma once

// Wake-on-LAN self-diagnosis (spec 12). Best-effort: the NIC/driver and OS wake
// settings are checkable locally while awake; the BIOS/UEFI toggle is NOT visible
// to any software check, so never claim certainty there. Reported as part of the
// presence beacon so peers know whether a WoL attempt is plausible.

#include <string>

namespace sm::platform {

struct WolStatus {
    bool nic_supported = false;   // a network adapter can be armed to wake the PC
    bool os_wake_enabled = false; // a network adapter is currently armed
    bool bios_checkable = false;  // always false -- BIOS/UEFI state is unknowable here
    std::string note;             // user-facing guidance
};

WolStatus diagnoseWol();

} // namespace sm::platform
