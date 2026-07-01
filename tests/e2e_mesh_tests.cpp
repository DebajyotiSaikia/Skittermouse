#include "test_framework.h"

#include "mesh_simulator.h"

#include <optional>
#include <string>

using namespace sm::core;
using smtest::MeshSimulator;
using std::optional;
using std::string;

void run_e2e_mesh_tests() {
    // Two machines: a switch converges the whole mesh (Section 11.2).
    {
        MeshSimulator m({"A", "B"}, {"A", "B"});
        m.switchTo("A", "B");
        SM_CHECK(m.allAgreeOnOwner());
        SM_CHECK_EQ(m.ownerView("A"), string("B"));
        SM_CHECK_EQ(m.ownerView("B"), string("B"));
    }

    // Three machines: a switch broadcasts to ALL -- no node is left with stale
    // state (Section 2.1 / 11.2).
    {
        MeshSimulator m({"A", "B", "C"}, {"A", "B", "C"});
        m.switchTo("A", "C");
        SM_CHECK(m.allAgreeOnOwner());
        SM_CHECK_EQ(m.ownerView("A"), string("C"));
        SM_CHECK_EQ(m.ownerView("B"), string("C"));
        SM_CHECK_EQ(m.ownerView("C"), string("C"));
    }

    // Switching back to self restores local ownership across the mesh.
    {
        MeshSimulator m({"A", "B"}, {"A", "B"});
        m.switchTo("A", "B");
        m.switchTo("A", "A");
        SM_CHECK(m.allAgreeOnOwner());
        SM_CHECK_EQ(m.ownerView("A"), string("A"));
    }

    // Concurrent switch claims from two machines converge identically everywhere
    // (Section 11.3). Both claims are built first (each applied only locally),
    // then interleaved onto the bus.
    {
        MeshSimulator m({"A", "B", "C"}, {"A", "B", "C"});
        const OwnershipClaim claimA = m.node("A").ownership.requestSwitchTo("B"); // {B,A,1}
        const OwnershipClaim claimC = m.node("C").ownership.requestSwitchTo("A"); // {A,C,1}
        m.broadcastClaim(claimA);
        m.broadcastClaim(claimC);
        SM_CHECK(m.allAgreeOnOwner());
        // Equal sequence; origin "C" > "A" wins, so the owner is claimC.target.
        SM_CHECK_EQ(m.ownerView("B"), string("A"));
    }

    // Delivery order of concurrent claims does not change the outcome.
    {
        MeshSimulator m({"A", "B", "C"}, {"A", "B", "C"});
        const OwnershipClaim claimA = m.node("A").ownership.requestSwitchTo("B");
        const OwnershipClaim claimC = m.node("C").ownership.requestSwitchTo("A");
        m.broadcastClaim(claimC);   // reversed order vs the previous scenario
        m.broadcastClaim(claimA);
        SM_CHECK(m.allAgreeOnOwner());
        SM_CHECK_EQ(m.ownerView("B"), string("A"));
    }

    // Server election baseline: with everyone online, the top of the list serves,
    // and every node agrees.
    {
        MeshSimulator m({"1", "2", "3", "4", "5"}, {"1", "2", "3", "4", "5"});
        SM_CHECK(m.allAgreeOnServer());
        SM_CHECK(m.serverView("1") == optional<PeerId>("1"));
    }

    // Server failover chain (user's 1..5 example) stays mesh-consistent at each step.
    {
        MeshSimulator m({"1", "2", "3", "4", "5"}, {"1", "2", "3", "4", "5"});
        m.setOnline("1", false);
        SM_CHECK(m.allAgreeOnServer());
        SM_CHECK(m.serverView("3") == optional<PeerId>("2"));
        m.setOnline("2", false);
        SM_CHECK(m.serverView("5") == optional<PeerId>("3"));
        m.setOnline("3", false);
        m.setOnline("4", false);
        SM_CHECK(m.allAgreeOnServer());
        SM_CHECK(m.serverView("5") == optional<PeerId>("5"));
        m.setOnline("5", false);
        SM_CHECK(m.allAgreeOnServer());
        SM_CHECK(!m.serverView("5").has_value());   // nobody online -> no server
    }

    // Preemptive failback across the mesh when a higher-priority machine returns.
    {
        MeshSimulator m({"1", "2", "3"}, {"1", "2", "3"});
        m.setOnline("1", false);
        SM_CHECK(m.serverView("2") == optional<PeerId>("2"));
        m.setOnline("1", true);
        SM_CHECK(m.allAgreeOnServer());
        SM_CHECK(m.serverView("2") == optional<PeerId>("1"));
    }

    // User removes the current primary from the priority list on every machine.
    {
        MeshSimulator m({"A", "B", "C"}, {"A", "B", "C"});
        SM_CHECK(m.serverView("A") == optional<PeerId>("A"));
        m.removeEligibilityEverywhere("A");
        SM_CHECK(m.allAgreeOnServer());
        SM_CHECK(m.serverView("A") == optional<PeerId>("B"));
    }

    // Combined fail-safe scenario: the input owner drops off the network, the
    // surviving machine reclaims LOCAL control (Section 15), and the elected
    // primary fails over too. Priority makes "B" the preferred primary here.
    {
        MeshSimulator m({"A", "B"}, {"B", "A"});
        SM_CHECK(m.serverView("A") == optional<PeerId>("B"));
        m.switchTo("A", "B");                 // B owns input
        SM_CHECK_EQ(m.ownerView("A"), string("B"));
        m.setOnline("B", false);              // B crashes / lid closed
        m.switchTo("A", "A");                 // fail-safe: A reclaims local input
        SM_CHECK_EQ(m.ownerView("A"), string("A"));
        SM_CHECK(m.serverView("A") == optional<PeerId>("A")); // primary failover B -> A
    }
}
