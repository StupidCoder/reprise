// Drawing of the level, Sonic, objects and the HUD; see scene.hpp.

#include "sonic1_gg/render/scene.hpp"
#include "sonic1_gg/enhancements/basin_water.hpp"
#include "sonic1_gg/enhancements/widescreen.hpp"
#include "sonic1_gg/game/rom_tables.hpp"
#include <algorithm>

namespace sonic1_gg {
using gg::decode_palette;
using gg::draw_tile;
using gg::Screen;
using gg::tile_pixel;
using platform::Color;

namespace {

namespace type = object_type;

// Tiles changed at run time (collected rings, collapsing bridges, the capsule).
// Name table entries: bits 0-8 tile, 9/10 flip X/Y, 11 sprite palette, 12 priority.
void draw_patches(gg::Screen& image, const Level& level, int cx, int cy, bool foreground) {
    auto palette = decode_palette(level.palette);
    for (auto [position, entry] : level.tile_overrides) {
        int tile = entry & 511;
        const auto& data = tile < 256   ? level.background_patterns
                           : tile < 384 ? level.object_patterns
                                        : level.common_patterns;
        int local = tile - (tile < 256 ? 0 : tile < 384 ? 256 : 384);
        int x = int(position % (level.columns * 4)) * 8 - cx,
            y = int(position / (level.columns * 4)) * 8 - cy;
        if (foreground && !(entry & 0x1000)) {
            continue;
        }
        for (int py = 0; py < 8; ++py) {
            for (int px = 0; px < 8; ++px) {
                int color = tile_pixel(data, local, (entry & 0x200) ? 7 - px : px,
                                       (entry & 0x400) ? 7 - py : py);
                if (!foreground || color) {
                    image.put_index(x + px, y + py, (entry & 0x800 ? 16 : 0) + color, palette);
                }
            }
        }
    }
}
} // namespace

// Labyrinth: colours below the water line come from the underwater palette.
void configure_water_palette(gg::Screen& image, const Game& game, int camera_y) {
    if (!game.level.has_water()) {
        return;
    }
    Bytes bytes;
    int base = game.water.boss_palette ? 0x256 : 0x216;
    for (int i = 0; i < 64; ++i) {
        bytes.push_back(game.level.data(base + i));
    }
    auto underwater = decode_palette(bytes);
    if ((image.height == 144 && game.water.scanline == 255) ||
        (image.height != 144 && game.water.y - camera_y + 21 <= 0)) {
        // 0185 loads wet colours in VBlank; 1696 can subsequently overwrite
        // one colour for a boss flash. Partial-water IRQs replace it again.
        if (game.flash.written) {
            underwater[(game.flash.color & 15) + (game.flash.color & 128 ? 16 : 0)] =
                decode_palette(game.level.palette)[(game.flash.color & 15) +
                                                   (game.flash.color & 128 ? 16 : 0)];
        }
        image.palette_changes.push_back({0, underwater});
        return;
    }
    if (image.height == 144 && !game.water.scanline) {
        return;
    }
    // The line interrupt (01F1-0203) streams the 64 palette bytes rather than
    // replacing CRAM at once, so the colours change over nine scanlines below
    // the surface; measured on the original, reproduced as a mixed band.
    static constexpr int committed[] = {2, 6, 11, 14, 18, 22, 26, 30, 32};
    auto palette = decode_palette(game.level.palette);
    for (int row = 0; row < 9; ++row) {
        std::copy_n(underwater.begin(), committed[row], palette.begin());
        image.palette_changes.push_back({game.water.y - camera_y + 13 + row, palette});
    }
}

void draw_level(gg::Screen& image, const sonic1_gg::Level& level, int camera_x, int camera_y,
                const BasinWater* basins) {
    const auto palette = decode_palette(level.palette);
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const int wx = x + camera_x, wy = y + camera_y;
            if (wx < 0 || wy < 0 || wx >= level.columns * 32 || wy >= level.rows * 32) {
                image.put_index(x, y, 0, palette);
                continue;
            }
            auto block = level.block_at(wx, wy);
            auto tile = level.blocks[std::size_t(block) * 16 + ((wy & 31) / 8) * 4 + (wx & 31) / 8];
            auto color = basins && basins->replaces(wx, wy)
                             ? basins->background_index(level, wx, wy)
                             : tile_pixel(level.background_patterns, tile, wx & 7, wy & 7);
            image.put_index(x, y, color, palette);
        }
    }
    draw_patches(image, level, camera_x, camera_y, false);
}

