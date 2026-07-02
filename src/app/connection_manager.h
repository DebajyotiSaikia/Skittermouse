#pragma once

// Peer connection lifecycle manager (spec 2.1, 5.2, 9-mesh). Owns the Transport for
// every peer link and runs the peer-hello handshake that names a fresh connection,
// then registers it with the MeshNode. It unifies the two ways a link appears --
// this machine dialed out to a known peer, or accepted an inbound socket -- so there
// is no fixed client/server role (spec 2.1): both sides announce themselves and each
// learns the other's id from its hello.
//
// PURE LOGIC over the abstract Transport (spec 5.5): the OS layer runs the actual
// connect()/accept() on a socket thread and hands the resulting Transports here,
// then pumps poll(); every decision below is deterministic and unit-testable with
// loopback transports and an injected clock.

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <vector>

#include "app/mesh_node.h"
#include "core/peer_id.h"
#include "net/transport.h"

namespace sm::app {

class ConnectionManager {
public:
    explicit ConnectionManager(MeshNode& mesh) : mesh_(mesh) {}

    // A dial-out to a KNOWN peer completed. We announce ourselves immediately and
    // register the link once the peer's hello confirms it really is `expected`
    // (a mismatch is dropped -- it is not the machine we meant to reach).
    void addOutgoing(const sm::core::PeerId& expected,
                     std::unique_ptr<sm::net::Transport> transport);

    // An inbound socket was accepted; its peer id is learned from the hello.
    void addIncoming(std::unique_ptr<sm::net::Transport> transport);

    // Advance one step: finish pending handshakes, reap dead links, then pump the
    // mesh (poll + heartbeats). This is the single drive point for the OS timer.
    void poll(uint64_t now_ms);

    bool isConnected(const sm::core::PeerId& id) const {
        return connected_.find(id) != connected_.end();
    }
    std::size_t pendingCount() const { return pending_.size(); }
    std::size_t connectedCount() const { return connected_.size(); }

    std::function<void(const sm::core::PeerId&)> onPeerConnected;
    std::function<void(const sm::core::PeerId&)> onPeerDisconnected;

private:
    struct Pending {
        std::unique_ptr<sm::net::Transport> transport;
        sm::core::PeerId expected; // empty for inbound (identity learned from hello)
    };

    MeshNode& mesh_;
    std::vector<Pending> pending_;
    std::map<sm::core::PeerId, std::unique_ptr<sm::net::Transport>> connected_;
};

} // namespace sm::app
