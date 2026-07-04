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
    using EscapeFn = std::function<void()>;

    virtual bool start(Sink sink) = 0; // install hooks (become owner)
    virtual void stop() = 0;           // fully uninstall (relinquish ownership)
    virtual bool active() const = 0;

    // True-KVM swallow (spec 3.1/4): when enabled, captured input is CONSUMED on this
    // machine (not also applied locally) so only the driven peer reacts. Default off.
    virtual void setSwallow(bool /*swallow*/) {}

    // Reclaim chord (spec 4/15): the one combo the owner-side hook never swallows or
    // forwards -- pressing it while driving invokes onEscape so the user can always
    // return control to this machine, even though all other input is being consumed.
    virtual void setEscape(uint32_t /*modifiers*/, uint16_t /*key*/, EscapeFn /*onEscape*/) {}
};

// Returns the platform capture (caller owns). Null if unsupported.
Capture* createCapture();

} // namespace sm::platform
