#include "test_framework.h"

#include "core/ownership_state.h"

#include <string>

using namespace qq::core;

void run_ownership_state_tests() {
    // Fail-safe default: a machine starts owning itself (Section 15).
    {
        OwnershipState s{"A"};
        QQ_CHECK(s.isLocalOwner());
        QQ_CHECK_EQ(s.owner(), std::string("A"));
        QQ_CHECK_EQ(s.self(), std::string("A"));
        QQ_CHECK_EQ(s.clock(), 0u);
        QQ_CHECK_EQ(s.currentClaim().target, std::string("A"));
        QQ_CHECK_EQ(s.currentClaim().origin, std::string("A"));
        QQ_CHECK_EQ(s.currentClaim().sequence, 0u);
    }

    // A local switch hands ownership away and advances the logical clock.
    {
        OwnershipState s{"A"};
        OwnershipClaim c = s.requestSwitchTo("B");
        QQ_CHECK(!s.isLocalOwner());
        QQ_CHECK_EQ(s.owner(), std::string("B"));
        QQ_CHECK_EQ(c.origin, std::string("A"));
        QQ_CHECK_EQ(c.target, std::string("B"));
        QQ_CHECK_EQ(c.sequence, 1u);
        QQ_CHECK_EQ(s.clock(), 1u);
    }

    // Switching back to self restores local ownership (fail-safe reclaim).
    {
        OwnershipState s{"A"};
        s.requestSwitchTo("B");
        s.requestSwitchTo("A");
        QQ_CHECK(s.isLocalOwner());
        QQ_CHECK_EQ(s.clock(), 2u);
    }

    // A newer remote claim is adopted; a stale (lower-sequence) one is ignored.
    {
        OwnershipState s{"A"};
        QQ_CHECK(s.applyClaim({"C", "B", 5}) == OwnershipState::ApplyResult::Adopted);
        QQ_CHECK_EQ(s.owner(), std::string("C"));
        QQ_CHECK(s.clock() >= 5u);
        QQ_CHECK(s.applyClaim({"D", "B", 3}) == OwnershipState::ApplyResult::Superseded);
        QQ_CHECK_EQ(s.owner(), std::string("C")); // unchanged
    }

    // Re-delivery of the same claim is idempotent (Section 11.2 broadcast reflects
    // our own claims back to us).
    {
        OwnershipState s{"A"};
        OwnershipClaim c{"C", "B", 7};
        QQ_CHECK(s.applyClaim(c) == OwnershipState::ApplyResult::Adopted);
        QQ_CHECK(s.applyClaim(c) == OwnershipState::ApplyResult::Superseded);
        QQ_CHECK_EQ(s.owner(), std::string("C"));
    }

    // Lamport clock keeps a locally-issued claim ahead of everything seen.
    {
        OwnershipState s{"A"};
        s.applyClaim({"C", "B", 9});
        OwnershipClaim c = s.requestSwitchTo("A");
        QQ_CHECK_EQ(c.sequence, 10u);
        QQ_CHECK(s.isLocalOwner());
    }

    // Concurrent claims (equal sequence) converge on the same winner across peers
    // (Section 11.3). Origin "B" > "A", so B's claim (target "D") wins the tie on
    // both machines.
    {
        OwnershipState a{"A"};
        OwnershipState b{"B"};
        OwnershipClaim ca = a.requestSwitchTo("C"); // {C, A, 1}
        OwnershipClaim cb = b.requestSwitchTo("D"); // {D, B, 1}
        a.applyClaim(cb);
        b.applyClaim(ca);
        QQ_CHECK_EQ(a.owner(), b.owner());          // converged
        QQ_CHECK_EQ(a.owner(), std::string("D"));
    }

    // Total-order sanity for claimSupersedes.
    {
        QQ_CHECK(claimSupersedes({"X", "A", 2}, {"Y", "A", 1}));
        QQ_CHECK(!claimSupersedes({"X", "A", 1}, {"Y", "A", 2}));
        QQ_CHECK(claimSupersedes({"X", "B", 1}, {"Y", "A", 1}));   // origin tiebreak
        QQ_CHECK(!claimSupersedes({"X", "A", 1}, {"Y", "A", 1}));  // equal -> not superseding
    }
}
