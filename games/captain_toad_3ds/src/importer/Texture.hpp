// PICA texture decoding (general formats): ETC1/ETC1A4, Morton-tiled pixel formats, BFLIM layout
// images and BFFNT glyph sheets. Output is RGBA8 with rows in stored order (first tile row
// first), the convention the viewer's texture upload expects.
#pragma once
#include "Binary.hpp"
#include "Json.hpp"

#include <map>
#include <string>
#include <vector>

namespace captain_toad {

// PICA format 12 (ETC1) or 13 (ETC1A4).
Bytes decodeEtc(const Bytes& raw, uint32_t w, uint32_t h, uint32_t format);
// PICA format 6 (HILO8 as used for normal maps): red/green from the two bytes.
Bytes decodeRg8(const Bytes& raw, uint32_t w, uint32_t h);
// Tiled pixel formats by CTR image format code (0 L8 ... 9 RGBA8).
Bytes decodeTiled(const Bytes& raw, uint32_t w, uint32_t h, int ctrFormat);
// Bits per pixel of a CTR image format code.
int ctrBitsPerPixel(int ctrFormat);
// PICA texture format -> CTR image format code (same pixel layouts); -1 when unknown.
int picaToCtr(uint32_t picaFormat);
// Any supported PICA texture format from a BCH record.
Bytes decodePica(const Bytes& raw, uint32_t w, uint32_t h, uint32_t format);
// Byte size of a PICA texture.
size_t picaSize(uint32_t w, uint32_t h, uint32_t format);
// 4-bit-per-channel tiled texture (PTCL code 0x1c).
Bytes decodeRgba4(const Bytes& raw, uint32_t w, uint32_t h);

struct Flim {
    uint32_t width, height, alignment, format, flags, size;
    uint32_t allocWidth, allocHeight;   // power-of-two raster (transposed when rotated)
    Bytes rgba;                         // the full stored raster
};
Flim decodeFlim(const Bytes& raw);

struct Font {
    json::Value info;   // name-independent fields written to the package font JSON
    json::Value sheet;
    std::map<int, int> cmap;
    struct Width {
        int left, glyph, chr;
    };
    std::map<int, Width> widths;
    Width defaultWidth;
    std::vector<Bytes> sheets;   // A8 sheets (coverage), or RGBA sheets when `color`
    bool color = false;          // colour glyphs (icon fonts with RGB formats)
};
Font readFont(const Bytes& raw);

}   // namespace captain_toad
