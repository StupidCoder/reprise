#pragma once
// Fonts of the 3DS adventures (.xf packs: FNT.bin + one glyph sheet).
//
// FNT.bin: "FNTC01", u32 1, u32 line height, u16 character count, u16
// 0xffff, ..., then three (u16 offset / 4, u16 count) pairs at 0x1c of
// method-word streams: glyph boxes (4 bytes: x offset, y offset, width,
// height), characters (8 bytes: u16 code point, u16 advance << 10 | box
// index, u32 sheet position: plane in bits 0-3, x in bits 4-13, y in bits
// 18-23) and a third table (empty in every font seen).
// The sheet is an RGBA5551 picture holding three independent 5-bit glyph
// planes in its red, green and blue channels (plane 0, 1, 2). Found from the
// data: rendering texts with these rules reproduces the game's words.
#include "graphics/ctr3d/raster.hpp"
#include "platform/image.hpp"
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace picarat::adventure56 {

class Pack;

struct Font {
    struct Glyph {
        int plane = 0, x = 0, y = 0;         // in the sheet
        int width = 0, height = 0;           // box
        int offsetX = 0, offsetY = 0;        // box position in the cell
        int advance = 0;
    };

    int lineHeight = 0;
    std::map<std::uint32_t, Glyph> glyphs;
    // One alpha texture per plane (white with the glyph coverage as alpha).
    std::shared_ptr<ctr3d::Texture> planes[3];

    const Glyph* glyph(std::uint32_t code) const;
    int advance(std::uint32_t code) const;
};

std::shared_ptr<Font> loadFont(const Pack& pack, const std::filesystem::path& folder);

// The games' text escapes in display code points: "{:o}" o umlaut, "{ss}"
// sharp s, "{'e}" e acute ...; markup (<CR>...</CR>) is left to the caller.
std::vector<std::uint32_t> decodeText(const std::string& text);

} // namespace picarat::adventure56
