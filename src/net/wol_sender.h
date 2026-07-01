#pragma once

// Wake-on-LAN magic-packet sender (spec 12). buildMagicPacket / parseMac are PURE
// LOGIC (unit-tested); sendMagicPacket uses native UDP sockets (WinSock on Windows,
// BSD sockets elsewhere) to broadcast the packet -- no third-party networking.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace sm::net {

using Bytes = std::vector<uint8_t>;
using Mac = std::array<uint8_t, 6>;

// 102-byte magic packet: 6 x 0xFF followed by the target MAC repeated 16 times.
Bytes buildMagicPacket(const Mac& mac);

// Parse "AA:BB:CC:DD:EE:FF" or "AA-BB-CC-DD-EE-FF" into 6 bytes. False if malformed.
bool parseMac(const std::string& s, Mac& out);

// Broadcast the magic packet to (broadcastIp, port). Returns false on socket error.
bool sendMagicPacket(const Mac& mac, const std::string& broadcastIp, uint16_t port);

} // namespace sm::net
