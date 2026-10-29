#include "spindizzy_worlds_amiga/view/front.hpp"
#include <algorithm>
#include <numeric>

namespace spindizzy::view {
namespace {

constexpr std::uint32_t attract_frame_lists = 0x17A5E;
constexpr std::uint32_t sine_table = 0x164EC;       // 256 words, ±$4000
constexpr std::uint32_t swap_buffer = 0x56CE8;      // $A(a5); SELECT.SWP's data from + 4 ($115F8 keeps a tag long first)
constexpr std::uint32_t planet_pictures = swap_buffer + 0x1404;
constexpr std::uint32_t mystery_planet = swap_buffer + 0x3344;
constexpr std::uint32_t planet_shadow = swap_buffer + 0x192C;
constexpr std::uint32_t break_frames = swap_buffer + 0x35C4; // $16AF2
constexpr std::uint32_t break_list = 0x16B38;       // (ticks, frame offset) words; $FFFF loops
constexpr std::uint32_t planet_sizes = 0x16A2C;     // per size: dy, offset, rows − 1 (8 bytes, backwards)
constexpr int map_x = 0x90, map_y = 0x58;           // $16868
constexpr std::uint8_t map_box_colour = 11;         // $1693C: planes 0, 1 and 3

void set(Canvas& c, int x, int y, std::uint8_t v) {
    if (x >= 0 && y >= 0 && x < c.width && y < c.height) c.pixels[std::size_t(y * c.width + x)] = v;
}

// Tile `index` OR'ed into the canvas ($172C0).
void or_tile(Canvas& c, const data::Graphics& g, int index, int x, int y) {
    if (index < 0 || std::size_t(index) >= g.tiles.size()) return;
    const auto& tile = g.tiles[std::size_t(index)];
    for (int r = 0; r < data::tile_height; ++r)
        for (int b = 0; b < data::tile_width; ++b) {
            const int px = x + b, py = y + r;
            if (px < 0 || py < 0 || px >= c.width || py >= c.height) continue;
            auto& p = c.pixels[std::size_t(py * c.width + px)];
            p = std::uint8_t(p | tile[std::size_t(r * data::tile_width + b)]);
        }
}

// A masked 32-pixel frame (mask, then four planes, two words each per
// row) blitted at (x, y): the blitter's A = mask, B = planes ($10252).
void blit_frame(Canvas& c, const data::Memory& source, std::uint32_t frame, int rows, int x, int y) {
    for (int r = 0; r < rows; ++r) {
        const std::uint32_t row = frame + std::uint32_t(r) * 20;
        const std::uint32_t mask = source.u32(row);
        for (int b = 0; b < 32; ++b) {
            if (!(mask >> (31 - b) & 1)) continue;
            int colour = 0;
            for (int p = 0; p < 4; ++p) colour |= int(source.u32(row + 4 + std::uint32_t(p) * 4) >> (31 - b) & 1) << p;
            set(c, x + b, y + r, std::uint8_t(colour));
        }
    }
}

} // namespace

void draw_block_picture(Canvas& c, const data::Assets& assets, int offset, std::uint16_t word) {
    const int type = word & 0xFF;
    const int x = (offset % 40) * 8;
    int y = offset / 40;
    const bool lowered = assets.block_attributes[std::size_t(type)] & 2;
    const auto& pieces = assets.graphics.pieces[std::size_t(type)];
    auto piece_row = [&](const std::array<data::TilePiece, 8>& from, int row) {
        or_tile(c, assets.graphics, from[std::size_t(row * 2)].tile, x, y);
        or_tile(c, assets.graphics, from[std::size_t(row * 2 + 1)].tile, x + 16, y);
        y += data::tile_height;
    };
    if (lowered) y += data::tile_height;
    for (int row = 0; row < 4; ++row) piece_row(pieces, row);
    if (lowered || (word & 0x8000)) return;
    piece_row(assets.graphics.pieces[std::size_t(type & 0xC0)], 0); // the base row
}

std::vector<AttractSprite> read_attract_sprites(const data::Memory& main, std::uint32_t at) {
    // $179DC: entries of a kind byte (0 ends) and the 11 bytes $179F2 reads.
    std::vector<AttractSprite> sprites;
    for (; main.u8(at) != 0; at += 12) {
        const int kind = std::int8_t(main.u8(at));
        AttractSprite s;
        s.wide = kind != -12; // −12 draws through $178E8 (16 pixels); the others $178CA
        s.rows = main.u8(at + 1) + 1;
        s.frames = main.u32(at + 2);
        s.list = attract_frame_lists + main.u16(at + 6);
        s.offset = main.u16(at + 8);
        s.countdown = main.u8(at + 10);
        s.period = main.u8(at + 11);
        sprites.push_back(s);
    }
    return sprites;
}

void step_attract_sprite(Canvas& c, const data::Memory& main, AttractSprite& s) {
    if (--s.countdown >= 0) return;
    s.countdown = s.period;
    const std::uint32_t frame = s.frames + main.u16(s.list + std::uint32_t(s.entry) * 2);
    ++s.entry;
    if (std::int16_t(main.u16(s.list + std::uint32_t(s.entry) * 2)) < 0) s.entry = 0;
    const int x = (s.offset % 40) * 8, y = s.offset / 40;
    const int words = s.wide ? 2 : 1;
    const std::uint32_t row_bytes = std::uint32_t(words) * 10;
    for (int r = 0; r < s.rows; ++r) {
        const std::uint32_t row = frame + std::uint32_t(r) * row_bytes;
        for (int b = 0; b < words * 16; ++b) {
            const int word = b / 16, bit = 15 - b % 16;
            int colour = 0;
            for (int p = 0; p < 4; ++p)
                colour |= (main.u16(row + std::uint32_t((p + 1) * words + word) * 2) >> bit & 1) << p;
            set(c, x + b, y + r, std::uint8_t(colour));
        }
    }
}

std::vector<Planet> read_cluster(const data::Memory& main, std::uint32_t list) {
    std::vector<Planet> planets;
    const int count = main.u16(list) + 1;
    for (int i = 0; i < count; ++i) {
        const std::uint32_t at = list + 2 + std::uint32_t(i) * 10;
        planets.push_back({std::int16_t(main.u16(at)), std::int16_t(main.u16(at + 2)), std::int16_t(main.u16(at + 4)),
                           std::int16_t(main.u16(at + 6)), std::int16_t(main.u16(at + 8))});
    }
    return planets;
}

void ClusterView::steer(std::uint8_t joystick) {
    // $168BE: up/down and left/right push the two speeds by 1.0 a frame,
    // which lose an eighth each frame; the third angle keeps its speed (0).
    if (joystick & 1) speed_[0] += 0x10000;
    if (joystick & 2) speed_[0] -= 0x10000;
    if (joystick & 4) speed_[1] += 0x10000;
    if (joystick & 8) speed_[1] -= 0x10000;
    for (int i = 0; i < 2; ++i) {
        speed_[i] -= speed_[i] >> 3;
        angle_[i] += speed_[i];
    }
}

int ClusterView::update(const data::Memory& main, const std::vector<Planet>& planets, const std::vector<bool>& shown, bool choose,
                        int prefer, int slack) {
    if (break_removed_) breaking_ = -1;
    // $16704: sine and cosine of the three angles ($1673A: cos = 64 entries on).
    auto sincos = [&](std::int32_t angle) {
        const int i = (angle >> 16) & 0xFF;
        return std::pair{std::int16_t(main.u16(sine_table + std::uint32_t(i) * 2)),
                         std::int16_t(main.u16(sine_table + std::uint32_t((i + 64) & 0xFF) * 2))};
    };
    const auto [s0, c0] = sincos(angle_[0]);
    const auto [s1, c1] = sincos(angle_[1]);
    const auto [s2, c2] = sincos(angle_[2]);
    // $16756: three turns, each (a·cos ± b·sin) × 4 >> 16 on words.
    auto turn = [](std::int16_t a, std::int16_t b, std::int16_t s, std::int16_t k, bool add) {
        const std::int32_t v = add ? std::int32_t(a) * s + std::int32_t(b) * k : std::int32_t(a) * s - std::int32_t(b) * k;
        return std::int16_t(std::uint32_t(v * 4) >> 16);
    };
    placed_.clear();
    if (spin_.size() != planets.size()) spin_.assign(planets.size(), 0x400);
    for (std::size_t i = 0; i < planets.size(); ++i) {
        if (!shown[i] && int(i) != breaking_) continue;
        std::int16_t x = planets[i].x, y = planets[i].y, z = planets[i].z;
        // first about y with angle 2 ($36AC/$36AE), then about z with angle 0
        // ($36A4/$36A6), then about x with angle 1 ($36A8/$36AA).
        const std::int16_t x1 = turn(x, z, s2, c2, true), z1 = turn(z, x, s2, c2, false);
        const std::int16_t x2 = turn(x1, y, s0, c0, true), y2 = turn(y, x1, s0, c0, false);
        const std::int16_t y3 = turn(y2, z1, c1, s1, true), z3 = turn(z1, y2, c1, s1, false); // $167A2: sine and cosine swapped
        placed_.push_back({int(i), x2, y3, z3, std::uint16_t(z3 + 0x8000)});
    }
    // $1440E: farthest first; the nearest is the chosen one ($1445A).
    std::stable_sort(placed_.begin(), placed_.end(), [](const Placed& a, const Placed& b) { return a.sort < b.sort; });
    chosen_ = placed_.empty() || !choose ? -1 : placed_.back().index;
    if (chosen_ >= 0 && prefer >= 0) {
        const auto at = std::find_if(placed_.begin(), placed_.end(), [&](const Placed& p) { return p.index == prefer; });
        if (at != placed_.end() && int(placed_.back().sort) - int(at->sort) <= slack) chosen_ = prefer;
    }
    if (chosen_ >= 0) ++spin_[std::size_t(chosen_)];
    return chosen_;
}

void ClusterView::step_break_up(const data::Memory& main, int steps, bool slow) {
    if (breaking_ < 0) return;
    break_under_ = break_entry_ < 4; // $16AE2: +$1F before this frame's steps
    for (int i = 0; i < steps; ++i) {
        // $10058: the ticks count down once a call (twice without slow
        // mode, $24A6); at 0 the next entry, a negative one going back to
        // the first.
        break_ticks_ = (break_ticks_ - 1) & 0xFF;
        bool next = break_ticks_ == 0;
        if (!next && !slow) next = (break_ticks_ = (break_ticks_ - 1) & 0xFF) == 0;
        if (next) {
            ++break_entry_;
            std::uint16_t ticks = main.u16(break_list + std::uint32_t(break_entry_) * 4);
            if (ticks & 0x8000) { break_entry_ = 0; ticks = main.u16(break_list); }
            break_ticks_ = ticks & 0xFF;
        }
        break_offset_ = main.u16(break_list + std::uint32_t(break_entry_) * 4 + 2);
    }
    // $16AA6 (after the drawing): +$1E negative removes the object.
    break_removed_ = break_ticks_ & 0x80;
}

void draw_break_frame(Canvas& c, const data::Assets& assets, int n, int x, int y) {
    // Picture 11 (the hard ending's $16B62 with d0 = 10) lies past the
    // file's end: the original blits whatever the level left in RAM there;
    // here it is left out.
    const std::uint32_t frame = break_frames + std::uint32_t(n) * 0x280;
    if (frame + 0x280 > swap_buffer + 4 + assets.select_swap.size()) return;
    const data::Memory swap(swap_buffer + 4, assets.select_swap);
    blit_frame(c, swap, frame, 32, x, y);
}

void ClusterView::draw(Canvas& c, const data::Assets& assets, const data::Memory& main, const std::vector<Planet>& planets) const {
    const data::Memory swap(swap_buffer + 4, assets.select_swap);
    // $1693C: the box, 192 × 192 from row 4, byte 8.
    for (int y = 4; y < 196; ++y)
        for (int x = 64; x < 256; ++x) set(c, x, y, map_box_colour);
    // $169F8 → $1688A: each planet's shadow first (plane 0 cleared under it).
    for (const auto& p : placed_) {
        if (planets[std::size_t(p.index)].picture < 0 || p.index == breaking_) continue;
        const int sx = p.x + 0x98 - (std::int16_t(std::uint16_t(p.sort + 0x8080)) >> 2), sy = p.y + 0x5C;
        for (int r = 0; r < 18; ++r) {
            const std::uint32_t mask = swap.u32(planet_shadow + std::uint32_t(r) * 20);
            for (int b = 0; b < 32; ++b)
                if (mask >> (31 - b) & 1) {
                    const int x = sx + b, y = sy + r;
                    if (x >= 0 && y >= 0 && x < c.width && y < c.height) c.pixels[std::size_t(y * c.width + x)] &= 0xFE;
                }
        }
    }
    for (const auto& p : placed_) {
        const auto& planet = planets[std::size_t(p.index)];
        const bool breaking = p.index == breaking_;
        if (breaking && !break_under_) { // $16B2A: the frame alone
            blit_frame(c, swap, break_frames + break_offset_, 32, p.x + map_x, p.y - 4 + map_y);
            continue;
        }
        if (planet.picture < 0) { // $16A76: the "?" planet, 32 rows
            blit_frame(c, swap, mystery_planet, 32, p.x + map_x, p.y - 4 + map_y);
            if (breaking) blit_frame(c, swap, break_frames + break_offset_, 32, p.x + map_x, p.y - 4 + map_y);
            continue;
        }
        std::uint32_t frame = planet_pictures + std::uint32_t(planet.picture & 3) * 0x7D0;
        if (p.index == chosen_) { // $16A50: the chosen one cycles its pictures every other 8 frames
            const int count = spin_[std::size_t(p.index)];
            if (count & 8) frame = planet_pictures + std::uint32_t(count & 3) * 0x7D0;
        }
        int size = std::max(0, int(p.sort) - 0x7FC0) >> 5; // $16846
        if (size >= 5) size = 4;
        const std::uint32_t entry = planet_sizes - std::uint32_t(size) * 8;
        const int dy = std::int16_t(main.u16(entry));
        frame += main.u32(entry + 2);
        const int rows = main.u16(entry + 6) + 1;
        blit_frame(c, swap, frame, rows, p.x + map_x, p.y + dy + map_y);
        // $16AC8 draws the picture with the size of the depth in d6 as the
        // drawing loop leaves it (hypothesis: the planet's own depth).
        if (breaking) blit_frame(c, swap, break_frames + break_offset_, 32, p.x + map_x, p.y - 4 + map_y);
    }
}

void shrink_cluster(Canvas& to, const Canvas& from, int offset) {
    // $11D88: per 48 pixels (three words, right to left) the bits taken
    // after shifting by these counts; d2 first, then d0:d1 as one long.
    static constexpr int part_a[] = {2, 1, 2, 1, 2, 1, 2, 1, 2, 1};
    static constexpr int part_b[] = {1, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1};
    const int to_x = (offset % 40) * 8, to_y = offset / 40;
    int source_row = 4;
    for (int row = 0; row < 128; ++row) {
        for (int group = 0; group < 4; ++group) {
            const int x0 = 64 + group * 48;
            // Pixels of the group from the right: bit k = pixel 47 − k.
            auto source = [&](int k) { return from.at(x0 + 47 - k, source_row); };
            std::array<std::uint8_t, 32> taken{};
            int k = 0, n = 0;
            for (int s : part_a) { k += s; taken[std::size_t(n++)] = source(k - 1); }
            k = 16;
            for (int s : part_b) { k += s; taken[std::size_t(n++)] = source(k - 1); }
            for (int b = 0; b < 32; ++b) set(to, to_x + group * 32 + 31 - b, to_y + row, taken[std::size_t(b)]);
        }
        source_row += (row & 1) ? 1 : 2; // $11D78: an extra row every other row (d7 odd first)
    }
}

} // namespace spindizzy::view
