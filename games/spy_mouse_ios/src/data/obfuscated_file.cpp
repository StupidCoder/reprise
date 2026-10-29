#include "data/obfuscated_file.hpp"

namespace doc::data {

std::optional<Bytes> decodeContainer(const Bytes& file, const Bytes& key) {
    if (key.empty()) return std::nullopt;
    auto le32 = [&](std::size_t o) {
        return std::uint32_t(file[o]) | (std::uint32_t(file[o + 1]) << 8) | (std::uint32_t(file[o + 2]) << 16) |
               (std::uint32_t(file[o + 3]) << 24);
    };
    bool hasHeader = file.size() >= 12 && le32(0) == 0xFFFFA329u;
    std::size_t start = hasHeader ? 12 : 0;
    Bytes body(file.begin() + start, file.end());
    for (std::size_t i = 0; i < body.size(); ++i) body[i] ^= key[i % key.size()];
    if (hasHeader) {
        std::uint32_t hash = 5381;  // djb2, as PFile accumulates while reading
        for (std::uint8_t b : body) hash = hash * 33 + b;
        if (hash != le32(4) || (hash ^ std::uint32_t(file.size())) != le32(8)) return std::nullopt;
    }
    return body;
}

std::optional<Bytes> decodeChunkTree(const Bytes& file, const Bytes& key) {
    if (file.size() < 2 || file[0] != 0x7B || file[1] != 0x02 || key.empty()) return std::nullopt;
    Bytes body(file.begin() + 2, file.end());
    std::uint8_t check = 0;
    for (std::size_t i = 0; i < body.size(); ++i) {
        body[i] ^= key[i % key.size()];
        check ^= body[i];
    }
    if (check != 0) return std::nullopt;
    return body;
}

}  // namespace doc::data
