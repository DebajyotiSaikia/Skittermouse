#pragma once

// Stable identifier for a machine in the mesh (e.g. a config-persisted UUID or
// machine name). Shared by core modules so they don't depend on each other just
// for this alias. Pure logic -- no OS includes.

#include <string>

namespace sm::core {

using PeerId = std::string;

} // namespace sm::core
