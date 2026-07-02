#pragma once

// Factory for the platform WSS/TCP transport (spec 5.1/5.5). Declared separately
// from the pure Transport interface so transport.h stays OS-free. Implemented in
// platform/ws_transport_win.cpp (and later a macOS counterpart).

#include "net/transport.h"

#include <string>

namespace sm::net {

// A client WebSocket transport connecting to a peer's listening port. Caller owns.
// (TLS wrapping via Schannel for full wss:// is the remaining step; message payloads
// are already AES-256-GCM sealed end-to-end per spec 5.4.)
Transport* createWsClientTransport();

// Block up to timeoutMs for one incoming connection on `port`, complete the server
// WebSocket handshake, and return the accepted transport (server role). Null on
// timeout/error. Caller owns. If outPeerIp is non-null it receives the remote IP
// (so the file channel, spec 5.1/9, can dial the source back on paste).
Transport* wsAcceptOne(uint16_t port, int timeoutMs, std::string* outPeerIp = nullptr);

} // namespace sm::net
