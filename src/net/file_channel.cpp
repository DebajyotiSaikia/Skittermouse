#include "net/file_channel.h"

#include "net/message_codec.h"

#include <cstdint>
#include <vector>

namespace sm::net {

using sm::core::MessageType;

namespace {
// Upper bound on one framed file message the receiver will accept in a single recv.
// Chunk payloads must stay comfortably below this (the default chunk size does).
constexpr std::size_t kMaxFileMessage = 1u << 20; // 1 MiB
constexpr std::size_t kDefaultChunk = 64u * 1024u;
} // namespace

bool FileChannel::sendAll(sm::net::Transport& t, FileSender& sender, std::size_t chunkSize) {
    if (chunkSize == 0) chunkSize = kDefaultChunk;

    const Bytes meta = sender.meta();
    const Bytes metaMsg =
        encodeVarMessage(MessageType::FilePromiseMeta, meta.data(), meta.size());
    if (!t.send(metaMsg.data(), metaMsg.size())) return false;

    for (uint32_t i = 0; i < static_cast<uint32_t>(sender.fileCount()); ++i) {
        while (!sender.complete(i)) {
            const Bytes chunk = sender.nextChunk(i, chunkSize);
            if (chunk.empty()) break; // guards against a stall if a file yields nothing
            const Bytes chunkMsg =
                encodeVarMessage(MessageType::FileChunk, chunk.data(), chunk.size());
            if (!t.send(chunkMsg.data(), chunkMsg.size())) return false;
        }
    }

    for (uint32_t i = 0; i < static_cast<uint32_t>(sender.fileCount()); ++i) {
        if (!sender.complete(i)) return false;
    }
    return true;
}

bool FileChannel::receiveAvailable(sm::net::Transport& t, FileReceiver& receiver) {
    std::vector<uint8_t> buf(kMaxFileMessage);
    for (;;) {
        const int n = t.recv(buf.data(), buf.size());
        if (n <= 0) break; // 0 = nothing more available, <0 = error/disconnect

        DecodedMessage m;
        std::size_t consumed = 0;
        if (decodeMessage(buf.data(), static_cast<std::size_t>(n), m, consumed) !=
            DecodeResult::Ok) {
            continue; // skip malformed / version-mismatched frames
        }
        if (m.isFixed) continue; // only variable-length file messages matter here
        if (m.type == MessageType::FilePromiseMeta) {
            receiver.acceptMeta(m.payload.data(), m.payload.size());
        } else if (m.type == MessageType::FileChunk) {
            receiver.acceptChunk(m.payload.data(), m.payload.size());
        }
    }
    return receiver.allComplete();
}

} // namespace sm::net
