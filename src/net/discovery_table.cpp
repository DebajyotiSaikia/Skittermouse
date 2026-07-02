#include "net/discovery_table.h"

namespace sm::net {

void DiscoveryTable::onBeacon(const Beacon& b, uint64_t now_ms) {
    entries_[b.machine_id] = Entry{b, now_ms};
}

std::vector<Beacon> DiscoveryTable::live(uint64_t now_ms, uint64_t timeout_ms) const {
    std::vector<Beacon> out;
    for (const auto& kv : entries_) {
        if (now_ms - kv.second.last_seen < timeout_ms) out.push_back(kv.second.beacon);
    }
    return out;
}

void DiscoveryTable::purge(uint64_t now_ms, uint64_t timeout_ms) {
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (now_ms - it->second.last_seen >= timeout_ms) it = entries_.erase(it);
        else ++it;
    }
}

} // namespace sm::net
