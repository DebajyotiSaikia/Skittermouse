#pragma once

// Peer-hello handshake codec (spec 2.1, 5.2). The very first message on any freshly
// established connection identifies the sender's machine id, so the connection
// manager can register the socket under the right peer without a fixed client/server
// role. Version-gated like every other message (spec 15): a mismatched protocol
// version is rejected here, before the link is ever handed to the mesh. PURE LOGIC.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/peer_id.h"

namespace sm::net {

// [protocol_version:1][type=PeerHello:1][id_length:4 LE][id bytes].
std::vector<uint8_t> encodePeerHello(const sm::core::PeerId& self);

// Returns true and fills `outId` for a well-formed, version-matching hello; false
// on a short buffer, wrong type, version mismatch, or a length that doesn't match.
bool decodePeerHello(const uint8_t* data, std::size_t len, sm::core::PeerId& outId);

} // namespace sm::net
