#pragma once

// File-channel driver (spec 5.1, 9). Drives the (pure) net/file_session sender and
// receiver over the abstract Transport that the OS layer opens on demand for a
// transfer -- the dedicated second connection kept separate from the input channel
// so a large transfer never head-of-line-blocks a mouse event (spec 5.1). The
// sender emits FilePromiseMeta then streams FileChunks; the receiver reassembles by
// offset. PURE LOGIC over Transport + file_session + message_codec, unit-testable
// with a loopback transport. The bytes only flow once the OS delay-render provider
// (IStream / NSFilePromiseProvider) actually opens the channel on paste/drop, so
// this is destination-initiated end to end (spec 9).

#include <cstddef>

#include "net/file_session.h"
#include "net/transport.h"

namespace sm::net {

class FileChannel {
public:
    // Sender side: push the promise metadata then every file's chunks over `t`.
    // `chunkSize` bounds each FileChunk payload (0 -> a sensible default). Returns
    // true once all files have been fully sent, false on a transport send error.
    static bool sendAll(sm::net::Transport& t, FileSender& sender, std::size_t chunkSize = 0);

    // Receiver side: drain every message currently available on `t` into `receiver`
    // (FilePromiseMeta / FileChunk). Returns true once all promised files are
    // complete. Call again as more bytes arrive until it returns true.
    static bool receiveAvailable(sm::net::Transport& t, FileReceiver& receiver);
};

} // namespace sm::net
