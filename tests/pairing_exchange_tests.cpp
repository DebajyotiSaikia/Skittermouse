#include "test_framework.h"

#include "loopback_transport.h"

#include "crypto/crypto.h"
#include "pairing/pairing_exchange.h"

#include <cstdint>
#include <string>
#include <vector>

using namespace sm;

void run_pairing_exchange_tests() {
    // Two peers exchange id + ECDH public point over a loopback transport and must
    // independently derive the SAME 6-digit code and PSK (the numeric-comparison
    // security property), each having learned the other's id.
    {
        smtest::LoopbackPair link;
        pairing::PairingExchange a(link.a, "MACHINE-A");
        pairing::PairingExchange b(link.b, "MACHINE-B");

        SM_CHECK(a.start());
        SM_CHECK(b.start());

        // Pump both until each has consumed the other's message.
        pairing::PairingExchange::Status sa = pairing::PairingExchange::Status::NeedMore;
        pairing::PairingExchange::Status sb = pairing::PairingExchange::Status::NeedMore;
        for (int i = 0; i < 5 && (sa == pairing::PairingExchange::Status::NeedMore ||
                                  sb == pairing::PairingExchange::Status::NeedMore);
             ++i) {
            sa = a.poll();
            sb = b.poll();
        }

        SM_CHECK(sa == pairing::PairingExchange::Status::Ok);
        SM_CHECK(sb == pairing::PairingExchange::Status::Ok);
        SM_CHECK_EQ(a.peerId(), std::string("MACHINE-B"));
        SM_CHECK_EQ(b.peerId(), std::string("MACHINE-A"));
        SM_CHECK_EQ(a.code(), b.code());     // humans compare these -> must match
        SM_CHECK(a.code().size() == 6);
        SM_CHECK(a.psk() == b.psk());        // same long-term key on both sides
    }

    // A reveal claiming our own id is rejected (no self-pairing). Both sides must be
    // pumped so the round-2 reveal (which carries the id) is actually exchanged.
    {
        smtest::LoopbackPair link;
        pairing::PairingExchange a(link.a, "SOLO");
        pairing::PairingExchange evil(link.b, "SOLO"); // same id as `a`
        SM_CHECK(a.start());
        SM_CHECK(evil.start());
        pairing::PairingExchange::Status sa = pairing::PairingExchange::Status::NeedMore;
        for (int i = 0; i < 5 && sa == pairing::PairingExchange::Status::NeedMore; ++i) {
            sa = a.poll();
            evil.poll(); // pump so evil sends its reveal (id = SOLO)
        }
        SM_CHECK(sa == pairing::PairingExchange::Status::Error);
    }

    // Transport error (recv < 0, e.g. the peer dropped) -> Error, not a hang.
    {
        struct ErrTransport : sm::net::Transport {
            bool connect(const std::string&, uint16_t) override { return true; }
            bool isConnected() const override { return true; }
            bool send(const uint8_t*, std::size_t) override { return true; }
            int recv(uint8_t*, std::size_t) override { return -1; }
            void close() override {}
        } err;
        pairing::PairingExchange e(err, "X");
        SM_CHECK(e.poll() == pairing::PairingExchange::Status::Error);
    }

    // A first message that isn't exactly a 32-byte commitment is rejected cleanly.
    {
        smtest::LoopbackPair link;
        pairing::PairingExchange a(link.a, "A");
        const uint8_t bad[] = {5, 'a', 'b', 'c', 'd', 'e'}; // 6 bytes, not a commitment
        link.b.send(bad, sizeof(bad));
        SM_CHECK(a.poll() == pairing::PairingExchange::Status::Error);
    }

    // Helpers to hand-craft a peer's round-1 commitment and round-2 reveal.
    auto makeCommit = [](const std::vector<uint8_t>& pub, const std::vector<uint8_t>& nonce,
                         const std::string& id) {
        std::vector<uint8_t> c;
        c.insert(c.end(), pub.begin(), pub.end());
        c.insert(c.end(), nonce.begin(), nonce.end());
        c.insert(c.end(), id.begin(), id.end());
        return sm::crypto::sha256(c.data(), c.size());
    };
    auto makeReveal = [](const std::vector<uint8_t>& pub, const std::vector<uint8_t>& nonce,
                         const std::string& id) {
        std::vector<uint8_t> m;
        m.push_back(static_cast<uint8_t>(id.size()));
        m.insert(m.end(), id.begin(), id.end());
        m.insert(m.end(), pub.begin(), pub.end());
        m.insert(m.end(), nonce.begin(), nonce.end());
        return m;
    };

    // A reveal that MATCHES its commitment but carries an invalid EC point still fails
    // at the ECDH step (commitment gate passed, key agreement did not).
    {
        smtest::LoopbackPair link;
        pairing::PairingExchange a(link.a, "A");
        SM_CHECK(a.start());
        const std::vector<uint8_t> badPub(64, 0x00); // not a valid P-256 point
        const std::vector<uint8_t> nonce(32, 0x11);
        const std::string peer = "BADPT";
        auto commit = makeCommit(badPub, nonce, peer);
        link.b.send(commit.data(), commit.size());
        SM_CHECK(a.poll() == pairing::PairingExchange::Status::NeedMore); // stored commit, sent reveal
        auto reveal = makeReveal(badPub, nonce, peer);
        link.b.send(reveal.data(), reveal.size());
        SM_CHECK(a.poll() == pairing::PairingExchange::Status::Error); // ECDH fails
    }

    // Anti-grinding gate: a reveal whose (pub,nonce,id) does NOT hash to the round-1
    // commitment is rejected -- this is what stops a MITM grinding the 6-digit code.
    {
        smtest::LoopbackPair link;
        pairing::PairingExchange a(link.a, "A");
        SM_CHECK(a.start());
        const std::vector<uint8_t> pub(64, 0x02);
        const std::vector<uint8_t> nonce1(32, 0x11), nonce2(32, 0x22);
        const std::string peer = "EVIL";
        auto commit = makeCommit(pub, nonce1, peer); // commit to nonce1...
        link.b.send(commit.data(), commit.size());
        SM_CHECK(a.poll() == pairing::PairingExchange::Status::NeedMore);
        auto reveal = makeReveal(pub, nonce2, peer); // ...but reveal nonce2
        link.b.send(reveal.data(), reveal.size());
        SM_CHECK(a.poll() == pairing::PairingExchange::Status::Error); // commitment mismatch
    }
}
