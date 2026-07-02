// Base64 encode (RFC 4648), portable. Used for the WebSocket opening handshake
// (Sec-WebSocket-Key / -Accept). Standard C++ only.

#include "crypto/crypto.h"

namespace sm::crypto {

std::string base64Encode(const uint8_t* data, size_t len) {
    static const char* T =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);

    size_t i = 0;
    for (; i + 3 <= len; i += 3) {
        uint32_t n = (static_cast<uint32_t>(data[i]) << 16) |
                     (static_cast<uint32_t>(data[i + 1]) << 8) |
                     static_cast<uint32_t>(data[i + 2]);
        out += T[(n >> 18) & 63];
        out += T[(n >> 12) & 63];
        out += T[(n >> 6) & 63];
        out += T[n & 63];
    }

    size_t rem = len - i;
    if (rem == 1) {
        uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        out += T[(n >> 18) & 63];
        out += T[(n >> 12) & 63];
        out += "==";
    } else if (rem == 2) {
        uint32_t n = (static_cast<uint32_t>(data[i]) << 16) |
                     (static_cast<uint32_t>(data[i + 1]) << 8);
        out += T[(n >> 18) & 63];
        out += T[(n >> 12) & 63];
        out += T[(n >> 6) & 63];
        out += '=';
    }
    return out;
}

} // namespace sm::crypto
