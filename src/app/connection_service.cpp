#include "app/connection_service.h"

#include <utility>

namespace sm::app {

void ConnectionService::pollConnections(uint64_t now_ms, int acceptTimeoutMs) {
    // 1. Dial peers we know but aren't connected to yet. Rate-limited per peer so an
    //    offline peer is retried on a cooldown rather than hammered every poll. A
    //    duplicate that races an in-flight link is dropped by ConnectionManager's
    //    dedup, so re-dialing is safe.
    for (const Peer& p : peers_) {
        if (p.id == self_ || cm_.isConnected(p.id) || !dial_) continue;
        uint64_t& last = lastDial_[p.id];
        if (last != 0 && now_ms - last < dialCooldownMs) continue;
        last = now_ms;

        std::unique_ptr<sm::net::Transport> raw = dial_(p.host, p.port);
        if (!raw) continue; // connect failed (peer offline) -> retry after cooldown
        SecureLink link = secureOutbound(std::move(raw), keys_, self_, p.id);
        if (link.transport) cm_.addOutgoing(p.id, std::move(link.transport));
    }

    // 2. Accept at most one inbound socket this poll; its identity is learned from
    //    the clear id-hint during the handshake.
    if (accept_) {
        std::unique_ptr<sm::net::Transport> in = accept_(listenPort_, acceptTimeoutMs);
        if (in) {
            pendingIn_.push_back(
                std::make_unique<InboundHandshake>(std::move(in), keys_, self_));
        }
    }

    // 3. Advance inbound handshakes: register the sealed link on success, keep
    //    waiting on NeedMore, drop on rejection/error.
    std::vector<std::unique_ptr<InboundHandshake>> still;
    still.reserve(pendingIn_.size());
    for (auto& h : pendingIn_) {
        const InboundHandshake::Status st = h->poll();
        if (st == InboundHandshake::Status::Ok) {
            SecureLink link = h->take();
            cm_.addIncoming(std::move(link.transport));
        } else if (st == InboundHandshake::Status::NeedMore) {
            still.push_back(std::move(h));
        }
    }
    pendingIn_.swap(still);
}

} // namespace sm::app
