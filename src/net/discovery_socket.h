#pragma once

// UDP presence broadcast + receive (spec 6). Native sockets (WinSock / BSD), no
// mDNS/Bonjour dependency. Broadcasts stay link-local (255.255.255.255 is not
// forwarded by routers). The beacon reveals only presence -- pairing (spec 7) is
// the real security gate. Decoded beacons feed net/discovery_table.

#include <cstdint>

#include "net/discovery_beacon.h"

namespace sm::net {

// Broadcast one beacon to 255.255.255.255:port. Returns false on socket error.
bool broadcastBeacon(const Beacon& b, uint16_t port);

// Send one beacon by UNICAST to ip:port. Used to reply to a received broadcast so the
// sender discovers us too, even when OUR broadcast can't reach it (VPN split routing,
// a corp firewall dropping broadcast, WiFi client isolation). Refuses non-private
// targets, so a spoofed beacon can't turn this into a reflector toward a routed/public
// address -- replies stay on the LAN. Returns false on error or a non-private target.
bool sendBeaconTo(const Beacon& b, const std::string& ip, uint16_t port);

// True if `ip` is a private/link-local/loopback IPv4 (RFC1918 10/8, 172.16/12,
// 192.168/16, 169.254/16, 127/8) -- i.e. an on-LAN address, not a routed one.
bool isPrivateIpv4(const std::string& ip);

// Bind to port and wait up to timeout_ms for a beacon; decode into out. Returns
// false on timeout, socket error, or an undecodable packet.
bool receiveBeacon(uint16_t port, int timeout_ms, Beacon& out);

} // namespace sm::net
