#pragma once

// Pairing exchange over a transport (spec 7.1). Drives the ECDH numeric-comparison
// handshake to completion on top of the abstract Transport: each side sends its id +
// ephemeral public point, then both derive the shared secret, the 6-digit code, and
// the long-term PSK. The caller shows the code (confirmPairingCode) and, on mutual
// confirm, stores the PSK. Stateful + pollable (like InboundHandshake), so the OS
// layer can pump it without blocking. PURE LOGIC over Transport + crypto.

#include <array>
#include <cstdint>
#include <string>

#include "core/peer_id.h"
#include "net/transport.h"
#include "pairing/ecdh_handshake.h"

namespace sm::pairing {

class PairingExchange {
public:
    enum class Status { NeedMore, Ok, Error };

    PairingExchange(sm::net::Transport& transport, sm::core::PeerId self)
        : transport_(transport), self_(std::move(self)) {}

    // Generate the ephemeral keypair and send our id + public point. Returns false on
    // keygen/send failure.
    bool start();

    // Read the peer's id + public point (when it arrives) and derive the shared
    // secret, code, and PSK. Call until the status is no longer NeedMore.
    Status poll();

    const sm::core::PeerId& peerId() const { return peerId_; }
    const std::string& code() const { return code_; }
    const std::array<uint8_t, 32>& psk() const { return psk_; }

private:
    sm::net::Transport& transport_;
    sm::core::PeerId self_;
    EcdhHandshake handshake_;
    sm::core::PeerId peerId_;
    std::string code_;
    std::array<uint8_t, 32> psk_{};
    bool started_ = false;
    Status status_ = Status::NeedMore;
};

} // namespace sm::pairing
