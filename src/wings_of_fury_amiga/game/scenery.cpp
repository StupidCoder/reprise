#include "wings_of_fury_amiga/game/scenery.hpp"
#include <algorithm>
#include <cstdlib>

namespace wings::game {
namespace {

constexpr int sky_colour = 1;
constexpr int sand_colour = 17;
constexpr int sand_rows = 4;          // below the ground line ($1040E8: d3 = d1 + 4)
constexpr int column_width = 8;
constexpr int beach_left_index = 1;   // bchl
constexpr int beach_right_index = 2;  // bchr
constexpr int first_wave_index = 0x4E; // wava; + the wave frame
constexpr int wave_copies = 5;
constexpr int wave_spacing = 0x60;
// Map specials: the column value is the word >> 2 without its draw bit, so
// the drop bits must be 0 ($103B1C).
constexpr int special_us_flag = 0x22;
constexpr int special_deck_crew = 0x9F;
constexpr int special_enemy_flag = 0x113;
constexpr int us_flag_frames = 0xB4;  // flg3..flg6, frame = this + step table
constexpr int crew_frames = 0x8F;     // fgy0.., the second figure 8 further
constexpr int enemy_flag_frames = 0xB0; // flg0..flg2
constexpr int bare_pole = 0xB3;        // POST

// Screen x of a world x (the -$60CE origin: camera - 160).
constexpr int eighth_line = 0x97;     // the 1/8 view's scenery line
constexpr int eighth_sea_colour = 10;  // $103E7A: the sea band
constexpr int eighth_shift = 3;        // -$60CA

// $104EAC/$104A4E: how far a column on a ship is moved, if a ship carries
// it. The ship is looked up at world x = (column x - $A0) (x 8 in the 1/8
// view) + camera, as a byte offset of the map (x / 8 x 2); in the 1/8 view
// the drop is shifted right by 3.
int ship_drop(const BackdropState& state, int column_x) {
    const int dx = state.eighth_scale ? std::int16_t((column_x - 0xA0) << eighth_shift) : column_x - 0xA0;
    const int offset = (std::uint16_t(dx + state.camera) >> 3) * 2;
    for (const auto& ship : state.ships) {
        if (offset >= ship.first && offset <= ship.last) {
            const int drop = ship.bob + state.vertical_scroll;
            return state.eighth_scale ? std::uint16_t(drop) >> eighth_shift : drop;
        }
    }
    return 0;
}

// A world x on the screen (-$60CE = camera - 160, x 8 in the 1/8 view; the
// difference is scaled down by an arithmetic shift).
int screen_x(int world_x, int camera, bool eighth) {
    const int origin = std::int16_t(camera + std::int16_t(0xFF60 << (eighth ? eighth_shift : 0)));
    const int dx = std::int16_t(world_x - origin);
    return eighth ? dx >> eighth_shift : dx;
}

// $105174: a table entry at a world position, skipped when its screen x
// (before the handle) is outside -$80 .. $1C0.
void draw_at_world(IndexedCanvas& canvas, const data::Shape* shape, int world_x, int camera, bool eighth, int y) {
    const int x = screen_x(world_x, camera, eighth);
    if (shape == nullptr || x < -0x80 || x > 0x1C0) return;
    canvas.draw(*shape, x - shape->offset_x, y - shape->offset_y);
}

} // namespace

void IndexedCanvas::fill(int x0, int y0, int x1, int y1, std::uint8_t colour) {
    // $111010 clips to the window first.
    x0 = std::max(x0, clip.left);
    y0 = std::max(y0, clip.top);
    x1 = std::min(x1, clip.right - 1);
    y1 = std::min(y1, clip.bottom - 1);
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            pixels[std::size_t(y * width + x)] = colour;
        }
    }
}

