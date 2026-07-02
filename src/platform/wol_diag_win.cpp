// Windows Wake-on-LAN self-diagnosis (spec 12). Queries powercfg for wake-capable /
// wake-armed devices and looks for a network adapter. Best-effort: the BIOS/UEFI
// wake toggle is not visible to any software check, so it is never claimed here.

#include "platform/wol_diag.h"

#include <cstdio>
#include <string>

namespace sm::platform {

namespace {

std::string runCapture(const char* cmd) {
    std::string out;
    FILE* p = _popen(cmd, "r");
    if (!p) return out;
    char buf[512];
    while (std::fgets(buf, sizeof(buf), p)) out += buf;
    _pclose(p);
    return out;
}

bool containsNic(const std::string& s) {
    auto has = [&](const char* k) { return s.find(k) != std::string::npos; };
    return has("Ethernet") || has("Wi-Fi") || has("Wireless") || has("Network") ||
           has("802.11") || has("NIC");
}

} // namespace

WolStatus diagnoseWol() {
    WolStatus st;
    std::string programmable = runCapture("powercfg /devicequery wake_programmable");
    std::string armed = runCapture("powercfg /devicequery wake_armed");

    st.nic_supported = containsNic(programmable);
    st.os_wake_enabled = containsNic(armed);
    st.bios_checkable = false;

    if (st.os_wake_enabled) {
        st.note = "A network adapter is armed to wake this PC.";
    } else if (st.nic_supported) {
        st.note = "A network adapter can wake this PC, but is not currently armed "
                  "(enable \"Allow this device to wake the computer\").";
    } else {
        st.note = "No wake-capable network adapter detected; also check the BIOS/UEFI "
                  "Wake-on-LAN setting and disable Fast Startup.";
    }
    return st;
}

} // namespace sm::platform
