#include "mindwalker_amiga/landscape/view_cells.hpp"
#include <cstdlib>

namespace mindwalker::landscape {
namespace {

// A's tables (research layout, .analysis/mindwalker_amiga/game.md).
constexpr std::uint32_t platform_kinds = 0x20984C;   // kind by platform height (5 words)
constexpr std::uint32_t find_bits = 0x209856;        // object bit of the two finds (2 words)
constexpr std::uint32_t marker_masks = 0x20985A;     // by stage / 7 (8 words)
constexpr std::uint32_t empty_masks = 0x20986A;      // by stage / 7 (8 words)
constexpr std::uint32_t red_shifts = 0x20981C, green_shifts = 0x20982C, blue_shifts = 0x20983C; // 4 longs each
constexpr std::uint32_t colour_table = 0x2004AC;

using Grid = std::array<std::array<std::uint16_t, 6>, 6>; // [x][y] over the view and a border of one

} // namespace

std::array<std::uint16_t, 32> stored_colours(const amiga::LoadedImage& a) {
    std::array<std::uint16_t, 32> colours{};
    for (std::uint32_t i = 0; i < 32; ++i) colours[i] = a.u16(colour_table + i * 2);
    return colours;
}

ViewCells build_view(const world::World& world, const amiga::LoadedImage& a, std::uint16_t x, std::uint16_t y,
                     const ViewInputs& in, const std::array<std::uint16_t, 32>& colours) {
    ViewCells view;
    view.colours = colours;
    const auto& keys = world.keys();
    const int stage_band = in.stage / 7;
    const bool from_stage_7 = in.stage >= 7, from_stage_14 = in.stage >= 14;
    const std::uint16_t stage_marker = (stage_band & 1) == 0 ? 0xA00 : 0x800;
    const std::uint16_t marker_mask = a.u16(marker_masks + std::uint32_t(stage_band * 2));
    const std::uint16_t empty_mask = a.u16(empty_masks + std::uint32_t(stage_band * 2));
    // 16-bit, as the original keeps them.
    const std::uint16_t x0 = std::uint16_t(x - 1), y0 = std::uint16_t(y - 1);

    // Rows and columns of the 6 x 6 window: each row's chain value plus each
    // column's (16-bit sums); the kind word from both chains' level-1 words.
    Grid height{}, mix{}, kind{};
    for (int i = 0; i < 6; ++i) {
        const auto row = world.rows().at(std::uint16_t((x0 + i) & 0xFFF));
        for (int j = 0; j < 6; ++j) {
            height[i][j] = row[0].a;
            mix[i][j] = row[0].b;
            kind[i][j] = row[1].b;
        }
    }
    for (int j = 0; j < 6; ++j) {
        const auto column = world.columns().at(std::uint16_t((y0 + j) & 0xFFF));
        for (int i = 0; i < 6; ++i) {
            height[i][j] = std::uint16_t(height[i][j] + column[0].a);
            mix[i][j] = std::uint16_t(mix[i][j] + column[0].b);
            kind[i][j] = (kind[i][j] ^ column[1].b) & 0xC000;
        }
    }

    for (int i = 1; i < 5; ++i) {
        const std::uint16_t cx = (x0 + i) & 0xFFF;
        const std::uint32_t row_bits = keys[2] >> (cx >> 4 & 15);
        for (int j = 1; j < 5; ++j) {
            const std::uint16_t cy = (y0 + j) & 0xFFF;
            auto& objects = view.objects[std::size_t(i - 1)][std::size_t(j - 1)];
            auto& terrain = view.terrain[std::size_t(i - 1)][std::size_t(j - 1)];
            objects = in.visited(cx, cy) ? 1 : 0;
            const std::uint32_t column_bits = keys[0] >> (cy >> 4 & 15);
            const std::uint16_t parity = (cx ^ cy ^ std::uint16_t((cy ^ keys[2]) << 1)) & 0x10;
            std::uint32_t level_word = height[i][j];
            std::uint16_t surface = kind[i][j];
            // Where the row and column bits disagree, the level is the mean of
            // the cell and two diagonal neighbours (low byte only).
            if ((row_bits ^ column_bits) & 1) {
                const int s = (row_bits & 1) == 0 ? 1 : -1;
                const std::uint32_t sum = level_word + height[i + 1][j - s] + height[i - 1][j + s];
                level_word = (height[i][j] & 0xFF00) | ((sum & 0xFF) / 3 & 0xFF);
            }
            // Bit 3 lets the surface follow a diagonal neighbour.
            if (level_word & 8) {
                const int s = (level_word & 1) == 0 ? 1 : -1;
                if (kind[i - 1][j + s] == surface) {
                    if (kind[i + 1][j - s] != surface) surface = kind[i + 1][j - s];
                } else {
                    surface = kind[i - 1][j + s];
                }
            }
            const std::uint16_t m = mix[i][j];
            view.mix[std::size_t(i - 1)][std::size_t(j - 1)] = m;
            if ((m & 0x3080) == 0) surface = m & 0xC000;
            const int raw_level = int(level_word & 0xFF);
            int level = raw_level;
            if (raw_level < 0x50) {
                level = (column_bits & row_bits & 1) ? world.alternate_level() : world.plain_level();
                if ((level_word & 5) == 0) level = raw_level + 0x1E;
            }
            const auto cell = world.platform(cx, cy);
            if (cell.height != 0) {
                if ((cell.centre && (m & 0x280) != 0) || (m & 0x410) == 0)
                    surface = a.u16(platform_kinds + std::uint32_t(cell.height * 2));
                if (cell.beacon && !cell.marked) {
                    view.beacon = true;
                    objects |= 2;
                }
            }
            if (surface == 0xC000) {
                view.water = true;
                level = std::max(level, (m & 6) + world.floor_level());
            } else if (level >= world.plain_level()) {
                level = world.plain_level();
            }
            if (level >= world.ceiling_level()) level = world.ceiling_level();
            terrain = std::uint16_t(surface | level);

            const int find = cell.height == 0 ? 1 : 0;
            if (from_stage_14 && !in.found[std::size_t(find)] && (m & 0x752) == 0) {
                view.find = true;
                objects |= a.u16(find_bits + std::uint32_t(find * 2));
            }
            if (cx == in.home_x && cy == in.home_y) {
                terrain |= 0x400;
                ++view.landmarks;
            } else if (cell.height == 0) {
                if ((level_word & 0x29) == 0 && level > 0x5F) {
                    terrain |= level_word & 0x1000;
                } else if ((level_word & 0x72C) == 0x400 && level > 0x32) {
                    terrain |= 0x400;
                    ++view.landmarks;
                } else if ((empty_mask & m) == 0) {
                    objects |= 0x40;
                }
            } else {
                if (parity == 0 || (level_word & 0x2000) == 0 || (marker_mask & (cy ^ cx ^ level_word)) != 0) {
                    if (cell.centre && (level_word & 0x14) == 0 && (level_word & 0x800) != 0 && !cell.beacon)
                        terrain |= stage_marker;
                } else {
                    view.marker_2000 = true;
                    terrain |= 0x2000;
                }
                if (!cell.marked) {
                    if (!from_stage_7 || (m & 0x28F) != 0 || cell.centre) {
                        if ((empty_mask & m) == 0) objects |= 0x40;
                    } else {
                        ++view.creature_places;
                        objects |= 4;
                    }
                } else {
                    terrain |= 0x100;
                }
            }
        }
    }

    // The colours ($208E0A on): 1-15 from the position and the keys, their
    // guns rearranged by one of four shift sets; 20-31 a ramp whose step
    // depends on how far the previous view was from a block's middle.
    auto& c = view.colours;
    const std::uint16_t mixed = keys[0] ^ y0 ^ x0;
    std::uint16_t value = std::uint16_t(((keys[3] ^ mixed) & 0x771) + 0x230);
    for (int k = 0; k < 7; ++k) c[std::size_t(9 + k)] = value = std::uint16_t(value + 2);
    std::uint32_t second = ((keys[1] ^ mixed) & 0x770) + 0x505;
    const std::uint32_t step = (x0 & 2) == 0 ? 0x10 : 0x100;
    std::uint32_t first = (x0 >> 1 & 0x770) + 0x550;
    for (int k = 0; k < 4; ++k) {
        second -= 0x101;
        c[std::size_t(5 + k)] = std::uint16_t(second);
        first -= step;
        c[std::size_t(1 + k)] = std::uint16_t(first | std::uint32_t(k + 1));
    }
    const std::uint32_t set = (mixed >> 4 & 3) * 4;
    const auto red = a.u32(red_shifts + set), green = a.u32(green_shifts + set), blue = a.u32(blue_shifts + set);
    for (int k = 1; k < 16; ++k) {
        const std::uint16_t old = c[std::size_t(k)];
        c[std::size_t(k)] = std::uint16_t(((old >> 8 & 15u) << (red & 63)) | ((old >> 4 & 15u) << (green & 63)) |
                                          ((old & 15u) << (blue & 63)));
    }
    const int px = (in.previous_x + 2) & 0xFFF, py = (in.previous_y + 2) & 0xFFF;
    const int distance = std::abs(((py + px) & 63) - 40) + std::abs(((px - py) & 63) - 40);
    const std::uint16_t ramp = distance < 11 ? 0x101 : distance < 25 ? 0x110 : distance < 45 ? 0x011 : 0x111;
    std::uint16_t sum = 0;
    for (int k = 20; k < 32; ++k) c[std::size_t(k)] = sum = std::uint16_t(sum + ramp);
    if (view.water) c[15] = 0x00F;
    if (view.marker_2000) c[12] = 0x9AF;
    c[17] = in.colour17;
    c[18] = in.colour18;
    c[19] = 0;
    c[0] = 0;
    return view;
}

} // namespace mindwalker::landscape
