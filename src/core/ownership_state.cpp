#include "core/ownership_state.h"

#include <utility>

namespace sm::core {

bool claimSupersedes(const OwnershipClaim& candidate, const OwnershipClaim& current) {
    if (candidate.sequence != current.sequence)
        return candidate.sequence > current.sequence;
    // Genuinely concurrent (equal logical time): break the tie deterministically
    // by origin id so every machine in the mesh converges on the same winner
    // (Section 11.3).
    return candidate.origin > current.origin;
}

OwnershipState::OwnershipState(PeerId self)
    : self_(std::move(self)),
      current_{self_, self_, 0},   // fail-safe default: a machine starts owning itself (Section 15)
      clock_(0) {}

OwnershipClaim OwnershipState::requestSwitchTo(const PeerId& target) {
    // A locally-initiated switch is, by construction, the newest claim we know
    // about (clock_ + 1), so it always wins locally and is then broadcast to
    // every peer (Section 11.2).
    OwnershipClaim claim{target, self_, clock_ + 1};
    applyClaim(claim);
    return claim;
}

OwnershipState::ApplyResult OwnershipState::applyClaim(const OwnershipClaim& claim) {
    // Lamport-style clock: never issue a future claim with a stale sequence.
    if (claim.sequence > clock_)
        clock_ = claim.sequence;

    if (claimSupersedes(claim, current_)) {
        current_ = claim;
        return ApplyResult::Adopted;
    }
    return ApplyResult::Superseded;
}

} // namespace sm::core
