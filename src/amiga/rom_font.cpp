#include "amiga/rom_font.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace amiga {
namespace {

constexpr std::size_t small_rom = 256u << 10, large_rom = 512u << 10;
constexpr std::uint32_t rom_end = 0x1000000;

std::uint16_t u16(std::span<const std::uint8_t> d, std::size_t at) {
    return std::uint16_t(d[at] << 8 | d[at + 1]);
}
std::uint32_t u32(std::span<const std::uint8_t> d, std::size_t at) {
    return std::uint32_t(u16(d, at)) << 16 | u16(d, at + 2);
}

// The ROM as the CPU sees it: a 512 KiB file holding a 256 KiB ROM twice
// is that ROM at $FC0000; otherwise the ROM ends at $1000000.
std::span<const std::uint8_t> rom_contents(std::span<const std::uint8_t> file) {
    if (file.size() == large_rom && std::equal(file.begin(), file.begin() + small_rom, file.begin() + small_rom)) {
        return file.first(small_rom);
    }
    return file;
}

// A TextFont's fields from tf_YSize on (Kickstart 1.x keeps its ROM fonts
// with the name pointer 6 bytes before tf_YSize, the system structure has
// it 10 bytes before; both are tried), checked against the ROM.
bool read_font(std::span<const std::uint8_t> rom, std::uint32_t base, std::size_t at, int height, RomFont& font) {
    if (at + 28 > rom.size()) {
        return false;
    }
    const int y_size = u16(rom, at), x_size = u16(rom, at + 4), baseline = u16(rom, at + 6);
    const std::uint8_t flags = rom[at + 3];
    const int low = rom[at + 12], high = rom[at + 13];
    const std::uint32_t data = u32(rom, at + 14), locations = u32(rom, at + 20);
    const int modulo = u16(rom, at + 18);
    const std::size_t glyph_count = std::size_t(high - low + 2);
    auto inside = [&](std::uint32_t address, std::size_t size) {
        return address >= base && address - base + size <= rom.size();
    };
    if (y_size != height || (flags & 1) == 0 || x_size == 0 || x_size > 32 || baseline >= y_size || high < low ||
        modulo == 0 || !inside(data, std::size_t(modulo) * std::size_t(y_size)) || !inside(locations, glyph_count * 4)) {
        return false;
    }
    font.height = y_size;
    font.width = x_size;
    font.baseline = baseline;
    font.low = low;
    font.high = high;
    font.modulo = modulo;
    const auto bitmap = rom.subspan(data - base, std::size_t(modulo) * std::size_t(y_size));
    font.bitmap.assign(bitmap.begin(), bitmap.end());
    font.glyphs.clear();
    for (std::size_t i = 0; i < glyph_count; ++i) {
        const RomFont::Glyph g{u16(rom, locations - base + i * 4), u16(rom, locations - base + i * 4 + 2)};
        if (g.offset + g.bits > modulo * 8) {
            return false;
        }
        font.glyphs.push_back(g);
    }
    return true;
}

} // namespace

const RomFont::Glyph& RomFont::glyph(unsigned char c) const {
    if (c < low || c > high) {
        return glyphs.back();
    }
    return glyphs[std::size_t(c - low)];
}

bool RomFont::pixel(const Glyph& g, int x, int y) const {
    if (x < 0 || x >= g.bits || y < 0 || y >= height) {
        return false;
    }
    const int bit = g.offset + x;
    return (bitmap[std::size_t(y * modulo + bit / 8)] >> (7 - bit % 8) & 1) != 0;
}

bool is_kickstart(std::span<const std::uint8_t> rom) {
    if (rom.size() != small_rom && rom.size() != large_rom) {
        return false;
    }
    const auto id = u16(rom, 0);
    return id == 0x1111 || id == 0x1114;
}

RomFont find_rom_font(std::span<const std::uint8_t> file, const std::string& name, int height) {
    if (file.size() >= 11 && std::memcmp(file.data(), "AMIROMTYPE1", 11) == 0) {
        throw std::runtime_error("This Kickstart ROM is encrypted; please use a decrypted ROM file.");
    }
    if (!is_kickstart(file)) {
        throw std::runtime_error("This is not a Kickstart ROM file.");
    }
    const auto rom = rom_contents(file);
    const std::uint32_t base = rom_end - std::uint32_t(rom.size());
    const std::string text = name + '\0';
    for (auto it = rom.begin(); (it = std::search(it, rom.end(), text.begin(), text.end())) != rom.end(); ++it) {
        const std::uint32_t address = base + std::uint32_t(it - rom.begin());
        // Every pointer to the name, as a TextFont's ln_Name.
        for (std::size_t at = 0; at + 4 <= rom.size(); at += 2) {
            if (u32(rom, at) != address) {
                continue;
            }
            RomFont font;
            for (const std::size_t gap : {6u, 10u}) {
                if (read_font(rom, base, at + gap, height, font)) {
                    return font;
                }
            }
        }
    }
    throw std::runtime_error("This Kickstart ROM has no " + name.substr(0, name.find('.')) + " " +
                             std::to_string(height) + " font.");
}

std::vector<std::uint8_t> save_font(const RomFont& font) {
    std::vector<std::uint8_t> out{'J', 'P', 'F', 'O', 'N', 'T', '1', 0};
    auto put16 = [&](int v) {
        out.push_back(std::uint8_t(v >> 8));
        out.push_back(std::uint8_t(v));
    };
    for (const int v : {font.height, font.width, font.baseline, font.low, font.high, font.modulo}) {
        put16(v);
    }
    out.insert(out.end(), font.bitmap.begin(), font.bitmap.end());
    for (const auto& g : font.glyphs) {
        put16(g.offset);
        put16(g.bits);
    }
    return out;
}

RomFont load_font(std::span<const std::uint8_t> file) {
    auto bad = [] { return std::runtime_error("The installed font file is damaged."); };
    if (file.size() < 20 || std::memcmp(file.data(), "JPFONT1", 8) != 0) {
        throw bad();
    }
    RomFont font;
    font.height = u16(file, 8);
    font.width = u16(file, 10);
    font.baseline = u16(file, 12);
    font.low = u16(file, 14);
    font.high = u16(file, 16);
    font.modulo = u16(file, 18);
    const std::size_t bitmap_size = std::size_t(font.modulo) * std::size_t(font.height);
    const std::size_t glyph_count = std::size_t(std::max(font.high - font.low + 2, 0));
    if (font.high < font.low || file.size() != 20 + bitmap_size + glyph_count * 4) {
        throw bad();
    }
    font.bitmap.assign(file.begin() + 20, file.begin() + 20 + std::ptrdiff_t(bitmap_size));
    for (std::size_t i = 0; i < glyph_count; ++i) {
        const std::size_t at = 20 + bitmap_size + i * 4;
        font.glyphs.push_back({u16(file, at), u16(file, at + 2)});
        if (font.glyphs.back().offset + font.glyphs.back().bits > font.modulo * 8) {
            throw bad();
        }
    }
    return font;
}

} // namespace amiga
