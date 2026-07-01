# QQMouse — Cross-Platform Software KVM — Build Specification

> This file is the persistent design specification and source of truth for QQMouse.
> It is loaded as repository context for GitHub Copilot. Implement it section by
> section, following the build order in Section 17. Working title "QQMouse" — nothing
> below depends on the name.

---

## 1. Overview

**What this is:** a lightweight native application that lets one physical mouse and
keyboard control multiple computers, switching between them on demand — a software
replacement for a hardware KVM switch, plus a shared clipboard and file transfer that
hardware KVMs don't offer.

**Platforms:** Windows and macOS only. No Linux, no mobile.

**Language & dependency philosophy:** C++, using native OS APIs wherever one exists —
Win32, WinSock2, CNG/BCrypt, Core Graphics, AppKit, CommonCrypto, Security.framework,
BSD sockets. No Boost, no OpenSSL, no Qt, no third-party networking or crypto library.
Every dependency must be justified by a genuine gap in native OS capability, not
convenience. (See Section 16 for the one narrow exception if it ever arises.)

### Core features

- **Peer-mesh input switching** — no fixed server/primary. Any paired machine can
  become the "active" input owner at any time; whichever one currently holds physical
  mouse/keyboard is the momentary source, all others are pure input sinks.
- **Priority-based coordinator failover** — an optional, user-ordered priority list
  elects a single "primary" (the highest-priority machine currently online). It has no
  special permissions; it is a deterministic, mesh-wide agreed reference that fails over
  automatically down the list as higher-priority machines go offline, and fails back
  when they return (Section 11.5).
- **Switching via hotkey-triggered picker + systray menu** — not edge-of-screen
  crawling. A global hotkey pops a small on-screen list of paired machines (navigable
  by arrow keys/Enter, or mouse click); the systray right-click menu offers the same
  list as a manual, always-available fallback.
- **Secure device pairing via numeric comparison** — ephemeral ECDH key exchange plus
  a 6-digit code shown on both machines for visual confirmation. No typed passwords, no
  certificate management.
- **Encrypted transport** — AES-256-GCM over two WebSocket-over-TLS (`wss://`)
  connections per paired machine: one persistent, low-latency channel for input/
  clipboard events, one opened on-demand for file bytes, so a large transfer can never
  stall mouse movement.
- **LAN auto-discovery** via a hand-rolled UDP broadcast presence beacon — no mDNS/
  Bonjour dependency.
- **Shared clipboard** — always-on sync (not just on-switch), plain text only in v1,
  with loop-prevention and automatic exclusion of password-manager clipboard writes.
- **Destination-initiated file transfer** using OS-native delay-rendered/virtual-file
  clipboard mechanisms (Windows `IDataObject` + `CFSTR_FILECONTENTS`; macOS
  `NSFilePromiseProvider`). The receiving machine pulls bytes only when a paste/drop
  actually happens, and the OS's own copy-progress UI (Explorer / Finder) appears
  automatically — no custom progress UI to build.
- **Cross-OS modifier key translation** by physical key position (Win↔Option, Alt↔Cmd)
  so muscle memory transfers when a Windows machine and a Mac are paired together.
