#include "test_framework.h"

#include "crypto/crypto.h"
#include "crypto/x509_selfsigned.h"

#include <ctime>
#include <vector>

using namespace sm;

#ifdef _WIN32

#include <windows.h>

#include <bcrypt.h>
#include <wincrypt.h>

namespace {

// DER INTEGER from a big-endian magnitude (positive): strip leading zeros, pad 0x00 if
// the top bit is set. Local copy so the test is independent of the builder internals.
crypto::Bytes derInt(const uint8_t* be, std::size_t n) {
    std::size_t i = 0;
    while (i + 1 < n && be[i] == 0) ++i;
    crypto::Bytes v(be + i, be + n);
    if (v[0] & 0x80) v.insert(v.begin(), 0x00);
    crypto::Bytes out;
    out.push_back(0x02);
    out.push_back(static_cast<uint8_t>(v.size()));
    out.insert(out.end(), v.begin(), v.end());
    return out;
}

} // namespace

// Generate a real P-256 key with CNG, build a self-signed cert with our pure-logic DER
// builder (signing via BCryptSignHash), then have CryptoAPI PARSE it and verify its own
// self-signature. If Windows accepts and self-verifies the cert, the DER is byte-correct
// -- and the identical bytes are what macOS SecureTransport will consume.
void run_x509_selfsigned_tests() {
    BCRYPT_ALG_HANDLE alg = nullptr;
    SM_CHECK(BCryptOpenAlgorithmProvider(&alg, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0) == 0);
    BCRYPT_KEY_HANDLE key = nullptr;
    SM_CHECK(BCryptGenerateKeyPair(alg, &key, 256, 0) == 0);
    SM_CHECK(BCryptFinalizeKeyPair(key, 0) == 0);

    // Public point 0x04 || X || Y from the exported ECC public blob.
    DWORD cb = 0;
    BCryptExportKey(key, nullptr, BCRYPT_ECCPUBLIC_BLOB, nullptr, 0, &cb, 0);
    std::vector<uint8_t> blob(cb);
    SM_CHECK(BCryptExportKey(key, nullptr, BCRYPT_ECCPUBLIC_BLOB, blob.data(), cb, &cb, 0) == 0);
    auto* hdr = reinterpret_cast<BCRYPT_ECCKEY_BLOB*>(blob.data());
    DWORD klen = hdr->cbKey; // 32
    const uint8_t* x = blob.data() + sizeof(BCRYPT_ECCKEY_BLOB);
    crypto::Bytes point;
    point.push_back(0x04);
    point.insert(point.end(), x, x + 2 * klen);

    auto sign = [&](const crypto::Bytes& tbs) -> crypto::Bytes {
        crypto::Hash256 h = crypto::sha256(tbs.data(), tbs.size());
        DWORD slen = 0;
        BCryptSignHash(key, nullptr, const_cast<PUCHAR>(h.data()), 32, nullptr, 0, &slen, 0);
        std::vector<uint8_t> raw(slen);
        if (BCryptSignHash(key, nullptr, const_cast<PUCHAR>(h.data()), 32, raw.data(), slen, &slen,
                           0) != 0)
            return {};
        // raw = r || s (32 each) -> DER SEQUENCE { INTEGER r, INTEGER s }
        crypto::Bytes r = derInt(raw.data(), klen);
        crypto::Bytes s = derInt(raw.data() + klen, klen);
        crypto::Bytes seq;
        seq.push_back(0x30);
        seq.push_back(static_cast<uint8_t>(r.size() + s.size()));
        seq.insert(seq.end(), r.begin(), r.end());
        seq.insert(seq.end(), s.begin(), s.end());
        return seq;
    };

    const int64_t now = static_cast<int64_t>(std::time(nullptr));
    crypto::Bytes cert =
        crypto::buildSelfSignedP256Cert(point, sign, now - 3600, now + 3600LL * 24 * 3650);
    SM_CHECK(!cert.empty());

    // CryptoAPI must accept the DER structure...
    PCCERT_CONTEXT ctx = CertCreateCertificateContext(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
                                                      cert.data(), static_cast<DWORD>(cert.size()));
    SM_CHECK(ctx != nullptr);
    if (ctx) {
        // ...and the self-signature must verify against the cert's own public key.
        BOOL ok = CryptVerifyCertificateSignatureEx(
            0, X509_ASN_ENCODING, CRYPT_VERIFY_CERT_SIGN_SUBJECT_CERT,
            const_cast<void*>(static_cast<const void*>(ctx)), CRYPT_VERIFY_CERT_SIGN_ISSUER_CERT,
            const_cast<void*>(static_cast<const void*>(ctx)), 0, nullptr);
        SM_CHECK(ok == TRUE);
        CertFreeCertificateContext(ctx);
    }

    // Bad input is rejected.
    SM_CHECK(crypto::buildSelfSignedP256Cert({0x01, 0x02}, sign, now, now + 1).empty());

    BCryptDestroyKey(key);
    BCryptCloseAlgorithmProvider(alg, 0);
}

#else

// Non-Windows: a structural sanity check (the byte-exact verification runs on Windows).
void run_x509_selfsigned_tests() {
    crypto::Bytes point(65, 0);
    point[0] = 0x04;
    auto sign = [](const crypto::Bytes&) -> crypto::Bytes {
        return crypto::Bytes(72, 0x11); // dummy DER-ish signature blob
    };
    crypto::Bytes cert = crypto::buildSelfSignedP256Cert(point, sign, 1700000000, 2000000000);
    SM_CHECK(!cert.empty());
    SM_CHECK(cert[0] == 0x30); // top-level SEQUENCE
    SM_CHECK(crypto::buildSelfSignedP256Cert({0x04}, sign, 1, 2).empty());
}

#endif
