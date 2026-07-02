#pragma once

// Self-signed P-256 X.509 certificate builder (spec 5.4 / macOS TLS). PURE LOGIC: it
// assembles the DER by hand and delegates the ECDSA-SHA256 signature to an injected
// callback, so it has no OS/crypto dependency and is unit-testable. It exists because
// macOS SecureTransport needs a server certificate but Security.framework has no API
// to create a self-signed one; the Windows (CertCreateSelfSignCertificate) and Linux
// (OpenSSL X509) backends have their own, so this is the macOS path. Verified on
// Windows (CryptoAPI parses + self-verifies the output), so the identical DER is
// trustworthy on macOS where it can't be unit-tested.

#include <cstdint>
#include <functional>

#include "crypto/crypto.h"

namespace sm::crypto {

// Build a self-signed P-256 certificate (CN=Skittermouse, no extensions) as DER.
//   publicPoint : the 65-byte uncompressed EC point (0x04 || X || Y).
//   sign        : given the TBSCertificate DER, returns the DER-encoded (X9.62
//                 SEQUENCE{r,s}) ECDSA-SHA256 signature; empty on failure.
//   notBefore/notAfter : validity, seconds since the Unix epoch (UTC).
// Returns the certificate DER, or empty on bad input / sign failure.
Bytes buildSelfSignedP256Cert(const Bytes& publicPoint,
                              const std::function<Bytes(const Bytes&)>& sign,
                              int64_t notBefore, int64_t notAfter);

} // namespace sm::crypto
