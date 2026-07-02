// macOS input injection via CGEventPost (spec 3.2). Native Core Graphics.

#include "platform/injector.h"

#import <CoreGraphics/CoreGraphics.h>

namespace sm::platform {

namespace {

CGPoint currentMouse() {
    CGEventRef cur = CGEventCreate(nullptr);
    CGPoint p = CGEventGetLocation(cur);
    CFRelease(cur);
    return p;
}

class MacInjector : public Injector {
public:
    void mouseMove(int dx, int dy) override {
        CGPoint p = currentMouse();
        p.x += dx;
        p.y += dy;
        CGEventRef e = CGEventCreateMouseEvent(nullptr, kCGEventMouseMoved, p, kCGMouseButtonLeft);
        CGEventPost(kCGHIDEventTap, e);
        CFRelease(e);
    }

    void mouseButton(uint8_t button, bool down) override {
        CGEventType type;
        CGMouseButton mb;
        switch (button) {
            case 1: mb = kCGMouseButtonLeft;   type = down ? kCGEventLeftMouseDown  : kCGEventLeftMouseUp;  break;
            case 2: mb = kCGMouseButtonRight;  type = down ? kCGEventRightMouseDown : kCGEventRightMouseUp; break;
            case 3: mb = kCGMouseButtonCenter; type = down ? kCGEventOtherMouseDown : kCGEventOtherMouseUp; break;
            default: return;
        }
        CGEventRef e = CGEventCreateMouseEvent(nullptr, type, currentMouse(), mb);
        CGEventPost(kCGHIDEventTap, e);
        CFRelease(e);
    }

    void key(uint8_t code, bool down) override {
        CGEventRef e = CGEventCreateKeyboardEvent(nullptr, static_cast<CGKeyCode>(code), down);
        CGEventPost(kCGHIDEventTap, e);
        CFRelease(e);
    }
};

} // namespace

Injector* createInjector() { return new MacInjector(); }

} // namespace sm::platform
