#pragma once

// Capture interface (spec 3.1). Installed ONLY while this machine is the input
// owner -- the hook is not merely ignored when inactive, it is not installed at
// all, so a non-owner has zero capture cost and no risk of double-handling.
// Implemented per-OS: Windows low-level hooks, macOS CGEventTap.

#include <cstdint>
#include <functional>

namespace sm::platform {

struct CapturedEvent {
    enum Kind { MouseMove, MouseButton, Key } kind;
    int dx = 0, dy = 0;   // MouseMove: relative delta
    uint8_t code = 0;     // MouseButton: 1/2/3; Key: OS keycode
    bool down = false;    // press vs release
};

class Capture {
public:
    virtual ~Capture() = default;
    using Sink = std::function<void(const CapturedEvent&)>;

    virtual bool start(Sink sink) = 0; // install hooks (become owner)
    virtual void stop() = 0;           // fully uninstall (relinquish ownership)
    virtual bool active() const = 0;
};

// Returns the platform capture (caller owns). Null if unsupported.
Capture* createCapture();

} // namespace sm::platform
