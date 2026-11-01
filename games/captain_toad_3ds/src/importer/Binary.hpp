// Byte buffers, bounds-checked little-endian (and a few big-endian) reads, SHA-256 and file output
// for the importer. Every read is range-checked, so a damaged ROM image fails with an ImportError
// instead of reading out of bounds.
#pragma once
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace captain_toad {

using Bytes = std::vector<uint8_t>;

struct ImportError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

inline void checkRange(const Bytes& b, size_t offset, size_t count, const char* what = "read") {
    if (offset > b.size() || count > b.size() - offset) {
        throw ImportError(std::string("Out-of-range ") + what);
    }
}

template <class T>
T readLE(const Bytes& b, size_t offset) {
    checkRange(b, offset, sizeof(T));
    T v;
    std::memcpy(&v, b.data() + offset, sizeof(T));
    return v;
}
inline uint8_t u8(const Bytes& b, size_t o) {
    checkRange(b, o, 1);
    return b[o];
}

inline uint16_t u16(const Bytes& b, size_t o) {
    return readLE<uint16_t>(b, o);
}

inline int16_t s16(const Bytes& b, size_t o) {
    return readLE<int16_t>(b, o);
}

inline uint32_t u32(const Bytes& b, size_t o) {
    return readLE<uint32_t>(b, o);
}

inline int32_t s32(const Bytes& b, size_t o) {
    return readLE<int32_t>(b, o);
}

inline uint64_t u64(const Bytes& b, size_t o) {
    return readLE<uint64_t>(b, o);
}

inline float f32(const Bytes& b, size_t o) {
    return readLE<float>(b, o);
}

inline uint32_t u24(const Bytes& b, size_t o) {
    checkRange(b, o, 3);
    return b[o] | b[o + 1] << 8 | b[o + 2] << 16;
}

inline uint32_t be32(const Bytes& b, size_t o) {
    checkRange(b, o, 4);
    return uint32_t(b[o]) << 24 | b[o + 1] << 16 | b[o + 2] << 8 | b[o + 3];
}

inline uint16_t be16(const Bytes& b, size_t o) {
    checkRange(b, o, 2);
    return uint16_t(b[o] << 8 | b[o + 1]);
}

inline Bytes slice(const Bytes& b, size_t offset, size_t count) {
    checkRange(b, offset, count, "slice");
    return Bytes(b.begin() + long(offset), b.begin() + long(offset + count));
}

// NUL-terminated string at `offset` (bounded by the buffer).
inline std::string cstring(const Bytes& b, size_t offset) {
    checkRange(b, offset, 1, "string");
    const auto* p = b.data() + offset;
    const void* end = std::memchr(p, 0, b.size() - offset);
    if (!end) {
        throw ImportError("Unterminated string");
    }
    return std::string(reinterpret_cast<const char*>(p), static_cast<const uint8_t*>(end) - p);
}

inline void appendFloats(Bytes& out, std::initializer_list<float> values) {
    for (float v : values) {
        uint8_t raw[4];
        std::memcpy(raw, &v, 4);
        out.insert(out.end(), raw, raw + 4);
    }
}
inline void appendU32(Bytes& out, uint32_t v) {
    out.insert(out.end(), {uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24)});
}

std::string sha256Hex(const uint8_t* data, size_t size);
inline std::string sha256Hex(const Bytes& b) {
    return sha256Hex(b.data(), b.size());
}

void writeFile(const std::filesystem::path& path, const Bytes& data);
// Whole file; throws ImportError when it cannot be read.
Bytes readFile(const std::filesystem::path& path);
void writeFile(const std::filesystem::path& path, const std::string& text);

}   // namespace captain_toad
