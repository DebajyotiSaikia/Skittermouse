#include "app/connection_manager.h"

#include "net/peer_hello.h"

#include <utility>

namespace sm::app {

namespace {

void announce(sm::net::Transport* t, const sm::core::PeerId& self) {
    const std::vector<uint8_t> hello = sm::net::encodePeerHello(self);
    t->send(hello.data(), hello.size());
}

} // namespace

void ConnectionManager::addOutgoing(const sm::core::PeerId& expected,
                                    std::unique_ptr<sm::net::Transport> transport) {
    if (!transport) return;
    announce(transport.get(), mesh_.self());
    pending_.push_back(Pending{std::move(transport), expected});
}

void ConnectionManager::addIncoming(std::unique_ptr<sm::net::Transport> transport) {
    if (!transport) return;
    announce(transport.get(), mesh_.self());
    pending_.push_back(Pending{std::move(transport), sm::core::PeerId{}});
}

void ConnectionManager::poll(uint64_t now_ms) {
    // 1. Resolve pending handshakes. A pending link is promoted the moment a valid
    //    hello arrives; it is dropped on error, protocol mismatch, wrong identity,
    //    self-connection, or if the peer is already connected by another link.
    std::vector<Pending> still;
    still.reserve(pending_.size());
    for (Pending& p : pending_) {
        if (!p.transport || !p.transport->isConnected()) continue; // dead -> drop

        uint8_t buf[1024];
        const int n = p.transport->recv(buf, sizeof(buf));
        if (n < 0) continue;                          // error -> drop
        if (n == 0) {                                 // nothing yet -> keep waiting
            still.push_back(std::move(p));
            continue;
        }

        sm::core::PeerId id;
        if (!sm::net::decodePeerHello(buf, static_cast<std::size_t>(n), id)) continue;
        if (id.empty() || id == mesh_.self()) continue;          // junk / self
        if (!p.expected.empty() && id != p.expected) continue;   // not who we dialed
        if (connected_.find(id) != connected_.end()) continue;   // duplicate link

        sm::net::Transport* raw = p.transport.get();
        connected_.emplace(id, std::move(p.transport));
        mesh_.addPeer(id, raw);
        if (onPeerConnected) onPeerConnected(id);
    }
    pending_.swap(still);

    // 2. Reap connections the transport reports as closed, so the mesh stops
    //    treating a dead link as a live peer and the node reverts to local control
    //    via the heartbeat fail-safe (spec 15).
    for (auto it = connected_.begin(); it != connected_.end();) {
        if (!it->second->isConnected()) {
            const sm::core::PeerId id = it->first;
            mesh_.removePeer(id);
            if (onPeerDisconnected) onPeerDisconnected(id);
            it = connected_.erase(it);
        } else {
            ++it;
        }
    }

    // 3. Drive the mesh over the currently-live links.
    mesh_.poll(now_ms);
    mesh_.sendHeartbeats(now_ms);
}

} // namespace sm::app
