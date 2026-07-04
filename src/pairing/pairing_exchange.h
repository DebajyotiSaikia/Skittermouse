#pragma once

// Pairing exchange over a transport (spec 7.1). Drives the ECDH numeric-comparison
// handshake to completion on top of the abstract Transport, with a commitment round
// so an active MITM can't grind the 6-digit code:
//   round 1: send SHA256(pubkey || nonce || id)   -- commit to our key + nonce
//   round 2: send [id][pubkey][nonce]             -- reveal (rejected unless it
//                                                    hashes to the peer's commitment)
// Then both derive the shared secret, the nonce-bound 6-digit code, and the long-term
// PSK. The caller shows the code (confirmPairingCode) and, on mutual confirm, stores
// the PSK. Stateful + pollable (like InboundHandshake), so the OS layer can pump it
// without blocking. PURE LOGIC over Transport + crypto.

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

    // Generate the ephemeral keypair and send our round-1 commitment. Returns false
    // on keygen/send failure.
    bool start();

    // Advance the exchange as peer messages arrive (round-1 commitment, then round-2
    // reveal), verifying the commitment before deriving the shared secret, code, and
    // PSK. Call until the status is no longer NeedMore.
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

    enum class Phase { AwaitCommit, AwaitReveal };
    Phase phase_ = Phase::AwaitCommit;
    sm::crypto::Bytes nonce_;          // our random pairing nonce (revealed in round 2)
    sm::crypto::Hash256 peerCommit_{}; // peer's round-1 commitment, verified on reveal
};

} // namespace sm::pairing
