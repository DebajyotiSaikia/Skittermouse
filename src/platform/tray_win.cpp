// Windows tray application shell (spec 10). A message-only window hosts the tray
// icon (Shell_NotifyIcon), the global hotkey (RegisterHotKey via the tested
// core/hotkey parser), and the clipboard listener (AddClipboardFormatListener ->
// WM_CLIPBOARDUPDATE) feeding the tested clipboard loop-prevention. The paired-
// machine menu is built from the tested ui/menu_model. Native Win32, zero
// third-party. Input capture is NOT installed here -- it is owner-only and wired
// once a peer connection exists (spec 3.1); the tray shell is always safe to run.

#include "platform/tray_app.h"

#include "app/mesh_node.h"
#include "core/config.h"
#include "core/event_types.h"
#include "core/hotkey.h"
#include "core/wake_flow.h"
#include "net/wol_sender.h"
#include "platform/autostart.h"
#include "platform/clipboard.h"
#include "platform/injector.h"
#include "ui/menu_model.h"
#include "ui/picker_window.h"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include <memory>
#include <string>
#include <vector>

namespace sm::platform {

namespace {

constexpr UINT kTrayCallback = WM_APP + 1;
constexpr UINT kIdQuit = 40001;
constexpr UINT kIdSettings = 40002;
constexpr UINT kIdStartup = 40003;
constexpr UINT kIdDeviceBase = 41000;
constexpr int kHotkeyId = 1;

struct AppState {
    sm::core::Config config;
    std::unique_ptr<sm::app::MeshNode> mesh;
    std::unique_ptr<sm::platform::Injector> injector;
    sm::core::WakeFlow wake; // Wake-on-LAN "Waking…" flow (spec 12)
    NOTIFYICONDATAW nid{};
    std::string self;
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

// A tray balloon notification (spec 15: connection/transfer status).
void showToast(const std::wstring& title, const std::wstring& message) {
    if (!g_app) return;
    NOTIFYICONDATAW n = g_app->nid;
    n.uFlags = NIF_INFO;
    wcsncpy_s(n.szInfoTitle, title.c_str(), _TRUNCATE);
    wcsncpy_s(n.szInfo, message.c_str(), _TRUNCATE);
    n.dwInfoFlags = NIIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY, &n);
}

void showMenu(HWND hwnd) {
    POINT pt;
    GetCursorPos(&pt);
    HMENU menu = CreatePopupMenu();

    std::vector<sm::core::PeerId> online;
    for (const auto& d : g_app->config.devices)
        if (g_app->mesh && g_app->mesh->isPeerOnline(d.id)) online.push_back(d.id);
    sm::core::PeerId owner = g_app->mesh ? g_app->mesh->owner() : g_app->self;
    auto items = sm::ui::buildMachineMenu(g_app->config.devices, g_app->self, owner, online);
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
    AppendMenuW(menu, MF_STRING | (isAutostartEnabled() ? MF_CHECKED : MF_UNCHECKED), kIdStartup,
                L"Run on startup");
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
            } else if (id == kIdSettings) {
                // Settings is the flat-file config (spec 16). Create it if missing,
                // then open it in the default text editor; changes apply on next
                // launch. (Previously this menu item had no handler and did nothing.)
                std::string path = configPath();
                if (!path.empty()) {
                    std::wstring wpath = toWide(path);
                    if (GetFileAttributesW(wpath.c_str()) == INVALID_FILE_ATTRIBUTES)
                        g_app->config.saveToFile(path);
                    ShellExecuteW(hwnd, L"open", wpath.c_str(), nullptr, nullptr,
                                  SW_SHOWNORMAL);
                    showToast(L"Skittermouse",
                              L"Edit the config, then restart Skittermouse to apply.");
                }
            } else if (id == kIdStartup) {
                // Opt-in auto-start (spec 13), OFF by default so a reboot always
                // recovers. Enabling/disabling the elevated login task needs admin,
                // so this prompts UAC; a declined prompt leaves the setting unchanged.
                const bool wasEnabled = isAutostartEnabled();
                const bool ok = wasEnabled ? disableAutostart() : enableAutostart();
                if (ok) {
                    g_app->config.settings.run_on_startup = !wasEnabled;
                    std::string path = configPath();
                    if (!path.empty()) g_app->config.saveToFile(path);
                    showToast(L"Skittermouse", wasEnabled
                                                   ? L"Skittermouse will no longer run on startup."
                                                   : L"Skittermouse will run on startup.");
                } else {
                    showToast(L"Skittermouse",
                              L"Startup setting unchanged (administrator approval declined).");
                }
            } else if (id >= kIdDeviceBase && g_app->mesh) {
                std::size_t idx = id - kIdDeviceBase;
                if (idx < g_app->config.devices.size())
                    g_app->mesh->requestSwitchTo(g_app->config.devices[idx].id);
            }
            return 0;
        }

        case WM_HOTKEY:
            if (g_app->mesh) {
                std::vector<sm::core::PeerId> online;
                for (const auto& d : g_app->config.devices)
                    if (g_app->mesh->isPeerOnline(d.id)) online.push_back(d.id);
                auto items = sm::ui::buildMachineMenu(g_app->config.devices, g_app->self,
                                                      g_app->mesh->owner(), online);
                std::string sel = sm::ui::showPicker(items);
                if (!sel.empty()) g_app->mesh->requestSwitchTo(sel);
            }
            return 0;

