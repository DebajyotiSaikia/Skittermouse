// macOS input capture via CGEventTap (spec 3.1). Requires Accessibility permission
// (the app must prompt on first run and handle denial/revocation). Installed only
// while this machine owns input. Native Core Graphics.

#include "platform/capture.h"

#import <CoreGraphics/CoreGraphics.h>

namespace sm::platform {

namespace {

class MacCapture : public Capture {
public:
    bool start(Sink sink) override {
        if (tap_) return true;
        sink_ = std::move(sink);
        CGEventMask mask =
            CGEventMaskBit(kCGEventMouseMoved) | CGEventMaskBit(kCGEventLeftMouseDown) |
            CGEventMaskBit(kCGEventLeftMouseUp) | CGEventMaskBit(kCGEventRightMouseDown) |
            CGEventMaskBit(kCGEventRightMouseUp) | CGEventMaskBit(kCGEventOtherMouseDown) |
            CGEventMaskBit(kCGEventOtherMouseUp) | CGEventMaskBit(kCGEventKeyDown) |
            CGEventMaskBit(kCGEventKeyUp);
        tap_ = CGEventTapCreate(kCGHIDEventTap, kCGHeadInsertEventTap,
                                kCGEventTapOptionDefault, mask, &MacCapture::callback, this);
        if (!tap_) return false; // Accessibility permission not granted
        source_ = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, tap_, 0);
        CFRunLoopAddSource(CFRunLoopGetCurrent(), source_, kCFRunLoopCommonModes);
        CGEventTapEnable(tap_, true);
        return true;
    }

    void stop() override {
        if (tap_) {
            CGEventTapEnable(tap_, false);
            if (source_) {
                CFRunLoopRemoveSource(CFRunLoopGetCurrent(), source_, kCFRunLoopCommonModes);
                CFRelease(source_);
                source_ = nullptr;
            }
            CFRelease(tap_);
            tap_ = nullptr;
        }
    }

    bool active() const override { return tap_ != nullptr; }

private:
    static CGEventRef callback(CGEventTapProxy, CGEventType type, CGEventRef e, void* ctx) {
        auto* self = static_cast<MacCapture*>(ctx);
        if (self) self->onEvent(type, e);
        return e; // pass through
    }

    void onEvent(CGEventType type, CGEventRef e) {
        CapturedEvent ev{};
        if (type == kCGEventMouseMoved) {
            ev.kind = CapturedEvent::MouseMove;
            ev.dx = static_cast<int>(CGEventGetIntegerValueField(e, kCGMouseEventDeltaX));
            ev.dy = static_cast<int>(CGEventGetIntegerValueField(e, kCGMouseEventDeltaY));
            if ((ev.dx || ev.dy) && sink_) sink_(ev);
            return;
        }
        if (type == kCGEventKeyDown || type == kCGEventKeyUp) {
            ev.kind = CapturedEvent::Key;
            ev.code = static_cast<uint8_t>(CGEventGetIntegerValueField(e, kCGKeyboardEventKeycode));
            ev.down = (type == kCGEventKeyDown);
            if (sink_) sink_(ev);
            return;
        }
        ev.kind = CapturedEvent::MouseButton;
        switch (type) {
            case kCGEventLeftMouseDown:  ev.code = 1; ev.down = true;  break;
            case kCGEventLeftMouseUp:    ev.code = 1; ev.down = false; break;
            case kCGEventRightMouseDown: ev.code = 2; ev.down = true;  break;
            case kCGEventRightMouseUp:   ev.code = 2; ev.down = false; break;
            case kCGEventOtherMouseDown: ev.code = 3; ev.down = true;  break;
            case kCGEventOtherMouseUp:   ev.code = 3; ev.down = false; break;
            default: return;
        }
        if (sink_) sink_(ev);
    }

    CFMachPortRef tap_ = nullptr;
    CFRunLoopSourceRef source_ = nullptr;
    Sink sink_;
};

} // namespace

Capture* createCapture() { return new MacCapture(); }

} // namespace sm::platform
