#include "net/frame_assembler.h"

namespace sm::net {

void WsFrameAssembler::feed(const uint8_t* data, std::size_t len) {
    buf_.insert(buf_.end(), data, data + len);
}

bool WsFrameAssembler::next(WsFrame& out) {
    if (buf_.empty()) return false;
    long consumed = wsDecodeFrame(buf_.data(), buf_.size(), out);
    if (consumed > 0) {
        buf_.erase(buf_.begin(), buf_.begin() + consumed);
        return true;
    }
    if (consumed < 0) {
        buf_.clear(); // protocol error -- drop the unusable stream
    }
    return false; // 0 == need more bytes
}

} // namespace sm::net
