#pragma once

// Minimal RFC 6455 WebSocket frame codec (spec 5.1: transport is WSS). PURE LOGIC
// -- encode/decode a single frame. Client-role frames are masked per the RFC; the
// mask key is random. Only the opcodes this app needs are enumerated.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace sm::net {

using Bytes = std::vector<uint8_t>;

enum class WsOpcode : uint8_t {
    Continuation = 0x0,
    Text = 0x1,
    Binary = 0x2,
    Close = 0x8,
    Ping = 0x9,
    Pong = 0xA,
};

struct WsFrame {
    WsOpcode opcode = WsOpcode::Binary;
    bool fin = true;
    Bytes payload;
};

// Encode one frame. When masked, a random 4-byte key is generated (client role).
Bytes wsEncodeFrame(WsOpcode op, const uint8_t* payload, std::size_t len,
                    bool masked, bool fin = true);

// Decode one frame from buf. Returns bytes consumed (>0) on a complete frame,
// 0 if more bytes are needed, or -1 on a protocol error.
long wsDecodeFrame(const uint8_t* buf, std::size_t len, WsFrame& out);

} // namespace sm::net
