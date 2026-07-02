#pragma once

// Plain-text clipboard access (spec 8, v1 = text only). Implemented per-OS. The
// loop-prevention decision (core/clipboard_sync) and the password-manager exclusion
// check keep synced writes from ping-ponging or leaking secrets.

#include <string>

namespace sm::platform {

bool getClipboardText(std::string& utf8Out);
bool setClipboardText(const std::string& utf8);

// True if the current clipboard content is flagged to be excluded from monitoring
// by a password manager (spec 8): the presence of the well-known clipboard format
// "ExcludeClipboardContentFromMonitorProcessing". Such changes must not be synced.
bool clipboardExcludedFromMonitoring();

} // namespace sm::platform