- **Wake-on-LAN**, self-diagnosed per machine (NIC + OS wake settings, best-effort —
  BIOS-level state can't be verified remotely), with a guided fallback message when it
  fails.
- **Auto-start at login**, registered to run elevated, to avoid Windows UIPI silently
  blocking input injection into elevated target windows.
- **Screen-lock propagation** (opt-in, lock-only) and unlock via switch-then-type —
  never scripted credential injection, just ordinary forwarded keystrokes into a
  machine you've switched control to, same as sitting at it.
- **Fail-safe behavior throughout** — local control always wins if a connection drops,
  stuck-key release on every switch, reconnect/heartbeat watchdogs, protocol version
  checks on connect.
- **Invisible plumbing** — the user never sees IPs, ports, or "server/client" language.
  Just a list of paired devices, a hotkey, and a tray icon.

### Explicit non-goals — do not build these

- Linux support, in any form.
- Edge-of-screen crossing as the primary or only switch trigger. Hotkey + systray are
  the primary mechanism. Edge-crossing may be layered on later purely as a bonus, gated
  behind the layout config in Section 11.4, and must never be load-bearing.
- Remote desktop, screen streaming, or any video capture. This is pure input-forwarding
  — every paired machine already has its own physical monitor. (If a headless-machine
  use case ever appears, the answer is a cheap periodic screenshot-on-switch, not a
  video pipeline — flag it, don't build it speculatively.)
- Bluetooth as the primary transport. Network/WSS is primary. Implement the `Transport`
  interface (Section 5.5) so Bluetooth could be added later without a rewrite, but do
  not build a Bluetooth backend now.
- Rich clipboard formats — images, RTF, HTML. Plain text only for v1.
- Resumable or queued file transfers. One transfer at a time; on failure, fail cleanly
  and let the user retry manually.
- Any form of unattended, scripted, or programmatic credential injection into a lock
  screen. Not in scope, not a "v2 stretch goal" — out entirely.

---

## 2. Architecture

### 2.1 Process model: peer mesh, not client/server

Every machine runs the same binary and the same logic — there is no separate "server
build" and "client build." Roles are dynamic:

- Every paired machine maintains a persistent connection to every other paired machine
  it knows about (N−1 connections per machine). At the scale this tool targets (a
  handful of machines, not hundreds), this is trivial.
- Exactly one machine at a time is the input owner — the one whose physical mouse/
  keyboard hooks are actively capturing and forwarding. All others are listeners,
  injecting whatever they receive.
- A `switch` message broadcasts to all paired peers, not just the newly-targeted one,
  so every machine's tray icon always reflects the true current owner. Never let a
  switch be a private handshake between two machines while a third is left with stale
  state.
- New machines join by pairing individually with each existing member of the mesh. Do
  not implement transitive trust ("pairing with one vouches for the rest") — it's a
  bigger security surface for very little UX gain at this scale.

### 2.2 Module layout

```
QQMouse/
├── CMakeLists.txt
├── README.md
├── src/
│   ├── main.cpp                        # entry point, app lifecycle, config load
│   │
│   ├── core/
│   │   ├── event_types.h               # wire protocol structs (Section 5.3)
│   │   ├── ownership_state.h/.cpp      # peer-mesh input-owner state machine (Section 11)
│   │   ├── key_translation.h/.cpp      # cross-OS modifier remap tables (Section 4.4)
│   │   └── config.h/.cpp               # paired-device list, layout config, settings — flat file, no DB
│   │
│   ├── platform/
│   │   ├── capture_win.cpp             # SetWindowsHookEx (Section 3.1)
│   │   ├── capture_mac.mm              # CGEventTapCreate (Section 3.1)
│   │   ├── inject_win.cpp              # SendInput (Section 3.2)
│   │   ├── inject_mac.mm               # CGEventPost (Section 3.2)
│   │   ├── hotkey_win.cpp              # RegisterHotKey + picker trigger (Section 4.1)
│   │   ├── hotkey_mac.mm               # event-tap-based hotkey + picker trigger (Section 4.1)
│   │   ├── tray_win.cpp                # Shell_NotifyIcon (Section 10)
│   │   ├── tray_mac.mm                 # NSStatusBar (Section 10)
│   │   ├── clipboard_win.cpp           # AddClipboardFormatListener (Section 8)
│   │   ├── clipboard_mac.mm            # NSPasteboard polling (Section 8)
│   │   ├── filepromise_win.cpp         # IDataObject / CFSTR_FILECONTENTS (Section 9.1)
│   │   ├── filepromise_mac.mm          # NSFilePromiseProvider (Section 9.2)
│   │   ├── lock_win.cpp                # LockWorkStation (Section 14)
│   │   ├── lock_mac.mm                 # equivalent lock call (Section 14)
│   │   ├── autostart_win.cpp           # Task Scheduler registration, elevated (Section 13)
│   │   ├── autostart_mac.mm            # LaunchAgents plist (Section 13)
│   │   ├── wol_diag_win.cpp            # NIC/OS wake-capability self-check (Section 12)
│   │   └── crypto_win.cpp / crypto_mac.mm   # AES-256-GCM + ECDH wrappers (Section 7)
│   │
│   ├── net/
│   │   ├── ws_input_channel.h/.cpp     # persistent WSS: input + clipboard (Section 5.1)
│   │   ├── ws_file_channel.h/.cpp      # on-demand WSS: chunked file bytes (Section 5.1)
│   │   ├── session_token.h/.cpp        # correlates 2nd connection to 1st (Section 5.2)
│   │   ├── discovery_beacon.h/.cpp     # UDP broadcast presence beacon (Section 6)
│   │   ├── wol_sender.h/.cpp           # magic packet sender (Section 12)
│   │   └── transport.h                 # abstract Transport interface (Section 5.5)
│   │
│   ├── pairing/
│   │   ├── ecdh_handshake.h/.cpp       # ephemeral key exchange (Section 7.1)
│   │   ├── verification_code.h/.cpp    # 6-digit code derivation (Section 7.1)
│   │   ├── pairing_dialog_win.cpp      # native confirm dialog (Section 7.1)
│   │   ├── pairing_dialog_mac.mm
│   │   └── key_store.h/.cpp            # encrypted-at-rest PSK storage per device (Section 7.2)
│   │
│   └── ui/
│       ├── picker_window_win.cpp       # hotkey-triggered machine picker (Section 4.2)
│       ├── picker_window_mac.mm
│       ├── tray_menu.h/.cpp            # shared menu-building logic (Section 10)
│       └── toast_notify.h/.cpp         # connection/transfer status notifications (Section 15)
│
└── tests/
    └── ...                             # unit tests for core/ (pure logic, no OS calls)
```

Keep `core/` free of any platform-specific includes — it should compile and be
unit-testable without touching Win32 or Cocoa headers at all. Everything OS-specific
lives in `platform/`, behind a common interface `core/` calls into.

---

## 3. Capture & injection layer

### 3.1 Capture (runs only on the current input owner)

- **Windows:** `SetWindowsHookEx` with `WH_MOUSE_LL` and `WH_KEYBOARD_LL`. Low-level
  hooks, system-wide, no admin required for the hook itself (though see Section 13 on
  elevation for injection targets).
- **macOS:** `CGEventTapCreate`. Requires Accessibility permission — prompt for it
  explicitly on first run with a clear explanation of why, and detect/handle the case
  where permission is denied or later revoked (don't just silently stop working).
- Capture must be fully suspended (not just ignored) on machines that are not the
  current owner — don't run the hook and discard events; don't install the hook at all
  until this machine becomes the owner. This avoids wasted CPU and avoids any risk of
  double-handling.

### 3.2 Injection (runs on whichever machine receives forwarded events)

- **Windows:** `SendInput`.
- **macOS:** `CGEventPost`.
- Injection must run continuously on every non-owner machine, ready to receive at any
  time — this is the "sink" side and should have effectively zero idle cost between
  events (event-driven from the network channel, not polling).

---

## 4. Switching UX

### 4.1 Global hotkey

One hotkey combo opens the picker. Default suggestion: `Ctrl+Alt+Space` — chosen to
minimize collision risk (see caveats below), but must be user-configurable.

- **Windows:** `RegisterHotKey`. This call fails immediately and loudly if another
  process already owns the combo — use that. On startup, attempt registration and if it
  fails, fall back to a secondary combo (e.g. `Ctrl+Shift+Alt+Space`) and surface which
  one is active in the tray tooltip/menu, rather than failing silently.
- **macOS:** register via a filtering `CGEventTap` on the combo, or `RegisterEventHotKey`
  (Carbon, still supported).

Known collision risks to be aware of, not to "solve" magically:

- `Ctrl+Alt` together is interpreted as AltGr on many non-US physical keyboard layouts
  (German, French, Spanish, etc.) — if any paired machine uses a non-US layout, avoid
  bare `Ctrl+Alt+<letter>` combos that could collide with typing accented/special
  characters. `Ctrl+Alt+Space` is lower-risk than `Ctrl+Alt+<number>` here but still
  worth flagging to the user in settings.
- VM software (VMware, VirtualBox, Parallels) commonly uses bare `Ctrl+Alt` to release
  input grab back to the host — relevant if the user runs VMs on a paired machine.
- `Ctrl+Alt+Delete` is hardware/OS-level unhookable by design (Secure Attention
  Sequence) — not a conflict risk, just a boundary that exists and can't be worked
  around, worth knowing so it's never mistaken for a bug.

### 4.2 Picker window

A small, topmost, focus-stealing native window listing all currently-paired machines,
with the active one marked.

- **Windows:** `WS_EX_TOPMOST` + `SetForegroundWindow`, plus a local (not global)
  keyboard hook installed only while the picker is open, to capture arrow keys/Enter/
  Escape before they reach whatever app had focus underneath.
- **macOS:** equivalent topmost panel that steals key focus for its duration only.
- Navigation: Up/Down to move selection, Enter to switch, Escape (or click-away, or a
  short idle timeout) to dismiss without switching. The picker must never become a stuck
  modal that blocks normal input if the user does nothing — always have a clean dismiss
  path.
- Machines that are currently unreachable should appear in the list but visibly
  disabled/greyed, not omitted (so the user understands why they can't switch there,
  rather than wondering where a paired machine went).

### 4.3 Systray menu (always-available fallback)

Right-click → list of paired machines, click any to switch directly, no hotkey needed.
Same underlying list/logic as the picker (Section 10).

### 4.4 Stuck-key prevention on every switch

On every switch-out (regardless of trigger — hotkey, picker selection, or tray click),
the outgoing owner must force a synthetic key-up / button-up for every key/button it is
currently tracking as physically down, before handing off capture. This is the single
most common bug class in this category of software — a modifier held through a switch
leaves the outgoing machine believing it's still pressed, since the real key-up event
never arrives locally. Track "currently down" state explicitly in the capture layer;
don't rely on the OS's own key state, since you're intercepting before it gets there.

### 4.5 Cross-OS modifier key translation

Only relevant if a Windows machine and a Mac machine are paired together — skip entirely
for same-OS pairs.

Remap by physical key position, not label, so muscle memory transfers:

- Windows keyboard physical order (left to right): Ctrl – Win – Alt
- Mac keyboard physical order (left to right): Ctrl – Option – Cmd
- So: Win's physical slot ↔ Option's physical slot; Alt's physical slot ↔ Cmd's
  physical slot.

Implement as a configurable lookup table applied at the injection layer when the target
machine's OS differs from the source, not as an automatic heuristic guess.

---

## 5. Networking layer

### 5.1 Two WSS connections per paired machine

- **Input channel** — persistent, opened once pairing completes and kept alive
  (heartbeat, see Section 15) for as long as both machines are online. Carries
  `MouseMove`, `MouseButton`, `KeyEvent`, `SwitchOwner`, `Heartbeat`, `ClipboardUpdate`.
- **File channel** — opened on-demand only when an actual file transfer begins, closed
  immediately after. Carries chunked file bytes. Kept entirely separate from the input
  channel specifically so a large transfer can never head-of-line-block a mouse event on
  the same TCP connection.
- Both connections terminate at the same listening port on each machine; differentiate
  by path (`wss://host:port/input` vs `wss://host:port/files`) or by a `role` field in
  the very first message after the WS upgrade.

### 5.2 Session correlation (no re-pairing for the file channel)

The input channel performs the full PSK-based authentication (Section 7). On success,
issue a short-lived session token. When the file channel opens, it presents that token
instead of repeating the full handshake — automatic and invisible to the user, no second
confirmation dialog. Only the underlying TCP+TLS handshake for the second socket is
unavoidable, and it's cheap (roughly 1–2 round trips on LAN, happens once per transfer
session, not per chunk).

### 5.3 Wire protocol

Fixed-size struct for the hot path; variable-length framing for everything else, with a
protocol version byte on every message so a mismatched pair (one machine updated, one
not) rejects cleanly instead of silently misparsing:

```cpp
#pragma pack(push, 1)

enum class MessageType : uint8_t {
    MouseMove       = 1,
    MouseButton     = 2,
    KeyEvent        = 3,
    SwitchOwner     = 4,
    Heartbeat       = 5,
    ClipboardUpdate = 6,   // variable length
    FilePromiseMeta = 7,   // variable length, file channel
    FileChunk       = 8,   // variable length, file channel
    PairingMsg      = 9,   // variable length, pairing only
};

struct InputEvent {
    uint8_t  protocol_version;
    uint8_t  type;          // MessageType
    int16_t  dx, dy;        // mouse delta, MouseMove only
    uint8_t  code;          // key or mouse-button code
    uint8_t  down;          // 1 = press, 0 = release
    uint32_t timestamp_ms;
};
// Packs to 12 bytes for MouseMove / MouseButton / KeyEvent / SwitchOwner / Heartbeat.
// (The spec prose said "10 bytes"; the field list packs to 12. The struct layout is
//  authoritative — see event_types.h static_assert.)

#pragma pack(pop)

// Variable-length types (ClipboardUpdate, FilePromiseMeta, FileChunk, PairingMsg) use:
// [protocol_version:1][type:1][payload_length:4][payload: payload_length bytes]
// WebSocket framing already provides message boundaries, so no manual length-prefixing
// is needed beyond this header for parsing the payload's internal structure.
```

**Reasoning to preserve:** WebSocket-over-TLS was chosen deliberately over raw TCP for
this project despite the framing overhead, because it keeps the transport layer simple
(no hand-rolled message boundary logic, encryption story is clean) — the actual
per-message cost (masking, frame headers) is negligible at LAN scale and this event
rate. Don't relitigate this into a raw-socket rewrite without a measured reason to.

### 5.4 Encryption

AES-256-GCM, OS-native, no third-party crypto library:

- **Windows:** CNG / BCrypt (`bcrypt.dll`) — `BCryptEncrypt` / `BCryptDecrypt` with
  `BCRYPT_CHAIN_MODE_GCM`.
- **macOS:** CommonCrypto (`CommonCrypto/CommonCryptor.h`).
- Nonce must be a strictly incrementing counter per session, never reused. Treat nonce
  reuse as a hard bug, not a tolerable edge case.

### 5.5 Transport abstraction

Define a `Transport` interface (`send()` / `recv()` / `connect()` / `close()`) and
implement exactly one backend now — WSS. This exists purely so a Bluetooth backend could
be added later (Windows: `AF_BTH` socket family via Winsock; macOS: IOBluetooth) without
touching anything above the transport layer. Do not build the Bluetooth backend itself
in this pass — Classic Bluetooth SPP is the only viable BT profile if it's ever added
(BLE's payload/throughput limits make it unsuitable for this event rate), and macOS's
IOBluetooth support has been shrinking, which is exactly why it stays out of v1.

---

## 6. Discovery (LAN)

Hand-rolled UDP broadcast beacon — not real mDNS/Bonjour, to avoid the dependency
(Windows has no built-in mDNS responder/browser comparable to Apple's, so "real" mDNS
would mean bundling something like Bonjour, which is unnecessary here).

- Each running instance periodically broadcasts (or responds to a query broadcast) a
  small packet: `{machine_name, machine_id, ip, port}` on the LAN broadcast address.
- The "Connect to…" UI listens for beacons for a second or two and populates a live
  list.
- Maintain a "last seen" timestamp per discovered machine and drop entries from the list
  after a short timeout, so offline machines don't linger as stale/misleading options.

**Security note to preserve in code comments:** the beacon never leaves the LAN — limited
broadcast (`255.255.255.255`) is link-local by definition and routers do not forward it;
subnet-directed broadcast is typically blocked by default per RFC 2644. The beacon only
reveals presence (hostname, IP, port), never grants access — the pairing/confirm step in
Section 7 is the actual security gate, not beacon secrecy. Still, add a user-facing
"don't broadcast on this network" toggle for laptops that roam onto shared/untrusted
Wi-Fi (coffee shops, coworking spaces), where more strangers share the broadcast domain
than at home.

---

## 7. Security & pairing

### 7.1 Numeric-comparison pairing flow

Modeled on Bluetooth Secure Simple Pairing — cryptographically MITM-resistant, no typed
secrets:

1. Client discovers a machine (via beacon or manual IP entry) and initiates connection.
2. Both sides generate an ephemeral ECDH keypair (P-256) and exchange public keys in the
   clear:
   - **Windows:** CNG `BCryptSecretAgreement` with `BCRYPT_ECDH_P256_ALGORITHM`.
   - **macOS:** Security.framework `SecKeyCopyKeyExchangeResult` with
     `kSecKeyAlgorithmECDHKeyExchangeStandard`, P-256.
   - This exchange is unauthenticated at this point — a MITM could complete two separate
     exchanges, one with each side, and land on two different shared secrets.
3. Derive a 6-digit verification code from the shared secret:
   `HMAC-SHA256(shared_secret, "pairing")`, truncated to 6 digits.
4. Display the code simultaneously on both machines via a native dialog (Win32 custom
   window or `MessageBox`-style; macOS `NSAlert`), each with Confirm/Reject.
5. The human visually compares the two codes. If a MITM was present, the codes came from
   different shared secrets and will not match — this is the actual security property,
   not a formality. Only proceed on a human-confirmed match.
6. On confirm on both sides: derive a long-term key via
   `HKDF(shared_secret, salt = device_ids)` → 256-bit PSK.
7. Discard the ephemeral ECDH keys — they only existed to bootstrap this once.

### 7.2 Long-term key storage

Store the derived PSK per paired device, encrypted at rest locally (same pattern as this
project's other local-secret handling — AES-256-GCM with a machine-local protection
key). Every subsequent connection to that device uses the stored PSK directly with
AES-GCM (Section 5.4) — no repeat handshake, no repeat digit comparison, ever, unless the
device is manually un-paired and re-paired.

### 7.3 Threat model — state this honestly in docs/comments

This protects against a MITM present during the initial pairing exchange. On a home or
office LAN that's a reasonably high bar (an attacker needs to already be actively
intercepting traffic on that network), but it is the same trust model Bluetooth pairing
accepts — proportionate for this use case, not a claim of maximal security. If this is
ever extended beyond a trusted LAN, revisit this assumption explicitly rather than
carrying it forward silently.

---

## 8. Clipboard sync

Always-on, not just on-switch: broadcast a clipboard update to all paired peers whenever
the local clipboard changes, regardless of current switch state.

- **Windows:** `AddClipboardFormatListener` — event-driven, fires `WM_CLIPBOARDUPDATE`,
  no polling.
- **macOS:** no equivalent push API — poll `NSPasteboard.general.changeCount` on an
  interval (200–500ms is a reasonable default).
- **Loop prevention:** when a machine receives and writes a synced clipboard update, that
  local write will itself trigger the change listener. Tag incoming synced writes with a
  hash/source marker and skip re-broadcasting if the newly-detected clipboard content
  matches what was just received — otherwise this ping-pongs indefinitely between peers.
- **Format scope, v1:** plain text only (`CF_UNICODETEXT` on Windows,
  `NSPasteboardTypeString` on macOS). Do not build rich text/image sync in this pass —
  same mechanism extends later, just isn't in scope now.
- **Password manager exclusion:** before broadcasting a clipboard change, check for the
  presence of the exclusion format flag
  (`CFSTR_EXCLUDECLIPBOARDCONTENTFROMMONITORPROCESSING` on Windows) that password
  managers (1Password, Bitwarden, KeePass, etc.) already tag their clipboard writes with.
  If present, skip the broadcast entirely — this is a real, common gap to close, not an
  edge case to skip.

---

## 9. File transfer — destination-initiated, native progress UI

The design goal: pasting on the destination machine is what triggers the actual byte
transfer, and the OS's own copy-progress dialog appears without any custom UI being
built. This falls directly out of using each platform's delay-rendered / promised file
clipboard mechanism correctly — the same mechanism Outlook uses for drag-and-drop of
unsaved email attachments.

### 9.1 Windows: `IDataObject` + delay-rendering

On `Ctrl+C` over a file (or files) on the source machine, do not send any bytes yet. Put
a custom `IDataObject` on the clipboard via `OleSetClipboard`, advertising:

- `CFSTR_FILEDESCRIPTORW` — metadata only: filename(s), size(s), attributes. Cheap, sent
  immediately.
- `CFSTR_FILECONTENTS` — promised, backed by an `IStream` implementation that has not yet
  been asked to produce data.

On the destination machine, when Explorer (or any paste/drop target) actually calls
`GetData(CFSTR_FILECONTENTS)`, your `IStream::Read` implementation is invoked — this is
the moment, and the only moment, real file bytes should be requested over the network.
Pull them over the file channel (Section 5.1), chunked, as `Read` is called.

Because Explorer believes it's reading from a slow stream source (identical to its
behavior copying from a network share), it automatically displays its own native
copy-progress dialog — no custom progress UI to build.

Support multiple files: `CFSTR_FILEDESCRIPTORW`/`CF_HDROP`-style clipboard formats are
inherently list-based — implement for N files from the start, not a single-file special
case.

On a stream read error mid-transfer, let it propagate as an `IStream::Read` failure —
Explorer surfaces its own native error UI for this; don't build a parallel error UI.

No staging/temp directory is needed with this approach — data flows destination-request →
network pull → Explorer, with no intermediate local copy to manage or clean up.

### 9.2 macOS: `NSFilePromiseProvider`

Same promise-then-materialize-on-paste-or-drop pattern. Finder shows its own native
progress UI under the same conditions. Implement source and destination roles using this
API symmetrically with the Windows side.

### 9.3 Bidirectionality

Every machine implements both the promise-provider role and the promise-consumer role —
there is no fixed "sender" or "receiver" machine. Whichever machine's clipboard currently
holds a file promise is the source for that transfer; whichever machine pastes is the
destination, regardless of which one is the current input owner or which one initiated
pairing.

---

## 10. Systray application shell

- **Windows:** `Shell_NotifyIcon`.
- **macOS:** `NSStatusBar` (AppKit, via Objective-C++).
- Icon reflects current input-owner state at a glance (e.g. color or badge change).
- Right-click menu: list of paired machines with current owner marked (click to switch
  directly — same list/logic as the hotkey picker, Section 4.3), per-machine online/
  offline status, "Connect to…" (opens the discovery list from Section 6), "Add device"
  / pairing entry point, settings.
- No IP addresses, ports, or "server/client" terminology anywhere in this UI — just
  named, paired devices.

---

## 11. Peer-mesh ownership protocol

### 11.1 Ownership state machine

Each machine tracks a single piece of shared truth: which machine currently owns input.
Represent it as an explicit state (`local_owner` vs `remote_owner(peer_id)`), not
inferred from side effects.

### 11.2 Switch broadcast

A switch request (from hotkey, picker, or tray click) is broadcast to every paired peer,
not sent privately to the target. This keeps every machine's tray icon and internal state
consistent — never let ownership change be known to only two of three-or-more paired
machines.

### 11.3 Race handling

If two machines attempt to claim ownership near-simultaneously (unlikely, but possible
with hotkeys on different machines), arbitrate with: the current owner has authority to
reject a conflicting claim, or fall back to a monotonic sequence number/timestamp if the
claims are genuinely concurrent. Don't leave this undefined — pick one rule and apply it
consistently.

### 11.4 Layout config (retained, edge-crossing intentionally not built on top of it yet)

Keep a simple spatial arrangement config — which machine is notionally "to the left/right
of" which, similar to the Windows/macOS multi-monitor arrangement screen — purely as
forward-compatible data. Do not implement edge-of-screen switching against it in this
pass (Section 1 non-goals); this config existing now just avoids a data-model change
later if edge-crossing is ever added as a bonus feature. If any single machine in the
mesh runs multiple monitors, model the layout at the monitor level, not the machine
level, so this stays correct if edge-crossing is added later.

### 11.5 Elected coordinator with priority failover

A user-ordered priority list elects a single coordinator (the user may call this the
"server"). Semantics:

- The current coordinator is the highest-priority machine that is currently online. For
  machine k to be coordinator, every higher-priority machine must be offline; when a
  higher-priority machine returns, it preemptively reclaims the role (deterministic
  failback).
- The coordinator has **no special authority** — input ownership stays coordinator-free
  (Section 11.3) and the role grants no permissions. It is purely a stable, mesh-wide
  agreed reference point, computed identically on every machine from (priority list,
  online set) with no negotiation, so failover is automatic and split-brain-free.
- All paired machines are on the priority list by default; the user may remove a machine
  so it is never eligible. New pairings append at lowest priority.
- Implemented as pure logic in `core/server_election` (no OS calls). Terminology note: to
  honor Section 10 (no "server/client" language in the UI), surface this as "primary" in
  any UI; "server" is user-facing shorthand in the priority/settings screen only.

---

## 12. Wake-on-LAN

What can actually be checked, and by whom: WoL's enabled/disabled state lives partly in
the target's BIOS/UEFI (invisible to any software check, on that machine or remotely) and
partly in the OS/NIC driver (checkable locally on that machine while it's awake, not
remotely).

Each machine, while awake, self-diagnoses and reports a best-effort status as part of its
presence beacon (Section 6):

- NIC-level WoL support: `powercfg /deviceenablewake` / equivalent driver query.
- OS-level wake permission: the "Allow this device to wake the computer" setting,
  readable via WMI (`MSPower_DeviceWakeEnable`).
- Explicitly cannot check the BIOS-level toggle — never claim certainty here.

When switching to a machine that's currently unreachable and its last-known status
suggests WoL is plausible: send a magic packet (UDP broadcast containing the target's MAC
address) over `wol_sender`, show a "Waking DESKTOP-B…" state with a timeout (30–60s).

On success (machine reconnects): proceed normally. On timeout or a last-known status
indicating WoL isn't properly configured: show a guided, advisory message — check
BIOS/UEFI wake-on-LAN setting, check the Windows adapter's "Allow this device to wake the
computer" property, disable Fast Startup (known to interfere). Frame this explicitly as
last-known-state guidance, not a live guarantee, since by definition you can't query a
machine that's currently unreachable.

---

## 13. Auto-start & elevation

- Register via Task Scheduler, not the Registry `Run` key — Task Scheduler supports "run
  with highest privileges" without a UAC prompt on every login.
- Run elevated by default. **Reason to preserve in comments:** Windows UIPI (User
  Interface Privilege Isolation) blocks a non-elevated process from injecting input into
  an elevated window. If QQMouse isn't elevated and the user later interacts with an
  admin-elevated app on the target machine, injection into that specific window will
  silently fail — running elevated from the start avoids this whole failure class.
- **macOS:** register via a `LaunchAgent` plist.

---

## 14. Screen lock / unlock

- **Locking:** a simple fire-and-forget command over the input channel — target calls
  `LockWorkStation()` (Windows) or the macOS equivalent. Make this opt-in per machine,
  not automatic-always (a machine mid-build/render shouldn't lock just because the
  primary user stepped away).
- **Unlocking:** switch input ownership to the locked machine first (hotkey/picker/tray,
  same as any other switch), then the user physically types their own password through
  the normal forwarded-input path — mechanically identical to sitting at that machine and
  typing. This is categorically different from, and does not reintroduce, the
  scripted-credential-injection approach ruled out in Section 1's non-goals: no password
  is ever stored, scripted, or injected unattended by the app.

**Open question requiring real-device testing, not an assumption:** Windows lock screens
(`LogonUI.exe`) run on a separate Secure Desktop specifically to block synthetic/scripted
input. Whether `SendInput`-based forwarding (mimicking real hardware input) crosses that
boundary the same way a physically-wired keyboard does is genuinely uncertain and
version/build-dependent — build switch-then-type expecting it to work with no
special-case code, but verify on an actual locked machine before relying on it. If the
Secure Desktop boundary blocks it, the honest fallback is that unlock requires physical
presence at that machine, and QQMouse simply resumes normal operation the moment it's
back at an unlocked desktop session.

---

## 15. Failure & edge-state handling — consolidated checklist

Implement all of these explicitly; none of them are optional polish:

- **Fail-safe local control:** if the connection to the current input owner drops, every
  listening machine immediately reverts to normal local input. Use a heartbeat watchdog
  (no heartbeat received for ~1–2s → assume dead, release to local control) — design for
  the owner disappearing silently (crash, lid closed, power loss), not just for a clean
  goodbye message, since a crashed owner can't send one.
- **Connection-dropped UI:** tray icon shows a distinct disconnected state (color/badge
  change) per-machine; a one-time notification on drop, not a repeating one.
- **Switch-to-unreachable:** hotkey/picker selection of an offline machine is a no-op (or
  a brief "unavailable" flash) — never a hang or crash.
- **File transfer failure mid-stream:** propagates as a native `IStream`/promise-provider
  error (Section 9.1), surfaced by Explorer/Finder's own error UI — verify this behaves
  gracefully in testing rather than assuming it.
- **Discovery staleness:** entries drop from the "Connect to…" list after a last-seen
  timeout (Section 6) so offline machines don't linger.
- **Protocol version mismatch:** the version byte (Section 5.3) is checked on every
  connection; a mismatch is rejected cleanly with a clear "update QQMouse on <machine>"
  message, never a silent misparse of the binary struct.
- **Simultaneous switch claims:** resolved per the rule in Section 11.3, not left as
  undefined behavior.

---

## 16. Build setup

- CMake, with per-platform source file gating from day one — `.mm` files (macOS/
  Objective-C++) must never be compiled on the Windows toolchain and vice versa.
  Structure `CMakeLists.txt` to select the right `platform/*_win.cpp` or
  `platform/*_mac.mm` set based on the target OS, rather than relying on
  `#ifdef`-wrapping single shared files for the genuinely platform-specific pieces
  (capture, injection, tray, file-promise, crypto).
- Confirm zero third-party dependencies for both Windows and macOS builds — Win32/
  WinSock2/CNG, and Core Graphics/AppKit/CommonCrypto/Security.framework respectively,
  cover every requirement in this spec natively. If a library (Boost, OpenSSL, a JSON
  library, etc.) is suggested for anything described above, treat that as a signal to
  re-check against this spec rather than accept it — the native path exists for all of
  it.
- Config storage (paired devices, layout, settings) is a flat file — no embedded
  database needed for the data volumes involved here (a handful of fields per paired
  machine).

---

## 17. Suggested build order

Sequenced to get a working core loop early, with everything else layered on:

1. `core/event_types.h` + `core/ownership_state` — pure logic, no OS calls,
   unit-testable in isolation first.
2. Capture + injection, one platform only (pick your primary dev machine) — get local
   hook → local inject working as a sanity check before any networking exists.
3. Pairing (Section 7) — ECDH exchange, numeric comparison, PSK derivation and storage.
4. Input channel (Sections 5.1, 5.3, 5.4) — get two machines forwarding real mouse/
   keyboard events end-to-end, encrypted.
5. Switching UX (Section 4) — hotkey, picker, systray, stuck-key handling. This is the
   point at which it becomes a usable daily tool for two machines.
6. Second platform — bring the other OS's capture/injection/tray/hotkey up to parity.
7. Discovery (Section 6) — replace manual IP entry with the broadcast beacon.
8. Clipboard sync (Section 8) — including loop-prevention and password-manager exclusion.
9. Peer mesh (Section 11) — generalize from two machines to N, including
   switch-broadcast-to-all and race handling.
10. File transfer (Section 9) — the delay-rendered/promised-file mechanism, per platform.
11. Wake-on-LAN, auto-start, lock/unlock (Sections 12–14) — polish layer, once the core
    loop is solid.
12. Failure/edge-state hardening (Section 15) — pass over everything above with the
    checklist explicitly, don't assume it's covered implicitly by earlier steps.

---

## 18. Prior art worth reading (not reusing wholesale)

Deskflow (github.com/deskflow/deskflow) is the actively-maintained continuation of the
Synergy → Barrier → Input Leap lineage — worth skimming for its clipboard-sync edge cases
and multi-platform quirks even though this project's architecture (peer-mesh,
hotkey-first switching, native delay-rendered file transfer) deliberately diverges from
its hub-and-spoke, edge-crossing-first model. No code reuse intended — reference only, to
avoid re-discovering already-known bugs.
