// Windows input injection via SendInput (spec 3.2). Runs on the sink side, ready
// to replay whatever the owner forwards. Native Win32, zero third-party.

#include "platform/injector.h"

#include <windows.h>

namespace sm::platform {

namespace {

class WinInjector : public Injector {
public:
    void mouseMove(int dx, int dy) override {
        INPUT in{};
        in.type = INPUT_MOUSE;
        in.mi.dx = dx;
        in.mi.dy = dy;
        in.mi.dwFlags = MOUSEEVENTF_MOVE; // relative
        SendInput(1, &in, sizeof(INPUT));
    }

    void mouseButton(uint8_t button, bool down) override {
        INPUT in{};
        in.type = INPUT_MOUSE;
        switch (button) {
            case 1: in.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP; break;
            case 2: in.mi.dwFlags = down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP; break;
            case 3: in.mi.dwFlags = down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP; break;
            default: return;
        }
        SendInput(1, &in, sizeof(INPUT));
    }

    void key(uint8_t code, bool down) override {
        INPUT in{};
        in.type = INPUT_KEYBOARD;
        in.ki.wVk = code;
        in.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
        SendInput(1, &in, sizeof(INPUT));
    }
};

} // namespace

Injector* createInjector() { return new WinInjector(); }

} // namespace sm::platform
