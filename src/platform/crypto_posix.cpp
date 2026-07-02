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

// Expose the OpenSSL 1.1 low-level EC_KEY API without 3.0 deprecation warnings (used
// only by the ECDH helpers below, for the Linux pairing test).
#define OPENSSL_API_COMPAT 0x10100000L
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/obj_mac.h>
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

// ECDH P-256. Public point travels as raw 64-byte X||Y (big-endian); the shared
// secret is the 32-byte big-endian X coordinate -- identical wire format to the
// native BCrypt/CommonCrypto backends, so a Linux test node pairs byte-for-byte.
bool ecdhGenerateKeyPair(EcdhKeyPair& out) {
    EC_KEY* key = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
    BIGNUM* x = BN_new();
    BIGNUM* y = BN_new();
    bool ok = false;
    if (key && x && y && EC_KEY_generate_key(key) == 1) {
        const EC_GROUP* grp = EC_KEY_get0_group(key);
        const EC_POINT* pub = EC_KEY_get0_public_key(key);
        const BIGNUM* priv = EC_KEY_get0_private_key(key);
        if (EC_POINT_get_affine_coordinates(grp, pub, x, y, nullptr) == 1) {
            out.publicPoint.assign(kEcPointLen, 0);
            out.privateBlob.assign(32, 0);
            BN_bn2binpad(x, out.publicPoint.data(), 32);
            BN_bn2binpad(y, out.publicPoint.data() + 32, 32);
            BN_bn2binpad(priv, out.privateBlob.data(), 32);
            ok = true;
        }
    }
    BN_free(x);
    BN_free(y);
    EC_KEY_free(key);
    return ok;
}

bool ecdhComputeShared(const EcdhKeyPair& mine, const Bytes& peerPublicPoint,
                       Bytes& sharedOut) {
    if (peerPublicPoint.size() != kEcPointLen || mine.privateBlob.size() != 32) return false;
    EC_KEY* key = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
    BIGNUM* priv = BN_bin2bn(mine.privateBlob.data(), 32, nullptr);
    BIGNUM* px = BN_bin2bn(peerPublicPoint.data(), 32, nullptr);
    BIGNUM* py = BN_bin2bn(peerPublicPoint.data() + 32, 32, nullptr);
    BIGNUM* sx = BN_new();
    EC_POINT* peer = nullptr;
    EC_POINT* secret = nullptr;
    bool ok = false;
    if (key && priv && px && py && sx) {
        const EC_GROUP* grp = EC_KEY_get0_group(key);
        peer = EC_POINT_new(grp);
        secret = EC_POINT_new(grp);
        if (peer && secret && EC_KEY_set_private_key(key, priv) == 1 &&
            EC_POINT_set_affine_coordinates(grp, peer, px, py, nullptr) == 1 &&
            EC_POINT_is_on_curve(grp, peer, nullptr) == 1 &&
            EC_POINT_mul(grp, secret, nullptr, peer, priv, nullptr) == 1 &&
            EC_POINT_get_affine_coordinates(grp, secret, sx, nullptr, nullptr) == 1) {
            sharedOut.assign(32, 0);
            BN_bn2binpad(sx, sharedOut.data(), 32);
            ok = true;
        }
    }
    EC_POINT_free(peer);
    EC_POINT_free(secret);
    BN_free(priv);
    BN_free(px);
    BN_free(py);
    BN_free(sx);
    EC_KEY_free(key);
    return ok;
}

} // namespace sm::crypto
