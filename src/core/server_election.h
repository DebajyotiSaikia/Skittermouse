#pragma once

// Elected-coordinator role with static-priority failover (spec Section 11.5).
//
// PURE LOGIC -- no OS calls, no platform includes. Layered on top of the peer
// mesh (Section 2.1): the elected "server" (surfaced as "primary" in any UI, to
// honor Section 10) is simply the highest-priority machine currently online. It
// has NO special authority -- input ownership stays coordinator-free
// (Section 11.3), and the role grants no permissions.
//
// Because the election is a deterministic function of (priority list, online
// set), every machine computes the same result with no negotiation, so failover
// and failback are automatic and split-brain-free.
//
// User model (Section 11.5): the user designates machines in priority order
// (index 0 = highest). All paired machines are eligible by default; the user may
// remove a machine so it is never elected, and new pairings append at lowest
// priority. For machine k to be the server, every higher-priority machine must be
// offline; when a higher-priority machine returns it preemptively reclaims the
// role.

#include <optional>
#include <vector>

#include "core/peer_id.h"

namespace qq::core {

class ServerElection {
public:
    ServerElection() = default;
    explicit ServerElection(std::vector<PeerId> priority);

    // --- Priority / eligibility (Section 11.5) -------------------------------
    // Replace the whole priority list (index 0 = highest priority).
    void setPriority(std::vector<PeerId> priority);
    const std::vector<PeerId>& priority() const { return priority_; }

    // Append a machine at lowest priority if not already present -- the default
    // when a new device is paired ("on the list by default").
    void addToPriority(const PeerId& id);
    // Remove a machine so it can never be elected ("unless the user removes it").
    void removeFromPriority(const PeerId& id);
    bool isEligible(const PeerId& id) const;

    // --- Online set (fed by the presence beacon, Section 6) ------------------
    void setOnline(const std::vector<PeerId>& online);
    void markOnline(const PeerId& id);
    void markOffline(const PeerId& id);
    bool isOnline(const PeerId& id) const;

    // --- Election result -----------------------------------------------------
    // Highest-priority eligible machine currently online, or nullopt if none.
    std::optional<PeerId> currentServer() const;
    bool isServer(const PeerId& id) const;

private:
    std::vector<PeerId> priority_;
    std::vector<PeerId> online_;  // small N (a handful of machines); linear scans are fine

    static bool contains(const std::vector<PeerId>& v, const PeerId& id);
    static void addUnique(std::vector<PeerId>& v, const PeerId& id);
    static void eraseValue(std::vector<PeerId>& v, const PeerId& id);
};

} // namespace qq::core
