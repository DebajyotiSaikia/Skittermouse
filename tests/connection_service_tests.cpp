#include "test_framework.h"

#include "app/connection_service.h"

#include <map>
#include <memory>
#include <string>

using namespace sm;

void run_connection_service_tests() {
    // --- Dial scheduling: each peer once per cooldown, never self ------------
    {
        app::MeshNode mesh("A");
        app::ConnectionManager cm(mesh);
        sm::pairing::KeyStore keys; // empty: dial returns null before any PSK use

        std::map<std::string, int> dials;
        auto dial = [&](const std::string& host, uint16_t) -> std::unique_ptr<net::Transport> {
            dials[host]++;
            return nullptr; // simulate an offline peer -> never connects
        };
        auto accept = [](uint16_t, int) -> std::unique_ptr<net::Transport> { return nullptr; };

        app::ConnectionService svc(cm, keys, "A", 9000, dial, accept);
        svc.dialCooldownMs = 2000;
        svc.setPeers({{"A", "hostA", 1}, {"B", "hostB", 2}, {"C", "hostC", 3}});

        svc.pollConnections(1000);
        SM_CHECK_EQ(dials["hostB"], 1);
        SM_CHECK_EQ(dials["hostC"], 1);
        SM_CHECK_EQ(static_cast<int>(dials.count("hostA")), 0); // never dial self

        // Within the cooldown window -> no re-dial.
        svc.pollConnections(1500);
        SM_CHECK_EQ(dials["hostB"], 1);

        // After the cooldown elapses -> retried (peer still offline).
        svc.pollConnections(4000);
        SM_CHECK_EQ(dials["hostB"], 2);
        SM_CHECK_EQ(dials["hostC"], 2);

        // Nothing ever connected (dial always failed), so the mesh has no peers.
        SM_CHECK_EQ(static_cast<int>(cm.connectedCount()), 0);
    }

    // --- Accepted sockets become in-flight handshakes (no crash on junk) ------
    {
        app::MeshNode mesh("B");
        app::ConnectionManager cm(mesh);
        sm::pairing::KeyStore keys;

        int accepts = 0;
        auto dial = [](const std::string&, uint16_t) -> std::unique_ptr<net::Transport> {
            return nullptr;
        };
        // Accept returns null (no inbound) -> service simply has nothing to advance.
        auto accept = [&](uint16_t, int) -> std::unique_ptr<net::Transport> {
            ++accepts;
            return nullptr;
        };
        app::ConnectionService svc(cm, keys, "B", 9000, dial, accept);
        svc.pollConnections(100);
        svc.pollConnections(200);
        SM_CHECK_EQ(accepts, 2); // accept attempted every poll
        SM_CHECK_EQ(static_cast<int>(cm.connectedCount()), 0);
    }
}
