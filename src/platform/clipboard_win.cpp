// Windows plain-text clipboard (spec 8). CF_UNICODETEXT read/write plus the
// password-manager exclusion check. Native Win32.

#include "platform/clipboard.h"

#include <windows.h>

#include <cstring>
#include <string>
#include <vector>

namespace sm::platform {

namespace {

std::string wideToUtf8(const wchar_t* w) {
    if (!w) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string s(static_cast<std::size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring utf8ToWide(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(static_cast<std::size_t>(n - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

} // namespace

bool getClipboardText(std::string& utf8Out) {
    if (!OpenClipboard(nullptr)) return false;
    bool ok = false;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h) {
        auto* p = static_cast<wchar_t*>(GlobalLock(h));
        if (p) {
            utf8Out = wideToUtf8(p);
            GlobalUnlock(h);
            ok = true;
        }
    }
    CloseClipboard();
    return ok;
}

bool setClipboardText(const std::string& utf8) {
    std::wstring w = utf8ToWide(utf8);
    if (!OpenClipboard(nullptr)) return false;
    EmptyClipboard();
    bool ok = false;
    std::size_t bytes = (w.size() + 1) * sizeof(wchar_t);
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (g) {
        void* d = GlobalLock(g);
        if (d) {
            std::memcpy(d, w.c_str(), bytes);
            GlobalUnlock(g);
            if (SetClipboardData(CF_UNICODETEXT, g)) ok = true;
            else GlobalFree(g);
        } else {
            GlobalFree(g);
        }
    }
    CloseClipboard();
    return ok;
}

bool clipboardExcludedFromMonitoring() {
    UINT fmt = RegisterClipboardFormatW(L"ExcludeClipboardContentFromMonitorProcessing");
    return fmt != 0 && IsClipboardFormatAvailable(fmt) != 0;
}

} // namespace sm::platform
