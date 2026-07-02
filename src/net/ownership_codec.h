#pragma once

// Ownership-claim wire codec (spec 11.2). A SwitchOwner message carries the full
// claim -- target, origin, and the logical-clock sequence -- so every peer applies
// the same total order and converges without a coordinator. PURE LOGIC. Payload:
//   [target_len:1][target][origin_len:1][origin][sequence:8]

#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/ownership_state.h"

namespace sm::net {

using Bytes = std::vector<uint8_t>;

Bytes encodeOwnershipClaim(const sm::core::OwnershipClaim& c);
bool decodeOwnershipClaim(const uint8_t* data, std::size_t len, sm::core::OwnershipClaim& out);

} // namespace sm::net
