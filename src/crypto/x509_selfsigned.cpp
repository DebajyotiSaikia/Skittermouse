#include "crypto/x509_selfsigned.h"

#include <cstring>
#include <ctime>
#include <initializer_list>

namespace sm::crypto {

namespace {

// DER length: short form < 128, else 0x80|nbytes then big-endian length.
void putLen(Bytes& out, std::size_t n) {
    if (n < 0x80) {
        out.push_back(static_cast<uint8_t>(n));
        return;
    }
    uint8_t tmp[8];
    int i = 0;
    while (n > 0) {
        tmp[i++] = static_cast<uint8_t>(n & 0xFF);
        n >>= 8;
    }
    out.push_back(static_cast<uint8_t>(0x80 | i));
    for (int j = i - 1; j >= 0; --j) out.push_back(tmp[j]);
}

Bytes tlv(uint8_t tag, const Bytes& content) {
    Bytes out;
    out.push_back(tag);
    putLen(out, content.size());
    out.insert(out.end(), content.begin(), content.end());
    return out;
}

Bytes oid(std::initializer_list<uint8_t> bytes) { return tlv(0x06, Bytes(bytes)); }

Bytes concat(std::initializer_list<Bytes> parts) {
    Bytes out;
    for (const auto& p : parts) out.insert(out.end(), p.begin(), p.end());
    return out;
}

// INTEGER from big-endian magnitude: strip leading zeros, keep one byte if all-zero,
// prepend 0x00 if the top bit is set (so it stays positive).
Bytes integerFromBE(const Bytes& be) {
    std::size_t i = 0;
    while (i + 1 < be.size() && be[i] == 0) ++i;
    Bytes v(be.begin() + i, be.end());
    if (v.empty()) v.push_back(0);
    if (v[0] & 0x80) v.insert(v.begin(), 0x00);
    return tlv(0x02, v);
}

// UTCTime "YYMMDDHHMMSSZ" (valid 1950-2049, which covers a 10-year cert from now).
Bytes utcTime(int64_t epoch) {
    std::time_t t = static_cast<std::time_t>(epoch);
    char buf[16] = {};
#ifdef _WIN32
    std::tm g{};
    gmtime_s(&g, &t);
    std::strftime(buf, sizeof(buf), "%y%m%d%H%M%SZ", &g);
#else
    std::tm g{};
    gmtime_r(&t, &g);
    std::strftime(buf, sizeof(buf), "%y%m%d%H%M%SZ", &g);
#endif
    Bytes content(reinterpret_cast<const uint8_t*>(buf),
                  reinterpret_cast<const uint8_t*>(buf) + std::strlen(buf));
    return tlv(0x17, content);
}

} // namespace

Bytes buildSelfSignedP256Cert(const Bytes& publicPoint,
                              const std::function<Bytes(const Bytes&)>& sign, int64_t notBefore,
                              int64_t notAfter) {
    if (publicPoint.size() != 65 || publicPoint[0] != 0x04 || !sign) return {};

    // AlgorithmIdentifier: ecdsa-with-SHA256 (1.2.840.10045.4.3.2), no parameters.
    const Bytes ecdsaSha256 = tlv(0x30, oid({0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x04, 0x03, 0x02}));

    const Bytes version = tlv(0xA0, tlv(0x02, Bytes{0x02})); // [0] v3
    const Bytes serial = integerFromBE(Bytes{0x01});

    // Name: SEQUENCE { SET { SEQUENCE { OID commonName, UTF8String "Skittermouse" } } }
    const Bytes cnValue = {'S', 'k', 'i', 't', 't', 'e', 'r', 'm', 'o', 'u', 's', 'e'};
    const Bytes rdn = tlv(0x30, concat({oid({0x55, 0x04, 0x03}), tlv(0x0C, cnValue)}));
    const Bytes name = tlv(0x30, tlv(0x31, rdn));

    const Bytes validity = tlv(0x30, concat({utcTime(notBefore), utcTime(notAfter)}));

    // SubjectPublicKeyInfo: SEQUENCE { SEQUENCE { id-ecPublicKey, prime256v1 },
    //                                  BIT STRING(0x00 || point) }
    const Bytes ecAlg = tlv(0x30, concat({oid({0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x02, 0x01}),
                                          oid({0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x03, 0x01, 0x07})}));
    Bytes pkBits;
    pkBits.push_back(0x00);
    pkBits.insert(pkBits.end(), publicPoint.begin(), publicPoint.end());
    const Bytes spki = tlv(0x30, concat({ecAlg, tlv(0x03, pkBits)}));

    // TBSCertificate (issuer == subject == name, since self-signed).
    const Bytes tbs =
        tlv(0x30, concat({version, serial, ecdsaSha256, name, validity, name, spki}));

    const Bytes sig = sign(tbs);
    if (sig.empty()) return {};
    Bytes sigBits;
    sigBits.push_back(0x00);
    sigBits.insert(sigBits.end(), sig.begin(), sig.end());

    return tlv(0x30, concat({tbs, ecdsaSha256, tlv(0x03, sigBits)}));
}

} // namespace sm::crypto
