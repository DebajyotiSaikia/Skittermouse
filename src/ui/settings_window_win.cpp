// Windows native settings window (spec 10/16). A small modal Win32 window with real
// controls -- a hotkey edit field and checkboxes for the boolean settings -- built
// programmatically (no dialog resource). On Save it writes the values back into the
// Config. Native Win32, zero third-party.

#include "ui/settings_window.h"

#include <windows.h>

#include <string>

namespace sm::ui {

namespace {

constexpr int kIdHotkey = 1001;
constexpr int kIdBroadcast = 1002;
constexpr int kIdLock = 1003;
constexpr int kIdStartup = 1004;
constexpr int kIdSave = 1005;
constexpr int kIdCancel = 1006;

struct SettingsState {
    sm::core::Config* config = nullptr;
    HWND hotkey = nullptr;
    HWND broadcast = nullptr;
    HWND lock = nullptr;
    HWND startup = nullptr;
    bool saved = false;
    bool done = false;
};

SettingsState* g_ss = nullptr;

std::wstring toW(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? static_cast<std::size_t>(n - 1) : 0, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

std::string fromW(const std::wstring& w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? static_cast<std::size_t>(n - 1) : 0, '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
    return s;
}

void setCheck(HWND h, bool on) {
    SendMessageW(h, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
}
bool getCheck(HWND h) { return SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED; }

void applyAndClose(HWND hwnd, bool save) {
    if (save && g_ss && g_ss->config) {
        wchar_t buf[256];
        GetWindowTextW(g_ss->hotkey, buf, 256);
        g_ss->config->settings.hotkey = fromW(buf);
        g_ss->config->settings.broadcast_presence = getCheck(g_ss->broadcast);
        g_ss->config->settings.lock_propagation_optin = getCheck(g_ss->lock);
        g_ss->config->settings.run_on_startup = getCheck(g_ss->startup);
        g_ss->saved = true;
    }
    if (g_ss) g_ss->done = true;
    DestroyWindow(hwnd);
}

HWND makeControl(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, int x,
                 int y, int w, int h, int id) {
    return CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, parent,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                           GetModuleHandleW(nullptr), nullptr);
}

LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
        case WM_CREATE: {
            HFONT font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            makeControl(hwnd, L"STATIC", L"Switch hotkey:", 0, 16, 18, 110, 20, 0);
            g_ss->hotkey = makeControl(hwnd, L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
                                       130, 16, 180, 22, kIdHotkey);
            g_ss->broadcast =
                makeControl(hwnd, L"BUTTON", L"Announce this PC on the LAN (discovery)",
                            BS_AUTOCHECKBOX | WS_TABSTOP, 16, 52, 320, 22, kIdBroadcast);
            g_ss->lock = makeControl(hwnd, L"BUTTON", L"Allow this PC to be locked remotely",
                                     BS_AUTOCHECKBOX | WS_TABSTOP, 16, 80, 320, 22, kIdLock);
            g_ss->startup =
                makeControl(hwnd, L"BUTTON", L"Run Skittermouse on startup (needs admin)",
                            BS_AUTOCHECKBOX | WS_TABSTOP, 16, 108, 320, 22, kIdStartup);
            makeControl(hwnd, L"BUTTON", L"Save", BS_DEFPUSHBUTTON | WS_TABSTOP, 150, 148, 75, 26,
                        kIdSave);
            makeControl(hwnd, L"BUTTON", L"Cancel", WS_TABSTOP, 235, 148, 75, 26, kIdCancel);

            for (HWND c : {g_ss->hotkey, g_ss->broadcast, g_ss->lock, g_ss->startup,
                           GetDlgItem(hwnd, kIdSave), GetDlgItem(hwnd, kIdCancel)}) {
                if (c) SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            }
            if (g_ss && g_ss->config) {
                SetWindowTextW(g_ss->hotkey, toW(g_ss->config->settings.hotkey).c_str());
                setCheck(g_ss->broadcast, g_ss->config->settings.broadcast_presence);
                setCheck(g_ss->lock, g_ss->config->settings.lock_propagation_optin);
                setCheck(g_ss->startup, g_ss->config->settings.run_on_startup);
            }
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(w) == kIdSave) applyAndClose(hwnd, true);
            else if (LOWORD(w) == kIdCancel) applyAndClose(hwnd, false);
            return 0;
        case WM_CLOSE:
            applyAndClose(hwnd, false);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, w, l);
    }
}

} // namespace

bool showSettingsWindow(sm::core::Config& config) {
    SettingsState st;
    st.config = &config;
    g_ss = &st;

    HINSTANCE hInst = GetModuleHandleW(nullptr);
    WNDCLASSW wc{};
    wc.lpfnWndProc = proc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"SkittermouseSettings";
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
    RegisterClassW(&wc);

    const int width = 350, height = 225;
    int sx = GetSystemMetrics(SM_CXSCREEN), sy = GetSystemMetrics(SM_CYSCREEN);
    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, wc.lpszClassName,
                                L"Skittermouse Settings", WS_POPUP | WS_CAPTION | WS_SYSMENU,
                                (sx - width) / 2, (sy - height) / 2, width, height, nullptr,
                                nullptr, hInst, nullptr);
    if (!hwnd) {
        g_ss = nullptr;
        return false;
    }
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);

    MSG msg;
    while (!st.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (IsDialogMessageW(hwnd, &msg)) continue; // Tab/Enter/Esc navigation
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    g_ss = nullptr;
    return st.saved;
}

} // namespace sm::ui