void IndexedCanvas::draw(const data::Shape& shape, int x0, int y0) {
    for (int y = 0; y < shape.height; ++y) {
        for (int x = 0; x < shape.width(); ++x) {
            if (!inside(x0 + x, y0 + y) || !shape.covers(x, y)) continue;
            auto& pixel = pixels[std::size_t((y0 + y) * width + x0 + x)];
            pixel = std::uint8_t(shape.colour(x, y, pixel));
        }
    }
}

void IndexedCanvas::draw_mirrored(const data::Shape& shape, int x0, int y0) {
    const int last = shape.width() - 1;
    for (int y = 0; y < shape.height; ++y) {
        for (int x = 0; x < shape.width(); ++x) {
            if (!inside(x0 + x, y0 + y) || !shape.covers(last - x, y)) continue;
            auto& pixel = pixels[std::size_t((y0 + y) * width + x0 + x)];
            pixel = std::uint8_t(shape.colour(last - x, y, pixel));
        }
    }
}

void IndexedCanvas::draw_opaque(const data::Shape& shape, int x0, int y0) {
    for (int y = 0; y < shape.height; ++y) {
        for (int x = 0; x < shape.width(); ++x) {
            if (!inside(x0 + x, y0 + y)) continue;
            auto& pixel = pixels[std::size_t((y0 + y) * width + x0 + x)];
            pixel = std::uint8_t(shape.colour(x, y, pixel));
        }
    }
}

void IndexedCanvas::line(int x0, int y0, int x1, int y1, std::uint8_t colour) {
    const int left = clip.left, top = clip.top, right = clip.right - 1, bottom = clip.bottom - 1;
    const auto code = [&](int x, int y) {
        int c = x < left ? 8 : x > right ? 4 : 0;
        if (y < top) c |= 2;
        else if (y > bottom) c |= 1;
        return c;
    };
    // An end outside is moved onto the first edge it is beyond.
    const auto move_end = [&](int& x, int& y, int c, int dx, int dy) {
        const auto along_x = [&](int edge) {
            if (dx != 0) y += std::int16_t(std::int16_t(edge - x) * dy / dx);
            x = edge;
        };
        const auto along_y = [&](int edge) {
            if (dy != 0) x += std::int16_t(std::int16_t(edge - y) * dx / dy);
            y = edge;
        };
        if (c & 8) along_x(left);
        else if (c & 4) along_x(right);
        else if (c & 2) along_y(top);
        else if (c & 1) along_y(bottom);
    };
    for (;;) {
        const int c0 = code(x0, y0), c1 = code(x1, y1);
        if ((c0 | c1) == 0) break;
        if ((c0 & c1) != 0) return;
        if (c0 != 0) move_end(x0, y0, c0, x1 - x0, y1 - y0);
        else move_end(x1, y1, c1, x1 - x0, y1 - y0);
    }
    const int dx = std::abs(x1 - x0), dy = std::abs(y1 - y0);
    const int sx = x1 < x0 ? -1 : 1, sy = y1 < y0 ? -1 : 1;
    const bool steep = dy > dx;
    const int major = steep ? dy : dx, minor = steep ? dx : dy;
    int error = 2 * minor - major;
    int x = x0, y = y0;
    for (int i = 0; i <= major; ++i) {
        if (inside(x, y)) pixels[std::size_t(y * width + x)] = colour;
        if (error >= 0) {
            if (steep) x += sx;
            else y += sy;
            error += 2 * (minor - major);
        } else {
            error += 2 * minor;
        }
        if (steep) y += sy;
        else x += sx;
    }
}

void IndexedCanvas::draw_xor(const data::Shape& shape, int x0, int y0) {
    for (int y = 0; y < shape.height; ++y) {
        for (int x = 0; x < shape.width(); ++x) {
            if (!inside(x0 + x, y0 + y)) continue;
            auto& pixel = pixels[std::size_t((y0 + y) * width + x0 + x)];
            int value = pixel ^ shape.set_planes;
            for (const auto& plane : shape.planes) {
                if (plane.bits[std::size_t(y * shape.bytes_per_row + x / 8)] >> (7 - x % 8) & 1) {
                    value ^= plane.bitplanes;
                }
            }
            pixel = std::uint8_t(value);
        }
    }
}

