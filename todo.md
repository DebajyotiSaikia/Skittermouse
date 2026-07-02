# Skittermouse — Unimplemented Work

Derived from [spec.md](spec.md); section refs (§) point there. This file lists **only what
is not yet built** — as each item lands, delete it from here.

## Status

- Native C++ only (product), **799 checks green**, Windows + macOS CI green at every commit.
- **The entire data plane is validated over real TCP** by a two-container Docker rig
  (`tests/docker/`, run `pwsh tests/docker/run.ps1`): (1) ECDH numeric-comparison **pairing** →
  both nodes derive the same 6-digit code + PSK; (2) **encrypted input** — secure link →
  coordinator election → ownership switch → a forwarded keystroke decrypted + injected; (3)
  **file transfer** — a 500 KB file chunked → reassembled by offset → byte-verified. All exercise
  the SAME product logic (secure_link → ConnectionService/ConnectionManager → MeshNode →
  EncryptedTransport / FileChannel) over the real POSIX WS transport.
- Implemented + unit-tested: all core logic + crypto (AES-256-GCM / ECDH P-256 / SHA / HMAC /
  HKDF / base64, KAT-verified), pairing + secure-link handshake, the protocol/codec layer, the
  mesh brain (N-peer switch, coordinator failover, heartbeat fail-safe, clipboard sync, input
  forwarding, one-time drop/return/unavailable/version toasts), `ConnectionService`, the
  file-transfer session + `FileChannel`, WoL `WakeFlow`, and the config store. Both Windows
  (Shell_NotifyIcon) and macOS (NSStatusBar) are real tray apps; the Windows tray has the mesh,
  hotkey+fallback, picker, clipboard, WoL, an opt-in run-on-startup toggle, and a working Settings
  item; macOS has the Carbon hotkey + NSPanel picker. Client+server WS transport on Windows; POSIX
  WS transport for macOS/Linux.
- Remaining items all need real hardware (two Windows machines, a Mac desktop, or Explorer/Finder)
  to build correctly _and_ validate — see below.

---

## VERY IMPORTANT — native C++ only (product); OpenSSL only in the Linux test rig

Every SHIPPED item must use **native, system-provided C++ APIs only** — Win32, WinSock2,
CNG/BCrypt, Schannel, Core Graphics, AppKit, CommonCrypto, Security.framework, BSD sockets, and
the C++ standard library. **No third-party dependencies in the product** (spec §16). The ONE
exception is `src/platform/crypto_posix.cpp` + `tools/net*.cpp`: they link OpenSSL, but they are
the Linux two-container TEST RIG only (`tests/docker/`) and are never compiled into the
Windows/macOS product (guarded by the CMake `else()`/`UNIX AND NOT APPLE` branches).

---

## By build order (spec §17)

### Step 4 — Input channel: transport finish (§5)

- [ ] TLS-wrap (Schannel) the client + server WebSocket transport for full `wss://`. Lower
      priority / defense-in-depth: message payloads are already AES-256-GCM sealed end-to-end by
      the app-layer `EncryptedTransport` (Docker-proven), so TLS is not the security gate.

### Step 9 — Peer mesh: macOS connection thread

- [ ] macOS tray: run the same background connect/accept -> secure-link -> mesh I/O thread over
      the (written) POSIX transport, plus an "Add device" pairing flow. The **Windows tray is
      done**: a background I/O thread dials paired peers (bounded connect timeout) + accepts
      inbound on port 47800, runs the secure link off the UI thread, and hands sealed links to
      the UI thread (mesh stays single-threaded); "Add device" runs the ECDH numeric-comparison
      (`PairingExchange` + confirm dialog) on port 47801 and stores the PSK in an
      encrypted-at-rest keystore. Runtime check is in Manual validation below.

### Step 10 — File transfer: OS delay-render (§9)

- [ ] `platform/filepromise_win.cpp` — `IDataObject` delay-render: `CFSTR_FILEDESCRIPTORW` +
      `CFSTR_FILECONTENTS` (`IStream::Read` pulls bytes on paste) over the (done) file session;
      multi-file; native Explorer progress + error UI (§9.1).
