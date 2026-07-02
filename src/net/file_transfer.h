#pragma once

// File-transfer wire payloads (spec 9). The destination-initiated design puts a
// FilePromiseMeta (filenames + sizes) on the clipboard first; bytes are pulled
// only when a paste/drop happens, chunked as FileChunk messages. These are PURE
// encode/decode helpers for the payload bodies (the outer message framing is in
// net/message_codec). Multi-file from the start (spec 9.1). Little-endian targets.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sm::net {

using Bytes = std::vector<uint8_t>;

struct FileEntry {
    std::string name;
    uint64_t size = 0;
};

// FilePromiseMeta payload: [count:4]{ [namelen:2][name][size:8] } x count.
Bytes encodeFilePromiseMeta(const std::vector<FileEntry>& files);
bool decodeFilePromiseMeta(const uint8_t* data, std::size_t len, std::vector<FileEntry>& out);

// FileChunk payload: [file_index:4][offset:8][data...].
Bytes encodeFileChunk(uint32_t index, uint64_t offset, const uint8_t* data, std::size_t len);
bool decodeFileChunk(const uint8_t* data, std::size_t len, uint32_t& index, uint64_t& offset,
                     Bytes& chunk);

} // namespace sm::net
