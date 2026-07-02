# Skittermouse — Unimplemented Work

Derived from [spec.md](spec.md); section refs (§) point there. This file lists **only what
is not yet built** — as each item lands, delete it from here.

## Status

- Foundation + app-logic landed and unit-tested (native C++ only, **533 checks green**,
  Windows + macOS CI green): core logic (config, key_translation, ownership, election,
  stuck-key tracker, clipboard loop-prevention, heartbeat fail-safe, input pipeline), crypto
  (AES-256-GCM + ECDH P-256 + SHA-256/SHA-1/HMAC/HKDF/base64, KAT-verified), pairing (ECDH
  handshake, 6-digit code, encrypted key store), net (WS handshake + framing + assembler,
  version-checked message codec, session token, WoL, discovery beacon + table), and a headless
  e2e flow through the real Transport (loopback). Windows capture/injection, TCP+WebSocket
  client transport, and screen-lock compile.
- Remaining: TLS-wrap + listener for the transport, the switching UX (hotkey/picker/tray), OS
  clipboard/file-promise/autostart/wol-diag glue, all macOS `.mm` backends, the real
  `main.cpp` app wiring, and hardware / two-machine / macOS validation.
- The shipped app is still a console stub — the subsystems above aren't wired into a UI yet.

---

## VERY IMPORTANT — native C++ only

Every item below must be built with **native, system-provided C++ APIs only** — Win32,
WinSock2, CNG/BCrypt, Schannel, Core Graphics, AppKit, CommonCrypto, Security.framework,
BSD sockets, and the C++ standard library. **No third-party dependencies** (no Boost, no
OpenSSL, no Qt, no JSON/networking/crypto library). If any task seems to "need" a library,
re-check against [spec.md](spec.md) §16 — the native path exists for all of it.

---

## By build order (spec §17)

### Step 2 — Capture + injection

- [ ] Wire the (done, tested) `core/input_pipeline` (capture → stuck-key tracker → forward)
      to `capture_win`/`inject_win` in the real app, and run the local hook→inject sanity
      loop. (pure pipeline + capture/inject compile; app wiring + hardware validation remain.)

### Step 3 — Pairing (§7)

- [ ] `pairing/pairing_dialog_win.cpp` — native confirm/reject dialog showing the 6-digit
      code (§7.1). (crypto, ECDH handshake, verification code, HKDF PSK, key store: done.)

### Step 4 — Input channel (§5)

- [ ] TLS-wrap (Schannel) the (done) TCP+WebSocket client transport for full `wss://`, and add
      the server/listener side (accept + server handshake); differentiate `/input` vs `/files`.
- [ ] `net/ws_input_channel.h/.cpp` — persistent channel carrying the hot-path messages;
      AES-256-GCM per message with a strictly-incrementing nonce; version check on connect.
- [ ] Milestone: two machines forwarding encrypted input end-to-end.
      (transport interface, WS handshake/framing/assembler, client transport, message codec: done.)

### Step 5 — Switching UX (§4, §10, §15)

- [ ] `platform/hotkey_win.cpp` — `RegisterHotKey` using the (done) `core/hotkey` parser
      (default `Ctrl+Alt+Space`), loud-fail → fallback combo, surface active combo (§4.1).
- [ ] `ui/picker_window_win.cpp` — topmost focus-stealing list over the (done) `ui/menu_model`;
      Up/Down/Enter/Esc; local keyboard hook while open; offline greyed; clean dismiss (§4.2).
- [ ] `platform/tray_win.cpp` + `ui/tray_menu.h/.cpp` — `Shell_NotifyIcon` over the (done)
      `ui/menu_model`; owner marked; online/offline; Connect…/Add device/settings (§10, §4.3).
- [ ] Apply `key_translation` at injection when target OS differs (§4.5). (remap table: done.)
- [ ] `ui/toast_notify.h/.cpp` — connection/transfer status notifications (§15).
- [ ] Turn `main.cpp` into the real app: WIN32 subsystem, message loop, subsystem wiring, config load.

### Step 6 — Second platform (macOS) parity

- [ ] `platform/capture_mac.mm` — `CGEventTapCreate`; explicit Accessibility permission prompt + denial/revocation handling (§3.1).
- [ ] `platform/inject_mac.mm` — `CGEventPost` (§3.2).
- [ ] `platform/hotkey_mac.mm` — filtering `CGEventTap` / `RegisterEventHotKey` (§4.1).
- [ ] `ui/picker_window_mac.mm` — topmost key-focus-stealing panel (§4.2).
- [ ] `platform/tray_mac.mm` — `NSStatusBar` (§10).
- [ ] `platform/crypto_mac.mm` — CommonCrypto AES-GCM + Security.framework ECDH (§5.4, §7.1).
- [ ] `pairing/pairing_dialog_mac.mm` — `NSAlert` code confirm (§7.1).

### Step 7 — Discovery (§6)

- [ ] Wire the (done) `net/discovery_socket` (broadcast/receive) + `discovery_beacon` codec +
      `discovery_table` into a periodic beacon loop; honor the `broadcast_presence` config
      toggle. (sockets + codec + table: done.)

### Step 8 — Clipboard sync (§8)

- [ ] Wire `AddClipboardFormatListener` / `WM_CLIPBOARDUPDATE` (needs the app window) to the
      (done) `clipboard_win` get/set + `core/clipboard_sync` loop-prevention.