- [ ] `platform/filepromise_mac.mm` — `NSFilePromiseProvider`; native Finder progress (§9.2).
- [ ] On-paste socket-open glue: open the dedicated `/files` WS connection (+ session token)
      when a promised paste/drop happens, and hand it to the (done) `net/FileChannel` driver.
      The FileChannel byte path (FilePromiseMeta + chunked FileChunks, reassembled by offset;
      multi-file/zero-byte/partial-delivery unit-tested) is **validated over real TCP** by the
      two-container rig (`tools/netfile` transfers a 500 KB file, byte-verified). Only the OS
      delay-render trigger + connection-open remain, and they need Explorer/Finder to test.

### Step 11 — Lock/unlock finish (§14)

- [ ] Unlock = switch-then-type only; **verify Secure Desktop / `LogonUI` behavior on a real
      locked machine** (open question). (lock_win/lock_mac, autostart_win/mac: done. WoL
      **"Waking…" flow** — bounded-timeout state machine (`core/WakeFlow`, unit-tested) + magic
      packet + guided-fallback toast, wired into the tray on switch-to-unreachable with a
      per-device MAC (config round-tripped): done.)

### Step 12 — Failure & edge-state UI (§15)

- [ ] File-transfer mid-stream failure surfaced via native `IStream`/promise error
      (lands with the file-promise work in Step 10).
      (heartbeat fail-safe, discovery staleness, simultaneous-claim resolution, codec version
      gate, one-time connection-dropped/return toast, switch-to-unreachable "unavailable" flash,
      **protocol-version-mismatch "update Skittermouse on <machine>" toast** (throttled per peer,
      e2e-tested) — all via MeshNode callbacks wired into the Windows tray: done.)

---

## Manual validation (needs real hardware — the code is built + CI-green, but these paths can't

## be runtime-tested from the dev box; validate on your machines)

### Two Windows machines — pair, connect, forward input (Step 9, built)

1. Install the same nightly on both PCs; both show the tray icon.
2. Same LAN; allow Skittermouse through the firewall (TCP 47800 mesh + 47801 pairing) when prompted.
3. On PC-A: tray → **Add device**, enter PC-B's IP, OK. On PC-B: tray → **Add device**, leave the
   IP blank (it waits), OK. (Either side may enter the other's IP; the other leaves it blank.)
4. Both show a **6-digit code** — confirm they MATCH and click Yes on both → toast "Paired with …".
5. Within a few seconds → toast "Connected to <PC>"; the peer appears in the tray menu.
6. Click the peer (or hotkey → pick it) to switch input, then type/move → it appears on the other
   PC; switch back with the hotkey/menu. (This is exactly the flow the Docker rig proves headless.)

- Not connecting? Check the firewall, the IP, and that both PCs run the same build.

### Run-on-startup installer checkbox (Step 13)

- Install with "Run Skittermouse on startup" ticked → after reboot the tray reappears.
- Default (unticked) → no autostart; enable later via tray "Run on startup" or Settings.

### Lock-screen unlock (Step 14, open question)

- Switch to a LOCKED PC and type its password through forwarded input. Verify whether SendInput
  crosses the Secure Desktop (LogonUI) boundary on your Windows build; if not, unlock needs
  physical presence (documented limitation, not a bug).

---

## Do NOT build (spec §1 non-goals — listed so they aren't accidentally added)

- Linux support in the PRODUCT (the Linux build is a TEST RIG only, never shipped).
- Edge-of-screen crossing as a primary/load-bearing switch trigger.
- Remote desktop, screen streaming, video capture.
- A Bluetooth backend now (keep only the `transport.h` abstraction).

## Evaluate the below - don't implement yet

- Rich clipboard formats (images/RTF/HTML) — plain text only v1.
- Resumable/queued file transfers — one at a time, fail cleanly.
- Any unattended/scripted credential injection into a lock screen.
