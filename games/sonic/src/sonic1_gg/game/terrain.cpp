// Terrain: collision of every body with the level, and the special terrain
// blocks (springs, spikes, ramps, conveyors, water) that act on Sonic.

#include "sonic1_gg/game/arithmetic.hpp"
#include "sonic1_gg/game/game.hpp"
#include "sonic1_gg/game/rom_tables.hpp"
#include <stdexcept>
#include <string>

namespace sonic1_gg {

using namespace body_flag;
namespace flag = body_flag;

// Movement and terrain collision for Sonic and every object (2CD4).
//
// The level is a grid of 32x32-pixel blocks. For each block type the level data
// holds four contour tables with 32 entries each (one per pixel row or column):
// where the solid surface is when approached from the left, right, above or
// below. An entry of 0x80 means "no surface"; a negative entry means the whole
// row or column is solid.
//
// The body first moves by its full velocity on both axes; then exactly two
// sensor points are tested:
//
//   * a horizontal sensor at mid-height, on the leading edge (right edge when
//     moving right or stopped, left edge when moving left), and
//   * a vertical sensor at the horizontal centre, on the bottom edge when
//     moving down or stopped, the top edge when moving up.
//
// A sensor inside the solid part of its block snaps the body back to the
// contour and stops that axis. Floors also carry a sideways push (slopes and
// conveyors) and walls a vertical push. Snapping changes the whole-pixel part
// of the position only; the 1/256 fraction is kept, as in the original.
void Game::integrate_body(Body& b) const {
    b.x = wrap24(b.x + b.vx);
    b.y = wrap24(b.y + b.vy);
    if (b.flags & no_terrain) {
        return;
    }

    // --- Horizontal sensor ---
    const int x_offset = ((b.vx & 0x8000) == 0) ? b.width : 0; // Leading edge.
    int sx = b.pixel_x() + x_offset;
    int sy = b.pixel_y() + b.height / 2;
    auto block = level.block_at(sx, sy);
    const auto& side = x_offset ? level.right_contours : level.left_contours;
    int boundary = signed8(side[block * 32 + (sy & 31)]);
    b.flags &= ~wall_contact;
    if (boundary != -128 &&
        (boundary < 0 || (b.vx >= 0 ? (sx & 31) >= boundary : (sx & 31) < boundary))) {
        b.set_pixel_x((sx & ~31) + boundary - x_offset);
        b.flags |= wall_contact;
        b.contact_class = level.block_attributes[block] & 63;
        b.vx = 0;
        b.vy += signed8(level.wall_push[block]);
    }

    // --- Vertical sensor ---
    const int y_offset = ((b.vy & 0x8000) == 0) ? b.height : 0;
    sx = b.pixel_x() + b.width / 2;
    sy = b.pixel_y() + y_offset;
    block = level.block_at(sx, sy);
    const auto& vertical = y_offset ? level.floor_contours : level.ceiling_contours;
    boundary = signed8(vertical[block * 32 + (sx & 31)]);
    // Each block type can shift the sensor's row (vertical_tolerance), which
    // lets Sonic stick to slopes when running down them.
    const int local = ((sy & 31) + level.vertical_tolerance[block]) & 255;
    b.flags &= ~grounded;
    if (boundary != -128 && (boundary < 0 || (b.vy >= 0 ? local >= boundary : local < boundary))) {
        if (b.vy >= 0 && boundary >= 0) {
            b.flags |= grounded;
        }
        b.set_pixel_y((sy & ~31) + boundary - y_offset);
        b.contact_class = level.block_attributes[block] & 63;
        b.vy = 0;
        b.vx += signed8(level.floor_push[block]);
    }
}

// Terrain actions (dispatch table at 5BE1). Each block type has an action
// number, triggered when Sonic's centre (x + 8, y + 16) is inside such a block.
// This runs during Sonic's update, before acceleration and gravity, so these
// are not after-the-fact forces.
void Sonic::terrain_actions(Game& game) {
    const auto& level = game.level;
    const int cx = pixel_x() + 8;
    const int cy = pixel_y() + 16;
    const int action = level.terrain_actions[level.block_at(cx, cy)];
    const int local_x = cx & 31;
    switch (action) {
    case 0: // 5759: ordinary terrain; in acts without water it clears the water bit.
        if (!level.has_water()) {
            flags &= ~flag::underwater;
        }
        break;
    case 1: // 5763: spikes hurt even a curled or jumping Sonic.
        take_hit(game);
        break;
    case 2: // 576B: ramp. Rolling or jumping off its lip at > 3 px/update
            // launches Sonic upward at twice the whole-pixel part of his speed.
        if (local_x >= 26 && (flags & flag::curled) && vx >= 0x301) {
            vy = -2 * (vx / 256) * 256;
            game.sound(Effect::Launch);
        }
        break;
    case 3: // 57AC: spring facing left, in the right half of its block.
        if (local_x >= 16) {
            vx = -2048;
            flags |= flag::facing_left;
            game.sound(Effect::Spring);
        }
        break;
    case 4:
    case 9: { // 57CA/5853: upward springs, fired on the first grounded update.
        const bool aligned = action == 4 ? local_x >= 16 : (local_x >= 8 && local_x < 24);
        if (aligned && grounded() && !(previous_flags & flag::grounded)) {
            hurt = false;
            vy = -3072;
            game.sound(Effect::Spring);
        }
        break;
    }
    case 5: // 57F3: spring facing right, in the left half of its block.
        if (local_x < 16) {
            hurt = false;
            vx = 2048;
            flags &= ~flag::facing_left;
            game.sound(Effect::Spring);
        }
        break;
    case 6:
    case 7: // 5815/582D: conveyors move a grounded Sonic left (1.5 px) or right (2 px).
        if (grounded()) {
            x = (x + (action == 6 ? -384 : 512)) & 0xffffff;
        }
        break;
    case 11: // 5888: teleporter; the table at 58E0 lists its cells and destinations.
        if (!game.area_transition && local_x >= 8 && local_x < 24) {
            for (int i = 0; i < 5; ++i) {
                const int entry = rom::teleporters + i * 5;
                if (level.data(entry) == ((cx >> 5) & 255) &&
                    level.data(entry + 1) == ((cy >> 5) & 255)) {
                    game.teleport_table = entry + 2;
                    game.teleport_ticks = 80;
                    game.sound(Effect::Roll);
                    break;
                }
            }
        }
        break;
    case 23:
    case 28: // 5AC6/5BBF: electric floor, dangerous in the live part of its cycle.
        if (game.electric.ticks >= 130 && (action == 23 || (local_x >= 6 && local_x < 26)) &&
            !dead) {
            take_hit(game);
        }
        break;
    case 27: // 5BAA: while Sonic stands above the camera's top, controls are ignored.
        if (grounded() && pixel_y() < game.camera.y) {
            game.current_input = {};
        }
        break;
    case 13:
    case 14:
    case 15:
    case 16: // 5919-5963: strong water currents, right (13, 14) or left (15, 16),
             // at 5 or 6 px per update. They carry Sonic for six updates.
        vx = (action < 15 ? 1 : -1) * (action == 13 || action == 15 ? 1280 : 1536);
        if (action < 15) {
            flags &= ~flag::facing_left;
        } else {
            flags |= flag::facing_left;
        }
        current_ticks = 6;
        vy = (vy & 255) + 1024;
        flags &= ~flag::curled;
        break;
    case 12: // 58F9: a gentle current slows Sonic by 8/256 px per update.
        vx = signed24(vx - 8);
        if (!(flags & flag::underwater)) {
            game.sound(Effect::WaterEnter);
        }
        flags |= flag::underwater;
        break;
    case 8: // 5845: water; the underwater constants apply from the next update.
        if (!(flags & flag::underwater)) {
            game.sound(Effect::WaterEnter);
        }
        flags |= flag::underwater;
        break;
    case 10: // 587F: falling through a waterfall plays the launch sound.
        if (vy >= 0) {
            game.sound(Effect::Launch);
        }
        break;
    case 24:
    case 25:
    case 26: // Collapsing bridge planks (bridge.cpp).
        game.collapse_bridge(action);
        break;
    case 17:
    case 18:
    case 19:
    case 20:
    case 21:
    case 22: // Special stage bumpers and springs (special_stage.cpp).
        game.special_terrain(action);
        break;
    default: // 4C1E ignores action numbers >= 29.
        if (action < 29) {
            throw std::runtime_error("Unrecovered terrain action " + std::to_string(action));
        }
        break;
    }
}

} // namespace sonic1_gg
