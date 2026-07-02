#include "test_framework.h"

#include "crypto/crypto.h"

#include <algorithm>
#include <string>

using namespace sm::crypto;

namespace {

Bytes hx(const std::string& h) {
    auto v = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return 0;
    };
    Bytes b;
    for (std::size_t i = 0; i + 1 < h.size(); i += 2)
        b.push_back(static_cast<uint8_t>((v(h[i]) << 4) | v(h[i + 1])));
    return b;
}

std::string hex(const uint8_t* p, std::size_t n) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (std::size_t i = 0; i < n; ++i) {
        s += d[p[i] >> 4];
        s += d[p[i] & 15];
    }
    return s;
}

} // namespace

void run_crypto_tests() {
    // --- SHA-256 KAT ("abc") ------------------------------------------------
    {
        auto h = sha256(reinterpret_cast<const uint8_t*>("abc"), 3);
        SM_CHECK_EQ(hex(h.data(), 32),
                    std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    }

    // --- SHA-1 KAT ("abc") + base64 KATs (RFC 4648) -------------------------
    {
        auto h = sha1(reinterpret_cast<const uint8_t*>("abc"), 3);
        SM_CHECK_EQ(hex(h.data(), 20),
                    std::string("a9993e364706816aba3e25717850c26c9cd0d89d"));

        auto b64 = [](const std::string& s) {
            return base64Encode(reinterpret_cast<const uint8_t*>(s.data()), s.size());
        };
        SM_CHECK_EQ(b64(""), std::string(""));
        SM_CHECK_EQ(b64("f"), std::string("Zg=="));
        SM_CHECK_EQ(b64("fo"), std::string("Zm8="));
        SM_CHECK_EQ(b64("foo"), std::string("Zm9v"));
        SM_CHECK_EQ(b64("foob"), std::string("Zm9vYg=="));
        SM_CHECK_EQ(b64("fooba"), std::string("Zm9vYmE="));
        SM_CHECK_EQ(b64("foobar"), std::string("Zm9vYmFy"));
    }

    // --- HMAC-SHA256 KAT (RFC 4231, Test Case 2) ----------------------------
    {
        std::string key = "Jefe", data = "what do ya want for nothing?";
        auto m = hmacSha256(reinterpret_cast<const uint8_t*>(key.data()), key.size(),
                            reinterpret_cast<const uint8_t*>(data.data()), data.size());
        SM_CHECK_EQ(hex(m.data(), 32),
                    std::string("5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"));
    }

    // --- HKDF-SHA256 KAT (RFC 5869, Test Case 1) ----------------------------
    {
        Bytes ikm = hx("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b");
        Bytes salt = hx("000102030405060708090a0b0c");
        Bytes info = hx("f0f1f2f3f4f5f6f7f8f9");
        Bytes okm = hkdfSha256(ikm.data(), ikm.size(), salt.data(), salt.size(),
                               info.data(), info.size(), 42);
        SM_CHECK_EQ(hex(okm.data(), okm.size()),
                    std::string("3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56"
                                "ecc4c5bf34007208d5b887185865"));
    }

    // --- AES-256-GCM KAT: empty PT/AAD (McGrew GCM Test Case 13) -------------
    {
        Bytes key(32, 0), nonce(12, 0);
        uint8_t tag[16];
        uint8_t ct[1];
        bool ok = aesGcmEncrypt(key.data(), nonce.data(), 12, nullptr, 0, nullptr, 0, ct, tag);
        SM_CHECK(ok);
        SM_CHECK_EQ(hex(tag, 16), std::string("530f8afbc74536b9a963b4f1c4cb738b"));
    }

    // --- AES-256-GCM KAT: 60-byte PT, no AAD (McGrew GCM Test Case 15) -------
    {
        Bytes key = hx("feffe9928665731c6d6a8f9467308308feffe9928665731c6d6a8f9467308308");
        Bytes iv = hx("cafebabefacedbaddecaf888");
        Bytes pt = hx("d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a7"
                      "21c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b39");
        Bytes ct(pt.size());
        uint8_t tag[16];
        SM_CHECK(aesGcmEncrypt(key.data(), iv.data(), 12, nullptr, 0,
                               pt.data(), pt.size(), ct.data(), tag));
        SM_CHECK_EQ(hex(ct.data(), ct.size()),
                    std::string("522dc1f099567d07f47f37a32a84427d643a8cdcbfe5c0c97598a2bd25"
                                "55d1aa8cb08e48590dbb3da7b08b1056828838c5f61e6393ba7a0abcc9f662"));
        SM_CHECK_EQ(hex(tag, 16), std::string("eb9f796c8d356fc31a8433884b696f4f"));

        // Round-trip decrypt succeeds and recovers the plaintext.
        Bytes dec(pt.size());
        SM_CHECK(aesGcmDecrypt(key.data(), iv.data(), 12, nullptr, 0,
                               ct.data(), ct.size(), tag, dec.data()));
        SM_CHECK(dec == pt);

        // A flipped tag bit must be rejected.
        uint8_t bad[16];
        std::copy(tag, tag + 16, bad);
        bad[0] ^= 0x01;
        Bytes dec2(pt.size());
        SM_CHECK(!aesGcmDecrypt(key.data(), iv.data(), 12, nullptr, 0,
                                ct.data(), ct.size(), bad, dec2.data()));
    }

    // --- AES-256-GCM with AAD: round-trip + AAD tamper rejection -------------
    {
        Bytes key = randomBytes(32), nonce = randomBytes(12);
        std::string pt = "hello skittermouse";
        std::string aad = "v1|input";
        Bytes ct(pt.size());
        uint8_t tag[16];
        SM_CHECK(aesGcmEncrypt(key.data(), nonce.data(), 12,
                               reinterpret_cast<const uint8_t*>(aad.data()), aad.size(),
                               reinterpret_cast<const uint8_t*>(pt.data()), pt.size(),
                               ct.data(), tag));
        Bytes dec(pt.size());
        SM_CHECK(aesGcmDecrypt(key.data(), nonce.data(), 12,
                               reinterpret_cast<const uint8_t*>(aad.data()), aad.size(),
                               ct.data(), ct.size(), tag, dec.data()));
        SM_CHECK(std::string(dec.begin(), dec.end()) == pt);

        std::string aad2 = "v2|input"; // different associated data
        Bytes dec3(pt.size());
        SM_CHECK(!aesGcmDecrypt(key.data(), nonce.data(), 12,
                                reinterpret_cast<const uint8_t*>(aad2.data()), aad2.size(),
                                ct.data(), ct.size(), tag, dec3.data()));
    }

    // --- ECDH P-256: two-party agreement ------------------------------------
    {
        EcdhKeyPair a, b;
        SM_CHECK(ecdhGenerateKeyPair(a));
        SM_CHECK(ecdhGenerateKeyPair(b));
        SM_CHECK_EQ(a.publicPoint.size(), 64u);
        SM_CHECK_EQ(b.publicPoint.size(), 64u);

        Bytes sab, sba;
        SM_CHECK(ecdhComputeShared(a, b.publicPoint, sab));
        SM_CHECK(ecdhComputeShared(b, a.publicPoint, sba));
        SM_CHECK_EQ(sab.size(), 32u);
        SM_CHECK(sab == sba); // both sides land on the same secret

        // Two independent keypairs must not collide.
        EcdhKeyPair c;
        SM_CHECK(ecdhGenerateKeyPair(c));
        Bytes sac;
        SM_CHECK(ecdhComputeShared(a, c.publicPoint, sac));
        SM_CHECK(sac != sab);

        // Invalid peer point is rejected.
        Bytes bad(10, 0), out;
        SM_CHECK(!ecdhComputeShared(a, bad, out));
    }

    // --- randomBytes: right size, not constant ------------------------------
    {
        Bytes r1 = randomBytes(16), r2 = randomBytes(16);
        SM_CHECK_EQ(r1.size(), 16u);
        SM_CHECK(r1 != r2);
    }
}
