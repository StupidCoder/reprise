// Special stages: the countdown clock, bumpers and springs in the terrain, the
// moving bumper and the flippers.
//
// Sonic is always curled here (Game::update_player) and uses special stage
// constants in the air: low friction and a high speed limit.

#include "sonic1_gg/game/arithmetic.hpp"
#include "sonic1_gg/game/game.hpp"
#include "sonic1_gg/game/rom_tables.hpp"
#include <algorithm>
#include <stdexcept>

namespace sonic1_gg {

using namespace body_flag;

// The countdown (340B-3439). The fraction counts UP to 60; each rollover takes
// a second off. At 0:00 the stage still runs for a final sixty updates before
// it times out.
void Game::update_special_clock() {
    int seconds = elapsed_ticks / 60;
    int fraction = elapsed_ticks % 60 + 1;
    if (fraction == 60) {
        fraction = 0;
        if (--seconds < 0) {
            seconds = 0;
            finish_ticks = 1;
            special.time_out = true;
        }
    }
    elapsed_ticks = seconds * 60 + fraction;
}

// Special stage terrain actions (bank 1, 5973-5AC5). Like all terrain actions
// they run from Sonic's centre sensor during his update, before acceleration
// and gravity.
void Game::special_terrain(int action) {
    auto& p = sonic;
    // 59CC: reverse the horizontal speed if Sonic hits the bumper's side while
    // moving toward its centre, then speed it up by half. The +1 is applied
    // whether or not the speed was negated (one's complement plus one), as in
    // the original.
    auto horizontal_bounce = [&] {
        int speed = p.vx;
        const bool left_half = ((p.pixel_x() + 8) & 31) < 16;
        if (left_half ? speed >= 0 : speed < 0) {
            speed = ~speed;
        }
        speed = signed24(speed + 1);
        p.vx = signed24(speed + (speed >> 1));
    };
    // Reverse the vertical speed; an upward bounce gets an extra 56/256.
    auto vertical_bounce = [&] {
        p.vy = signed24(-p.vy);
        if (p.vy < 0) {
            p.vy = signed24(p.vy - 56);
        }
    };
    // Push Sonic out of the bumper's block, against his direction of motion.
    auto push_out = [&] {
        p.set_pixel_x(((p.pixel_x() + 8) & ~31) + (p.vx < 0 ? 32 : -16));
    };

    switch (action) {
    case 17: // Round bumper: bounce on both axes, ten points. One shared timer
             // limits the rebound rate and drives the sparkle graphic.
        if (special.bumper_ticks >= 8) {
            return;
        }
        horizontal_bounce();
        vertical_bounce();
        special.bumper_x = (p.pixel_x() + 8) & ~31;
        special.bumper_y = (p.pixel_y() + 16) & ~31;
        special.bumper_ticks = 16;
        add_score(10);
        sound(Effect::SpecialStage);
        break;
    case 18:
    case 19:
    case 20: // Springs of three strengths: 10, 12 and 14 px/update upward.
        p.vy = -(10 + 2 * (action - 18)) * 256;
        sound(Effect::Spring);
        break;
    case 21:
    case 22: // Wall (21) and diagonal (22) bumpers flash a background colour.
        if (flash.ticks) {
            return;
        }
        push_out();
        if (action == 22) {
            horizontal_bounce();
            vertical_bounce();
        } else {
            // Reverse and add 1 px/update.
            p.vx = signed24(-p.vx);
            p.vx = signed24(p.vx + (p.vx < 0 ? -256 : 256));
        }
        flash.ticks = 4;
        flash.color = 14; // Background palette entry 14.
        flash.value = 0xfff;
        sound(Effect::SpecialStage);
        break;
    default:
        throw std::runtime_error("Unrecovered special terrain " + std::to_string(action));
    }
}

// A horizontal bumper bar moving 1 px/update, reversing every 96 updates
// (bank 2, 9AD0-9B42). Landing on it bounces Sonic back up, followed by a
// short cooldown.
void Game::update_moving_bumper(Object& o) {
    o.flags |= no_terrain;
    o.width = 28;
    o.height = 6;
    o.mapping = rom::bumper_bar_layout;
    auto& cooldown = o.state[0];
    auto& timer = o.state[1];
    o.vx = timer < 96 ? -256 : 256;
    if (++timer >= 192) {
        timer = 0;
    }
    if (cooldown) {
        --cooldown;
    } else if (overlaps(o, 2, 6)) {
        sonic.vy = signed24(-sonic.vy - sonic.previous_gravity);
        cooldown = 8;
        sound(Effect::SpecialStage);
    }
}

// A flipper raised by the jump button (bank 2, 9868-9972 / 9A84-9ACF).
//
// Its pose (0..5) rises while the button is held and falls when it is
// released; old_pose remembers the pose before the latest press. Sonic rests on the
// flipper's contour for the current pose (three contour tables). A fast fall
// bounces him off at half speed. A flipper that is just being raised under him
// launches him with the per-position velocities of tables 99D3/9A13; a resting
// or fully raised flipper only nudges him along its slope.
void Game::update_flipper(Object& o) {
    o.flags |= no_terrain;
    o.mapping = rom::flipper_down;
    auto& pose = o.state[0];
    auto& old_pose = o.state[1];
    if (current_input.jump) {
        old_pose = pose;
        if (pose < 5) {
            ++pose;
        }
    } else if (pose) {
        --pose;
    }
    int contour, inset_x, inset_y;
    o.width = 30;
    o.height = 22;
    if (pose < 1) {
        contour = rom::flipper_contour_down;
        inset_x = 12;
        inset_y = 20;
    } else if (pose < 4) {
        o.mapping = rom::flipper_middle;
        contour = rom::flipper_contour_middle;
        inset_x = 15;
        inset_y = 8;
    } else {
        o.mapping = rom::flipper_up;
        contour = rom::flipper_contour_up;
        inset_x = 26;
        inset_y = 2;
    }
    if (!overlaps(o, inset_x, inset_y) || sonic.vy < 0) {
        return;
    }
    const int local = (sonic.pixel_x() + 8) & 31;
    sonic.set_pixel_y(o.pixel_y() + signed8(level.data(contour + local)));
    if (((sonic.vy >> 8) & 255) >= 3) {
        sonic.vy = signed24(-sonic.vy) >> 1;
        return;
    }
    if (pose < 1 || pose >= 4) {
        sonic.vy = -sonic.previous_gravity;
        sonic.vx = signed24(sonic.vx + (pose < 1 ? -4 : 26));
        return;
    }
    if (old_pose >= pose) {
        return;
    }
    sonic.vx = signed24(sonic.vx + level.word(rom::flipper_launch_x + local * 2) - 65536);
    sonic.vy = signed24(~sonic.vy + level.word(rom::flipper_launch_y + local * 2) - 65536);
}

} // namespace sonic1_gg
