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

### Step 9 — Peer mesh: remaining

- [ ] Wire the (done, Docker-proven) `app/ConnectionService` into the Windows tray on a
      background I/O thread: the thread dials/accepts + runs the secure link (blocking socket
      work, lock-free), and hands each sealed link to the UI thread (which owns the mesh) via a
      queue, so all `MeshNode`/`ConnectionManager` access stays single-threaded. Needs a dial
      `connect()` timeout and **two-Windows-machine runtime validation** before shipping (the
      threading can't be proven from one machine). `ConnectionService` itself (factory-injected
      dial/accept -> secure_link -> ConnectionManager) is built, unit-tested, and validated
      end-to-end over real TCP by the two-container rig (tests/docker/), which drives it through
      `tools/netcheck`. macOS needs the same thread over the (now-written) POSIX transport.

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

## Do NOT build (spec §1 non-goals — listed so they aren't accidentally added)

- Linux support in the PRODUCT (the Linux build is a TEST RIG only, never shipped).
- Edge-of-screen crossing as a primary/load-bearing switch trigger.
- Remote desktop, screen streaming, video capture.
- A Bluetooth backend now (keep only the `transport.h` abstraction).

## Evaluate the below - don't implement yet

- Rich clipboard formats (images/RTF/HTML) — plain text only v1.
- Resumable/queued file transfers — one at a time, fail cleanly.
- Any unattended/scripted credential injection into a lock screen.
