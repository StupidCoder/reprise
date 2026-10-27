#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>

// The bitmap fonts a Kickstart ROM carries (graphics.library's TextFont
// structures: topaz 8 and topaz 9). Joyport ships no Kickstart contents; a
// game that draws with a ROM font reads it from a Kickstart ROM file the
// user provides, at import, and keeps it in its content folder in the small
// form below.
namespace amiga {

struct RomFont {
    int height{};   // tf_YSize
    int width{};    // tf_XSize, the advance of this fixed-width font
    int baseline{}; // tf_Baseline, the line Text() puts at the pen's y
    int low{}, high{}; // tf_LoChar, tf_HiChar
    int modulo{};   // bytes per bitmap row (tf_Modulo)
    std::vector<std::uint8_t> bitmap; // height rows of `modulo` bytes (tf_CharData)
    // Per character low..high plus the default glyph after them
    // (tf_CharLoc): the glyph's first bit in a row and its width in bits.
    struct Glyph {
        std::uint16_t offset{}, bits{};
    };
    std::vector<Glyph> glyphs;

    // The glyph Text() draws for a character: characters outside
    // low..high take the default glyph.
    const Glyph& glyph(unsigned char c) const;
    bool pixel(const Glyph& g, int x, int y) const;
};

// Whether the file is a Kickstart ROM image (256 or 512 KiB, the ROM's
// $1111/$1114 header). A 512 KiB file holding a 256 KiB ROM twice counts.
bool is_kickstart(std::span<const std::uint8_t> rom);

// The ROM's font of that name and height ("topaz.font", 9). Throws
// std::runtime_error with a message for the user when the file is not a
// usable Kickstart ROM or has no such font.
RomFont find_rom_font(std::span<const std::uint8_t> rom, const std::string& name, int height);

// The content-folder form ("JPFONT1": the fields above, big-endian).
std::vector<std::uint8_t> save_font(const RomFont&);
RomFont load_font(std::span<const std::uint8_t> file);

} // namespace amiga
