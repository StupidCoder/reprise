// Scattered-rings enhancement.
//
// The original shows lost rings as one blinking object that cannot be
// collected. With this enhancement each lost ring (up to 99) becomes its own
// small body that bounces and slides on the level's terrain, fading out after
// about four seconds. They are cosmetic: they cannot be collected again, and
// the original lost-ring object still runs (invisibly) for its gameplay role.

#include "sonic1_gg/enhancements/ring_effects.hpp"
#include "sonic1_gg/game/rom_tables.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace sonic1_gg {
namespace {
// World-space contour height near a contacting foot, including block seams.
int floor_height(const Level& level, int x, int foot_y) {
    for (int offset : {0, -4, 4}) {
        int y = foot_y + offset;
        int block = level.block_at(x, y);
        int height = int(std::int8_t(level.floor_contours[block * 32 + (x & 31)]));
        if (height >= 0) {
            int surface = (y & ~31) + height;
            if (std::abs(surface - foot_y) <= 8) {
                return surface;
            }
        }
    }
    return foot_y;
}

void move(RingEffects::Ring& ring, const Game& game) {
    auto& p = ring.body;
    // Surface tangent accelerates settled rings downhill; friction reduces
    // horizontal energy. Ring motion never changes the original terrain state.
    if (p.grounded()) {
        int x = p.x / 256 + p.width / 2, y = p.y / 256 + p.height;
        int slope = floor_height(game.level, x + 2, y) - floor_height(game.level, x - 2, y);
        p.vx = std::clamp(p.vx * 250 / 256 + slope * 10, -1280, 1280);
    }
    p.vy = std::min(p.vy + 40, 1280);
    const int vx = p.vx, vy = p.vy;
    // Reuse the recovered terrain contours, with substeps no larger than one
    // pixel. Never put these bodies into the original game's object pool.
    int steps = std::max(1, (std::max(std::abs(vx), std::abs(vy)) + 255) / 256);
    int result_vx = vx, result_vy = vy;
    bool wall = false, floor = false, ceiling = false;
    for (int n = 0; n < steps; ++n) {
        p.vx = wall ? 0 : (vx * (n + 1) / steps - vx * n / steps);
        p.vy = (floor || ceiling) ? 0 : (vy * (n + 1) / steps - vy * n / steps);
        const int old_y = p.y;
        const int expected_y = old_y + p.vy;
        game.integrate_body(p);
        if (p.flags & 64) {
            wall = true;
            result_vx = -vx * 3 / 5;
        }
        if (p.grounded()) {
            floor = true;
            result_vy = vy > 160 ? -vy * 11 / 20 : 0;
        } else if (vy < 0 && p.y != expected_y) {
            ceiling = true;
            result_vy = -vy / 2;
        }
    }
    if (floor) {
        p.flags |= 128;
    }
    p.vx = result_vx;
    p.vy = result_vy;
    if (floor && std::abs(p.vx) < 8) {
        p.vx = 0;
    }
}
} // namespace

void RingEffects::reset() {
    rings_.clear();
    primed_ = false;
}

void RingEffects::tick(const Game* game, bool enabled) {
    if (!enabled || !game || (!game->sonic.dead && game->ending.sonic_hidden)) {
        reset();
        return;
    }
    const auto& g = *game;
    if (primed_ && g.ticks == ticks_) {
        return;
    }
    if (primed_ && (g.level.act != act_ || g.death_count != deaths_ || g.ticks != ticks_ + 1 ||
                    std::abs(g.sonic.x / 256 - x_) > 64 || std::abs(g.sonic.y / 256 - y_) > 64)) {
        reset();
    }
    for (auto& ring : rings_) {
        ++ring.age;
        move(ring, g);
    }
    std::erase_if(rings_, [&](const Ring& ring) {
        auto& p = ring.body;
        return ring.age >= 240 || p.x / 256 > g.level.columns * 32 + 32 ||
               p.y / 256 > g.level.rows * 32 + 64;
    });
    // Captured at the precise damage site, before rings=0. No inference from
    // HUD differences (which also change on extra lives and results screens).
    for (int i = 0; i < g.rings_lost_this_tick; ++i) {
        double angle = std::numbers::pi * (double(i) + 0.5) / g.rings_lost_this_tick;
        double speed = 2.5 + double(i % 4) * 0.65;
        Body body;
        body.flags = 0;
        body.width = body.height = 12;
        body.x = g.ring_loss_x + 2 * 256;
        body.y = g.ring_loss_y + 8 * 256;
        body.vx = int(std::cos(angle) * speed * 256);
        body.vy = -int((1.5 + std::sin(angle) * speed) * 256);
        rings_.push_back({body, 0, unsigned(i % 6)});
    }
    primed_ = true;
    act_ = g.level.act;
    ticks_ = g.ticks;
    deaths_ = g.death_count;
    x_ = g.sonic.x / 256;
    y_ = g.sonic.y / 256;
}

void RingEffects::draw(gg::Screen& image, const Game& game, int cx, int cy) const {
    const auto base = gg::decode_palette(game.level.palette);
    for (const auto& ring : rings_) {
        int x = ring.body.x / 256 - 2 - cx, y = ring.body.y / 256 - 2 - cy;
        if (x + 16 <= 0 || y + 16 <= 0 || x >= image.width || y >= image.height) {
            continue;
        }
        unsigned step = (ring.age / 4 + ring.phase) % 6;
        int mapping = game.level.word(rom::lost_ring_frames + step * 3);
        int frame = game.level.data(rom::lost_ring_frames + 2 + step * 3) - 1;
        // Continuous 70% alpha, with a smooth final second. Never frame flicker.
        unsigned alpha = ring.age < 180 ? 180 : (240 - ring.age) * 3;
        for (int py = 0; py < 16; ++py) {
            for (int px = 0; px < 16; ++px) {
                int dx = x + px, dy = y + py;
                if (dx < 0 || dy < 0 || dx >= image.width || dy >= image.height) {
                    continue;
                }
                int tile = game.level.data(mapping + px / 8) - 0xb4 + frame * 8 + py / 8;
                auto index = gg::tile_pixel(game.level.lost_ring_patterns, tile, px % 8, py % 8);
                if (!index) {
                    continue;
                }
                const gg::Palette* palette = &base;
                for (const auto& change : image.palette_changes) {
                    if (dy < change.first) {
                        break;
                    }
                    palette = &change.second;
                }
                auto color = (*palette)[16 + index];
                auto& under = image.pixels[dy * image.width + dx];
                auto blend = [&](unsigned a, unsigned b) {
                    return std::uint8_t((a * alpha + b * (255 - alpha) + 127) / 255);
                };
                under = {blend(color.r, under.r), blend(color.g, under.g), blend(color.b, under.b),
                         255};
            }
        }
    }
}
} // namespace sonic1_gg
