#include "net/peer_hello.h"

#include "core/event_types.h"

namespace sm::net {

std::vector<uint8_t> encodePeerHello(const sm::core::PeerId& self) {
    const uint32_t n = static_cast<uint32_t>(self.size());
    std::vector<uint8_t> out;
    out.reserve(6 + self.size());
    out.push_back(sm::core::kProtocolVersion);
    out.push_back(static_cast<uint8_t>(sm::core::MessageType::PeerHello));
    out.push_back(static_cast<uint8_t>(n & 0xFF));
    out.push_back(static_cast<uint8_t>((n >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((n >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((n >> 24) & 0xFF));
    out.insert(out.end(), self.begin(), self.end());
    return out;
}

bool decodePeerHello(const uint8_t* data, std::size_t len, sm::core::PeerId& outId) {
    if (data == nullptr || len < 6) return false;
    if (data[0] != sm::core::kProtocolVersion) return false;
    if (data[1] != static_cast<uint8_t>(sm::core::MessageType::PeerHello)) return false;
    const uint32_t n = static_cast<uint32_t>(data[2]) |
                       (static_cast<uint32_t>(data[3]) << 8) |
                       (static_cast<uint32_t>(data[4]) << 16) |
                       (static_cast<uint32_t>(data[5]) << 24);
    if (len - 6 != n) return false;
    outId.assign(reinterpret_cast<const char*>(data + 6), n);
    return true;
}

} // namespace sm::net
