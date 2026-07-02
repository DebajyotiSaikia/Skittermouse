#include "test_framework.h"

#include "loopback_transport.h"

#include "app/connection_manager.h"
#include "net/encrypted_transport.h"

#include <array>
#include <deque>
#include <memory>
#include <string>
#include <vector>

using namespace sm;
using sm::net::EncryptedTransport;

namespace {

std::array<uint8_t, 32> linkKey(uint8_t seed) {
    std::array<uint8_t, 32> k{};
    for (std::size_t i = 0; i < k.size(); ++i) k[i] = static_cast<uint8_t>(seed * 17 + i);
    return k;
}

} // namespace

// The real deployment path: the connection manager runs its peer-hello handshake
// and the whole mesh over AES-256-GCM-sealed links. Proves the hello, identity
// verification, switch broadcast and input forwarding all survive encryption end to
// end -- ConnectionManager and EncryptedTransport are unaware of each other.
void run_e2e_secure_connection_tests() {
    // Wire (deques) and the raw loopback endpoints must outlive the managers, so
    // declare them first; the managers own the EncryptedTransport wrappers.
    std::deque<std::vector<uint8_t>> aReads, bReads; // aReads: B->A, bReads: A->B
    auto epA = std::make_unique<smtest::LoopbackEndpoint>();
    epA->inbox_ = &aReads;
    epA->peerInbox_ = &bReads;
    epA->connected_ = true;
    auto epB = std::make_unique<smtest::LoopbackEndpoint>();
    epB->inbox_ = &bReads;
    epB->peerInbox_ = &aReads;
    epB->connected_ = true;

    const auto key = linkKey(0x2A); // one shared PSK for the A-B link

    app::MeshNode meshA("A"), meshB("B");
    std::vector<core::PeerId> pr = {"A", "B"};
    for (app::MeshNode* m : {&meshA, &meshB}) {
        m->setPriority(pr);
        m->heartbeatIntervalMs = 0;
        m->heartbeatTimeoutMs = 1000;
    }
    app::ConnectionManager cmA(meshA), cmB(meshB);

    // A dials out (Initiator), B accepts (Responder); both over the sealed link.
    cmA.addOutgoing("B", std::make_unique<EncryptedTransport>(
                             epA.get(), key, EncryptedTransport::Role::Initiator));
    cmB.addIncoming(std::make_unique<EncryptedTransport>(
        epB.get(), key, EncryptedTransport::Role::Responder));

    // The peer-hello handshake completes through the encryption layer.
    cmA.poll(100);
    cmB.poll(100);
    SM_CHECK(cmA.isConnected("B"));
    SM_CHECK(cmB.isConnected("A"));

    // Liveness + coordinator election over the encrypted links.
    for (int i = 1; i < 4; ++i) {
        cmA.poll(100 + i);
        cmB.poll(100 + i);
    }
    SM_CHECK(meshA.isPeerOnline("B"));
    SM_CHECK_EQ(meshA.primary().value_or(""), std::string("A"));

    // A switch and forwarded keystroke arrive decrypted and intact.
    int bInject = 0;
    core::InputEvent last{};
    meshB.onInject = [&](const core::InputEvent& e) {
        ++bInject;
        last = e;
    };
    meshA.requestSwitchTo("B");
    cmA.poll(200);
    cmB.poll(200);
    SM_CHECK(meshB.isLocalOwner());
    SM_CHECK_EQ(meshA.owner(), std::string("B"));

    // Now B owns input; switch back to A and have A forward to B (the sink).
    meshB.requestSwitchTo("A");
    cmB.poll(210);
    cmA.poll(210);
    SM_CHECK(meshA.isLocalOwner());

    meshA.forwardKey(0x5A, true, 9);
    cmA.poll(220);
    cmB.poll(220);
    SM_CHECK_EQ(bInject, 1);
    SM_CHECK_EQ(static_cast<int>(last.code), 0x5A);
    SM_CHECK_EQ(static_cast<int>(last.down), 1);
}
