# Skittermouse

A lightweight, native **software KVM** for Windows and macOS: one physical mouse and
keyboard controlling multiple computers, with switching by global hotkey and system
tray, an always-on shared clipboard, and destination-initiated file transfer that
reuses the OS's own copy-progress UI. Pure input forwarding — no screen streaming, no
video.

## Download

Website: **[mouse.deb0.com](https://mouse.deb0.com)** — it auto-detects your OS and points
to the right installer.

- Windows: [Skittermouse-Windows-x64-Setup.exe](https://github.com/DebajyotiSaikia/Skittermouse/releases/latest/download/Skittermouse-Windows-x64-Setup.exe)
- macOS: [Skittermouse-macOS.dmg](https://github.com/DebajyotiSaikia/Skittermouse/releases/latest/download/Skittermouse-macOS.dmg)
- [All releases](https://github.com/DebajyotiSaikia/Skittermouse/releases/latest)

Installers are built automatically every night (when there are new commits) by
[.github/workflows/nightly-release.yml](.github/workflows/nightly-release.yml) and are
early, unsigned development builds.

The full design specification is the source of truth for architecture and scope and
lives in [.github/copilot-instructions.md](.github/copilot-instructions.md).

## Status

Early build. Implemented so far, following the build order in Section 17 of the spec:

- **Step 1 — pure-logic core (no OS calls):**
  - [src/core/event_types.h](src/core/event_types.h) — wire-protocol message types and
    packed structs (Section 5.3).
  - [src/core/ownership_state.h](src/core/ownership_state.h) /
    [src/core/ownership_state.cpp](src/core/ownership_state.cpp) — peer-mesh input-owner
    state machine with deterministic, coordinator-free race resolution (Section 11).
  - [src/core/server_election.h](src/core/server_election.h) /
    [src/core/server_election.cpp](src/core/server_election.cpp) — priority-based
    coordinator ("primary") election with automatic failover/failback (Section 11.5).
  - [tests/](tests/) — zero-dependency unit and end-to-end scenario tests for the core.
- **Website + CI:** [docs/](docs/) static site (deployed to
  [mouse.deb0.com](https://mouse.deb0.com)) and GitHub Actions for nightly installers.

Not yet implemented: capture/injection, pairing, networking, switching UX, discovery,
clipboard sync, file transfer, wake-on-LAN, auto-start, lock/unlock.

## Design principles

- **Native OS APIs only** — zero third-party runtime dependencies. Win32 / WinSock2 /
  CNG on Windows; Core Graphics / AppKit / CommonCrypto / Security on macOS (Section 16).
- **Peer mesh, not client/server** — every machine runs the same binary; input
  ownership is dynamic (Section 2.1).
- `core/` stays free of any platform includes and is unit-testable in isolation
  (Section 2.2).

## Build

Requires CMake ≥ 3.20 and a C++17 compiler (MSVC on Windows, or clang on macOS).

```powershell
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

On Windows, run these from a "Developer PowerShell for VS" (or after importing the VS
developer environment) so the MSVC toolchain is on PATH.
