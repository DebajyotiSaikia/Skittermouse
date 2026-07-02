// Headless PAIRING-validation harness (spec 7.1). NOT shipped. Drives the numeric-
// comparison pairing flow between two containers (tests/docker/) over a real TCP/
// WebSocket socket: each side generates an ephemeral P-256 keypair, exchanges public
// points, computes the ECDH shared secret, and derives the 6-digit verification code
// + the long-term PSK. If both containers print the SAME code and PSK, pairing works
// end-to-end (a MITM would land on different secrets, so the codes would differ).
//
//   listener:  netpair --self B --peer A --listen 9000
//   dialer:    netpair --self A --peer B --listen 9001 --connect B_HOST:9000

#include "net/ws_transport.h"
#include "pairing/ecdh_handshake.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>

using namespace sm;

namespace {

uint64_t nowMs() {
    using namespace std::chrono;
    static const auto t0 = steady_clock::now();
    return static_cast<uint64_t>(duration_cast<milliseconds>(steady_clock::now() - t0).count());
}

std::string argVal(int argc, char** argv, const std::string& flag) {
    for (int i = 1; i + 1 < argc; ++i)
        if (flag == argv[i]) return argv[i + 1];
    return {};
}

std::string toHex(const std::array<uint8_t, 32>& b) {
    static const char* h = "0123456789abcdef";
    std::string s(64, '0');
    for (size_t i = 0; i < 32; ++i) {
        s[2 * i] = h[b[i] >> 4];
        s[2 * i + 1] = h[b[i] & 0xF];
    }
    return s;
}

} // namespace

int main(int argc, char** argv) {
    const std::string self = argVal(argc, argv, "--self");
    const std::string peer = argVal(argc, argv, "--peer");
    const std::string listenStr = argVal(argc, argv, "--listen");
    const std::string connect = argVal(argc, argv, "--connect");
    const std::string secStr = argVal(argc, argv, "--seconds");
    if (self.empty() || peer.empty() || listenStr.empty()) {
        std::fprintf(stderr, "usage: netpair --self ID --peer ID --listen PORT "
                             "[--connect HOST:PORT] [--seconds N]\n");
        return 2;
    }
    const uint16_t listenPort = static_cast<uint16_t>(std::stoi(listenStr));
    const uint64_t deadline = nowMs() + (secStr.empty() ? 30 : std::stoi(secStr)) * 1000ull;

    sm::pairing::EcdhHandshake hs;
    if (!hs.begin()) {
        std::printf("RESULT FAIL (keygen)\n");
        return 1;
    }

    // Establish a raw transport: the dialer connects (retrying until the listener is
    // up), the listener accepts. Public keys are not secret, so this exchange is in
    // the clear -- the security comes from the human comparing the derived codes.
    std::unique_ptr<net::Transport> t;
    while (!t && nowMs() < deadline) {
        if (!connect.empty()) {
            std::string host = connect;
            uint16_t port = 0;
            auto colon = connect.rfind(':');
            if (colon != std::string::npos) {
                host = connect.substr(0, colon);
                port = static_cast<uint16_t>(std::stoi(connect.substr(colon + 1)));
            }
            std::unique_ptr<net::Transport> raw(net::createWsClientTransport());
            if (raw->connect(host, port)) t = std::move(raw);
        } else {
            t.reset(net::wsAcceptOne(listenPort, 300));
        }
        if (!t) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (!t) {
        std::printf("RESULT FAIL (no connection)\n");
        return 1;
    }

    // Exchange public points (each is a single 64-byte WS message).
    const auto& myPub = hs.publicPoint();
    t->send(myPub.data(), myPub.size());

    sm::crypto::Bytes peerPub;
    uint8_t buf[256];
    while (peerPub.size() != sm::crypto::kEcPointLen && nowMs() < deadline) {
        int n = t->recv(buf, sizeof(buf));
        if (n < 0) {
            std::printf("RESULT FAIL (link dropped)\n");
            return 1;
        }
        if (n == static_cast<int>(sm::crypto::kEcPointLen)) {
            peerPub.assign(buf, buf + n);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (peerPub.size() != sm::crypto::kEcPointLen) {
        std::printf("RESULT FAIL (no peer key)\n");
        return 1;
    }

    if (!hs.computeShared(peerPub)) {
        std::printf("RESULT FAIL (ecdh)\n");
        return 1;
    }
    std::array<uint8_t, 32> psk{};
    if (!hs.derivePsk(self, peer, psk)) {
        std::printf("RESULT FAIL (hkdf)\n");
        return 1;
    }

    // Both machines derive these from the SAME shared secret, so a genuine pairing
    // yields identical output on both sides (the test asserts they match).
    std::printf("PAIR self=%s CODE=%s PSK=%s\n", self.c_str(),
                hs.verificationCode().c_str(), toHex(psk).c_str());
    std::fflush(stdout);
    return 0;
}
