#pragma once

// Live discovery table (spec 6). Tracks the most recent beacon per machine and its
// last-seen time; live() returns only machines seen within a timeout so offline
// entries don't linger in the "Connect to…" list. PURE LOGIC with an injected
// clock -- the UDP broadcast/listen sockets feed onBeacon().

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "net/discovery_beacon.h"

namespace sm::net {

class DiscoveryTable {
public:
    void onBeacon(const Beacon& b, uint64_t now_ms);
    std::vector<Beacon> live(uint64_t now_ms, uint64_t timeout_ms) const;
    void purge(uint64_t now_ms, uint64_t timeout_ms);
    std::size_t size() const { return entries_.size(); }

private:
    struct Entry {
        Beacon beacon;
        uint64_t last_seen = 0;
    };
    std::map<std::string, Entry> entries_; // keyed by machine_id
};

} // namespace sm::net
