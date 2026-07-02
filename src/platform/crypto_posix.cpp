// POSIX crypto implementation for the LINUX TEST RIG ONLY (spec 5.4/7). The shipped
// product uses native crypto (platform/crypto_win.cpp via BCrypt; crypto_mac.mm via
// CommonCrypto) with zero third-party deps, per spec 16. This file exists solely so
// the headless two-container network test (tests/docker/) can exercise the real
// EncryptedTransport / secure-link path over real TCP on Linux, where no native
// system crypto interface is available -- so it links OpenSSL, which is fine for
// test infrastructure but must NEVER be used by the Windows/macOS product builds.
//
// Wire formats match the native backends (AES-256-GCM key32/nonce12/tag16), so a
// Linux test node interoperates with a Windows/macOS node byte-for-byte.

#include "crypto/crypto.h"

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>

namespace sm::crypto {

bool randomBytes(uint8_t* buf, size_t len) {
    return RAND_bytes(buf, static_cast<int>(len)) == 1;
}

Bytes randomBytes(size_t len) {
    Bytes b(len);
    randomBytes(b.data(), len);
    return b;
}

Hash256 sha256(const uint8_t* data, size_t len) {
    Hash256 out{};
    unsigned int outLen = 0;
    EVP_Digest(data, len, out.data(), &outLen, EVP_sha256(), nullptr);
    return out;
}

Hash160 sha1(const uint8_t* data, size_t len) {
    Hash160 out{};
    unsigned int outLen = 0;
    EVP_Digest(data, len, out.data(), &outLen, EVP_sha1(), nullptr);
    return out;
}

Hash256 hmacSha256(const uint8_t* key, size_t keyLen, const uint8_t* data, size_t dataLen) {
    Hash256 out{};
    unsigned int outLen = 0;
    HMAC(EVP_sha256(), key, static_cast<int>(keyLen), data, dataLen, out.data(), &outLen);
    return out;
}

bool aesGcmEncrypt(const uint8_t* key32, const uint8_t* nonce, size_t nonceLen,
                   const uint8_t* aad, size_t aadLen, const uint8_t* plaintext,
                   size_t ptLen, uint8_t* ciphertext, uint8_t* tag16) {
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return false;
    bool ok = false;
    int outl = 0, tmpl = 0;
    do {
        if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) break;
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(nonceLen),
                                nullptr) != 1)
            break;
        if (EVP_EncryptInit_ex(ctx, nullptr, nullptr, key32, nonce) != 1) break;
        if (aad && aadLen > 0) {
            if (EVP_EncryptUpdate(ctx, nullptr, &outl, aad, static_cast<int>(aadLen)) != 1) break;
        }
        if (ptLen > 0) {
            if (EVP_EncryptUpdate(ctx, ciphertext, &outl, plaintext,
                                  static_cast<int>(ptLen)) != 1)
                break;
        } else {
            outl = 0;
        }
        if (EVP_EncryptFinal_ex(ctx, ciphertext + outl, &tmpl) != 1) break;
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag16) != 1) break;
        ok = true;
    } while (false);
    EVP_CIPHER_CTX_free(ctx);
    return ok;
}

bool aesGcmDecrypt(const uint8_t* key32, const uint8_t* nonce, size_t nonceLen,
                   const uint8_t* aad, size_t aadLen, const uint8_t* ciphertext,
                   size_t ctLen, const uint8_t* tag16, uint8_t* plaintext) {
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return false;
    bool ok = false;
    int outl = 0, tmpl = 0;
    do {
        if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) break;
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(nonceLen),
                                nullptr) != 1)
            break;
        if (EVP_DecryptInit_ex(ctx, nullptr, nullptr, key32, nonce) != 1) break;
        if (aad && aadLen > 0) {
            if (EVP_DecryptUpdate(ctx, nullptr, &outl, aad, static_cast<int>(aadLen)) != 1) break;
        }
        if (ctLen > 0) {
            if (EVP_DecryptUpdate(ctx, plaintext, &outl, ciphertext,
                                  static_cast<int>(ctLen)) != 1)
                break;
        } else {
            outl = 0;
        }
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16,
                                const_cast<uint8_t*>(tag16)) != 1)
            break;
        ok = EVP_DecryptFinal_ex(ctx, plaintext + outl, &tmpl) > 0; // 0 = auth failure
    } while (false);
    EVP_CIPHER_CTX_free(ctx);
    return ok;
}

// ECDH is not exercised by the network test (PSKs are pre-seeded), so these are
// unimplemented stubs here -- pairing is validated on the native backends.
bool ecdhGenerateKeyPair(EcdhKeyPair&) { return false; }
bool ecdhComputeShared(const EcdhKeyPair&, const Bytes&, Bytes&) { return false; }

} // namespace sm::crypto
