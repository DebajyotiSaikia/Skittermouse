#pragma once

// End-to-end mesh simulator for scenario tests.
//
// Models N virtual machines, each running the REAL core logic (OwnershipState +
// ServerElection), connected by:
//   - a broadcast bus that delivers every ownership claim to every node
//     (switch-broadcast-to-all, Section 11.2), and
//   - presence events that update every node's election view (Section 6).
//
// This exercises the protocol end-to-end at the logic layer -- the highest
// fidelity achievable in an automated test. Hardware-level e2e (real input
// capture -> encrypted WSS -> real injection across two physical machines, and
// the Secure Desktop question in Section 14) cannot be automated here and must be
// verified on real devices; those layers aren't implemented yet.

#include <map>
#include <optional>
#include <vector>

#include "core/ownership_state.h"
#include "core/server_election.h"

namespace smtest {

class MeshSimulator {
public:
    struct Node {
        sm::core::OwnershipState ownership;
        sm::core::ServerElection election;
    };

    MeshSimulator(std::vector<sm::core::PeerId> machines,
                  std::vector<sm::core::PeerId> priority)
        : machines_(std::move(machines)) {
        for (const auto& id : machines_) {
            nodes_.emplace(id, Node{sm::core::OwnershipState{id},
                                    sm::core::ServerElection{priority}});
        }
        // Everyone boots online.
        for (auto& [nid, n] : nodes_)
            for (const auto& m : machines_)
                n.election.markOnline(m);
    }

    Node& node(const sm::core::PeerId& id) { return nodes_.at(id); }

    // `initiator` triggers a switch to `target`; the claim is broadcast to ALL
    // nodes (including the initiator), as a real switch would be (Section 11.2).
    void switchTo(const sm::core::PeerId& initiator, const sm::core::PeerId& target) {
        const sm::core::OwnershipClaim claim =
            nodes_.at(initiator).ownership.requestSwitchTo(target);
        broadcastClaim(claim);
    }

    // Deliver a pre-built claim to every node -- used to model concurrent or
    // out-of-order delivery of claims issued near-simultaneously.
    void broadcastClaim(const sm::core::OwnershipClaim& claim) {
        for (auto& [nid, n] : nodes_)
            n.ownership.applyClaim(claim);
    }

    // A machine goes online/offline; every node's presence view is updated.
    void setOnline(const sm::core::PeerId& id, bool up) {
        for (auto& [nid, n] : nodes_) {
            if (up)
                n.election.markOnline(id);
            else
                n.election.markOffline(id);
        }
    }

    // The user removes a machine from the priority list; config is replicated to
    // every node (Section 11.5).
    void removeEligibilityEverywhere(const sm::core::PeerId& id) {
        for (auto& [nid, n] : nodes_)
            n.election.removeFromPriority(id);
    }

    // --- Invariants for assertions ------------------------------------------
    bool allAgreeOnOwner() const {
        const sm::core::PeerId& ref = nodes_.begin()->second.ownership.owner();
        for (const auto& [id, n] : nodes_)
            if (n.ownership.owner() != ref)
                return false;
        return true;
    }

    sm::core::PeerId ownerView(const sm::core::PeerId& id) const {
        return nodes_.at(id).ownership.owner();
    }

    bool allAgreeOnServer() const {
        const std::optional<sm::core::PeerId> ref =
            nodes_.begin()->second.election.currentServer();
        for (const auto& [id, n] : nodes_)
            if (n.election.currentServer() != ref)
                return false;
        return true;
    }

    std::optional<sm::core::PeerId> serverView(const sm::core::PeerId& id) const {
        return nodes_.at(id).election.currentServer();
    }

private:
    std::vector<sm::core::PeerId> machines_;
    std::map<sm::core::PeerId, Node> nodes_;
};

} // namespace smtest
