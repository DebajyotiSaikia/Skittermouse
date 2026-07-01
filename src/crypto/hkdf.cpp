// HKDF-SHA256 (RFC 5869), portable on top of the platform HMAC (spec Section 7.1).
// Standard C++ only; compiled on every OS.

#include "crypto/crypto.h"

#include <cstring>

namespace sm::crypto {

Bytes hkdfSha256(const uint8_t* ikm, size_t ikmLen,
                 const uint8_t* salt, size_t saltLen,
                 const uint8_t* info, size_t infoLen,
                 size_t okmLen) {
    // Extract: PRK = HMAC-SHA256(salt, IKM). A null/empty salt becomes 32 zero
    // bytes per the RFC.
    static const uint8_t zeros[32] = {0};
    const uint8_t* saltPtr = (salt && saltLen) ? salt : zeros;
    size_t saltUse = (salt && saltLen) ? saltLen : sizeof(zeros);
    Hash256 prk = hmacSha256(saltPtr, saltUse, ikm, ikmLen);

    // Expand: T(0)=empty; T(i)=HMAC(PRK, T(i-1) || info || i); OKM = T(1)|T(2)|...
    Bytes okm;
    okm.reserve(okmLen);
    Bytes t; // previous block (empty for the first iteration)
    uint8_t counter = 1;
    while (okm.size() < okmLen && counter != 0) {
        Bytes input;
        input.reserve(t.size() + infoLen + 1);
        input.insert(input.end(), t.begin(), t.end());
        if (info && infoLen) input.insert(input.end(), info, info + infoLen);
        input.push_back(counter);

        Hash256 block = hmacSha256(prk.data(), prk.size(), input.data(), input.size());
        t.assign(block.begin(), block.end());

        size_t take = okm.size() + t.size() <= okmLen ? t.size() : (okmLen - okm.size());
        okm.insert(okm.end(), t.begin(), t.begin() + static_cast<std::ptrdiff_t>(take));
        ++counter;
    }
    return okm;
}

} // namespace sm::crypto
