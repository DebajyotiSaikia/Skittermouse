// Windows input capture via low-level hooks (spec 3.1). WH_MOUSE_LL + WH_KEYBOARD_LL
// are system-wide and need no admin for the hook itself. Installed only while this
// machine owns input; stop() removes them entirely. Native Win32, zero third-party.
//
// NOTE: low-level hook procedures are invoked on the thread that installed them and
// require a running message loop on that thread to be dispatched.

#include "platform/capture.h"

#include <windows.h>

namespace sm::platform {

namespace {

class WinCapture;
WinCapture* g_active = nullptr; // low-level hooks are process-global; single owner

class WinCapture : public Capture {
public:
    bool start(Sink sink) override {
        if (installed_) return true;
        sink_ = std::move(sink);
        g_active = this;
        havePt_ = false;
        HINSTANCE mod = GetModuleHandleW(nullptr);
        hMouse_ = SetWindowsHookExW(WH_MOUSE_LL, &WinCapture::mouseProc, mod, 0);
        hKey_ = SetWindowsHookExW(WH_KEYBOARD_LL, &WinCapture::keyProc, mod, 0);
        installed_ = (hMouse_ != nullptr && hKey_ != nullptr);
        if (!installed_) stop();
        return installed_;
    }

    void stop() override {
        if (hMouse_) { UnhookWindowsHookEx(hMouse_); hMouse_ = nullptr; }
        if (hKey_) { UnhookWindowsHookEx(hKey_); hKey_ = nullptr; }
        installed_ = false;
        if (g_active == this) g_active = nullptr;
    }

    bool active() const override { return installed_; }

private:
    static LRESULT CALLBACK mouseProc(int nCode, WPARAM w, LPARAM l) {
        if (nCode == HC_ACTION && g_active)
            g_active->onMouse(w, reinterpret_cast<MSLLHOOKSTRUCT*>(l));
        return CallNextHookEx(nullptr, nCode, w, l);
    }

    static LRESULT CALLBACK keyProc(int nCode, WPARAM w, LPARAM l) {
        if (nCode == HC_ACTION && g_active)
            g_active->onKey(w, reinterpret_cast<KBDLLHOOKSTRUCT*>(l));
        return CallNextHookEx(nullptr, nCode, w, l);
    }

    void onMouse(WPARAM w, MSLLHOOKSTRUCT* m) {
        if (w == WM_MOUSEMOVE) {
            if (havePt_) {
                CapturedEvent e{};
                e.kind = CapturedEvent::MouseMove;
                e.dx = m->pt.x - lastX_;
                e.dy = m->pt.y - lastY_;
                if (e.dx || e.dy) sink_(e);
            }
            lastX_ = m->pt.x;
            lastY_ = m->pt.y;
            havePt_ = true;
            return;
        }
        CapturedEvent e{};
        e.kind = CapturedEvent::MouseButton;
        switch (w) {
            case WM_LBUTTONDOWN: e.code = 1; e.down = true; break;
            case WM_LBUTTONUP:   e.code = 1; e.down = false; break;
            case WM_RBUTTONDOWN: e.code = 2; e.down = true; break;
            case WM_RBUTTONUP:   e.code = 2; e.down = false; break;
            case WM_MBUTTONDOWN: e.code = 3; e.down = true; break;
            case WM_MBUTTONUP:   e.code = 3; e.down = false; break;
            default: return;
        }
        sink_(e);
    }

    void onKey(WPARAM w, KBDLLHOOKSTRUCT* k) {
        CapturedEvent e{};
        e.kind = CapturedEvent::Key;
        e.code = static_cast<uint8_t>(k->vkCode);
        e.down = (w == WM_KEYDOWN || w == WM_SYSKEYDOWN);
        sink_(e);
    }

    Sink sink_;
    HHOOK hMouse_ = nullptr;
    HHOOK hKey_ = nullptr;
    bool installed_ = false;
    bool havePt_ = false;
    LONG lastX_ = 0, lastY_ = 0;
};

} // namespace

Capture* createCapture() { return new WinCapture(); }

} // namespace sm::platform
