#pragma once

// Reassembles WebSocket frames from a byte stream (spec 5.1). A TCP transport
// feeds bytes as they arrive; next() yields one complete frame at a time. PURE
// LOGIC on top of wsDecodeFrame.

#include <cstddef>
#include <cstdint>

#include "net/ws_frame.h"

namespace sm::net {

class WsFrameAssembler {
public:
    void feed(const uint8_t* data, std::size_t len);

    // Pop one complete frame into out. Returns false if none is buffered yet. On a
    // protocol error the internal buffer is discarded (the connection is unusable).
    bool next(WsFrame& out);

    std::size_t buffered() const { return buf_.size(); }

private:
    Bytes buf_;
};

} // namespace sm::net
