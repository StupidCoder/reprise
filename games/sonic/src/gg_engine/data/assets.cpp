// Extraction and storage of the asset pack.

#include "gg_engine/data/assets.hpp"
#include <stdexcept>
#include <string>

namespace gg_engine {

namespace {

constexpr std::uint32_t tile_key(unsigned bank, unsigned address, bool mirrored) {
    return (bank << 17) | (mirrored ? 0x10000u : 0u) | (address & 0xFFFF);
}

void put16(Bytes& out, unsigned v) {
    out.push_back(std::uint8_t(v));
    out.push_back(std::uint8_t(v >> 8));
}

void put32(Bytes& out, std::uint32_t v) {
    put16(out, v & 0xFFFF);
    put16(out, v >> 16);
}

struct Reader {
    const Bytes& in;
    std::size_t pos{};

    std::uint8_t u8() {
        if (pos >= in.size()) {
            throw std::runtime_error("Asset pack file is truncated");
        }
        return in[pos++];
    }

    unsigned u16() {
        const unsigned lo = u8();
        return lo | (unsigned(u8()) << 8);
    }

    std::uint32_t u32() {
        const std::uint32_t lo = u16();
        return lo | (std::uint32_t(u16()) << 16);
    }

    void expect(const CartridgeInfo& info, const char* magic) {
        for (const char* p = magic; *p; ++p) {
            if (u8() != std::uint8_t(*p)) {
                throw std::runtime_error(std::string("Not a ") + info.short_name +
                                         " asset pack file");
            }
        }
    }
};

Bytes magic_bytes(const char* magic) {
    return Bytes(magic, magic + std::char_traits<char>::length(magic));
}

void clear_banks(std::array<Bytes, 32>& banks, std::array<std::vector<bool>, 32>& known) {
    for (unsigned b = 0; b < 32; ++b) {
        banks[b].assign(0x4000, 0);
        known[b].assign(0x4000, false);
    }
}

} // namespace

Assets Assets::extract(const CartridgeInfo& info, const Rom& rom) {
    Assets a(info);
    clear_banks(a.banks_, a.known_);
    for (const auto& r : info.data_regions) {
        for (unsigned i = 0; i < r.length; ++i) {
            const unsigned o = r.offset + i;
            a.banks_[r.bank][o] = rom.at(std::size_t(r.bank) * 0x4000 + o);
            a.known_[r.bank][o] = true;
        }
    }
    const ByteSource source = [&rom](unsigned b, unsigned address) {
        return rom.byte(b, address);
    };
    for (const auto& s : info.tile_sources) {
        a.tiles_[tile_key(s.bank, s.address, s.mirrored)] =
            decode_tiles(source, s.bank, s.address, s.mirrored);
    }
    return a;
}

void Assets::save(const std::filesystem::path& directory) const {
    Bytes data = magic_bytes(info_->data_magic);
    for (unsigned b = 0; b < 32; ++b) {
        // Runs of data bytes: bank, offset, length, bytes.
        unsigned o = 0;
        while (o < 0x4000) {
            if (!known_[b][o]) {
                ++o;
                continue;
            }
            unsigned end = o;
            while (end < 0x4000 && known_[b][end]) {
                ++end;
            }
            data.push_back(std::uint8_t(b));
            put16(data, o);
            put32(data, end - o);
            data.insert(data.end(), banks_[b].begin() + o, banks_[b].begin() + end);
            o = end;
        }
    }
    write_file(directory / "data.bin", data);

    if (!info_->tiles_magic) {
        return;
    }
    Bytes tiles = magic_bytes(info_->tiles_magic);
    for (const auto& [key, bytes] : tiles_) {
        put32(tiles, key);
        put32(tiles, std::uint32_t(bytes.size()));
        tiles.insert(tiles.end(), bytes.begin(), bytes.end());
    }
    write_file(directory / "tiles.bin", tiles);
}

Assets Assets::load(const CartridgeInfo& info, const std::filesystem::path& directory) {
    Assets a(info);
    clear_banks(a.banks_, a.known_);
    const auto data = read_file(directory / "data.bin");
    Reader r{data};
    r.expect(info, info.data_magic);
    while (r.pos < data.size()) {
        const unsigned bank = r.u8();
        const unsigned offset = r.u16();
        const std::uint32_t length = r.u32();
        if (bank >= 32 || offset + length > 0x4000) {
            throw std::runtime_error("Asset pack data range is invalid");
        }
        for (std::uint32_t i = 0; i < length; ++i) {
            a.banks_[bank][offset + i] = r.u8();
            a.known_[bank][offset + i] = true;
        }
    }
    if (!info.tiles_magic) {
        return a;
    }
    const auto tiles = read_file(directory / "tiles.bin");
    Reader t{tiles};
    t.expect(info, info.tiles_magic);
    while (t.pos < tiles.size()) {
        const std::uint32_t key = t.u32();
        const std::uint32_t length = t.u32();
        if (length > 0x4000 || length % 32) {
            throw std::runtime_error("Asset pack tile block is invalid");
        }
        Bytes bytes(length);
        for (auto& b : bytes) {
            b = t.u8();
        }
        a.tiles_[key] = std::move(bytes);
    }
    return a;
}

std::uint8_t Assets::byte(unsigned bank, unsigned address) const {
    const unsigned o = address & 0x3FFF;
    if (bank >= 32 || !known_[bank][o]) {
        throw std::runtime_error("Read of unknown cartridge data at bank " + std::to_string(bank) +
                                 " address " + std::to_string(address));
    }
    return banks_[bank][o];
}

Bytes Assets::bytes(unsigned bank, unsigned address, std::size_t count) const {
    Bytes out(count);
    for (std::size_t i = 0; i < count; ++i) {
        out[i] = byte(bank, unsigned(address + i));
    }
    return out;
}

const Bytes& Assets::tiles(unsigned bank, unsigned address, bool mirrored) const {
    const auto key = tile_key(bank, address, mirrored);
    if (const auto it = tiles_.find(key); it != tiles_.end()) {
        return it->second;
    }
    if (info_->tiles_magic) {
        throw std::runtime_error("Tile block not in the asset pack: bank " + std::to_string(bank) +
                                 " address " + std::to_string(address));
    }
    const ByteSource source = [this](unsigned b, unsigned a) {
        return byte(b, a);
    };
    return tiles_[key] = decode_tiles(source, bank, address, mirrored);
}

} // namespace gg_engine