std::vector<Island> islands(const data::MissionMap& map) {
    std::vector<Island> result;
    int left = -1;
    for (std::size_t i = 0; i < map.words.size(); ++i) {
        const auto column = map.column(i);
        if (!column.draw) continue;
        if (column.index == beach_left_index) {
            left = int(i);
        } else if (column.index == beach_right_index) {
            result.push_back({left, int(i)});
        }
    }
    return result;
}

// $1037BC: the first column is (camera - $120) / 8 (arithmetic shift) at
// x = 8 - (camera & 7) - $80; columns run on while x < $1D0. A shape is
// drawn at x - 8, which puts column c at world x c x 8. In the 1/8 view the
// camera is taken to a multiple of 8, the first column is (camera - $900) / 8
// and each column is one pixel. Calls visit(c, x) for the map's columns.
template <typename Visit>
void for_each_column(const data::MissionMap& map, const BackdropState& state, Visit visit) {
    const bool eighth = state.eighth_scale;
    const int view_camera = eighth ? state.camera & 0xFFF8 : state.camera;
    const int step = eighth ? 1 : column_width;
    int x = column_width - (view_camera & 7) - 0x80;
    for (int c = (view_camera - (eighth ? 0x900 : 0x120)) >> 3; x < 0x1D0; ++c, x += step) {
        if (c < 0 || std::size_t(c) >= map.words.size()) continue;
        visit(c, x);
    }
}

IndexedCanvas draw_backdrop(const data::Assets& assets, const data::MissionMap& map, const BackdropState& state) {
    auto canvas = draw_sky_and_scenery(assets, map, state);
    draw_sea_and_sand(canvas, assets, map, state);
    return canvas;
}

IndexedCanvas draw_sky_and_scenery(const data::Assets& assets, const data::MissionMap& map, const BackdropState& state) {
    IndexedCanvas canvas(view_width, view_height);
    const int camera = state.camera;
    const int ground_line = state.ground_line;
    int enemy_flag_counter = state.enemy_flag_counter;

    // $103784: the sky down to the ground line + 1 (at most row $A1).
    canvas.fill(0, 0, view_width - 1, std::min(ground_line + 1, 0xA1), sky_colour);

    const bool eighth = state.eighth_scale;
    for_each_column(map, state, [&](int c, int x) {
        const auto column = map.column(std::size_t(c));
        const data::Shape* shape = !column.draw ? nullptr
                                   : eighth     ? data::eighth_scale_shape(assets, column.index)
                                                : data::scenery_shape(assets, column.index);
        if (shape != nullptr) {
            const int line = eighth ? eighth_line : ground_line + column.drop;
            const int y = line + (column.surface == 1 ? ship_drop(state, x) : 0);
            canvas.draw(*shape, x - 8 - shape->offset_x, y - shape->offset_y);
        }
        if (eighth) return; // $103B2C: no specials in the 1/8 view
        const auto special = map.words[std::size_t(c)] >> 2 & 0x1FFF;
        const auto put = [&](int entry, int y) {
            if (const auto* s = data::scenery_shape(assets, entry)) canvas.draw(*s, x - 8 - s->offset_x, y - s->offset_y);
        };
        // The tower flag and the deck crew always ride the US carrier (the
        // last ship): its bob (-$5B26 +$1A) + the vertical scroll.
        const int carrier_drop = (state.ships.empty() ? 0 : state.ships.back().bob) + state.vertical_scroll;
        if (special == special_us_flag) {
            put(us_flag_frames + assets.us_flag_steps[std::size_t(state.us_flag_step & 3)], ground_line + carrier_drop);
        } else if (special == special_deck_crew) {
            put(crew_frames + state.crew_pose_a, ground_line + carrier_drop);
            put(crew_frames + 8 + state.crew_pose_b, ground_line + carrier_drop);
        } else if (special == special_enemy_flag) {
            // The island is the one the camera is over: the count of islands
            // whose right edge (in 4-pixel units) is at or left of camera / 4.
            const auto all = islands(map);
            std::size_t island = 0;
            while (island < all.size() && all[island].right_column * 2 <= (camera >> 2)) ++island;
            // Past the last island the lookup reads zeroed table words: a bare
            // pole (hypothesis; a flag column is always on an island).
            const bool up = island < all.size() &&
                            (island >= state.island_flag_up.size() || state.island_flag_up[island]);
            if (--enemy_flag_counter < 0) enemy_flag_counter = 2;
            put(up ? enemy_flag_frames + enemy_flag_counter : bare_pole, ground_line);
        }
    });

    return canvas;
}

