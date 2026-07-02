// Windows native crypto via CNG / BCrypt (spec Sections 5.4, 7). Zero third-party
// (Section 16): everything here is bcrypt.dll. Compiled only on Windows.

#include "crypto/crypto.h"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>

#ifndef NT_SUCCESS
#define NT_SUCCESS(s) (((NTSTATUS)(s)) >= 0)
#endif

namespace sm::crypto {

namespace {

// BCRYPT_ECCKEY_BLOB header for P-256: { magic, cbKey } then key material.
constexpr ULONG kEccMagicPublicP256 = 0x314B4345;  // BCRYPT_ECDH_PUBLIC_P256_MAGIC
constexpr ULONG kEccCoordLen = 32;                 // P-256 coordinate size

} // namespace

bool randomBytes(uint8_t* buf, size_t len) {
    return NT_SUCCESS(BCryptGenRandom(nullptr, buf, static_cast<ULONG>(len),
                                      BCRYPT_USE_SYSTEM_PREFERRED_RNG));
}

Bytes randomBytes(size_t len) {
    Bytes b(len);
    if (!randomBytes(b.data(), len)) b.clear();
    return b;
}

Hash256 sha256(const uint8_t* data, size_t len) {
    Hash256 out{};
    BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0,
               const_cast<PUCHAR>(data), static_cast<ULONG>(len),
               out.data(), static_cast<ULONG>(out.size()));
    return out;
}

Hash160 sha1(const uint8_t* data, size_t len) {
    Hash160 out{};
    BCryptHash(BCRYPT_SHA1_ALG_HANDLE, nullptr, 0,
               const_cast<PUCHAR>(data), static_cast<ULONG>(len),
               out.data(), static_cast<ULONG>(out.size()));
    return out;
}

Hash256 hmacSha256(const uint8_t* key, size_t keyLen,
                   const uint8_t* data, size_t dataLen) {
    Hash256 out{};
    BCryptHash(BCRYPT_HMAC_SHA256_ALG_HANDLE,
               const_cast<PUCHAR>(key), static_cast<ULONG>(keyLen),
               const_cast<PUCHAR>(data), static_cast<ULONG>(dataLen),
               out.data(), static_cast<ULONG>(out.size()));
    return out;
}

namespace {

// Shared AES-256-GCM setup for both encrypt and decrypt.
bool aesGcm(bool encrypt, const uint8_t* key32,
            const uint8_t* nonce, size_t nonceLen,
            const uint8_t* aad, size_t aadLen,
            const uint8_t* inBuf, size_t inLen,
            uint8_t* outBuf, uint8_t* tag16) {
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    if (!NT_SUCCESS(BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0)))
        return false;

    bool ok = false;
    BCRYPT_KEY_HANDLE hKey = nullptr;
    if (NT_SUCCESS(BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE,
                                     reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
                                     sizeof(BCRYPT_CHAIN_MODE_GCM), 0)) &&
        NT_SUCCESS(BCryptGenerateSymmetricKey(hAlg, &hKey, nullptr, 0,
                                              const_cast<PUCHAR>(key32),
                                              static_cast<ULONG>(kAesKeyLen), 0))) {
        BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
        BCRYPT_INIT_AUTH_MODE_INFO(info);
        info.pbNonce = const_cast<PUCHAR>(nonce);
        info.cbNonce = static_cast<ULONG>(nonceLen);
        info.pbAuthData = const_cast<PUCHAR>(aad);
        info.cbAuthData = static_cast<ULONG>(aadLen);
        info.pbTag = tag16;
        info.cbTag = static_cast<ULONG>(kGcmTagLen);

        ULONG done = 0;
        NTSTATUS st;
        if (encrypt) {
            st = BCryptEncrypt(hKey, const_cast<PUCHAR>(inBuf), static_cast<ULONG>(inLen),
                               &info, nullptr, 0, outBuf, static_cast<ULONG>(inLen), &done, 0);
        } else {
            st = BCryptDecrypt(hKey, const_cast<PUCHAR>(inBuf), static_cast<ULONG>(inLen),
                               &info, nullptr, 0, outBuf, static_cast<ULONG>(inLen), &done, 0);
        }
        ok = NT_SUCCESS(st);
    }

    if (hKey) BCryptDestroyKey(hKey);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return ok;
}

} // namespace

bool aesGcmEncrypt(const uint8_t* key32, const uint8_t* nonce, size_t nonceLen,
                   const uint8_t* aad, size_t aadLen,
                   const uint8_t* plaintext, size_t ptLen,
                   uint8_t* ciphertext, uint8_t* tag16) {
    return aesGcm(true, key32, nonce, nonceLen, aad, aadLen, plaintext, ptLen,
                  ciphertext, tag16);
}

bool aesGcmDecrypt(const uint8_t* key32, const uint8_t* nonce, size_t nonceLen,
                   const uint8_t* aad, size_t aadLen,
                   const uint8_t* ciphertext, size_t ctLen,
                   const uint8_t* tag16, uint8_t* plaintext) {
    // BCryptDecrypt needs a mutable tag buffer in the auth info; copy it.
    uint8_t tag[kGcmTagLen];
    std::copy(tag16, tag16 + kGcmTagLen, tag);
    return aesGcm(false, key32, nonce, nonceLen, aad, aadLen, ciphertext, ctLen,
                  plaintext, tag);
}

