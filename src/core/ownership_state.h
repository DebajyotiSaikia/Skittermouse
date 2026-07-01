#pragma once

// Peer-mesh input-owner state machine (spec Section 11).
//
// PURE LOGIC -- no OS calls, no platform includes. Unit-tested in isolation
// (Section 2.2). The capture/injection layer keys off isLocalOwner(): the owner
// captures and forwards; every other machine injects what it receives
// (Sections 3.1, 3.2).

#include <cstdint>

#include "core/peer_id.h"

namespace qq::core {

// A request to change which machine owns input. Broadcast to the WHOLE mesh, not
// sent privately to the target, so every machine's view stays consistent
// (Section 11.2). Claims carry a logical-clock value so all peers converge on a
// single winner with no central coordinator (Section 11.3).
struct OwnershipClaim {
    PeerId   target;    // machine that should become the input owner
    PeerId   origin;    // machine that issued this claim
    uint64_t sequence;  // logical-clock value; higher == newer
};

// Total order over claims. Higher sequence wins; an equal sequence means the
// claims are genuinely concurrent and is broken deterministically by origin id,
// so every machine independently computes the same winner. This subsumes the
// "current owner has authority" rule from Section 11.3: no veto round-trip is
// needed because the order is globally consistent.
bool claimSupersedes(const OwnershipClaim& candidate, const OwnershipClaim& current);

class OwnershipState {
public:
    explicit OwnershipState(PeerId self);

    const PeerId& self() const { return self_; }
    const PeerId& owner() const { return current_.target; }
    bool isLocalOwner() const { return current_.target == self_; }
    uint64_t clock() const { return clock_; }
    const OwnershipClaim& currentClaim() const { return current_; }

    // Local user hands ownership to `target` (via hotkey, picker, or tray).
    // Advances the logical clock and returns the claim to broadcast to every
    // peer (Section 11.2). The returned claim is already applied locally.
    OwnershipClaim requestSwitchTo(const PeerId& target);

    enum class ApplyResult {
        Adopted,     // claim won the total order; current owner updated
        Superseded,  // claim lost to what we already hold; ignored (idempotent)
    };

    // Apply a claim received from the network (or reflected back to us). Safe to
    // call repeatedly with the same claim -- re-delivery is a no-op.
    ApplyResult applyClaim(const OwnershipClaim& claim);

private:
    PeerId self_;
    OwnershipClaim current_;
    uint64_t clock_;
};

} // namespace qq::core
