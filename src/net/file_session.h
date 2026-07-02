#pragma once

// File-transfer session logic (spec 9): the sender advertises FilePromiseMeta and
// streams FileChunks on demand; the receiver reassembles by offset (chunks may be
// requested in any order). PURE LOGIC on top of net/file_transfer -- the OS
// delay-render provider (IStream / NSFilePromiseProvider) drives it. Multi-file
// from the start (spec 9.1).

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "net/file_transfer.h"

namespace sm::net {

class FileSender {
public:
    void addFile(const std::string& name, Bytes data);
    std::size_t fileCount() const { return files_.size(); }

    Bytes meta() const; // FilePromiseMeta payload for all files

    // Next FileChunk payload for `index` from the internal cursor; empty when done.
    Bytes nextChunk(uint32_t index, std::size_t chunkSize);
    bool complete(uint32_t index) const;

private:
    std::vector<std::pair<std::string, Bytes>> files_;
    std::vector<std::size_t> cursors_;
};

class FileReceiver {
public:
    bool acceptMeta(const uint8_t* data, std::size_t len);   // FilePromiseMeta
    bool acceptChunk(const uint8_t* data, std::size_t len);  // FileChunk

    std::size_t fileCount() const { return entries_.size(); }
    const FileEntry& entry(std::size_t i) const { return entries_[i]; }
    const Bytes& data(std::size_t i) const { return buffers_[i]; }
    bool complete(std::size_t i) const;
    bool allComplete() const;

private:
    std::vector<FileEntry> entries_;
    std::vector<Bytes> buffers_;
    std::vector<std::size_t> received_;
};

} // namespace sm::net
