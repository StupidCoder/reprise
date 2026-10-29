#include "spindizzy_worlds_amiga/view/panel.hpp"

namespace spindizzy::view {
namespace {

constexpr int panel_top = 168;            // $149F4: buffer + $1A40
constexpr std::uint32_t panel_text = 0x149A8;
constexpr std::uint32_t world_names = 0x119EE; // 13 bytes each ($119E0)
constexpr std::uint32_t compass_frames = 0x41670;
constexpr std::uint32_t pillar_rows = 0x1E800;
constexpr std::uint32_t gauge_full = 0x1E8F0, gauge_empty = 0x1E8E0;
constexpr int left_pillar_x = -1;          // HSTART $80 ($11016) against DIWSTRT $81
constexpr int right_pillar_x = 0x1B0 - 0x81; // HSTART $1B0 ($1102C)

// One row of 16 pixels from four plane words.
void planar_row(Screen& s, int x, int y, std::uint16_t p0, std::uint16_t p1, std::uint16_t p2, std::uint16_t p3,
                int base) {
    for (int b = 0; b < 16; ++b) {
        const int bit = 15 - b;
        const int c = (p0 >> bit & 1) | (p1 >> bit & 1) << 1 | (p2 >> bit & 1) << 2 | (p3 >> bit & 1) << 3;
        s.put(x + b, y, std::uint8_t(base + c));
    }
}

} // namespace

void draw_panel(Screen& screen, const data::Assets& assets, int world) {
    const auto& swap = assets.select_swap;
    auto word = [&](std::size_t i) { return std::uint16_t(swap.at(2 * i) << 8 | swap.at(1 + 2 * i)); }; // $A(a5) + 4 = the file's start
    for (int y = 0; y < 32; ++y)
        for (int column = 0; column < 20; ++column) {
            const std::size_t i = std::size_t(y * 20 + column) * 4;
            planar_row(screen, column * 16, panel_top + y, word(i), word(i + 1), word(i + 2), word(i + 3), 0);
        }
    TextPrinter text(assets.main);
    text.print(screen, panel_text);
    if (world >= 0 && world < 0x20) text.print(screen, world_names + std::uint32_t(world * 13)); // $119D8
}

void draw_compass(Screen& screen, const data::Memory& main, int rotation) {
    const std::uint32_t frame = compass_frames + std::uint32_t(rotation & 3) * 0xA0;
    for (int y = 0; y < 10; ++y) {
        const std::uint32_t row = frame + std::uint32_t(y) * 16;
        for (int half = 0; half < 2; ++half) {
            const std::uint32_t at = row + std::uint32_t(half) * 2;
            planar_row(screen, 32 * 8 + half * 16, panel_top + 11 + y, main.u16(at), main.u16(at + 4), main.u16(at + 8),
                       main.u16(at + 12), 0);
        }
    }
}

void draw_clue(Screen& screen, const data::Memory& main, std::uint16_t word) {
    // The big box: 13 rows of a 32-pixel frame (mask long, four plane longs).
    std::uint32_t frame = 0;
    if (word & 0x2000) frame = 0x2C000 + ((word & 0x1000) ? 0 : 0x104) + std::uint32_t(word >> 8 & 7) * 0x208;
    else if (word & 0x8000) frame = 0x25AD4;
    for (int y = 0; y < 13; ++y)
        for (int b = 0; b < 32; ++b) {
            int colour = 0;
            if (frame)
                for (int p = 0; p < 4; ++p)
                    colour |= int(main.u32(frame + std::uint32_t(y) * 20 + 4 + std::uint32_t(p) * 4) >> (31 - b) & 1) << p;
            screen.put(32 + b, 180 + y, std::uint8_t(colour));
        }
    // The four small boxes.
    const int bits = (word & 0x4000) ? word : 0;
    int at = 0x1B12;
    for (int box = 0; box < 4; ++box) {
        at += std::int16_t(main.u16(0x164AE + std::uint32_t(box) * 4));
        const int picture = std::int16_t(main.u16(0x164B0 + std::uint32_t(box) * 4));
        const bool on = bits >> box & 1, lit = bits >> (box + 4) & 1;
        const std::uint32_t source = 0x418F0 + std::uint32_t(picture) + (lit ? 0x40 : 0);
        for (int y = 0; y < 8; ++y) {
            const std::uint32_t row = source + std::uint32_t(y) * 8;
            planar_row(screen, (at % 40) * 8, at / 40 + y, on ? main.u16(row) : 0, on ? main.u16(row + 2) : 0,
                       on ? main.u16(row + 4) : 0, on ? main.u16(row + 6) : 0, 0);
        }
        at += 8 * 40;
    }
}

void draw_pillars(Screen& screen, const data::Memory& main, int fuel_shown) {
    // Source rows of the 168: 0-7 the top, 8-135 the gauge, 136-155 from
    // $1E840, 156-167 from $1E880 ($10FC6).
    auto source_row = [&](int y) -> std::uint32_t {
        if (y < 8) return pillar_rows + std::uint32_t(y) * 8;
        if (y < 136) return 0; // the gauge
        if (y < 156) return pillar_rows + 0x40 + std::uint32_t(y - 136) * 8;
        return pillar_rows + 0x80 + std::uint32_t(y - 156) * 8;
    };
    for (int side = 0; side < 2; ++side) {
        const int x = side == 0 ? left_pillar_x : right_pillar_x;
        const int shown = side == 0 ? std::min(fuel_shown, 0x80) : std::max(fuel_shown - 0x80, 0);
        for (int y = 0; y < 168; ++y) {
            std::uint32_t at = source_row(y);
            const bool gauge = at == 0;
            if (gauge) at = y > 0x87 - shown ? gauge_full : gauge_empty; // row $87 − level fills next
            const std::uint16_t p0 = main.u16(at), p1 = main.u16(at + 2), p2 = main.u16(at + 4), p3 = main.u16(at + 6);
            // Attached sprites: colour 0 in the source becomes 8 (sprite
            // colour 24, $10F96); the gauge's rows are copied as they are
            // ($10FBA, $10E96), their colour 0 stays transparent.
            if (gauge) {
                for (int b = 0; b < 16; ++b) {
                    const int bit = 15 - b;
                    const int c = (p0 >> bit & 1) | (p1 >> bit & 1) << 1 | (p2 >> bit & 1) << 2 | (p3 >> bit & 1) << 3;
                    if (c) screen.put(x + b, y, std::uint8_t(16 + c));
                }
            } else {
                planar_row(screen, x, y, p0, p1, p2, std::uint16_t(p3 | ~(p0 | p1 | p2 | p3)), 16);
            }
        }
    }
}

void set_level_palette(Screen& screen, const data::Graphics& graphics) {
    for (std::size_t i = 0; i < 16; ++i) {
        screen.palette[i] = graphics.palettes[0][i];
        screen.palette[16 + i] = graphics.palettes[0][i];
    }
    screen.palette[24] = 0;
}

} // namespace spindizzy::view
