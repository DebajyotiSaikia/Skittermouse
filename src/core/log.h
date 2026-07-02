#pragma once

// Minimal thread-safe file logger (for on-machine debugging of the network/pairing/
// TLS paths, which can only be validated on real hardware). PURE LOGIC (std only) --
// no OS includes. Disabled until init() is given a path; writes are serialized.

#include <string>

namespace sm::log {

// Point the log at a file (call once at startup). Empty path disables logging.
void init(const std::string& path);
bool enabled();

// Append one timestamped line (thread-safe; a no-op when disabled).
void write(const std::string& msg);

} // namespace sm::log