SpecialsShown specials_shown(const data::MissionMap& map, const BackdropState& state) {
    SpecialsShown shown;
    for_each_column(map, state, [&](int c, int) {
        const auto w = map.words[std::size_t(c)];
        if ((w & 0x8000) != 0 && (w >> 2 & 0x1FFF) == 5) shown.wrecked_huts.push_back(c * 2);
        if (state.eighth_scale) return;
        const auto special = w >> 2 & 0x1FFF;
        if (special == special_us_flag) ++shown.us_flag;
        else if (special == special_deck_crew) ++shown.deck_crew;
        else if (special == special_enemy_flag) ++shown.enemy_flag;
    });
    return shown;
}

void draw_sea_and_sand(IndexedCanvas& canvas, const data::Assets& assets, const data::MissionMap& map,
                       const BackdropState& state) {
    const int camera = state.camera;
    const int ground_line = state.ground_line;

    if (state.eighth_scale) {
        // $103E7A: a plain sea band; $1040E8: per island its small beach ends
        // (at the line $97 = ground $4B8 / 8) and one row of sand.
        canvas.fill(0, eighth_line, view_width - 1, 0xA2, eighth_sea_colour);
        const auto* left_end = data::eighth_scale_shape(assets, beach_left_index);
        const auto* right_end = data::eighth_scale_shape(assets, beach_right_index);
        for (const auto& island : islands(map)) {
            const int left = island.left_column * column_width, right = island.right_column * column_width;
            draw_at_world(canvas, left_end, left, camera, true, eighth_line);
            draw_at_world(canvas, right_end, right, camera, true, eighth_line);
            canvas.fill(screen_x(left, camera, true), eighth_line, screen_x(right, camera, true), eighth_line,
                        sand_colour);
        }
        return;
    }

    // $103E6C: the waves, from x = $3F - (camera & $3F) - $50.
    if (const auto* wave = data::scenery_shape(assets, first_wave_index + state.wave_frame)) {
        int wave_x = 0x3F - (camera & 0x3F) - 0x50;
        for (int i = 0; i < wave_copies; ++i, wave_x += wave_spacing) {
            canvas.draw(*wave, wave_x - wave->offset_x, ground_line + 0xA - wave->offset_y);
        }
    }

    // $1040E8: each island's beach ends, then its sand over them.
    const auto* left_end = data::scenery_shape(assets, beach_left_index);
    const auto* right_end = data::scenery_shape(assets, beach_right_index);
    for (const auto& island : islands(map)) {
        const int left = island.left_column * column_width, right = island.right_column * column_width;
        draw_at_world(canvas, left_end, left, camera, false, ground_line);
        draw_at_world(canvas, right_end, right, camera, false, ground_line);
        canvas.fill(screen_x(left, camera, false), ground_line, screen_x(right, camera, false), ground_line + sand_rows,
                    sand_colour);
    }
}

} // namespace wings::game
