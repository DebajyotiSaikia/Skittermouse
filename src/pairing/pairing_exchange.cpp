#include "pairing/pairing_exchange.h"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace sm::pairing {

namespace {
constexpr std::size_t kPubLen = sm::crypto::kEcPointLen; // 64
constexpr std::size_t kNonceLen = 32;                    // random per-side pairing nonce
constexpr std::size_t kCommitLen = 32;                   // SHA-256 commitment
} // namespace

bool PairingExchange::start() {
    if (started_) return true;
    if (!handshake_.begin()) return false;
    const sm::crypto::Bytes& pub = handshake_.publicPoint();
    if (pub.size() != kPubLen || self_.size() == 0 || self_.size() > 255) return false;

    nonce_ = sm::crypto::randomBytes(kNonceLen);
    if (nonce_.size() != kNonceLen) return false;

    // Round 1: commit to our public point + nonce + id before the peer reveals its
    // nonce, so neither side (nor a MITM) can grind the 6-digit code.
    std::vector<uint8_t> cbuf;
    cbuf.reserve(kPubLen + kNonceLen + self_.size());
    cbuf.insert(cbuf.end(), pub.begin(), pub.end());
    cbuf.insert(cbuf.end(), nonce_.begin(), nonce_.end());
    cbuf.insert(cbuf.end(), self_.begin(), self_.end());
    const sm::crypto::Hash256 commit = sm::crypto::sha256(cbuf.data(), cbuf.size());

    started_ = transport_.send(commit.data(), commit.size());
    return started_;
}

PairingExchange::Status PairingExchange::poll() {
    if (status_ != Status::NeedMore) return status_;

    uint8_t buf[512];
    const int n = transport_.recv(buf, sizeof(buf));
    if (n == 0) return Status::NeedMore; // peer message not here yet
    if (n < 0) {
        status_ = Status::Error;
        return status_;
    }

    if (phase_ == Phase::AwaitCommit) {
        if (static_cast<std::size_t>(n) != kCommitLen) {
            status_ = Status::Error; // a commitment is exactly one SHA-256 digest
            return status_;
        }
        std::copy(buf, buf + kCommitLen, peerCommit_.begin());

        // Round 2: reveal our id + public point + nonce.
        const sm::crypto::Bytes& pub = handshake_.publicPoint();
        std::vector<uint8_t> msg;
        msg.reserve(1 + self_.size() + kPubLen + kNonceLen);
        msg.push_back(static_cast<uint8_t>(self_.size()));
        msg.insert(msg.end(), self_.begin(), self_.end());
        msg.insert(msg.end(), pub.begin(), pub.end());
        msg.insert(msg.end(), nonce_.begin(), nonce_.end());
        if (!transport_.send(msg.data(), msg.size())) {
            status_ = Status::Error;
            return status_;
        }
        phase_ = Phase::AwaitReveal;
        return Status::NeedMore;
    }

    // Phase::AwaitReveal -- parse [id_len:1][id][public_point:64][nonce:32].
    const std::size_t idLen = buf[0];
    if (static_cast<std::size_t>(n) != 1 + idLen + kPubLen + kNonceLen || idLen == 0) {
        status_ = Status::Error; // malformed
        return status_;
    }
    peerId_.assign(reinterpret_cast<const char*>(buf + 1), idLen);
    if (peerId_ == self_) {
        status_ = Status::Error; // paired with ourselves -> reject
        return status_;
    }
    const uint8_t* peerPubPtr = buf + 1 + idLen;
    const uint8_t* peerNoncePtr = peerPubPtr + kPubLen;
    const sm::crypto::Bytes peerPub(peerPubPtr, peerPubPtr + kPubLen);
    const sm::crypto::Bytes peerNonce(peerNoncePtr, peerNoncePtr + kNonceLen);

    // The reveal must hash to the round-1 commitment -- this is the anti-grinding
    // gate: the peer (or a MITM) could not have chosen this point/nonce adaptively
    // after seeing ours.
    std::vector<uint8_t> vbuf;
    vbuf.reserve(kPubLen + kNonceLen + idLen);
    vbuf.insert(vbuf.end(), peerPub.begin(), peerPub.end());
    vbuf.insert(vbuf.end(), peerNonce.begin(), peerNonce.end());
    vbuf.insert(vbuf.end(), peerId_.begin(), peerId_.end());
    if (sm::crypto::sha256(vbuf.data(), vbuf.size()) != peerCommit_) {
        status_ = Status::Error; // commitment mismatch -> tamper / MITM
        return status_;
    }

    if (!handshake_.computeShared(peerPub)) {
        status_ = Status::Error;
        return status_;
    }
    code_ = handshake_.verificationCode(nonce_, peerNonce); // nonce-bound 6-digit code
    if (!handshake_.derivePsk(self_, peerId_, psk_)) {
        status_ = Status::Error;
        return status_;
    }
    status_ = Status::Ok;
    return status_;
}

} // namespace sm::pairing
