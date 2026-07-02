#pragma once

// WebSocket opening-handshake helpers (RFC 6455, spec 5.1). PURE LOGIC on top of
// the crypto interface (SHA-1 + base64). The socket plumbing that sends/receives
// these strings lives in the platform TCP transport.

#include <string>

namespace sm::net {

// Sec-WebSocket-Accept = base64(SHA1(clientKey + magic GUID)).
std::string wsAcceptKey(const std::string& clientKey);

// A fresh Sec-WebSocket-Key = base64(16 random bytes).
std::string wsGenerateClientKey();

// The client's GET upgrade request for host/path with the given key.
std::string wsBuildClientHandshake(const std::string& host, const std::string& path,
                                   const std::string& key);

// The server's "101 Switching Protocols" response for a received client key.
std::string wsBuildServerResponse(const std::string& clientKey);

} // namespace sm::net