void draw_foreground(gg::Screen& image, const sonic1_gg::Level& level, int camera_x, int camera_y,
                     const BasinWater* basins) {
    // 09FF-0A08 (also 07AA/081A): block attribute bit 7 becomes VDP tile
    // priority bit 12. Reapply those tiles after world sprites. Background
    // color zero never occludes a sprite, even when tile priority is set.
    // Act 1's animated flower patches occupy ordinary, non-priority blocks.
    const auto palette = decode_palette(level.palette);
    const int left = std::max(0, camera_x) / 32, top = std::max(0, camera_y) / 32;
    const int right = std::min(int(level.columns) * 32, camera_x + image.width);
    const int bottom = std::min(int(level.rows) * 32, camera_y + image.height);
    for (int by = top; by * 32 < bottom; ++by) {
        for (int bx = left; bx * 32 < right; ++bx) {
            const auto block = level.block_at(bx * 32, by * 32);
            if (!(level.block_attributes[block] & 0x80)) {
                continue;
            }
            for (int ty = 0; ty < 4; ++ty) {
                for (int tx = 0; tx < 4; ++tx) {
                    if (level.tile_overrides.contains((by * 4 + ty) * level.columns * 4 + bx * 4 +
                                                      tx)) {
                        continue;
                    }
                    int wx = bx * 32 + tx * 8, wy = by * 32 + ty * 8;
                    auto tile = level.blocks[block * 16 + ty * 4 + tx];
                    if (!basins) {
                        draw_tile(image, level.background_patterns, tile, wx - camera_x,
                                  wy - camera_y, palette, 0, true);
                    } else {
                        for (int py = 0; py < 8; ++py) {
                            for (int px = 0; px < 8; ++px) {
                                // Enhanced waterfall pixels are redrawn by BasinWater::draw_falls.
                                if (basins->fall_contains(wx + px, wy + py)) {
                                    continue;
                                }
                                auto color =
                                    basins->replaces(wx + px, wy + py)
                                        ? basins->foreground_index(level, wx + px, wy + py)
                                        : tile_pixel(level.background_patterns, tile, px, py);
                                if (color) {
                                    image.put_index(wx + px - camera_x, wy + py - camera_y, color,
                                                    palette);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    draw_patches(image, level, camera_x, camera_y, true);
}

void draw_player(gg::Screen& image, const sonic1_gg::Level& level, int frame, bool left, int x,
                 int y) {
    const auto palette = decode_palette(level.palette);
    const unsigned base = ((unsigned(frame) & 31) + (left ? 32 : 0)) * 8;
    // 5C1B describes four 8x16 sprites arranged as a 16x32 image. Each pair
    // of hardware tiles is vertical, unlike the row-major background blocks.
    for (int ty = 0; ty < 4; ++ty) {
        for (int tx = 0; tx < 2; ++tx) {
            draw_tile(image, level.player_patterns, base + (ty / 2) * 4 + tx * 2 + ty % 2,
                      x + tx * 8, y + ty * 8, palette, 16, true);
        }
    }
}
} // namespace sonic1_gg

namespace sonic1_gg {
namespace {
// Optional replacement for the sprite pattern bank and palette. The
// widescreen view uses it to draw objects that the LCD would not show while
// boss or goal art occupies the shared object tiles.
struct Art {
    const Bytes* patterns{};
    const Bytes* palette{};
};

void sprite(gg::Screen& image, const sonic1_gg::Level& level, int tile, int x, int y,
            Art art = {}) {
    const auto palette = decode_palette(art.palette ? *art.palette : level.palette);
    const auto& patterns =
        tile < 128 ? (art.patterns ? *art.patterns : level.object_patterns) : level.common_patterns;
    tile = (tile & 254) - (tile < 128 ? 0 : 128);
    draw_tile(image, patterns, tile, x, y, palette, 16, true);
    draw_tile(image, patterns, tile + 1, x, y + 8, palette, 16, true);
}

void mapping(gg::Screen& image, const sonic1_gg::Level& level, int address, int x, int y,
             Art art = {}) {
    if (!address) {
        return;
    }
    for (int row = 0; row < 3; ++row) {
        if (level.data(address + row * 6) == 255) {
            break;
        }
        for (int col = 0; col < 6; ++col) {
            int tile = level.data(address + row * 6 + col);
            if (tile < 254) {
                sprite(image, level, tile, x + col * 8, y + row * 16, art);
            }
        }
    }
}

// Widescreen: ordinary stage objects keep the stage's own sprite art while a
// boss, capsule or goal has replaced the shared tiles and sprite palette.
bool stage_art(const Game& game, const Object& o) {
    if (!game.widescreen() || !game.sprite_palette_kind) {
        return false;
    }
    switch (o.type) {
    case type::goal_sign:
    case type::explosion:
    case type::projectile:
    case type::green_hill_boss:
    case type::freed_animal_pausing:
    case type::freed_animal_bouncing:
    case type::capsule:
    case type::homing_trail:
    case type::jungle_boss:
    case type::homing_shot:
    case type::final_boss_orb:
    case type::bridge_boss:
    case type::labyrinth_boss:
    case type::final_boss:
    case type::ending_flyby:
    case type::ending_emerald:
    case type::lost_rings:
    case type::final_boss_flame:
        return false; // Boss, goal and capsule sequences, their shots and debris.
    default:
        return true;
    }
}
} // namespace

void draw_player_effects(gg::Screen& image, const Game& game, int cx, int cy) {
    // Invincibility stars and the ring sparkle are single 8x16 sprites; the
    // VDP shows a sprite one line below its stored Y. Earlier list entries
    // win overlaps, so draw the list backwards.
    for (auto it = game.effect_sprites.rbegin(); it != game.effect_sprites.rend(); ++it) {
        sprite(image, game.level, (*it)[0], (*it)[1] - cx, (*it)[2] - cy + 1);
    }
}

void draw_shield(gg::Screen& image, const Game& game, int x, int y, bool translucent) {
    // Original 5487-5498 uses one standing image or one curled image; neither
    // depends on facing. Preserve tile order from the player's 5C1B mapping.
    gg::Screen layer(16, 32);
    std::fill(layer.pixels.begin(), layer.pixels.end(), Color{0, 0, 0, 0});
    for (const auto& change : image.palette_changes) {
        layer.palette_changes.push_back({change.first - y, change.second});
    }
    const auto palette = decode_palette(game.level.palette);
    const int base = (game.sonic.flags & body_flag::curled) ? 8 : 0;
    for (int ty = 0; ty < 4; ++ty) {
        for (int tx = 0; tx < 2; ++tx) {
            draw_tile(layer, game.level.shield_patterns, base + (ty / 2) * 4 + tx * 2 + ty % 2,
                      tx * 8, ty * 8, palette, 16, true);
        }
    }
    for (int py = 0; py < 32; ++py) {
        for (int px = 0; px < 16; ++px) {
            auto color = layer.pixels[py * 16 + px];
            if (!color.a || x + px < 0 || y + py < 0 || x + px >= image.width ||
                y + py >= image.height) {
                continue;
            }
            if (translucent) {
                auto under = image.pixels[(y + py) * image.width + x + px];
                color = {std::uint8_t((unsigned(color.r) + under.r) / 2),
                         std::uint8_t((unsigned(color.g) + under.g) / 2),
                         std::uint8_t((unsigned(color.b) + under.b) / 2), 255};
            }
            image.put(x + px, y + py, color);
        }
    }
}

namespace {
void draw_object(gg::Screen& image, const sonic1_gg::Game& game, const Object& o, int cx, int cy,
                 bool enhanced_rings) {
    int x = o.x / 256 - cx, y = o.y / 256 - cy + 1;
    if (x > image.width || y > image.height || x + 48 < 0 || y + 48 < 0) {
        return;
    }
    if (o.staged && (o.type == type::goal_sign || o.type == type::capsule)) {
        // Widescreen: a waiting goal sign or capsule shows its own art
        // (64A8 / 78E8 load it when it starts) and palette.
        auto palette = game.level.palette;
        if (o.type == type::goal_sign) {
            std::copy(game.level.goal_palette.begin() + 34, game.level.goal_palette.end(),
                      palette.begin() + 34);
        } else {
            std::copy_n(game.level.boss_palette.begin() + 2, 30, palette.begin() + 34);
        }
        mapping(
            image, game.level, o.mapping, x, y + (o.type == type::capsule ? 16 : 0),
            {o.type == type::goal_sign ? &game.level.goal_patterns : &game.level.capsule_patterns,
             &palette});
        if (o.type == type::capsule) {
            // Its base is two 2x2 background patches (736B, 75A3), written once
            // it runs; draw them closed so the capsule appears whole.
            const auto colors = decode_palette(palette);
            for (int part = 0; part < 2; ++part) {
                const int patch = rom::capsule_patches + part * 12;
                // Patches snap to the 8x8 tile grid (3282).
                const int px = ((o.x / 256 + game.level.word(patch)) & ~7) - cx;
                const int py = ((o.y / 256 + 16 + game.level.word(patch + 2)) & ~7) - cy;
                for (int row = 0; row < 2; ++row) {
                    for (int col = 0; col < 2; ++col) {
                        const int entry = game.level.word(patch + 4 + (row * 2 + col) * 2);
                        draw_tile(image, game.level.capsule_patterns, (entry & 511) - 256,
                                  px + col * 8, py + row * 8, colors, 16, false);
                    }
                }
            }
        }
        return;
    }
    const Art art =
        stage_art(game, o) ? Art{&game.source.object_patterns, &game.source.palette} : Art{};
    if (o.type == type::seesaw) {
        // Plank ends and the weight, from the pose captured during the update.
        for (int part = 0; part < 3; ++part) {
            int address = part == 2 ? rom::seesaw_weight_tiles : rom::seesaw_plank_tiles;
            int dx = part == 0 ? 0 : part == 1 ? 40 : 44;
            int dy = part == 0   ? o.bridge_render[0] + 16
                     : part == 1 ? 44 - o.bridge_render[0]
                                 : o.bridge_render[1];
            while (game.level.data(address) < 128) {
                sprite(image, game.level, game.level.data(address++), x + dx, y + dy, art);
                dx += 8;
            }
        }
        return;
    }
    if (o.type == type::lost_rings) {
        if (enhanced_rings) {
            return;
        }
        // 7C53 maps a pair of tiles from one of three separately stored
        // lost-ring frames (ROM 23000). The player is hidden on these ticks.
        if (o.mapping) {
            int frame = game.level.data(rom::lost_ring_frames + 2 + o.state[1] * 3) - 1;
            const auto colors = decode_palette(game.level.palette);
            for (int col = 0; col < 2; ++col) {
                int tile = game.level.data(o.mapping + col) - 0xb4 + frame * 8;
                draw_tile(image, game.level.lost_ring_patterns, tile, x + col * 8, y, colors, 16,
                          true);
                draw_tile(image, game.level.lost_ring_patterns, tile + 1, x + col * 8, y + 8,
                          colors, 16, true);
            }
        }
        return;
    }
    for (auto part : o.extra_sprites) {
        sprite(image, game.level, part[0], part[1] - cx, part[2] - cy + 1, art);
    }
    mapping(image, game.level, o.mapping, x, y, art);
    const bool has_flame = o.type == type::green_hill_boss || o.type == type::bridge_boss ||
                           o.type == type::jungle_boss || o.type == type::labyrinth_boss ||
                           o.type == type::ending_flyby;
    if (has_flame && o.flame_tile >= 0) {
        sprite(image, game.level, o.flame_tile, o.flame_x - cx, o.flame_y - cy + 1);
    }
    // The speed bonus shown above the goal sign.
    if (o.type == type::goal_sign && game.goal.bonus_table) {
        int table = game.goal.bonus_table, offset = game.level.word(table + 2);
        if (offset >= 32768) {
            offset -= 65536;
        }
        mapping(image, game.level, game.level.word(table), x + offset, y - 14);
    }
    // A monitor showing its icon.
    if (o.mapping == rom::monitor_with_icon) {
        const int icon = o.type == type::continue_monitor                   ? 7
                         : o.type == type::checkpoint_monitor               ? 6
                         : o.type == type::life_monitor && (o.flags & 0x04) ? 0
                                                                            : o.type - 1;
        const auto colors = decode_palette(art.palette ? *art.palette : game.level.palette);
        for (int row = 0; row < 2; ++row) {
            for (int col = 0; col < 2; ++col) {
                draw_tile(image, game.level.monitor_icons, icon * 4 + col * 2 + row,
                          x + 4 + col * 8, y + row * 8, colors, 16, true);
            }
        }
    }
}

// Widescreen: the Bridge 3 boss (84AB) waits and sinks below the LCD's bottom
// edge, which the larger view shows. Hide it below the water surface instead,
// the top of block row 30 (world Y960) in this act's background art.
int clip_row(const sonic1_gg::Game& game, const Object& o, int cy) {
    return game.widescreen() && o.type == type::bridge_boss && game.level.act == 5 ? 960 - cy
                                                                                   : 1 << 30;
}

// Widescreen fades: draw into a transparent layer, then blend it in.
void draw_faded(gg::Screen& image, const sonic1_gg::Game& game, const Object& o, int cx, int cy,
                bool enhanced_rings, float alpha) {
    const int clip = clip_row(game, o, cy);
    if (alpha >= 1.f && clip >= image.height) {
        draw_object(image, game, o, cx, cy, enhanced_rings);
        return;
    }
    if (alpha <= 0.f) {
        return;
    }
    gg::Screen layer(image.width, image.height);
    layer.palette_changes = image.palette_changes;
    std::fill(layer.pixels.begin(), layer.pixels.end(), Color{0, 0, 0, 0});
    draw_object(layer, game, o, cx, cy, enhanced_rings);
    const std::size_t end = std::size_t(std::clamp(clip, 0, image.height)) * image.width;
    for (std::size_t i = 0; i < std::min(end, layer.pixels.size()); ++i) {
        const auto c = layer.pixels[i];
        if (!c.a) {
            continue;
        }
        auto& d = image.pixels[i];
        d = {std::uint8_t(d.r + (c.r - d.r) * alpha), std::uint8_t(d.g + (c.g - d.g) * alpha),
             std::uint8_t(d.b + (c.b - d.b) * alpha), 255};
    }
}
} // namespace

void draw_objects(gg::Screen& image, const sonic1_gg::Game& game, int cx, int cy,
                  bool enhanced_water, bool enhanced_rings, const ObjectFades* fades) {
    // Earlier sprite-list entries win overlapping pixels on the VDP. Reversing
    // the software draw order retains that priority without scanline limits.
    if (fades) {
        for (const auto& ghost : fades->ghosts()) {
            draw_faded(image, game, ghost.object, cx, cy, enhanced_rings,
                       float(ghost.ticks) / ObjectFades::fade_ticks);
        }
    }
    for (int i = 31; i > 0; --i) {
        const auto& o = game.objects[i];
        if (!(o.active || o.staged) || o.type == type::none || o.type == type::cloud ||
            o.type == type::ending_emerald) {
            continue;
        }
        draw_faded(image, game, o, cx, cy, enhanced_rings, fades ? fades->alpha(i) : 1.f);
    }
    // A98D/BF53 place clouds and the departing emerald ahead of ordinary
    // sprites in the hardware list; draw those reserved entries last.
    for (int i = 31; i > 0; --i) {
        const auto& o = game.objects[i];
        if (o.active && (o.type == type::cloud || o.type == type::ending_emerald)) {
            for (auto part : o.extra_sprites) {
                sprite(image, game.level, part[0], part[1] - cx, part[2] - cy + 1);
            }
        }
    }
    if (!enhanced_water) {
        for (auto part : game.water.wave_sprites) {
            sprite(image, game.level, part[0], part[1] - cx, part[2] - cy + 1);
        }
    }
    if (game.sonic.air_digit >= 0) {
        sprite(image, game.level, 128 + game.sonic.air_digit * 2, game.sonic.x / 256 - cx + 4,
               game.sonic.y / 256 - cy - 20 + 1);
    }
    // 54B0 reserves the first hardware sprites for the last struck bumper.
    if (game.special.bumper_ticks) {
        int x = game.special.bumper_x - cx, y = game.special.bumper_y - cy + 1;
        if (game.special.bumper_ticks < 6 || game.special.bumper_ticks >= 10) {
            mapping(image, game.level, rom::bumper_sparkle_layout, x, y);
        }
        sprite(image, game.level, 0x50, x + 12, y - 16 + game.special.bumper_ticks);
    }
}

void draw_flowers(gg::Screen& image, const sonic1_gg::Game& game, int cx, int cy) {
    auto colors = decode_palette(game.level.palette);
    for (const auto& o : game.objects) {
        if (o.type == type::flowers && (o.flags & 0x01)) {
            for (int half = 0; half < 2; ++half) {
                for (int row = 0; row < 2; ++row) {
                    for (int col = 0; col < 2; ++col) {
                        int entry = game.level.word(rom::flower_tiles + o.flower_frames[half] * 8 +
                                                    (row * 2 + col) * 2);
                        draw_tile(image, game.level.background_patterns, entry & 255,
                                  o.x / 256 - cx + col * 8, o.y / 256 - cy + half * 16 + row * 8,
                                  colors, 0, false);
                    }
                }
            }
        }
    }
}

// The HUD: rings, time (moved right in special stages) and lives, drawn with
// sprites like the original and attached to the edges of the view.
void draw_hud(gg::Screen& image, const sonic1_gg::Game& game) {
    auto digit = [&](int value, int x, int y) {
        sprite(image, game.level, 128 + 2 * (value % 10), x, y);
    };
    // Original GG HUD uses two ring digits, a minute/second clock and lives.
    // Keep it attached to the viewport when the optional larger view is enabled.
    sprite(image, game.level, 0xa6, -2, 1);
    sprite(image, game.level, 0xa8, 6, 1);
    digit(game.rings / 10, 18, 1);
    digit(game.rings, 26, 1);
    int seconds = game.elapsed_ticks / 60;
    int clock_x = game.level.act >= 28 ? 62 : 0;
    digit(seconds / 60, clock_x + 2, 17);
    sprite(image, game.level, 0xb0, clock_x + 10, 17);
    digit((seconds % 60) / 10, clock_x + 18, 17);
    digit(seconds % 10, clock_x + 26, 17);
    sprite(image, game.level, 0xa0, 0, image.height - 16);
    sprite(image, game.level, 0xa2, 8, image.height - 16);
    digit(std::min(game.lives, 9), 18, image.height - 16);
}
} // namespace sonic1_gg
