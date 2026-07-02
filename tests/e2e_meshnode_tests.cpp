#include "test_framework.h"

#include "loopback_transport.h"

#include "app/mesh_node.h"
#include "net/ownership_codec.h"

#include <string>
#include <vector>

using namespace sm;

void run_e2e_meshnode_tests() {
    // --- Ownership claim codec round-trip ------------------------------------
    {
        core::OwnershipClaim c{"target-machine", "origin-machine", 42};
        auto enc = net::encodeOwnershipClaim(c);
        core::OwnershipClaim d;
        SM_CHECK(net::decodeOwnershipClaim(enc.data(), enc.size(), d));
        SM_CHECK_EQ(d.target, c.target);
        SM_CHECK_EQ(d.origin, c.origin);
        SM_CHECK_EQ(d.sequence, c.sequence);
    }

    // Three fully-connected nodes over loopback pairs (A-B, A-C, B-C).
    smtest::LoopbackPair AB, AC, BC;
    app::MeshNode A("A"), B("B"), C("C");
    std::vector<core::PeerId> pr = {"A", "B", "C"};
    for (app::MeshNode* n : {&A, &B, &C}) {
        n->setPriority(pr);
        n->heartbeatIntervalMs = 0;   // force a heartbeat on every pump
        n->heartbeatTimeoutMs = 1000;
    }
    A.addPeer("B", &AB.a);
    A.addPeer("C", &AC.a);
    B.addPeer("A", &AB.b);
    B.addPeer("C", &BC.a);
    C.addPeer("A", &AC.b);
    C.addPeer("B", &BC.b);

    auto pump = [&](uint64_t now, bool aOn, bool bOn, bool cOn) {
        if (aOn) A.sendHeartbeats(now);
        if (bOn) B.sendHeartbeats(now);
        if (cOn) C.sendHeartbeats(now);
        A.poll(now);
        B.poll(now);
        C.poll(now);
    };

    // --- Liveness + coordinator election (spec 11.5) ------------------------
    for (int i = 0; i < 3; ++i) pump(100 + i, true, true, true);
    SM_CHECK(A.isPeerOnline("B"));
    SM_CHECK(A.isPeerOnline("C"));
    SM_CHECK_EQ(A.primary().value_or(""), std::string("A"));
    SM_CHECK_EQ(B.primary().value_or(""), std::string("A"));
    SM_CHECK_EQ(C.primary().value_or(""), std::string("A"));

    // --- Switch broadcasts to ALL peers (spec 11.2) -------------------------
    A.requestSwitchTo("B");
    pump(200, true, true, true);
    SM_CHECK_EQ(A.owner(), std::string("B"));
    SM_CHECK(!A.isLocalOwner());
    SM_CHECK(B.isLocalOwner());
    SM_CHECK_EQ(C.owner(), std::string("B")); // the third machine is NOT left stale

    // --- Owner forwards input; every sink injects (spec 3.2) ----------------
    int aInj = 0, cInj = 0;
    A.onInject = [&](const core::InputEvent&) { ++aInj; };
    C.onInject = [&](const core::InputEvent&) { ++cInj; };
    B.forwardKey(0x41, true, 1); // B is owner
    pump(210, true, true, true);
    SM_CHECK_EQ(aInj, 1);
    SM_CHECK_EQ(cInj, 1);

    // --- Clipboard sync + loop prevention across the mesh (spec 8) ----------
    std::string aClip, cClip;
    int cClipCount = 0;
    A.onRemoteClipboard = [&](const std::string& t) { aClip = t; };
    C.onRemoteClipboard = [&](const std::string& t) { cClip = t; ++cClipCount; };
    B.onLocalClipboardChange("hello");
    pump(220, true, true, true);
    SM_CHECK_EQ(aClip, std::string("hello"));
    SM_CHECK_EQ(cClip, std::string("hello"));
    SM_CHECK_EQ(cClipCount, 1);
    // A's OS re-fires a change for the value it just applied -> must NOT rebroadcast.
    A.onLocalClipboardChange("hello");
    pump(230, true, true, true);
    SM_CHECK_EQ(cClipCount, 1); // no second copy reached C

    // --- Coordinator failover + failback (spec 11.5) ------------------------
    for (int i = 0; i < 4; ++i) pump(2000 + i, false, true, true); // A goes silent
    SM_CHECK(!B.isPeerOnline("A"));
    SM_CHECK_EQ(B.primary().value_or(""), std::string("B"));
    SM_CHECK_EQ(C.primary().value_or(""), std::string("B"));
    for (int i = 0; i < 4; ++i) pump(3000 + i, true, true, true); // A returns
    SM_CHECK_EQ(B.primary().value_or(""), std::string("A")); // preemptive failback

    // --- Fail-safe: silent owner -> every sink reverts to local (spec 15) ---
    A.requestSwitchTo("A"); // owner = A
    pump(4000, true, true, true);
    SM_CHECK_EQ(B.owner(), std::string("A"));
    SM_CHECK(!B.isLocalOwner());
    for (int i = 0; i < 4; ++i) pump(5000 + i, false, true, true); // A dies
    SM_CHECK(B.isLocalOwner()); // B took back its own input
    SM_CHECK(C.isLocalOwner()); // C too
}
