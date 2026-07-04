#include "pairing/verification_code.h"

#include "crypto/crypto.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace sm::pairing {

namespace {
// Dynamic truncation to 6 digits: big-endian uint32 of the first 4 HMAC bytes, high
// bit cleared, mod 1e6, zero-padded. Shared by both code variants.
std::string truncate6(const sm::crypto::Hash256& mac) {
    uint32_t v = (static_cast<uint32_t>(mac[0]) << 24) |
                 (static_cast<uint32_t>(mac[1]) << 16) |
                 (static_cast<uint32_t>(mac[2]) << 8) |
                 static_cast<uint32_t>(mac[3]);
    v &= 0x7fffffffu; // clear high bit so the value is layout/sign-stable
    uint32_t code = v % 1000000u;
    char buf[7];
    std::snprintf(buf, sizeof(buf), "%06u", code);
    return std::string(buf);
}
} // namespace

std::string verificationCode(const uint8_t* shared, std::size_t len) {
    static const char kContext[] = "pairing";
    return truncate6(sm::crypto::hmacSha256(
        shared, len, reinterpret_cast<const uint8_t*>(kContext), sizeof(kContext) - 1));
}

std::string verificationCode(const uint8_t* shared, std::size_t len,
                             const uint8_t* nonceA, std::size_t nonceALen,
                             const uint8_t* nonceB, std::size_t nonceBLen) {
    // Canonical (sorted) nonce order so both peers derive the same code regardless of
    // who initiated.
    const uint8_t* lo = nonceA;
    std::size_t loLen = nonceALen;
    const uint8_t* hi = nonceB;
    std::size_t hiLen = nonceBLen;
    const std::size_t m = loLen < hiLen ? loLen : hiLen;
    const int c = (m > 0) ? std::memcmp(nonceA, nonceB, m) : 0;
    if (c > 0 || (c == 0 && nonceALen > nonceBLen)) {
        lo = nonceB;
        loLen = nonceBLen;
        hi = nonceA;
        hiLen = nonceALen;
    }

    static const char kContext[] = "pairing";
    std::vector<uint8_t> data;
    data.reserve(sizeof(kContext) - 1 + loLen + hiLen);
    data.insert(data.end(), kContext, kContext + sizeof(kContext) - 1);
    data.insert(data.end(), lo, lo + loLen);
    data.insert(data.end(), hi, hi + hiLen);
    return truncate6(sm::crypto::hmacSha256(shared, len, data.data(), data.size()));
}

} // namespace sm::pairing
