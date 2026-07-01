#pragma once

// Abstract transport (spec 5.5). Exactly one backend exists now -- WSS -- but
// everything above the transport (input channel, file channel) talks only to this
// interface, so a Bluetooth backend could be added later (Windows AF_BTH; macOS
// IOBluetooth) without touching anything upstream. PURE INTERFACE, no OS includes.

#include <cstddef>
#include <cstdint>
#include <string>

namespace sm::net {

class Transport {
public:
    virtual ~Transport() = default;

    virtual bool connect(const std::string& host, uint16_t port) = 0;
    virtual bool isConnected() const = 0;

    // Send one whole message. Returns false on error.
    virtual bool send(const uint8_t* data, std::size_t len) = 0;

    // Receive one whole message into buf (up to cap bytes). Returns the message
    // length (>0), 0 if none is available yet, or -1 on error/disconnect.
    virtual int recv(uint8_t* buf, std::size_t cap) = 0;

    virtual void close() = 0;
};

} // namespace sm::net
