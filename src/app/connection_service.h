#pragma once

// Connection service (spec 2.1, 5.1): the network-facing loop that turns the mesh
// from a set of pure objects into a live peer network. For every configured peer it
// dials out; for inbound sockets it accepts; each raw socket goes through the secure
// link (clear id-hint -> per-device PSK -> AES-256-GCM) and the sealed link is handed
// to the ConnectionManager. This exact orchestration is validated end-to-end over
// real TCP by tools/netcheck in the two-container rig (tests/docker/).
//
// The socket factories are INJECTED (createWsClientTransport / wsAcceptOne on the
// product; loopback fakes in unit tests), so this stays pure app logic. It does NOT
// pump the mesh -- the caller drives ConnectionManager::poll() -- so the tray can run
// pollConnections() on a network thread while the mesh pump stays on its UI timer.

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "app/connection_manager.h"
#include "app/secure_link.h"
#include "core/peer_id.h"
#include "net/transport.h"
#include "pairing/key_store.h"

namespace sm::app {

class ConnectionService {
public:
    struct Peer {
        sm::core::PeerId id;
        std::string host;
        uint16_t port = 0;
    };

    // Returns a CONNECTED client transport (or null on failure), and an accepted
    // inbound transport within timeoutMs (or null on timeout).
    using ClientDial =
        std::function<std::unique_ptr<sm::net::Transport>(const std::string&, uint16_t)>;
    using ServerAccept = std::function<std::unique_ptr<sm::net::Transport>(uint16_t, int)>;

    ConnectionService(ConnectionManager& cm, const sm::pairing::KeyStore& keys,
                      sm::core::PeerId self, uint16_t listenPort, ClientDial dial,
                      ServerAccept accept)
        : cm_(cm),
          keys_(keys),
          self_(std::move(self)),
          listenPort_(listenPort),
          dial_(std::move(dial)),
          accept_(std::move(accept)) {}

    void setPeers(std::vector<Peer> peers) { peers_ = std::move(peers); }

    // Dial not-yet-connected peers (rate-limited per peer), accept one inbound
    // socket, and advance in-flight inbound handshakes. `now_ms` drives the dial
    // cooldown so an offline peer is retried, not hammered.
    void pollConnections(uint64_t now_ms, int acceptTimeoutMs = 200);

    uint64_t dialCooldownMs = 2000;

private:
    ConnectionManager& cm_;
    const sm::pairing::KeyStore& keys_;
    sm::core::PeerId self_;
    uint16_t listenPort_;
    ClientDial dial_;
    ServerAccept accept_;
    std::vector<Peer> peers_;
    std::map<sm::core::PeerId, uint64_t> lastDial_;
    std::vector<std::unique_ptr<InboundHandshake>> pendingIn_;
};

} // namespace sm::app
