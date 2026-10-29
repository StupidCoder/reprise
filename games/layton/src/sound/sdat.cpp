// The SDK sound archive. See sdat.hpp.
#include "sound/sdat.hpp"
#include <stdexcept>

namespace picarat::sdat {

Archive::Archive(Bytes data) : data_(std::move(data)) {
    if (data_.size() < 0x40 || read32(data_, 0) != 0x54414453) { // "SDAT"
        throw std::runtime_error("not a sound archive");
    }
    info_ = read32(data_, 0x18);
    const std::size_t fat = read32(data_, 0x20);
    const std::uint32_t count = read32(data_, fat + 8);
    files_.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t entry = fat + 12 + std::size_t(i) * 16;
        const std::uint32_t offset = read32(data_, entry), size = read32(data_, entry + 4);
        if (std::size_t(offset) + size > data_.size()) {
            throw std::runtime_error("sound archive file out of range");
        }
        files_.emplace_back(offset, size);
    }
}

std::optional<std::size_t> Archive::record(unsigned table, std::uint32_t id) const {
    // INFO: eight table offsets after the block header; a table is a count
    // and record offsets, all relative to the block (0: no record).
    const std::size_t at = info_ + read32(data_, info_ + 8 + table * 4);
    if (id >= read32(data_, at)) {
        return std::nullopt;
    }
    const std::uint32_t offset = read32(data_, at + 4 + std::size_t(id) * 4);
    if (offset == 0) {
        return std::nullopt;
    }
    return info_ + offset;
}

std::optional<SequenceInfo> Archive::sequence(std::uint32_t id) const {
    const auto at = record(0, id);
    if (!at) {
        return std::nullopt;
    }
    SequenceInfo s;
    s.file = read32(data_, *at);
    s.bank = read16(data_, *at + 4);
    s.volume = data_[*at + 6];
    s.channelPriority = data_[*at + 7];
    s.playerPriority = data_[*at + 8];
    s.player = data_[*at + 9];
    return s;
}

std::optional<std::uint32_t> Archive::sequenceArchive(std::uint32_t id) const {
    const auto at = record(1, id);
    return at ? std::optional(read32(data_, *at)) : std::nullopt;
}

std::optional<BankInfo> Archive::bank(std::uint32_t id) const {
    const auto at = record(2, id);
    if (!at) {
        return std::nullopt;
    }
    BankInfo b;
    b.file = read32(data_, *at);
    for (unsigned i = 0; i < 4; ++i) {
        b.waveArchives[i] = read16(data_, *at + 4 + i * 2);
    }
    return b;
}

std::optional<std::uint32_t> Archive::waveArchive(std::uint32_t id) const {
    // Bit 24 asks for loading single samples; the game loads them whole.
    const auto at = record(3, id);
    return at ? std::optional(read32(data_, *at) & 0xffffff) : std::nullopt;
}

std::optional<PlayerInfo> Archive::player(std::uint32_t id) const {
    const auto at = record(4, id);
    if (!at) {
        return std::nullopt;
    }
    return PlayerInfo{data_[*at], read16(data_, *at + 2), read32(data_, *at + 4)};
}

std::vector<GroupItem> Archive::group(std::uint32_t id) const {
    std::vector<GroupItem> items;
    if (const auto at = record(5, id)) {
        const std::uint32_t count = read32(data_, *at);
        for (std::uint32_t i = 0; i < count; ++i) {
            const std::size_t e = *at + 4 + std::size_t(i) * 8;
            items.push_back({GroupItem::Kind(data_[e]), data_[e + 1], read32(data_, e + 4)});
        }
    }
    return items;
}

std::span<const std::uint8_t> Archive::file(std::uint32_t id) const {
    if (id >= files_.size()) {
        return {};
    }
    return std::span(data_).subspan(files_[id].first, files_[id].second);
}

std::span<const std::uint8_t> sequenceCode(std::span<const std::uint8_t> file) {
    // The DATA block's word at +0x18 is where the byte code starts.
    if (file.size() < 0x1c) {
        return {};
    }
    const std::uint32_t at = read32(file, 0x18);
    return at <= file.size() ? file.subspan(at) : std::span<const std::uint8_t>{};
}

std::optional<ArchivedSequence> archivedSequence(std::span<const std::uint8_t> file,
                                                 std::uint32_t index) {
    // 02098e48: a count at +0x1c, 12-byte entries from +0x20; offset -1
    // marks an empty entry.
    if (file.size() < 0x20 || index >= read32(file, 0x1c)) {
        return std::nullopt;
    }
    const std::size_t e = 0x20 + std::size_t(index) * 12;
    if (e + 12 > file.size() || read32(file, e) == 0xffffffff) {
        return std::nullopt;
    }
    return ArchivedSequence{read32(file, e), read16(file, e + 4), file[e + 6],
                            file[e + 7],     file[e + 8],         file[e + 9]};
}

} // namespace picarat::sdat
