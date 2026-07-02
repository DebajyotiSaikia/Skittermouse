#include "pairing/pairing_exchange.h"

#include <cstddef>
#include <vector>

namespace sm::pairing {

namespace {
// Wire message: [id_len:1][id bytes][public_point:64]. Ids are short machine names,
// so a single length byte is plenty.
constexpr std::size_t kPubLen = sm::crypto::kEcPointLen; // 64
} // namespace

bool PairingExchange::start() {
    if (started_) return true;
    if (!handshake_.begin()) return false;
    const sm::crypto::Bytes& pub = handshake_.publicPoint();
    if (pub.size() != kPubLen || self_.size() > 255) return false;

    std::vector<uint8_t> msg;
    msg.reserve(1 + self_.size() + kPubLen);
    msg.push_back(static_cast<uint8_t>(self_.size()));
    msg.insert(msg.end(), self_.begin(), self_.end());
    msg.insert(msg.end(), pub.begin(), pub.end());

    started_ = transport_.send(msg.data(), msg.size());
    return started_;
}

PairingExchange::Status PairingExchange::poll() {
    if (status_ != Status::NeedMore) return status_;

    uint8_t buf[512];
    const int n = transport_.recv(buf, sizeof(buf));
    if (n == 0) return Status::NeedMore; // peer message not here yet
    if (n < 1) {
        status_ = Status::Error;
        return status_;
    }
    const std::size_t idLen = buf[0];
    if (static_cast<std::size_t>(n) != 1 + idLen + kPubLen || idLen == 0) {
        status_ = Status::Error; // malformed
        return status_;
    }

    peerId_.assign(reinterpret_cast<const char*>(buf + 1), idLen);
    if (peerId_ == self_) {
        status_ = Status::Error; // paired with ourselves -> reject
        return status_;
    }
    const sm::crypto::Bytes peerPub(buf + 1 + idLen, buf + 1 + idLen + kPubLen);

    if (!handshake_.computeShared(peerPub)) {
        status_ = Status::Error;
        return status_;
    }
    code_ = handshake_.verificationCode();
    if (!handshake_.derivePsk(self_, peerId_, psk_)) {
        status_ = Status::Error;
        return status_;
    }
    status_ = Status::Ok;
    return status_;
}

} // namespace sm::pairing
