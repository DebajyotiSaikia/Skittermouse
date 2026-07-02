#include "net/ownership_codec.h"

namespace sm::net {

Bytes encodeOwnershipClaim(const sm::core::OwnershipClaim& c) {
    Bytes b;
    auto putStr = [&](const std::string& s) {
        b.push_back(static_cast<uint8_t>(s.size() > 255 ? 255 : s.size()));
        std::size_t n = s.size() > 255 ? 255 : s.size();
        b.insert(b.end(), s.begin(), s.begin() + n);
    };
    putStr(c.target);
    putStr(c.origin);
    for (int i = 7; i >= 0; --i) b.push_back(static_cast<uint8_t>((c.sequence >> (8 * i)) & 0xff));
    return b;
}

bool decodeOwnershipClaim(const uint8_t* data, std::size_t len, sm::core::OwnershipClaim& out) {
    std::size_t pos = 0;
    auto getStr = [&](std::string& s) -> bool {
        if (pos + 1 > len) return false;
        uint8_t n = data[pos++];
        if (pos + n > len) return false;
        s.assign(reinterpret_cast<const char*>(data + pos), n);
        pos += n;
        return true;
    };
    if (!getStr(out.target)) return false;
    if (!getStr(out.origin)) return false;
    if (pos + 8 > len) return false;
    out.sequence = 0;
    for (int i = 0; i < 8; ++i) out.sequence = (out.sequence << 8) | data[pos++];
    return true;
}

} // namespace sm::net
