#pragma once

// Wake-on-LAN "Waking…" flow (spec 12). PURE LOGIC state machine with an injected
// clock: when a switch targets an unreachable-but-WoL-plausible machine, the OS
// layer sends the magic packet (net/wol_sender) and drives this flow, which shows a
// bounded "Waking <machine>…" state that resolves to Connected the moment the peer
// comes back, or to TimedOut after the deadline (spec suggests 30-60 s) so the UI
// can present the guided fallback (check BIOS/UEFI, adapter wake setting, Fast
// Startup). No OS calls here -- deterministic and unit-testable.

#include <cstdint>

#include "core/peer_id.h"

namespace sm::core {

class WakeFlow {
public:
    enum class Status { Idle, Waking, Connected, TimedOut };

    // Begin waking `target` at now_ms with a timeout window. The caller sends the
    // magic packet; this only tracks the state/deadline.
    void start(const PeerId& target, uint64_t now_ms, uint64_t timeoutMs);

    // Advance the flow. While Waking, becomes Connected the instant `targetOnline`
    // is true, else TimedOut once the window elapses. A terminal state is sticky
    // until reset()/start(). Returns the (possibly updated) status.
    Status update(uint64_t now_ms, bool targetOnline);

    Status status() const { return status_; }
    const PeerId& target() const { return target_; }
    bool isWaking() const { return status_ == Status::Waking; }

    void reset();

private:
    Status status_ = Status::Idle;
    PeerId target_;
    uint64_t start_ = 0;
    uint64_t timeout_ = 0;
};

} // namespace sm::core
