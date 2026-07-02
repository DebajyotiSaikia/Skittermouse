#pragma once

// Auto-start at login, elevated (spec 13). Registered via Task Scheduler ("run with
// highest privileges") rather than the Registry Run key, so injection into elevated
// windows isn't blocked by UIPI. Implemented per-OS (Windows here; macOS LaunchAgent).

namespace sm::platform {

bool enableAutostart();
bool disableAutostart();
bool isAutostartEnabled();

} // namespace sm::platform
