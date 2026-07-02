#include "test_framework.h"

#include "loopback_transport.h"

#include "pairing/pairing_exchange.h"

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

    // A message claiming our own id is rejected (no self-pairing).
    {
        smtest::LoopbackPair link;
        pairing::PairingExchange a(link.a, "SOLO");
        pairing::PairingExchange evil(link.b, "SOLO"); // same id as `a`
        SM_CHECK(a.start());
        SM_CHECK(evil.start());
        pairing::PairingExchange::Status sa = pairing::PairingExchange::Status::NeedMore;
        for (int i = 0; i < 5 && sa == pairing::PairingExchange::Status::NeedMore; ++i)
            sa = a.poll();
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

    // A malformed inbound message (length doesn't match [idlen][id][64-byte point])
    // is rejected cleanly.
    {
        smtest::LoopbackPair link;
        pairing::PairingExchange a(link.a, "A");
        const uint8_t bad[] = {5, 'a', 'b', 'c', 'd', 'e'}; // claims idlen 5, no point
        link.b.send(bad, sizeof(bad));
        SM_CHECK(a.poll() == pairing::PairingExchange::Status::Error);
    }

    // A well-framed message with an INVALID public point fails the ECDH step.
    {
        smtest::LoopbackPair link;
        pairing::PairingExchange a(link.a, "A");
        std::vector<uint8_t> msg;
        const std::string peer = "BADPT";
        msg.push_back(static_cast<uint8_t>(peer.size()));
        msg.insert(msg.end(), peer.begin(), peer.end());
        msg.insert(msg.end(), 64, 0x00); // 64 zero bytes: not a valid EC point
        link.b.send(msg.data(), msg.size());
        SM_CHECK(a.poll() == pairing::PairingExchange::Status::Error);
    }
}
