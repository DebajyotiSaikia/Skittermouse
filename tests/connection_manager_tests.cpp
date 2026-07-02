#include "test_framework.h"

#include "loopback_transport.h"

#include "app/connection_manager.h"
#include "net/peer_hello.h"

#include <deque>
#include <memory>
#include <string>
#include <vector>

using namespace sm;

namespace {

// Build a heap-allocated loopback endpoint bound to the given inbox/outbox queues,
// so ownership can be transferred into a ConnectionManager (which owns its links).
std::unique_ptr<smtest::LoopbackEndpoint> makeEndpoint(
    std::deque<std::vector<uint8_t>>* inbox,
    std::deque<std::vector<uint8_t>>* peerInbox,
    smtest::LoopbackEndpoint** rawOut) {
    auto ep = std::make_unique<smtest::LoopbackEndpoint>();
    ep->inbox_ = inbox;
    ep->peerInbox_ = peerInbox;
    ep->connected_ = true;
    if (rawOut) *rawOut = ep.get();
    return ep;
}

} // namespace

void run_connection_manager_tests() {
    // --- peer-hello codec round-trip + rejection ------------------------------
    {
        auto enc = net::encodePeerHello("DESKTOP-B");
        core::PeerId id;
        SM_CHECK(net::decodePeerHello(enc.data(), enc.size(), id));
        SM_CHECK_EQ(id, std::string("DESKTOP-B"));

        SM_CHECK(!net::decodePeerHello(enc.data(), 3, id)); // truncated
        auto bad = enc;
        bad[0] ^= 0xFF; // wrong protocol version
        SM_CHECK(!net::decodePeerHello(bad.data(), bad.size(), id));
        bad = enc;
        bad[1] = 99; // wrong message type
        SM_CHECK(!net::decodePeerHello(bad.data(), bad.size(), id));
        bad = enc;
        bad.push_back('X'); // length field no longer matches payload
        SM_CHECK(!net::decodePeerHello(bad.data(), bad.size(), id));
    }

    // --- Two managers discover each other via hello, then the mesh works -------
    {
        // Queues outlive the managers (declared first, destroyed last).
        std::deque<std::vector<uint8_t>> aReads, bReads; // aReads: B->A, bReads: A->B
        app::MeshNode meshA("A"), meshB("B");
        std::vector<core::PeerId> pr = {"A", "B"};
        for (app::MeshNode* m : {&meshA, &meshB}) {
            m->setPriority(pr);
            m->heartbeatIntervalMs = 0;
            m->heartbeatTimeoutMs = 1000;
        }
        app::ConnectionManager cmA(meshA), cmB(meshB);

        smtest::LoopbackEndpoint* rawA = nullptr;
        smtest::LoopbackEndpoint* rawB = nullptr;
        cmA.addOutgoing("B", makeEndpoint(&aReads, &bReads, &rawA));
        cmB.addIncoming(makeEndpoint(&bReads, &aReads, &rawB));

        SM_CHECK_EQ(1, static_cast<int>(cmA.pendingCount()));
        SM_CHECK_EQ(1, static_cast<int>(cmB.pendingCount()));

        // First poll: each consumes the other's hello and registers the peer.
        cmA.poll(100);
        cmB.poll(100);
        SM_CHECK(cmA.isConnected("B"));
        SM_CHECK(cmB.isConnected("A"));
        SM_CHECK_EQ(0, static_cast<int>(cmA.pendingCount()));

        // A couple more pumps establish liveness + coordinator election.
        for (int i = 1; i < 4; ++i) {
            cmA.poll(100 + i);
            cmB.poll(100 + i);
        }
        SM_CHECK(meshA.isPeerOnline("B"));
        SM_CHECK(meshB.isPeerOnline("A"));
        SM_CHECK_EQ(meshA.primary().value_or(""), std::string("A"));

        // A switch initiated on A reaches B through the manager-owned link.
        meshA.requestSwitchTo("B");
        cmA.poll(200);
        cmB.poll(200);
        SM_CHECK(meshB.isLocalOwner());
        SM_CHECK_EQ(meshA.owner(), std::string("B"));

        // Disconnecting A's link is reaped: peer removed, node reverts to local.
        int lost = 0;
        cmA.onPeerDisconnected = [&](const core::PeerId& id) {
            if (id == "B") ++lost;
        };
        rawA->close();
        cmA.poll(300);
        SM_CHECK_EQ(lost, 1);
        SM_CHECK(!cmA.isConnected("B"));
    }

    // --- Identity mismatch is rejected (dialed B, peer claims C) --------------
    {
        std::deque<std::vector<uint8_t>> aReads, bReads;
        app::MeshNode meshA("A");
        app::ConnectionManager cmA(meshA);

        smtest::LoopbackEndpoint* rawA = nullptr;
        cmA.addOutgoing("B", makeEndpoint(&aReads, &bReads, &rawA));
        // The far end (not a manager) announces a DIFFERENT id than we dialed.
        auto wrong = net::encodePeerHello("C");
        aReads.emplace_back(wrong.begin(), wrong.end());

        cmA.poll(100);
        SM_CHECK(!cmA.isConnected("B"));
        SM_CHECK(!cmA.isConnected("C"));
        SM_CHECK_EQ(0, static_cast<int>(cmA.pendingCount())); // dropped, not stuck
    }

    // --- A hello claiming our own id is ignored (no self-connection) ----------
    {
        std::deque<std::vector<uint8_t>> aReads, bReads;
        app::MeshNode meshA("A");
        app::ConnectionManager cmA(meshA);

        cmA.addIncoming(makeEndpoint(&aReads, &bReads, nullptr));
        auto selfHello = net::encodePeerHello("A");
        aReads.emplace_back(selfHello.begin(), selfHello.end());

        cmA.poll(100);
        SM_CHECK(!cmA.isConnected("A"));
        SM_CHECK_EQ(0, static_cast<int>(cmA.connectedCount()));
    }

    // --- Duplicate link to an already-connected peer is dropped ---------------
    {
        std::deque<std::vector<uint8_t>> a1, b1, a2, b2;
        app::MeshNode meshA("A");
        app::ConnectionManager cmA(meshA);

        cmA.addIncoming(makeEndpoint(&a1, &b1, nullptr));
        auto hello = net::encodePeerHello("B");
        a1.emplace_back(hello.begin(), hello.end());
        cmA.poll(100);
        SM_CHECK(cmA.isConnected("B"));
        SM_CHECK_EQ(1, static_cast<int>(cmA.connectedCount()));

        // A second inbound link that also claims "B" must not double-register.
        cmA.addIncoming(makeEndpoint(&a2, &b2, nullptr));
        a2.emplace_back(hello.begin(), hello.end());
        cmA.poll(101);
        SM_CHECK_EQ(1, static_cast<int>(cmA.connectedCount()));
    }
}
