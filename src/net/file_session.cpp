#include "net/file_session.h"

#include <algorithm>
#include <cstring>

namespace sm::net {

void FileSender::addFile(const std::string& name, Bytes data) {
    files_.emplace_back(name, std::move(data));
    cursors_.push_back(0);
}

Bytes FileSender::meta() const {
    std::vector<FileEntry> entries;
    entries.reserve(files_.size());
    for (const auto& f : files_)
        entries.push_back(FileEntry{f.first, static_cast<uint64_t>(f.second.size())});
    return encodeFilePromiseMeta(entries);
}

Bytes FileSender::nextChunk(uint32_t index, std::size_t chunkSize) {
    if (index >= files_.size() || chunkSize == 0) return {};
    std::size_t& cur = cursors_[index];
    const Bytes& d = files_[index].second;
    if (cur >= d.size()) return {};
    std::size_t n = std::min(chunkSize, d.size() - cur);
    Bytes chunk = encodeFileChunk(index, cur, d.data() + cur, n);
    cur += n;
    return chunk;
}

bool FileSender::complete(uint32_t index) const {
    return index < files_.size() && cursors_[index] >= files_[index].second.size();
}

bool FileReceiver::acceptMeta(const uint8_t* data, std::size_t len) {
    std::vector<FileEntry> entries;
    if (!decodeFilePromiseMeta(data, len, entries)) return false;
    entries_ = std::move(entries);
    buffers_.assign(entries_.size(), Bytes{});
    received_.assign(entries_.size(), 0);
    for (std::size_t i = 0; i < entries_.size(); ++i)
        buffers_[i].resize(static_cast<std::size_t>(entries_[i].size));
    return true;
}

bool FileReceiver::acceptChunk(const uint8_t* data, std::size_t len) {
    uint32_t index = 0;
    uint64_t offset = 0;
    Bytes chunk;
    if (!decodeFileChunk(data, len, index, offset, chunk)) return false;
    if (index >= buffers_.size()) return false;
    if (offset + chunk.size() > buffers_[index].size()) return false; // out of bounds
    std::memcpy(buffers_[index].data() + offset, chunk.data(), chunk.size());
    received_[index] += chunk.size();
    return true;
}

bool FileReceiver::complete(std::size_t i) const {
    return i < entries_.size() &&
           received_[i] >= static_cast<std::size_t>(entries_[i].size);
}

bool FileReceiver::allComplete() const {
    for (std::size_t i = 0; i < entries_.size(); ++i)
        if (!complete(i)) return false;
    return !entries_.empty();
}

} // namespace sm::net
