#include "test_framework.h"

#include "core/server_election.h"

#include <optional>
#include <string>
#include <vector>

using namespace sm::core;
using std::optional;
using std::string;
using std::vector;

void run_server_election_tests() {
    // Empty priority list -> no server, regardless of who is online.
    {
        ServerElection e;                         // default ctor
        e.markOnline("A");
        SM_CHECK(!e.currentServer().has_value());
        SM_CHECK(!e.isServer("A"));               // no server -> isServer is false
    }

    // Nobody online -> no server.
    {
        ServerElection e{{"A", "B", "C"}};
        SM_CHECK(!e.currentServer().has_value());
    }

    // Highest-priority online machine is the server.
    {
        ServerElection e{{"A", "B", "C"}};
        e.setOnline({"A", "B", "C"});
        SM_CHECK(e.currentServer() == optional<PeerId>("A"));
        SM_CHECK(e.isServer("A"));
        SM_CHECK(!e.isServer("B"));               // online, but not the winner
    }

    // Failover down the list as higher-priority machines drop (user's 1..5 case).
    {
        ServerElection e{{"1", "2", "3", "4", "5"}};
        e.setOnline({"1", "2", "3", "4", "5"});
        SM_CHECK(e.currentServer() == optional<PeerId>("1"));
        e.markOffline("1");
        SM_CHECK(e.currentServer() == optional<PeerId>("2"));
        e.markOffline("2");
        e.markOffline("3");
        SM_CHECK(e.currentServer() == optional<PeerId>("4")); // 4 wins: 1-3 all down
        e.markOffline("4");
        SM_CHECK(e.currentServer() == optional<PeerId>("5"));
        e.markOffline("5");
        SM_CHECK(!e.currentServer().has_value());             // all down -> none
    }

    // Preemptive failback: a higher-priority machine returning reclaims the role.
    {
        ServerElection e{{"1", "2", "3"}};
        e.setOnline({"2", "3"});                  // 1 offline
        SM_CHECK(e.currentServer() == optional<PeerId>("2"));
        e.markOnline("1");
        SM_CHECK(e.currentServer() == optional<PeerId>("1"));
    }

    // Online but ineligible (not on the list) -> never server.
    {
        ServerElection e{{"B"}};                  // only B eligible
        e.setOnline({"A", "B"});
        SM_CHECK(!e.isEligible("A"));
        SM_CHECK(e.isEligible("B"));
        SM_CHECK(e.currentServer() == optional<PeerId>("B"));
    }

    // Removing the current server hands off to the next eligible online machine.
    {
        ServerElection e{{"A", "B", "C"}};
        e.setOnline({"A", "B", "C"});
        SM_CHECK(e.currentServer() == optional<PeerId>("A"));
        e.removeFromPriority("A");
        SM_CHECK(!e.isEligible("A"));
        SM_CHECK(e.currentServer() == optional<PeerId>("B"));
    }

    // Removing a non-server (or absent) machine leaves the server unchanged.
    {
        ServerElection e{{"A", "B", "C"}};
        e.setOnline({"A", "B", "C"});
        e.removeFromPriority("C");                 // present, not the server
        SM_CHECK(e.currentServer() == optional<PeerId>("A"));
        e.removeFromPriority("Z");                 // absent -> no-op
        SM_CHECK(e.currentServer() == optional<PeerId>("A"));
    }

    // addToPriority appends at lowest priority and de-dups ("on the list by default").
    {
        ServerElection e{{"A"}};
        e.addToPriority("B");                      // new -> appended
        e.addToPriority("A");                      // duplicate -> ignored
        SM_CHECK(e.priority() == vector<PeerId>({"A", "B"}));
        e.setOnline({"B"});                        // A offline -> B serves
        SM_CHECK(e.currentServer() == optional<PeerId>("B"));
        e.markOnline("A");                         // A back -> reclaims
        SM_CHECK(e.currentServer() == optional<PeerId>("A"));
    }

    // setPriority replaces the entire order.
    {
        ServerElection e{{"A", "B"}};
        e.setPriority({"B", "A"});
        e.setOnline({"A", "B"});
        SM_CHECK(e.currentServer() == optional<PeerId>("B"));
        SM_CHECK(e.priority() == vector<PeerId>({"B", "A"}));
    }

    // markOnline de-dups; markOffline of an absent id is a no-op.
    {
        ServerElection e{{"A"}};
        e.markOnline("A");
        e.markOnline("A");                         // dedup path
        SM_CHECK(e.isOnline("A"));
        e.markOffline("B");                        // absent -> no-op
        SM_CHECK(e.isOnline("A"));
        SM_CHECK(!e.isOnline("B"));
        e.markOffline("A");
        SM_CHECK(!e.isOnline("A"));
    }
}
