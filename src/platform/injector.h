#pragma once

// Injection interface (spec 3.2). Runs continuously on every non-owner machine to
// replay forwarded input. Implemented per-OS: Windows SendInput, macOS CGEventPost.

#include <cstdint>

namespace sm::platform {

class Injector {
public:
    virtual ~Injector() = default;

    virtual void mouseMove(int dx, int dy) = 0;              // relative motion
    virtual void mouseButton(uint8_t button, bool down) = 0; // 1=left, 2=right, 3=middle
    virtual void key(uint8_t code, bool down) = 0;           // OS virtual-key / keycode
};

// Returns the platform injector (caller owns). Null if unsupported.
Injector* createInjector();

} // namespace sm::platform
