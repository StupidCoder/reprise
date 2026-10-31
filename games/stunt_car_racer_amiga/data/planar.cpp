#include "stunt_car_racer_amiga/data/planar.hpp"

namespace stuntcar::data {
namespace {

constexpr int row_bytes = 40, planes = 4;

} // namespace

std::vector<std::uint8_t> unpack_rows(const Program& program, std::uint32_t at) {
    std::vector<std::uint8_t> pixels(std::size_t(screen_width * screen_height));
    std::uint8_t row[row_bytes];
    for (int y = 0; y < screen_height; ++y) {
        for (int p = 0; p < planes; ++p) {
            int filled = 0;
            while (filled < row_bytes) {
                const int code = program.u8(at++);
                if (code < 128) {
                    for (int k = 0; k <= code; ++k, ++at)
                        if (filled < row_bytes) row[filled++] = program.u8(at);
                } else if (code > 128) {
                    const auto value = program.u8(at++);
                    for (int k = 0; k < 257 - code; ++k)
                        if (filled < row_bytes) row[filled++] = value;
                }
            }
            for (int x = 0; x < screen_width; ++x)
                if (row[x >> 3] & (0x80 >> (x & 7))) pixels[std::size_t(y * screen_width + x)] |= std::uint8_t(1 << p);
        }
    }
    return pixels;
}

std::vector<std::uint8_t> read_interleaved(const Program& program, std::uint32_t at) {
    std::vector<std::uint8_t> pixels(std::size_t(screen_width * screen_height));
    for (int y = 0; y < screen_height; ++y)
        for (int column = 0; column < screen_width / 16; ++column) {
            const auto word_at = at + std::uint32_t(y * 160 + column * 8);
            for (int p = 0; p < planes; ++p) {
                const auto word = program.u16(word_at + std::uint32_t(p) * 2);
                for (int b = 0; b < 16; ++b)
                    if (word & (0x8000 >> b)) pixels[std::size_t(y * screen_width + column * 16 + b)] |= std::uint8_t(1 << p);
            }
        }
    return pixels;
}

Colour widen3(std::uint16_t word) {
    const auto gun = [](int c) { return std::uint8_t(c ? (c * 2 + 1) * 17 : 0); };
    return {gun(word >> 8 & 7), gun(word >> 4 & 7), gun(word & 7)};
}

std::vector<Colour> read_palette(const Program& program, std::uint32_t at) {
    std::vector<Colour> palette;
    for (int i = 0; i < 16; ++i) palette.push_back(widen3(program.u16(at + std::uint32_t(i) * 2)));
    return palette;
}

Font read_font(const Program& program) {
    constexpr std::uint32_t font_at = 0x1FE82;
    Font font{};
    for (std::size_t c = 0; c < font.size(); ++c)
        for (std::size_t row = 0; row < 8; ++row) font[c][row] = program.u8(font_at + std::uint32_t(c * 8 + row));
    const auto glyph = [&](char c) -> auto& { return font[std::size_t(c - ' ')]; };
    glyph('-')[3] = 0x7E;
    glyph('.')[5] = 0x00, glyph('.')[6] = 0x10;
    glyph('_') = {0, 0, 0, 0, 0, 0, 0x7E, 0};
    return font;
}

} // namespace stuntcar::data
