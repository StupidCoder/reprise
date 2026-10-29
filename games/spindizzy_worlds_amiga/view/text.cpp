#include "spindizzy_worlds_amiga/view/text.hpp"

namespace spindizzy::view {
namespace {

constexpr std::uint32_t usual_bank = 0x1B400;  // $184F2
constexpr int bank_step = 0x680;               // 26 glyphs per bank code
constexpr int glyph_size = 64;                 // 16 rows × 4 plane bytes
constexpr int width_columns = 0x28;            // $37AE
constexpr int last_row = 0xC7;                 // $1855C

std::uint8_t pixel(const Canvas& c, int x, int y) { return c.at(x, y); }
void set(Canvas& c, int x, int y, std::uint8_t v) {
    if (x >= 0 && y >= 0 && x < c.width && y < c.height) c.pixels[std::size_t(y * c.width + x)] = v;
}

// The four plane bits of a fill colour set in one plane byte at a time,
// as $18582 writes them for an odd column with movep (an instruction that
// writes every other byte: on the Atari ST, whose screen interleaves the
// planes by words, that is one byte per plane; on the Amiga the four
// bytes land in plane 0 at four columns two apart). Kept.
void plane_byte(Canvas& c, int column, int y, int plane, bool set_bits) {
    for (int b = 0; b < 8; ++b) {
        const int x = column * 8 + b;
        if (x < 0 || x >= c.width || y < 0 || y >= c.height) continue;
        std::uint8_t v = pixel(c, x, y);
        v = set_bits ? std::uint8_t(v | 1 << plane) : std::uint8_t(v & ~(1 << plane));
        set(c, x, y, v);
    }
}

} // namespace

void TextPrinter::reset() {
    bank_ = usual_bank;
    column = row = 0;
    column_offset_ = 0;
    height_ = 7; line_ = 9; skip_ = 4; // $184FE calls $183A6: the small size
    transparent = true;
    to_hidden = true;
}

void TextPrinter::glyph(Canvas& c, int code, int at_column, int at_row) const {
    // $18430: the glyph's 16 rows of 4 plane bytes; transparent text keeps
    // what is behind where the mask ($677E8, made by $18282 from the usual
    // bank whatever bank is chosen) says the glyph is colour 0.
    const std::uint32_t source = bank_ + std::uint32_t(code * glyph_size);
    const std::uint32_t mask_source = usual_bank + std::uint32_t(code * glyph_size);
    const int x0 = ((at_column + column_offset_) & 0x3F) * 8;
    std::uint32_t offset = 0;
    const int rows = height_ + 1 + (skip_ ? 1 : 0);
    for (int r = 0; r < rows; ++r) {
        // The small size reads every other row, then the last row again.
        if (skip_ && r == height_ + 1) offset -= std::uint32_t(skip_);
        const std::uint32_t at = source + offset;
        const std::uint32_t mask_at = mask_source + offset;
        for (int b = 0; b < 8; ++b) {
            int colour = 0, behind = 0;
            for (int p = 0; p < 4; ++p) {
                colour |= ((main_.u8(at + std::uint32_t(p)) >> (7 - b)) & 1) << p;
                behind |= ((main_.u8(mask_at + std::uint32_t(p)) >> (7 - b)) & 1) << p;
            }
            const int x = x0 + b, y = at_row + r;
            if (x >= c.width || y >= c.height) continue;
            if (transparent) {
                const std::uint8_t old = behind == 0 ? pixel(c, x, y) : 0;
                set(c, x, y, std::uint8_t(old | colour));
            } else {
                set(c, x, y, std::uint8_t(colour));
            }
        }
        offset += 4 + std::uint32_t(skip_);
    }
}

void TextPrinter::put(Canvas& c, std::uint8_t byte) {
    // $1854A: wrap, then the byte; the cursor moves one column after it
    // (codes take that step back where they should not move).
    if (column >= width_columns) {
        column = 0;
        row += line_;
    }
    if (height_ + row >= last_row) row = 0;
    if (byte > 0x20) {
        glyph(c, byte - 0x20, column, row);
    } else if (byte == 0x20) {
        if (!transparent) glyph(c, 0, column, row);
    } else {
        switch (byte) {
        case 0x00: case 0x10: height_ = 15; line_ = 16; skip_ = 0; --column; break; // $18392
        case 0x11: height_ = 7; line_ = 9; skip_ = 4; --column; break;              // $183A6
        case 0x01: case 0x02: case 0x03: case 0x04: case 0x05: case 0x06: case 0x07:
            // $18348: the code is still × 4 from the table jump: (code × 4
            // − 4) × $680, added as a signed word. Code 1 is the usual bank,
            // 2 the orange one at $1CE00; higher codes point past the fonts.
            bank_ = std::uint32_t(std::int32_t(usual_bank) + std::int16_t(((byte * 4 - 4) * bank_step) & 0xFFFF));
            --column;
            break;
        case 0x08: column = column - 2 < 0 ? -1 : column - 2; break; // $1835E
        case 0x0A: row += line_; --column; break;                    // $18378
        case 0x0B: --column; row = row - line_ < 0 ? 0 : row - line_; break; // $1836A
        case 0x0D: column = -1; row += line_; break;                 // $18380
        case 0x12: transparent = true; --column; break;
        case 0x13: transparent = false; --column; break;
        case 0x1C: to_hidden = false; --column; break;
        case 0x1D: to_hidden = true; --column; break;
        default: break; // $09, $0C and $15-$1A: one column on
        }
    }
    ++column;
}

void TextPrinter::print(Canvas& c, const std::uint8_t* text) {
    // $18518 with the codes that read further bytes.
    for (; *text; ++text) {
        const std::uint8_t byte = *text;
        if (column >= width_columns) { column = 0; row += line_; }
        if (height_ + row >= last_row) row = 0;
        switch (byte) {
        case 0x0E: column += text[1]; ++text; continue;           // $18386
        case 0x0F: row += text[1]; ++text; continue;              // $1838C
        case 0x14: line_ = (line_ & 0xFF00) | text[1]; ++text; continue; // $183CC
        case 0x1B: column = 0; column_offset_ = text[1]; ++text; continue; // $183D4
        case 0x1F: column = text[1]; row = text[2]; text += 2; continue;  // $1841E
        case 0x1E:                                                  // $183F0
            // The box's corner stays the cursor (d4/d5 are not restored).
            box(c, text[1], text[2], text[3], text[4], text[5]);
            column = text[2];
            row = text[3];
            text += 5;
            continue;
        default: put(c, byte); break;
        }
    }
    column_offset_ = 0; // $18544
}

void TextPrinter::print(Canvas& c, std::uint32_t address) {
    std::vector<std::uint8_t> text;
    for (std::uint32_t a = address;; ++a) {
        const std::uint8_t byte = main_.u8(a);
        text.push_back(byte);
        if (byte == 0) break;
        // Codes with argument bytes may carry zeros.
        int arguments = byte == 0x0E || byte == 0x0F || byte == 0x14 || byte == 0x1B ? 1 : byte == 0x1F ? 2 : byte == 0x1E ? 5 : 0;
        while (arguments-- > 0) text.push_back(main_.u8(++a));
    }
    print(c, text.data());
}

void TextPrinter::number(Canvas& c, int value) {
    // $18826: the last digit at the cursor, the others to its left.
    value &= 0xFFFF;
    do {
        put(c, std::uint8_t('0' + value % 10));
        column -= 2;
        value /= 10;
    } while (value != 0);
}

void TextPrinter::box(Canvas& c, int style, int x, int y, int w, int h) {
    // $18582. The fill: colour = the style's low nibble, words from the
    // even column at or after x; an odd x first gets the plane bytes the
    // ST way (see plane_byte); a fill of fewer than two words of width is
    // skipped (dbra count 0), and a box of one row draws nothing.
    if (h <= 1) return;
    const int fill = style & 15, border = style >> 4 & 15;
    const int first = (x + 1) & 0x7E;
    int width = w;
    if (x & 1) {
        for (int r = 0; r < h; ++r)
            for (int p = 0; p < 4; ++p) plane_byte(c, first - 7 + 2 * p, y + r, 0, (fill >> p) & 1);
        --width;
    }
    const int words = width / 2;
    const bool filled = words - 1 != 0;
    if (filled)
        for (int r = 0; r < h; ++r)
            for (int px = 0; px < words * 16; ++px) set(c, first * 8 + px, y + r, std::uint8_t(fill));
    if (border == 0 || x == 0 || y == 0 || x + w >= width_columns || y + h - 1 >= 200) return;
    // The border: lines above and below (below: after the fill's last row,
    // or on its first if nothing was filled), a pixel column each side.
    const int bottom = filled ? y + h : y;
    const int line_words = (w + 1) / 2;
    for (int px = 0; px < line_words * 16; ++px) {
        set(c, first * 8 + px, y - 1, std::uint8_t(border));
        set(c, first * 8 + px, bottom, std::uint8_t(border));
    }
    const int left = x * 8 - 1;
    const int right = first * 8 + line_words * 16 - ((w & 1) ? 16 : 0);
    for (int r = y - 1; r <= y + h; ++r) {
        set(c, left, r, std::uint8_t(border));
        set(c, right, r, std::uint8_t(border));
    }
}

} // namespace spindizzy::view
