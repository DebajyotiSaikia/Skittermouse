#include "test_framework.h"

#include "loopback_transport.h"

#include "net/file_channel.h"
#include "net/file_session.h"

#include <cstdint>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

using namespace sm;

namespace {

net::Bytes bytesOf(const std::string& s) {
    return net::Bytes(s.begin(), s.end());
}

} // namespace

void run_file_channel_tests() {
    // --- Multi-file transfer over a loopback file channel --------------------
    {
        net::FileSender sender;
        sender.addFile("hello.txt", bytesOf("hello world"));
        net::Bytes big(5000);
        for (std::size_t i = 0; i < big.size(); ++i) big[i] = static_cast<uint8_t>(i * 13 + 1);
        sender.addFile("blob.bin", big);
        sender.addFile("empty.dat", {}); // zero-byte file must transfer cleanly

        smtest::LoopbackPair pair;
        // Small chunk size to force many FileChunk messages (order preserved).
        SM_CHECK(net::FileChannel::sendAll(pair.a, sender, 64));

        net::FileReceiver receiver;
        bool done = net::FileChannel::receiveAvailable(pair.b, receiver);
        SM_CHECK(done);
        SM_CHECK_EQ(3, static_cast<int>(receiver.fileCount()));
        SM_CHECK(receiver.allComplete());

        SM_CHECK_EQ(receiver.entry(0).name, std::string("hello.txt"));
        SM_CHECK(receiver.data(0) == bytesOf("hello world"));
        SM_CHECK_EQ(receiver.entry(1).name, std::string("blob.bin"));
        SM_CHECK(receiver.data(1) == big);
        SM_CHECK_EQ(receiver.entry(2).name, std::string("empty.dat"));
        SM_CHECK(receiver.data(2).empty());
        SM_CHECK(receiver.complete(2));
    }

    // --- Partial delivery: receiver reports incomplete until the rest arrives -
    {
        net::FileSender sender;
        net::Bytes payload(300);
        for (std::size_t i = 0; i < payload.size(); ++i)
            payload[i] = static_cast<uint8_t>(255 - (i % 256));
        sender.addFile("part.bin", payload);

        // Drive the sender one message at a time into a shared queue so we can feed
        // the receiver only some of the chunks first.
        std::deque<std::vector<uint8_t>> wire;
        struct QueueTap : sm::net::Transport {
            std::deque<std::vector<uint8_t>>* q = nullptr;
            bool connect(const std::string&, uint16_t) override { return true; }
            bool isConnected() const override { return true; }
            bool send(const uint8_t* d, std::size_t n) override {
                q->emplace_back(d, d + n);
                return true;
            }
            int recv(uint8_t*, std::size_t) override { return 0; }
            void close() override {}
        };
        QueueTap outTap;
        outTap.q = &wire;
        SM_CHECK(net::FileChannel::sendAll(outTap, sender, 32)); // meta + many chunks

        // Feed only the first two messages (meta + first chunk) to the receiver.
        struct DrainTap : sm::net::Transport {
            std::deque<std::vector<uint8_t>>* q = nullptr;
            bool connect(const std::string&, uint16_t) override { return true; }
            bool isConnected() const override { return true; }
            bool send(const uint8_t*, std::size_t) override { return true; }
            int recv(uint8_t* b, std::size_t cap) override {
                if (!q || q->empty()) return 0;
                auto& m = q->front();
                std::size_t k = m.size() <= cap ? m.size() : cap;
                std::memcpy(b, m.data(), k);
                q->pop_front();
                return static_cast<int>(k);
            }
            void close() override {}
        };
        std::deque<std::vector<uint8_t>> firstTwo;
        firstTwo.push_back(wire[0]);
        firstTwo.push_back(wire[1]);
        DrainTap drain;
        drain.q = &firstTwo;

        net::FileReceiver receiver;
        bool done = net::FileChannel::receiveAvailable(drain, receiver);
        SM_CHECK(!done); // not all bytes yet
        SM_CHECK_EQ(1, static_cast<int>(receiver.fileCount()));
        SM_CHECK(!receiver.complete(0));

        // Now feed the remaining messages -> completes.
        std::deque<std::vector<uint8_t>> rest(wire.begin() + 2, wire.end());
        drain.q = &rest;
        done = net::FileChannel::receiveAvailable(drain, receiver);
        SM_CHECK(done);
        SM_CHECK(receiver.data(0) == payload);
    }
}
