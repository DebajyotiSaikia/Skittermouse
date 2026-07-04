// Windows input capture via low-level hooks (spec 3.1). WH_MOUSE_LL + WH_KEYBOARD_LL
// are system-wide and need no admin for the hook itself. Installed only while this
// machine owns input; stop() removes them entirely. Native Win32, zero third-party.
//
// NOTE: low-level hook procedures are invoked on the thread that installed them and
// require a running message loop on that thread to be dispatched.

#include "platform/capture.h"

#include "core/hotkey.h" // hotkey_mod flags for the reclaim-chord match

#include <windows.h>

#include <atomic>

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
        ctrl_ = alt_ = shift_ = win_ = false;
        HINSTANCE mod = GetModuleHandleW(nullptr);
        hMouse_ = SetWindowsHookExW(WH_MOUSE_LL, &WinCapture::mouseProc, mod, 0);
        hKey_ = SetWindowsHookExW(WH_KEYBOARD_LL, &WinCapture::keyProc, mod, 0);
        installed_ = (hMouse_ != nullptr && hKey_ != nullptr);
        if (!installed_) { stop(); return false; }
        if (swallow_) anchorCursor(); // freeze the local cursor for relative deltas
        return true;
    }

    void stop() override {
        if (hMouse_) { UnhookWindowsHookEx(hMouse_); hMouse_ = nullptr; }
        if (hKey_) { UnhookWindowsHookEx(hKey_); hKey_ = nullptr; }
        installed_ = false;
        if (g_active == this) g_active = nullptr;
    }

    bool active() const override { return installed_; }

    void setSwallow(bool swallow) override {
        swallow_ = swallow;
        if (installed_ && swallow) anchorCursor();
    }

    void setEscape(uint32_t modifiers, uint16_t key, EscapeFn onEscape) override {
        escMods_ = modifiers;
        escKey_ = key;
        onEscape_ = std::move(onEscape);
    }

private:
    static LRESULT CALLBACK mouseProc(int nCode, WPARAM w, LPARAM l) {
        if (nCode == HC_ACTION && g_active &&
            g_active->onMouse(w, reinterpret_cast<MSLLHOOKSTRUCT*>(l)))
            return 1; // swallow: consume locally so only the driven peer reacts
        return CallNextHookEx(nullptr, nCode, w, l);
    }

    static LRESULT CALLBACK keyProc(int nCode, WPARAM w, LPARAM l) {
        if (nCode == HC_ACTION && g_active &&
            g_active->onKey(w, reinterpret_cast<KBDLLHOOKSTRUCT*>(l)))
            return 1; // swallow
        return CallNextHookEx(nullptr, nCode, w, l);
    }

    // Park the local cursor at the virtual-screen centre. While swallowing, every move
    // is measured as (pt - anchor) and the cursor is snapped back, so the local pointer
    // stays put and relative deltas are correct regardless of OS cursor clamping.
    void anchorCursor() {
        anchor_.x = GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN) / 2;
        anchor_.y = GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN) / 2;
        SetCursorPos(anchor_.x, anchor_.y);
    }

    // Returns true when the event should be swallowed (consumed on this machine).
    bool onMouse(WPARAM w, MSLLHOOKSTRUCT* m) {
        const bool swallow = swallow_.load();
        if (w == WM_MOUSEMOVE) {
            if (swallow) {
                if (m->flags & LLMHF_INJECTED) return true; // our own re-centre; drop
                const int dx = m->pt.x - anchor_.x;
                const int dy = m->pt.y - anchor_.y;
                if (dx || dy) {
                    CapturedEvent e{};
                    e.kind = CapturedEvent::MouseMove;
                    e.dx = dx;
                    e.dy = dy;
                    sink_(e);
                    SetCursorPos(anchor_.x, anchor_.y); // keep the local cursor still
                }
                return true;
            }
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
            return false;
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
            default: return swallow; // wheel / X-buttons: consume locally, not forwarded yet
        }
        sink_(e);
        return swallow;
    }

    // Returns true when the event should be swallowed.
    bool onKey(WPARAM w, KBDLLHOOKSTRUCT* k) {
        const bool down = (w == WM_KEYDOWN || w == WM_SYSKEYDOWN);
        const uint32_t vk = k->vkCode;
        updateModifier(vk, down);

        // Reclaim chord: the one combo that is never forwarded and always swallowed --
        // it returns control to this machine even while every other key is consumed.
        if (down && escKey_ != 0 && vk == escKey_ && modifiersMatch()) {
            if (onEscape_) onEscape_();
            return true;
        }

        CapturedEvent e{};
        e.kind = CapturedEvent::Key;
        e.code = static_cast<uint8_t>(vk);
        e.down = down;
        sink_(e);
        return swallow_.load();
    }

    void updateModifier(uint32_t vk, bool down) {
        switch (vk) {
            case VK_LCONTROL: case VK_RCONTROL: case VK_CONTROL: ctrl_ = down; break;
            case VK_LMENU:    case VK_RMENU:    case VK_MENU:    alt_ = down; break;
            case VK_LSHIFT:   case VK_RSHIFT:   case VK_SHIFT:   shift_ = down; break;
            case VK_LWIN:     case VK_RWIN:                       win_ = down; break;
            default: break;
        }
    }

    bool modifiersMatch() const {
        using namespace sm::core::hotkey_mod;
        const auto need = [&](uint32_t f) { return (escMods_ & f) != 0; };
        return ctrl_ == need(Control) && alt_ == need(Alt) && shift_ == need(Shift) &&
               win_ == need(Win);
    }

    Sink sink_;
    EscapeFn onEscape_;
    HHOOK hMouse_ = nullptr;
    HHOOK hKey_ = nullptr;
    bool installed_ = false;
    std::atomic<bool> swallow_{false};
    uint32_t escMods_ = 0;
    uint16_t escKey_ = 0;
    bool ctrl_ = false, alt_ = false, shift_ = false, win_ = false;
    bool havePt_ = false;
    LONG lastX_ = 0, lastY_ = 0;
    POINT anchor_{};
};

} // namespace

Capture* createCapture() { return new WinCapture(); }

} // namespace sm::platform
