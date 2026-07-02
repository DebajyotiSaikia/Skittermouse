// Headless network-validation harness (spec 17, milestone: "two real machines
// forwarding encrypted input end-to-end"). NOT part of the shipped product -- it is
// the driver for the two-container integration test under tests/docker/. It runs the
// exact product logic (secure_link -> ConnectionManager -> MeshNode -> EncryptedTransport)
// over a REAL TCP/WebSocket socket between two isolated machines.
//
//   listener:  netcheck --self B --listen 9000 --peer A --psk <hex64> --role listener
//   dialer:    netcheck --self A --listen 9001 --connect B_HOST:9000 --peer B \
//                       --psk <hex64> --role dialer
//
// The dialer connects, becomes the input owner, and forwards keystrokes; the listener
// injects them and prints "RESULT PASS" on the first forwarded key it receives.

#include "app/connection_manager.h"
#include "app/mesh_node.h"
#include "app/secure_link.h"
#include "core/event_types.h"
#include "net/ws_transport.h"
#include "pairing/key_store.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace sm;

namespace {

uint64_t nowMs() {
    using namespace std::chrono;
    static const auto t0 = steady_clock::now();
    return static_cast<uint64_t>(duration_cast<milliseconds>(steady_clock::now() - t0).count());
}

bool parseHexPsk(const std::string& hex, sm::pairing::Psk& out) {
    if (hex.size() != 64) return false;
    auto nyb = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < 32; ++i) {
        int hi = nyb(hex[2 * i]), lo = nyb(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return true;
}

std::string argVal(int argc, char** argv, const std::string& flag) {
    for (int i = 1; i + 1 < argc; ++i)
        if (flag == argv[i]) return argv[i + 1];
    return {};
}

} // namespace

int main(int argc, char** argv) {
    const std::string self = argVal(argc, argv, "--self");
    const std::string peer = argVal(argc, argv, "--peer");
    const std::string role = argVal(argc, argv, "--role");
    const std::string connect = argVal(argc, argv, "--connect");
    const std::string pskHex = argVal(argc, argv, "--psk");
    const std::string listenStr = argVal(argc, argv, "--listen");
    const std::string secStr = argVal(argc, argv, "--seconds");

    if (self.empty() || peer.empty() || pskHex.empty() || listenStr.empty()) {
        std::fprintf(stderr, "usage: netcheck --self ID --peer ID --listen PORT --psk HEX "
                             "--role dialer|listener [--connect HOST:PORT] [--seconds N]\n");
        return 2;
    }
    const uint16_t listenPort = static_cast<uint16_t>(std::stoi(listenStr));
    const int seconds = secStr.empty() ? 30 : std::stoi(secStr);

    sm::pairing::Psk psk{};
    if (!parseHexPsk(pskHex, psk)) {
        std::fprintf(stderr, "bad --psk (need 64 hex chars)\n");
        return 2;
    }

    // Both machines already share this PSK from pairing; seed it under the peer's id.
    sm::pairing::KeyStore keys;
    keys.setPsk(peer, psk);

    app::MeshNode mesh(self);
    mesh.setPriority({self, peer});
    mesh.heartbeatIntervalMs = 200;
    mesh.heartbeatTimeoutMs = 3000;

    bool injected = false;
    mesh.onInject = [&](const core::InputEvent& e) {
        std::printf("EVENT inject code=%d down=%d\n", static_cast<int>(e.code),
                    static_cast<int>(e.down));
        std::fflush(stdout);
        injected = true;
    };
    mesh.onOwnerChanged = [&](const core::PeerId& o) {
        std::printf("EVENT owner=%s\n", o.c_str());
        std::fflush(stdout);
    };

    app::ConnectionManager cm(mesh);
    cm.onPeerConnected = [&](const core::PeerId& id) {
        std::printf("EVENT connected peer=%s\n", id.c_str());
        std::fflush(stdout);
    };

    // Dialer: connect out (retry until the listener is up), then secure the link.
    bool dialed = false;
    std::unique_ptr<app::InboundHandshake> pending; // listener's in-flight handshake

    const uint64_t deadline = nowMs() + static_cast<uint64_t>(seconds) * 1000;
    bool switched = false;
    int keysSent = 0;
    uint64_t lastKey = 0;

    std::printf("EVENT start self=%s role=%s\n", self.c_str(), role.c_str());
    std::fflush(stdout);

    while (nowMs() < deadline) {
        const uint64_t now = nowMs();

        if (role == "dialer" && !dialed && !connect.empty()) {
            std::string host = connect;
            uint16_t port = 0;
            auto colon = connect.rfind(':');
            if (colon != std::string::npos) {
                host = connect.substr(0, colon);
                port = static_cast<uint16_t>(std::stoi(connect.substr(colon + 1)));
            }
            std::unique_ptr<net::Transport> raw(net::createWsClientTransport());
            if (raw->connect(host, port)) {
                app::SecureLink link = app::secureOutbound(std::move(raw), keys, self, peer);
                if (link.transport) {
                    cm.addOutgoing(peer, std::move(link.transport));
                    dialed = true;
                    std::printf("EVENT dialed %s:%d\n", host.c_str(), port);
                    std::fflush(stdout);
                }
            }
        }

        if (role == "listener") {
            if (!pending) {
                net::Transport* t = net::wsAcceptOne(listenPort, 200);
                if (t) {
                    pending = std::make_unique<app::InboundHandshake>(
                        std::unique_ptr<net::Transport>(t), keys, self);
                }
            }
            if (pending) {
                auto st = pending->poll();
                if (st == app::InboundHandshake::Status::Ok) {
                    app::SecureLink link = pending->take();
                    std::printf("EVENT accepted peer=%s\n", link.peerId.c_str());
                    std::fflush(stdout);
                    cm.addIncoming(std::move(link.transport));
                    pending.reset();
                } else if (st != app::InboundHandshake::Status::NeedMore) {
                    std::printf("EVENT handshake-rejected status=%d\n", static_cast<int>(st));
                    std::fflush(stdout);
                    pending.reset();
                }
            }
        }

        cm.poll(now);

        // Dialer takes ownership and streams keystrokes once the peer is live.
        if (role == "dialer" && cm.isConnected(peer)) {
            if (!switched) {
                mesh.requestSwitchTo(self); // become the input owner
                switched = true;
            }
            if (mesh.isLocalOwner() && keysSent < 20 && now - lastKey > 250) {
                mesh.forwardKey(0x41, true, static_cast<uint32_t>(now));  // 'A' down
                mesh.forwardKey(0x41, false, static_cast<uint32_t>(now)); // 'A' up
                ++keysSent;
                lastKey = now;
            }
        }

        if (injected) {
            std::printf("RESULT PASS (%s injected forwarded input over real TCP)\n",
                        self.c_str());
            std::fflush(stdout);
            return 0;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // The dialer's job is to deliver input; if the peer connected and it sent keys,
    // that side succeeded (the listener is the one that asserts injection).
    if (role == "dialer" && cm.isConnected(peer) && keysSent > 0) {
        std::printf("RESULT PASS (%s connected + forwarded %d keys)\n", self.c_str(), keysSent);
        std::fflush(stdout);
        return 0;
    }
    std::printf("RESULT FAIL (%s timed out)\n", self.c_str());
    std::fflush(stdout);
    return 1;
}
