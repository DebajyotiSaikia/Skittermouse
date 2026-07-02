#include "net/file_transfer.h"

namespace sm::net {

namespace {

void putU16(Bytes& b, uint16_t v) {
    b.push_back(static_cast<uint8_t>(v >> 8));
    b.push_back(static_cast<uint8_t>(v & 0xff));
}
void putU32(Bytes& b, uint32_t v) {
    for (int i = 3; i >= 0; --i) b.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xff));
}
void putU64(Bytes& b, uint64_t v) {
    for (int i = 7; i >= 0; --i) b.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xff));
}
uint16_t getU16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }
uint32_t getU32(const uint8_t* p) {
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v = (v << 8) | p[i];
    return v;
}
uint64_t getU64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v = (v << 8) | p[i];
    return v;
}

} // namespace

Bytes encodeFilePromiseMeta(const std::vector<FileEntry>& files) {
    Bytes b;
    putU32(b, static_cast<uint32_t>(files.size()));
    for (const auto& f : files) {
        putU16(b, static_cast<uint16_t>(f.name.size()));
        b.insert(b.end(), f.name.begin(), f.name.end());
        putU64(b, f.size);
    }
    return b;
}

bool decodeFilePromiseMeta(const uint8_t* data, std::size_t len, std::vector<FileEntry>& out) {
    out.clear();
    if (len < 4) return false;
    std::size_t pos = 0;
    uint32_t count = getU32(data);
    pos = 4;
    for (uint32_t i = 0; i < count; ++i) {
        if (pos + 2 > len) return false;
        uint16_t nl = getU16(data + pos);
        pos += 2;
        if (pos + nl + 8 > len) return false;
        FileEntry e;
        e.name.assign(reinterpret_cast<const char*>(data + pos), nl);
        pos += nl;
        e.size = getU64(data + pos);
        pos += 8;
        out.push_back(std::move(e));
    }
    return true;
}

Bytes encodeFileChunk(uint32_t index, uint64_t offset, const uint8_t* data, std::size_t len) {
    Bytes b;
    putU32(b, index);
    putU64(b, offset);
    if (len) b.insert(b.end(), data, data + len);
    return b;
}

bool decodeFileChunk(const uint8_t* data, std::size_t len, uint32_t& index, uint64_t& offset,
                     Bytes& chunk) {
    if (len < 12) return false;
    index = getU32(data);
    offset = getU64(data + 4);
    chunk.assign(data + 12, data + len);
    return true;
}

} // namespace sm::net
