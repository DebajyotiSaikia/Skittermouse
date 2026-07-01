#pragma once

// LAN presence beacon packet codec (spec 6). A small UDP broadcast advertising a
// machine's presence: name, id, ip, port, OS, and best-effort Wake-on-LAN status
// (spec 12). encode/decode are PURE LOGIC (unit-tested); the actual broadcast/
// listen sockets live with the platform networking. No mDNS/Bonjour dependency.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sm::net {

using Bytes = std::vector<uint8_t>;

struct Beacon {
    std::string machine_name;
    std::string machine_id;
    std::string ip;
    uint16_t port = 0;
    uint8_t os = 0;          // 0 = windows, 1 = macos
    uint8_t wol_capable = 0; // last self-diagnosed WoL status (best-effort)
};

Bytes encodeBeacon(const Beacon& b);
bool decodeBeacon(const uint8_t* data, std::size_t len, Beacon& out);

} // namespace sm::net