- [ ] `platform/clipboard_mac.mm` — poll `NSPasteboard.changeCount` 200–500 ms.
      (Windows CF_UNICODETEXT read/write + password-manager exclusion check: done.)

### Step 9 — Peer mesh, generalized to N (§2.1, §11)

- [ ] Wire to real N-1 persistent connections per machine.
- [ ] `SwitchOwner` broadcasts to **all** peers (never a private 2-party handshake) (§11.2).
- [ ] Over-the-wire race handling per §11.3; pair-individually join (no transitive trust).
- [ ] `core/config` layout config — monitor-level spatial arrangement, forward-compat data only, **no** edge-crossing (§11.4).

#### Coordinator failover — priority-ordered "server" fallback

- [ ] User designates paired machines in a **priority order** (1, 2, 3, 4, 5, …). The current
      coordinator ("server") is the highest-priority machine currently online; for machine 5
      to act as server, all of 1–4 must be offline. Fails over down the list automatically,
      and **fails back** preemptively when a higher-priority machine returns.
- [ ] The coordinator has **no special permissions** — it is only a deterministic, mesh-wide
      agreed reference computed identically on every machine from (priority list, online set).
- [ ] **All paired machines are on the list by default**; the user may remove a machine so it
      is never eligible. New pairings append at lowest priority.
- [ ] Wire the (done) `core/server_election` logic to the live online set from the mesh,
      persist the priority list in `core/config` (done), and surface it as "primary" (never
      "server") in the settings UI (§10 wording).

### Step 10 — File transfer (§9)

- [ ] `net/ws_file_channel.h/.cpp` — on-demand WSS, chunked bytes, opened per transfer, closed
      after (§5.1). (session_token + `net/file_transfer` meta/chunk codec: done.)
- [ ] `platform/filepromise_win.cpp` — `IDataObject` delay-render: `CFSTR_FILEDESCRIPTORW` (meta now) + `CFSTR_FILECONTENTS` (`IStream` pulls bytes only on `GetData`); multi-file from the start; native Explorer progress + error UI (§9.1).
- [ ] `platform/filepromise_mac.mm` — `NSFilePromiseProvider`; native Finder progress (§9.2).
- [ ] Bidirectional: every machine is both promise-provider and consumer (§9.3).

### Step 11 — Wake-on-LAN, auto-start, lock/unlock (§12–14)

- [ ] `platform/wol_diag_win.cpp` — NIC WoL (`powercfg /deviceenablewake`) + OS wake (WMI `MSPower_DeviceWakeEnable`); cannot check BIOS — never claim certainty (§12).
- [ ] "Waking…" state + 30–60 s timeout + guided fallback flow around the (done) magic-packet UDP sender (§12).
- [ ] `platform/autostart_mac.mm` — `LaunchAgent` plist (§13). (`autostart_win` = Task Scheduler, elevated: done.)
- [ ] `platform/lock_mac.mm` — equivalent lock call (§14). (`lock_win` = LockWorkStation: done.)
- [ ] Unlock = switch-then-type only (no scripted credentials); **verify Secure Desktop/`LogonUI` behavior on a real locked machine** (§14 open question).

### Step 12 — Failure & edge-state hardening (§15)

- [ ] Wire the (done) `core/heartbeat` watchdog into the app: on owner-drop, every sink reverts
      to local control (~1–2 s timeout, silent-death safe).
- [ ] Connection-dropped tray state + one-time (non-repeating) notification.
- [ ] Switch-to-unreachable = no-op / brief "unavailable" flash, never hang/crash.
- [ ] File-transfer mid-stream failure surfaced via native `IStream`/promise error (verify in testing).
- [ ] Discovery staleness timeout (drop offline machines from "Connect to…").
- [ ] Surface protocol-version mismatch cleanly ("update Skittermouse on <machine>") at connect. (codec-level version gate: done.)
- [ ] Simultaneous switch claims resolved per §11.3, not undefined.

---

## Tests — 100% coverage (all steps)

- [~] Unit tests for every pure-logic module landed so far (**533 checks**, native harness):
  core logic, crypto (KAT + OpenSSL cross-check, incl. SHA-1/base64), pairing, session
  token, WS handshake/framing/assembler, message codec, WoL, beacon, discovery table,
  heartbeat, input pipeline, config, key_translation. Extend to 100% as new modules land.
- [~] Headless e2e flow through the real Transport (loopback): pairing → session token → input
  forwarding + stuck-key release → ownership switch → clipboard loop-prevention → heartbeat
  fail-safe → protocol-mismatch reject. Extend with clipboard/file-transfer over the wire
  and coordinator failover/failback as those land.
- [~] Keep the Windows + macOS CI builds green and all tests passing at every step. (533/533.)

---

## Do NOT build (spec §1 non-goals — listed so they aren't accidentally added)

- Linux support (any form).
- Edge-of-screen crossing as a primary/load-bearing switch trigger.
- Remote desktop, screen streaming, video capture.
- A Bluetooth backend now (keep only the `transport.h` abstraction).
- Rich clipboard formats (images/RTF/HTML) — plain text only v1.
- Resumable/queued file transfers — one at a time, fail cleanly.
- Any unattended/scripted credential injection into a lock screen.
