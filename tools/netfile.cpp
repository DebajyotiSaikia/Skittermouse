// Headless FILE-TRANSFER-validation harness (spec 9). NOT shipped. Drives the (done,
// unit-tested) net/FileChannel over a real TCP/WebSocket socket between two containers
// (tests/docker/): the sender advertises FilePromiseMeta and streams FileChunks; the
// receiver reassembles by offset and verifies the bytes. This validates the file
// transfer wire path end-to-end (the OS delay-render UI -- Explorer/Finder -- is the
// only part that still needs a real desktop).
//
//   receiver:  netfile --role receiver --listen 9000 --size 500000
//   sender:    netfile --role sender   --listen 9001 --connect R_HOST:9000 --size 500000

#include "net/file_channel.h"
#include "net/file_session.h"
#include "net/ws_transport.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
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

std::string argVal(int argc, char** argv, const std::string& flag) {
    for (int i = 1; i + 1 < argc; ++i)
        if (flag == argv[i]) return argv[i + 1];
    return {};
}

// Deterministic payload both sides agree on, so the receiver can verify byte-for-byte.
net::Bytes makeBlob(std::size_t n) {
    net::Bytes b(n);
    for (std::size_t i = 0; i < n; ++i) b[i] = static_cast<uint8_t>((i * 31u + 7u) & 0xFF);
    return b;
}

} // namespace

int main(int argc, char** argv) {
    const std::string role = argVal(argc, argv, "--role");
    const std::string listenStr = argVal(argc, argv, "--listen");
    const std::string connect = argVal(argc, argv, "--connect");
    const std::string sizeStr = argVal(argc, argv, "--size");
    const std::string secStr = argVal(argc, argv, "--seconds");
    if (role.empty() || listenStr.empty() || sizeStr.empty()) {
        std::fprintf(stderr, "usage: netfile --role sender|receiver --listen PORT --size N "
                             "[--connect HOST:PORT] [--seconds N]\n");
        return 2;
    }
    const uint16_t listenPort = static_cast<uint16_t>(std::stoi(listenStr));
    const std::size_t size = static_cast<std::size_t>(std::stoull(sizeStr));
    const uint64_t deadline = nowMs() + (secStr.empty() ? 40 : std::stoi(secStr)) * 1000ull;
    const net::Bytes expected = makeBlob(size);

    // Establish a raw transport: sender connects (retrying), receiver accepts.
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

    if (role == "sender") {
        net::FileSender snd;
        snd.addFile("payload.bin", expected);
        const bool ok = net::FileChannel::sendAll(*t, snd, 4096);
        std::printf("SENT bytes=%zu ok=%d\n", size, ok ? 1 : 0);
        std::fflush(stdout);
        // Stay alive so TCP flushes and the receiver drains before we exit.
        while (nowMs() < deadline) {
            uint8_t b[256];
            if (t->recv(b, sizeof(b)) < 0) break; // peer closed
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        return ok ? 0 : 1;
    }

    // Receiver: reassemble and verify byte-for-byte.
    net::FileReceiver rcv;
    while (nowMs() < deadline) {
        const bool done = net::FileChannel::receiveAvailable(*t, rcv);
        if (done && rcv.fileCount() > 0) {
            const net::Bytes& got = rcv.data(0);
            bool match = got.size() == expected.size();
            for (std::size_t i = 0; match && i < got.size(); ++i)
                if (got[i] != expected[i]) match = false;
            std::printf("RESULT %s recv=%zu name=%s\n", match ? "PASS" : "FAIL", got.size(),
                        rcv.entry(0).name.c_str());
            std::fflush(stdout);
            return match ? 0 : 1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::printf("RESULT FAIL (timeout, %zu of %zu files complete)\n", rcv.fileCount(),
                static_cast<std::size_t>(1));
    return 1;
}
