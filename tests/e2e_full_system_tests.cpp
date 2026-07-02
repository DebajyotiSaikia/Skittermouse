#include "test_framework.h"

#include "mock_systems.h"

#include "app/connection_manager.h"
#include "app/connection_service.h"
#include "app/mesh_node.h"
#include "core/event_types.h"
#include "core/peer_id.h"
#include "loopback_transport.h"
#include "pairing/key_store.h"
#include "pairing/pairing_exchange.h"

#include <memory>
#include <string>
#include <vector>

using namespace sm;

namespace {

// Drive two ConnectionServices + their mesh pumps until both report the peer, or we
// give up. Loopback/switchboard delivery is synchronous, so a few rounds suffice.
void pumpConnect(app::ConnectionService& svcA, app::ConnectionService& svcB,
                 app::ConnectionManager& cmA, app::ConnectionManager& cmB) {
    for (int i = 0; i < 12; ++i) {
        const uint64_t now = 100 + static_cast<uint64_t>(i);
        svcA.pollConnections(now);
        svcB.pollConnections(now);
        cmA.poll(now);
        cmB.poll(now);
    }
}

void pump(app::ConnectionManager& cmA, app::ConnectionManager& cmB, uint64_t base) {
    for (int i = 0; i < 4; ++i) {
        cmA.poll(base + static_cast<uint64_t>(i));
        cmB.poll(base + static_cast<uint64_t>(i));
    }
}

} // namespace

// Full-app e2e with EVERY system boundary mocked in-process (no sockets, no real
// input, no real clipboard): ECDH pairing -> stored PSK -> ConnectionService dial/
// accept over the switchboard -> secure (AES-256-GCM) link -> mesh switch -> forwarded
// keystroke reaches the MockInjector -> clipboard sync reaches the MockClipboard.
// This is the whole product loop the tray wires on Windows, validated headlessly.
void run_e2e_full_system_tests() {
    // --- 1. Pairing (spec 7.1): ECDH numeric-comparison over a loopback transport.
    smtest::LoopbackPair pp;
    pairing::PairingExchange exA(pp.a, "A");
    pairing::PairingExchange exB(pp.b, "B");
    SM_CHECK(exA.start());
    SM_CHECK(exB.start());

    using PStatus = pairing::PairingExchange::Status;
    PStatus sa = PStatus::NeedMore, sb = PStatus::NeedMore;
    for (int i = 0; i < 50 && (sa == PStatus::NeedMore || sb == PStatus::NeedMore); ++i) {
        if (sa == PStatus::NeedMore) sa = exA.poll();
        if (sb == PStatus::NeedMore) sb = exB.poll();
    }
    SM_CHECK(sa == PStatus::Ok);
    SM_CHECK(sb == PStatus::Ok);
    SM_CHECK_EQ(exA.peerId(), std::string("B"));
    SM_CHECK_EQ(exB.peerId(), std::string("A"));
    SM_CHECK_EQ(exA.code(), exB.code());          // human sees matching 6-digit codes
    SM_CHECK(exA.psk() == exB.psk());             // both derive the identical PSK

    pairing::KeyStore keysA, keysB;
    keysA.setPsk("B", exA.psk());
    keysB.setPsk("A", exB.psk());

    // --- 2. Connect (spec 5.1/5.2): dial/accept via the switchboard -> secure link.
    app::MeshNode meshA("A"), meshB("B");
    std::vector<core::PeerId> pr = {"A", "B"};
    for (app::MeshNode* m : {&meshA, &meshB}) {
        m->setPriority(pr);
        m->heartbeatIntervalMs = 0;
        m->heartbeatTimeoutMs = 1000;
    }
    app::ConnectionManager cmA(meshA), cmB(meshB);

    smtest::Switchboard sw;
    auto dial = [&sw](const std::string& h, uint16_t p) { return sw.dial(h, p); };
    auto accept = [&sw](uint16_t p, int t) { return sw.accept(p, t); };

    app::ConnectionService svcA(cmA, keysA, "A", /*listen*/ 47810, dial, accept);
    app::ConnectionService svcB(cmB, keysB, "B", /*listen*/ 47820, dial, accept);
    svcA.setPeers({{"B", "B", 47820}}); // A dials B
    svcB.setPeers({});                   // B only accepts

    pumpConnect(svcA, svcB, cmA, cmB);
    SM_CHECK(cmA.isConnected("B"));
    SM_CHECK(cmB.isConnected("A"));
    SM_CHECK(meshA.isPeerOnline("B"));
    SM_CHECK(meshB.isPeerOnline("A"));

    // --- 3. Switch + input (spec 4/3.2): forwarded events reach the MockInjector.
    smtest::MockInjector injB;
    meshB.onInject = [&injB](const core::InputEvent& e) {
        using MT = core::MessageType;
        switch (static_cast<MT>(e.type)) {
            case MT::MouseMove:   injB.mouseMove(e.dx, e.dy); break;
            case MT::MouseButton: injB.mouseButton(e.code, e.down != 0); break;
            case MT::KeyEvent:    injB.key(e.code, e.down != 0); break;
            default: break;
        }
    };

    meshA.requestSwitchTo("B");
    pump(cmA, cmB, 200);
    SM_CHECK(meshB.isLocalOwner());
    SM_CHECK_EQ(meshA.owner(), std::string("B"));

    meshB.requestSwitchTo("A"); // hand input back to A so A forwards to sink B
    pump(cmB, cmA, 210);
    SM_CHECK(meshA.isLocalOwner());

    meshA.forwardKey(0x4B, true, 1);   // 'K' down
    meshA.forwardKey(0x4B, false, 2);  // 'K' up
    meshA.forwardMouseMove(7, -3, 3);
    meshA.forwardMouseButton(1, true, 4);
    pump(cmA, cmB, 220);

    SM_CHECK_EQ(static_cast<int>(injB.events.size()), 4);
    SM_CHECK(injB.events[0].kind == smtest::InjectedEvent::Kind::Key);
    SM_CHECK_EQ(static_cast<int>(injB.events[0].code), 0x4B);
    SM_CHECK(injB.events[0].down);
    SM_CHECK(!injB.events[1].down);
    SM_CHECK(injB.events[2].kind == smtest::InjectedEvent::Kind::MouseMove);
    SM_CHECK_EQ(injB.events[2].dx, 7);
    SM_CHECK_EQ(injB.events[2].dy, -3);
    SM_CHECK(injB.events[3].kind == smtest::InjectedEvent::Kind::MouseButton);
    SM_CHECK_EQ(static_cast<int>(injB.events[3].code), 1);

    // --- 4. Clipboard sync (spec 8): local change propagates to the MockClipboard.
    smtest::MockClipboard clipB;
    meshB.onRemoteClipboard = [&clipB](const std::string& t) { clipB.set(t); };
    meshA.onLocalClipboardChange("hello from A");
    pump(cmA, cmB, 230);
    SM_CHECK_EQ(clipB.get(), std::string("hello from A"));
}