        case WM_TIMER: {
            if (g_app->mesh) {
                uint64_t now = GetTickCount64();
                g_app->mesh->sendHeartbeats(now);
                g_app->mesh->poll(now);

                // Resolve any in-progress Wake-on-LAN attempt (spec 12).
                if (g_app->wake.isWaking()) {
                    const sm::core::PeerId target = g_app->wake.target();
                    auto st = g_app->wake.update(now, g_app->mesh->isPeerOnline(target));
                    if (st == sm::core::WakeFlow::Status::Connected) {
                        showToast(L"Skittermouse", toWide(target) + L" is awake");
                        g_app->mesh->requestSwitchTo(target); // now reachable -> switch
                        g_app->wake.reset();
                    } else if (st == sm::core::WakeFlow::Status::TimedOut) {
                        showToast(L"Skittermouse",
                                  L"Could not wake " + toWide(target) +
                                      L". Check BIOS/UEFI Wake-on-LAN, the adapter's "
                                      L"\u201cAllow this device to wake the computer\u201d "
                                      L"setting, and disable Fast Startup.");
                        g_app->wake.reset();
                    }
                }
            }
            return 0;
        }

        case WM_CLIPBOARDUPDATE: {
            if (!clipboardExcludedFromMonitoring() && g_app->mesh) { // skip password-manager writes
                std::string text;
                if (getClipboardText(text)) g_app->mesh->onLocalClipboardChange(text);
            }
            return 0;
        }

        case WM_DESTROY:
            KillTimer(hwnd, 1);
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

    // Derive a stable self id from the machine name and build the mesh brain.
    char nameBuf[256];
    DWORD nameLen = sizeof(nameBuf);
    app.self = GetComputerNameA(nameBuf, &nameLen) ? std::string(nameBuf, nameLen)
                                                   : std::string("this-machine");
    app.injector.reset(sm::platform::createInjector());
    app.mesh = std::make_unique<sm::app::MeshNode>(app.self);
    app.mesh->setPriority(app.config.priority);
    sm::platform::Injector* inj = app.injector.get();
    app.mesh->onInject = [inj](const sm::core::InputEvent& e) {
        using MT = sm::core::MessageType;
        switch (static_cast<MT>(e.type)) {
            case MT::MouseMove:   inj->mouseMove(e.dx, e.dy); break;
            case MT::MouseButton: inj->mouseButton(e.code, e.down != 0); break;
            case MT::KeyEvent:    inj->key(e.code, e.down != 0); break;
            default: break;
        }
    };
    app.mesh->onRemoteClipboard = [](const std::string& t) { setClipboardText(t); };
    app.mesh->onOwnerChanged = [](const sm::core::PeerId& newOwner) {
        showToast(L"Skittermouse", L"Input owner is now " + toWide(newOwner));
    };
    // Connection state (spec 15): one-time balloon on drop/return, never repeating
    // (the transition callbacks fire exactly once per change).
    app.mesh->onPeerOffline = [](const sm::core::PeerId& id) {
        showToast(L"Skittermouse", L"Lost connection to " + toWide(id));
    };
    app.mesh->onPeerOnline = [](const sm::core::PeerId& id) {
        showToast(L"Skittermouse", L"Connected to " + toWide(id));
    };
    // Switch-to-unreachable (spec 15/12): if the target is WoL-plausible and has a
    // known MAC, send the magic packet and enter the bounded "Waking…" flow;
    // otherwise just flash "unavailable".
    app.mesh->onSwitchUnavailable = [](const sm::core::PeerId& id) {
        if (!g_app) return;
        const sm::core::PairedDevice* dev = nullptr;
        for (const auto& d : g_app->config.devices)
            if (d.id == id) { dev = &d; break; }

        sm::net::Mac mac;
        if (dev && dev->wol_capable && !dev->mac.empty() &&
            sm::net::parseMac(dev->mac, mac)) {
            sm::net::sendMagicPacket(mac, "255.255.255.255", 9); // WoL discard port
            g_app->wake.start(id, GetTickCount64(), 45000);      // 45 s window (spec 12)
            const std::wstring name = toWide(dev->name.empty() ? id : dev->name);
            showToast(L"Skittermouse", L"Waking " + name + L"\u2026");
        } else {
            showToast(L"Skittermouse", toWide(id) + L" is unavailable");
        }
    };
    // Protocol-version mismatch (spec 15): tell the user which machine to update.
    app.mesh->onVersionMismatch = [](const sm::core::PeerId& id) {
        showToast(L"Skittermouse", L"Update Skittermouse on " + toWide(id));
    };

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
    SetTimer(hwnd, 1, 50, nullptr); // pump the mesh: heartbeats + incoming messages

    // Global hotkey from config (spec 4.1). MOD_NOREPEAT avoids auto-repeat storms.
    // If the configured combo is already owned by another process, RegisterHotKey
    // fails loudly -- fall back to a secondary combo and surface it in the tooltip.
    auto hk = sm::core::parseHotkey(app.config.settings.hotkey);
    bool hkOk = hk.valid && RegisterHotKey(hwnd, kHotkeyId, hk.modifiers | MOD_NOREPEAT, hk.key);
    if (!hkOk) {
        auto fb = sm::core::parseHotkey("Ctrl+Shift+Alt+Space");
        hkOk = fb.valid && RegisterHotKey(hwnd, kHotkeyId, fb.modifiers | MOD_NOREPEAT, fb.key);
        if (hkOk) {
            NOTIFYICONDATAW tip = app.nid;
            tip.uFlags = NIF_TIP;
            wcscpy_s(tip.szTip, L"Skittermouse (hotkey: Ctrl+Shift+Alt+Space)");
            Shell_NotifyIconW(NIM_MODIFY, &tip);
        }
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