bool ecdhGenerateKeyPair(EcdhKeyPair& out) {
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    if (!NT_SUCCESS(BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_ECDH_P256_ALGORITHM, nullptr, 0)))
        return false;

    bool ok = false;
    BCRYPT_KEY_HANDLE hKey = nullptr;
    if (NT_SUCCESS(BCryptGenerateKeyPair(hAlg, &hKey, 256, 0)) &&
        NT_SUCCESS(BCryptFinalizeKeyPair(hKey, 0))) {
        // Public: header(8) + X(32) + Y(32). Wire form = X||Y.
        ULONG needPub = 0;
        if (NT_SUCCESS(BCryptExportKey(hKey, nullptr, BCRYPT_ECCPUBLIC_BLOB, nullptr, 0, &needPub, 0))) {
            Bytes pub(needPub);
            if (NT_SUCCESS(BCryptExportKey(hKey, nullptr, BCRYPT_ECCPUBLIC_BLOB, pub.data(), needPub, &needPub, 0)) &&
                needPub >= sizeof(BCRYPT_ECCKEY_BLOB) + 2 * kEccCoordLen) {
                out.publicPoint.assign(pub.begin() + sizeof(BCRYPT_ECCKEY_BLOB),
                                       pub.begin() + sizeof(BCRYPT_ECCKEY_BLOB) + 2 * kEccCoordLen);
                // Private blob is opaque; keep it verbatim, never transmit it.
                ULONG needPriv = 0;
                if (NT_SUCCESS(BCryptExportKey(hKey, nullptr, BCRYPT_ECCPRIVATE_BLOB, nullptr, 0, &needPriv, 0))) {
                    out.privateBlob.resize(needPriv);
                    ok = NT_SUCCESS(BCryptExportKey(hKey, nullptr, BCRYPT_ECCPRIVATE_BLOB,
                                                    out.privateBlob.data(), needPriv, &needPriv, 0));
                }
            }
        }
    }

    if (hKey) BCryptDestroyKey(hKey);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return ok;
}

bool ecdhComputeShared(const EcdhKeyPair& mine, const Bytes& peerPublicPoint,
                       Bytes& sharedOut) {
    if (peerPublicPoint.size() != kEcPointLen || mine.privateBlob.empty()) return false;

    BCRYPT_ALG_HANDLE hAlg = nullptr;
    if (!NT_SUCCESS(BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_ECDH_P256_ALGORITHM, nullptr, 0)))
        return false;

    bool ok = false;
    BCRYPT_KEY_HANDLE hMine = nullptr, hPeer = nullptr;
    BCRYPT_SECRET_HANDLE hSecret = nullptr;

    // Rebuild a public blob from the peer's raw X||Y point.
    Bytes peerBlob(sizeof(BCRYPT_ECCKEY_BLOB) + 2 * kEccCoordLen);
    auto* hdr = reinterpret_cast<BCRYPT_ECCKEY_BLOB*>(peerBlob.data());
    hdr->dwMagic = kEccMagicPublicP256;
    hdr->cbKey = kEccCoordLen;
    std::copy(peerPublicPoint.begin(), peerPublicPoint.end(),
              peerBlob.begin() + sizeof(BCRYPT_ECCKEY_BLOB));

    if (NT_SUCCESS(BCryptImportKeyPair(hAlg, nullptr, BCRYPT_ECCPRIVATE_BLOB, &hMine,
                                       const_cast<PUCHAR>(mine.privateBlob.data()),
                                       static_cast<ULONG>(mine.privateBlob.size()), 0)) &&
        NT_SUCCESS(BCryptImportKeyPair(hAlg, nullptr, BCRYPT_ECCPUBLIC_BLOB, &hPeer,
                                       peerBlob.data(), static_cast<ULONG>(peerBlob.size()), 0)) &&
        NT_SUCCESS(BCryptSecretAgreement(hMine, hPeer, &hSecret, 0))) {
        ULONG need = 0;
        if (NT_SUCCESS(BCryptDeriveKey(hSecret, BCRYPT_KDF_RAW_SECRET, nullptr,
                                       nullptr, 0, &need, 0)) && need == kSharedLen) {
            Bytes raw(need);
            if (NT_SUCCESS(BCryptDeriveKey(hSecret, BCRYPT_KDF_RAW_SECRET, nullptr,
                                           raw.data(), need, &need, 0))) {
                // BCRYPT_KDF_RAW_SECRET yields the X coordinate little-endian;
                // reverse to big-endian so both OS backends agree byte-for-byte.
                std::reverse(raw.begin(), raw.end());
                sharedOut = std::move(raw);
                ok = true;
            }
        }
    }

    if (hSecret) BCryptDestroySecret(hSecret);
    if (hPeer) BCryptDestroyKey(hPeer);
    if (hMine) BCryptDestroyKey(hMine);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return ok;
}

} // namespace sm::crypto
