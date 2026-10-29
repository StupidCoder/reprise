// Extraction and storage of the asset pack.

#include "sonic_advance_gba/data/assets.hpp"
#include "sonic_advance_gba/data/code_regions.hpp"
#include <cstdio>
#include <stdexcept>
#include <string>

namespace sonic_advance_gba {

namespace {

constexpr char data_magic[] = "SADVDATA";

// The cartridge header (Nintendo logo, title, maker) is not game data.
constexpr std::uint32_t header_end = 0xC0;

void put32(Bytes& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(std::uint8_t(v >> (8 * i)));
    }
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

    std::uint32_t u32() {
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            v |= std::uint32_t(u8()) << (8 * i);
        }
        return v;
    }

    void expect(const char* magic) {
        for (const char* p = magic; *p; ++p) {
            if (u8() != std::uint8_t(*p)) {
                throw std::runtime_error("Not a Sonic Advance asset pack file");
            }
        }
    }
};

std::string hex(std::uint32_t v) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%08X", v);
    return buf;
}

} // namespace

Assets Assets::extract(const Rom& rom) {
    Assets a;
    a.data_.assign(rom_size, 0);
    a.known_.assign(rom_size, true);
    for (std::uint32_t o = 0; o < header_end; ++o) {
        a.known_[o] = false;
    }
    for (const auto& r : code_regions) {
        for (std::uint32_t addr = r.start; addr < r.end; ++addr) {
            a.known_[addr - cartridge_base] = false;
        }
    }
    for (std::size_t o = 0; o < rom_size; ++o) {
        if (a.known_[o]) {
            a.data_[o] = rom.at(o);
        }
    }
    return a;
}

void Assets::save(const std::filesystem::path& directory) const {
    write_file(directory / "data.bin", data_file());
}

Bytes Assets::data_file() const {
    // Runs of data bytes: offset, length, bytes.
    Bytes out(std::begin(data_magic), std::end(data_magic) - 1);
    std::size_t o = 0;
    while (o < rom_size) {
        if (!known_[o]) {
            ++o;
            continue;
        }
        std::size_t end = o;
        while (end < rom_size && known_[end]) {
            ++end;
        }
        put32(out, std::uint32_t(o));
        put32(out, std::uint32_t(end - o));
        out.insert(out.end(), data_.begin() + std::ptrdiff_t(o),
                   data_.begin() + std::ptrdiff_t(end));
        o = end;
    }
    return out;
}

Assets Assets::load(const std::filesystem::path& directory) {
    Assets a;
    a.data_.assign(rom_size, 0);
    a.known_.assign(rom_size, false);
    const auto in = read_file(directory / "data.bin");
    Reader r{in};
    r.expect(data_magic);
    while (r.pos < in.size()) {
        const std::uint32_t offset = r.u32();
        const std::uint32_t length = r.u32();
        if (offset > rom_size || length > rom_size - offset || length > in.size() - r.pos) {
            throw std::runtime_error("Asset pack data range is invalid");
        }
        std::copy(in.begin() + std::ptrdiff_t(r.pos), in.begin() + std::ptrdiff_t(r.pos + length),
                  a.data_.begin() + offset);
        for (std::uint32_t i = 0; i < length; ++i) {
            a.known_[offset + i] = true;
        }
        r.pos += length;
    }
    return a;
}

bool Assets::is_data(std::uint32_t address) const {
    const std::uint32_t o = address - cartridge_base;
    return address >= cartridge_base && o < rom_size && known_[o];
}

std::uint8_t Assets::u8(std::uint32_t address) const {
    if (!is_data(address)) {
        throw std::runtime_error("Read of unknown cartridge data at " + hex(address));
    }
    return data_[address - cartridge_base];
}

Bytes Assets::bytes(std::uint32_t address, std::size_t count) const {
    Bytes out(count);
    for (std::size_t i = 0; i < count; ++i) {
        out[i] = u8(address + std::uint32_t(i));
    }
    return out;
}

} // namespace sonic_advance_gba
