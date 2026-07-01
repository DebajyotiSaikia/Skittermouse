#include "net/ws_frame.h"

#include "crypto/crypto.h"

namespace sm::net {

namespace {
constexpr uint64_t kMaxPayload = 100ull * 1024 * 1024; // sanity cap
}

Bytes wsEncodeFrame(WsOpcode op, const uint8_t* payload, std::size_t len,
                    bool masked, bool fin) {
    Bytes f;
    f.push_back(static_cast<uint8_t>((fin ? 0x80 : 0x00) | static_cast<uint8_t>(op)));

    const uint8_t maskBit = masked ? 0x80 : 0x00;
    if (len < 126) {
        f.push_back(static_cast<uint8_t>(maskBit | len));
    } else if (len <= 0xFFFF) {
        f.push_back(static_cast<uint8_t>(maskBit | 126));
        f.push_back(static_cast<uint8_t>((len >> 8) & 0xff));
        f.push_back(static_cast<uint8_t>(len & 0xff));
    } else {
        f.push_back(static_cast<uint8_t>(maskBit | 127));
        for (int i = 7; i >= 0; --i)
            f.push_back(static_cast<uint8_t>((static_cast<uint64_t>(len) >> (8 * i)) & 0xff));
    }

    if (masked) {
        Bytes mk = sm::crypto::randomBytes(4);
        if (mk.size() != 4) mk = Bytes{0, 0, 0, 0};
        f.insert(f.end(), mk.begin(), mk.end());
        for (std::size_t i = 0; i < len; ++i)
            f.push_back(static_cast<uint8_t>(payload[i] ^ mk[i & 3]));
    } else {
        f.insert(f.end(), payload, payload + len);
    }
    return f;
}

long wsDecodeFrame(const uint8_t* buf, std::size_t len, WsFrame& out) {
    if (len < 2) return 0;
    bool fin = (buf[0] & 0x80) != 0;
    uint8_t opcode = buf[0] & 0x0f;
    bool masked = (buf[1] & 0x80) != 0;
    uint64_t plen = buf[1] & 0x7f;
    std::size_t pos = 2;

    if (plen == 126) {
        if (len < pos + 2) return 0;
        plen = (static_cast<uint64_t>(buf[pos]) << 8) | buf[pos + 1];
        pos += 2;
    } else if (plen == 127) {
        if (len < pos + 8) return 0;
        plen = 0;
        for (int i = 0; i < 8; ++i) plen = (plen << 8) | buf[pos + i];
        pos += 8;
    }
    if (plen > kMaxPayload) return -1;

    uint8_t mask[4] = {0, 0, 0, 0};
    if (masked) {
        if (len < pos + 4) return 0;
        for (int i = 0; i < 4; ++i) mask[i] = buf[pos + i];
        pos += 4;
    }
    if (len < pos + plen) return 0; // incomplete

    Bytes payload(static_cast<std::size_t>(plen));
    for (uint64_t i = 0; i < plen; ++i) {
        uint8_t b = buf[pos + static_cast<std::size_t>(i)];
        if (masked) b = static_cast<uint8_t>(b ^ mask[i & 3]);
        payload[static_cast<std::size_t>(i)] = b;
    }

    out.fin = fin;
    out.opcode = static_cast<WsOpcode>(opcode);
    out.payload = std::move(payload);
    return static_cast<long>(pos + static_cast<std::size_t>(plen));
}

} // namespace sm::net
