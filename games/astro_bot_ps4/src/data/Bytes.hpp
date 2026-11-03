// Little-endian access to game file bytes, and whole-file loading.
#pragma once

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace beacon::data {

using Bytes = std::vector<std::uint8_t>;
using ByteView = std::span<const std::uint8_t>;

template <typename T>
T read(ByteView bytes, std::size_t offset) {
    T value{};
    if (offset + sizeof(T) <= bytes.size()) {
        std::memcpy(&value, bytes.data() + offset, sizeof(T));
    }
    return value;
}

inline std::uint32_t u32(ByteView bytes, std::size_t offset) {
    return read<std::uint32_t>(bytes, offset);
}
inline std::uint16_t u16(ByteView bytes, std::size_t offset) {
    return read<std::uint16_t>(bytes, offset);
}

// Reads a whole file; returns false (and leaves `out` empty) if it cannot be read.
bool loadFile(const std::filesystem::path& path, Bytes& out);

}   // namespace data
