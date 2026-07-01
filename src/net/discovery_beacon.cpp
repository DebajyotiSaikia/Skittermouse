#include "net/discovery_beacon.h"

#include <algorithm>

namespace sm::net {

namespace {
constexpr uint8_t kMagic[4] = {'S', 'M', 'B', '1'};
}

Bytes encodeBeacon(const Beacon& b) {
    Bytes o;
    o.insert(o.end(), kMagic, kMagic + 4);

    auto putStr = [&](const std::string& s) {
        uint16_t n = static_cast<uint16_t>(std::min<std::size_t>(s.size(), 0xFFFF));
        o.push_back(static_cast<uint8_t>(n >> 8));
        o.push_back(static_cast<uint8_t>(n & 0xff));
        o.insert(o.end(), s.begin(), s.begin() + n);
    };
    putStr(b.machine_name);
    putStr(b.machine_id);
    putStr(b.ip);
    o.push_back(static_cast<uint8_t>(b.port >> 8));
    o.push_back(static_cast<uint8_t>(b.port & 0xff));
    o.push_back(b.os);
    o.push_back(b.wol_capable);
    return o;
}

bool decodeBeacon(const uint8_t* data, std::size_t len, Beacon& out) {
    if (len < 4 || data[0] != kMagic[0] || data[1] != kMagic[1] ||
        data[2] != kMagic[2] || data[3] != kMagic[3]) {
        return false;
    }
    std::size_t pos = 4;
    auto getStr = [&](std::string& s) -> bool {
        if (pos + 2 > len) return false;
        uint16_t n = static_cast<uint16_t>((data[pos] << 8) | data[pos + 1]);
        pos += 2;
        if (pos + n > len) return false;
        s.assign(reinterpret_cast<const char*>(data + pos), n);
        pos += n;
        return true;
    };
    if (!getStr(out.machine_name)) return false;
    if (!getStr(out.machine_id)) return false;
    if (!getStr(out.ip)) return false;
    if (pos + 2 > len) return false;
    out.port = static_cast<uint16_t>((data[pos] << 8) | data[pos + 1]);
    pos += 2;
    if (pos + 2 > len) return false;
    out.os = data[pos++];
    out.wol_capable = data[pos++];
    return true;
}

} // namespace sm::net
