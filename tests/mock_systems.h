#pragma once

// Mocked system boundaries for the full-app e2e set (tests only -- never linked into
// the product exe). Everything the app touches at the OS edge has an in-memory stand-in
// so the WHOLE stack (pairing -> secure link -> mesh switch -> input inject -> clipboard)
// can run headless in one process, with no sockets, no real input, no real clipboard.
//
//   - MockInjector   : records injected input instead of calling SendInput/CGEventPost.
//   - MockClipboard  : an in-memory clipboard (get/set text).
//   - Switchboard    : an in-process replacement for the dial/accept socket factories
//                      that ConnectionService injects, wiring a dialer to an acceptor
//                      through a bidirectional in-memory Wire.

#include "net/transport.h"
#include "platform/injector.h"

#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace smtest {

// --- Mock injector (spec 3.2 boundary) -------------------------------------------
struct InjectedEvent {
    enum class Kind { MouseMove, MouseButton, Key } kind;
    int dx = 0, dy = 0;
    uint8_t code = 0;
    bool down = false;
};

class MockInjector : public sm::platform::Injector {
public:
    void mouseMove(int dx, int dy) override {
        events.push_back({InjectedEvent::Kind::MouseMove, dx, dy, 0, false});
    }
    void mouseButton(uint8_t button, bool down) override {
        events.push_back({InjectedEvent::Kind::MouseButton, 0, 0, button, down});
    }
    void key(uint8_t code, bool down) override {
        events.push_back({InjectedEvent::Kind::Key, 0, 0, code, down});
    }
    std::vector<InjectedEvent> events;
};

// --- Mock clipboard (spec 8 boundary) --------------------------------------------
class MockClipboard {
public:
    void set(const std::string& t) { text_ = t; }
    const std::string& get() const { return text_; }

private:
    std::string text_;
};

// --- In-process network switchboard (spec 5.5 boundary) --------------------------
// A bidirectional in-memory pipe. Each side sends into one queue and reads the other.
struct Wire {
    std::deque<std::vector<uint8_t>> aToB, bToA;
};

class MockNetTransport : public sm::net::Transport {
public:
    MockNetTransport(std::shared_ptr<Wire> w, bool sideA) : w_(std::move(w)), sideA_(sideA) {}

    bool connect(const std::string&, uint16_t) override {
        connected_ = true;
        return true;
    }
    bool isConnected() const override { return connected_; }

    bool send(const uint8_t* data, std::size_t len) override {
        if (!connected_) return false;
        (sideA_ ? w_->aToB : w_->bToA).emplace_back(data, data + len);
        return true;
    }
    int recv(uint8_t* buf, std::size_t cap) override {
        auto& in = sideA_ ? w_->bToA : w_->aToB;
        if (in.empty()) return 0;
        std::vector<uint8_t>& m = in.front();
        std::size_t n = m.size() <= cap ? m.size() : cap;
        std::memcpy(buf, m.data(), n);
        in.pop_front();
        return static_cast<int>(n);
    }
    void close() override { connected_ = false; }

private:
    std::shared_ptr<Wire> w_;
    bool sideA_;
    bool connected_ = true;
};

// dial(host, port) creates a fresh Wire, returns the CLIENT end, and parks the SERVER
// end under `port` so a later accept(port) hands it back -- exactly the contract the
// product's createWsClientTransport / wsAcceptOne satisfy over real sockets.
class Switchboard {
public:
    std::unique_ptr<sm::net::Transport> dial(const std::string&, uint16_t port) {
        auto w = std::make_shared<Wire>();
        pending_[port].push_back(std::make_unique<MockNetTransport>(w, /*sideA=*/false));
        return std::make_unique<MockNetTransport>(w, /*sideA=*/true);
    }
    std::unique_ptr<sm::net::Transport> accept(uint16_t port, int /*timeoutMs*/) {
        auto& q = pending_[port];
        if (q.empty()) return nullptr;
        std::unique_ptr<sm::net::Transport> t = std::move(q.front());
        q.pop_front();
        return t;
    }

private:
    std::map<uint16_t, std::deque<std::unique_ptr<sm::net::Transport>>> pending_;
};

} // namespace smtest
