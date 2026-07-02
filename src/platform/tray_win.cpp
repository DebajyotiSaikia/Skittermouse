// Windows tray application shell (spec 10). A message-only window hosts the tray
// icon (Shell_NotifyIcon), the global hotkey (RegisterHotKey via the tested
// core/hotkey parser), and the clipboard listener (AddClipboardFormatListener ->
// WM_CLIPBOARDUPDATE) feeding the tested clipboard loop-prevention. The paired-
// machine menu is built from the tested ui/menu_model. Native Win32, zero
// third-party. Input capture is NOT installed here -- it is owner-only and wired
// once a peer connection exists (spec 3.1); the tray shell is always safe to run.

#include "platform/tray_app.h"

#include "core/clipboard_sync.h"
#include "core/config.h"
#include "core/hotkey.h"
#include "platform/clipboard.h"
#include "ui/menu_model.h"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include <string>
#include <vector>

namespace sm::platform {

namespace {

constexpr UINT kTrayCallback = WM_APP + 1;
constexpr UINT kIdQuit = 40001;
constexpr UINT kIdSettings = 40002;
constexpr UINT kIdDeviceBase = 41000;
constexpr int kHotkeyId = 1;

struct AppState {
    sm::core::Config config;
    sm::core::ClipboardSync clipboard;
    NOTIFYICONDATAW nid{};
    std::string self = "this-machine";
};

AppState* g_app = nullptr;

std::wstring toWide(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? static_cast<std::size_t>(n - 1) : 0, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

std::string configPath() {
    wchar_t appdata[MAX_PATH];
    if (!SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdata))) return {};
    std::wstring dir = std::wstring(appdata) + L"\\Skittermouse";
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wstring cfg = dir + L"\\config.txt";
    int n = WideCharToMultiByte(CP_UTF8, 0, cfg.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(n > 0 ? static_cast<std::size_t>(n - 1) : 0, '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, cfg.c_str(), -1, out.data(), n, nullptr, nullptr);
    return out;
}

void showMenu(HWND hwnd) {
    POINT pt;
    GetCursorPos(&pt);
    HMENU menu = CreatePopupMenu();

    auto items = sm::ui::buildMachineMenu(g_app->config.devices, g_app->self,
                                          g_app->self, /*online*/ {});
    if (items.empty()) {
        AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"No devices paired");
    } else {
        UINT id = kIdDeviceBase;
        for (const auto& it : items) {
            UINT flags = MF_STRING;
            if (it.is_owner) flags |= MF_CHECKED;
            if (!it.is_online) flags |= MF_GRAYED;
            AppendMenuW(menu, flags, id++, toWide(it.name).c_str());
        }
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kIdSettings, L"Settings\u2026");
    AppendMenuW(menu, MF_STRING, kIdQuit, L"Quit Skittermouse");

    SetForegroundWindow(hwnd); // so the menu dismisses on click-away
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(menu);
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
        case kTrayCallback:
            if (LOWORD(l) == WM_RBUTTONUP || LOWORD(l) == WM_CONTEXTMENU) showMenu(hwnd);
            return 0;

        case WM_COMMAND: {
            UINT id = LOWORD(w);
            if (id == kIdQuit) {
                PostQuitMessage(0);
            }
            // Settings and per-device switch are wired once the mesh/UI land.
            return 0;
        }

        case WM_HOTKEY:
            // The picker window (spec 4.2) opens here once built.
            return 0;

        case WM_CLIPBOARDUPDATE: {
            if (!clipboardExcludedFromMonitoring()) { // skip password-manager writes
                std::string text;
                if (getClipboardText(text) &&
                    g_app->clipboard.shouldBroadcastLocalChange(text)) {
                    // Broadcast to paired peers once the mesh network is wired.
                }
            }
            return 0;
        }

        case WM_DESTROY:
            RemoveClipboardFormatListener(hwnd);
            UnregisterHotKey(hwnd, kHotkeyId);
            Shell_NotifyIconW(NIM_DELETE, &g_app->nid);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, w, l);
}

} // namespace

int runTrayApp() {
    AppState app;
    g_app = &app;

    std::string cfg = configPath();
    if (!cfg.empty()) app.config = sm::core::Config::loadFromFile(cfg);

    HINSTANCE hInst = GetModuleHandleW(nullptr);
    WNDCLASSW wc{};
    wc.lpfnWndProc = wndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"SkittermouseTray";
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Skittermouse", 0, 0, 0, 0, 0,
                                HWND_MESSAGE, nullptr, hInst, nullptr);
    if (!hwnd) return 1;

    // Tray icon -- reuse our embedded app icon (resource id 1 from skittermouse.rc).
    app.nid.cbSize = sizeof(app.nid);
    app.nid.hWnd = hwnd;
    app.nid.uID = 1;
    app.nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    app.nid.uCallbackMessage = kTrayCallback;
    app.nid.hIcon = static_cast<HICON>(LoadImageW(hInst, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                                  GetSystemMetrics(SM_CXSMICON),
                                                  GetSystemMetrics(SM_CYSMICON),
                                                  LR_DEFAULTCOLOR));
    if (!app.nid.hIcon) {
        // IDI_APPLICATION is an ANSI resource macro; pass its numeric id to the W API.
        app.nid.hIcon = LoadIconW(nullptr, reinterpret_cast<LPCWSTR>(IDI_APPLICATION));
    }
    wcscpy_s(app.nid.szTip, L"Skittermouse");
    Shell_NotifyIconW(NIM_ADD, &app.nid);

    AddClipboardFormatListener(hwnd);

    // Global hotkey from config (spec 4.1). MOD_NOREPEAT avoids auto-repeat storms.
    auto hk = sm::core::parseHotkey(app.config.settings.hotkey);
    if (hk.valid) {
        RegisterHotKey(hwnd, kHotkeyId, hk.modifiers | MOD_NOREPEAT, hk.key);
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    g_app = nullptr;
    return 0;
}

} // namespace sm::platform
