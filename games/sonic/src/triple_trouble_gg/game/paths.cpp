// Paths (tubes): the player follows a curve from tables in bank 0E, by his
// position along it (D3D0-D3D2: a fraction byte and a 16-bit step), from
// the origin where he entered (373D, 378C). States 0C (rightward, 3542),
// 0D (leftward, 36B3) and, in zone 2, 13 (35B7); their frames come from
// ADB7.

#include "triple_trouble_gg/game/game.hpp"

namespace triple_trouble_gg {

namespace {
// The curves: Y and X offsets from the origin by step.
struct Curve {
    Table y, x;
};

constexpr Curve rightward{{0x0E, 0x9B2D}, {0x0E, 0x9EC5}}; // 34F1
constexpr Curve leftward{{0x0E, 0xA1DB}, {0x0E, 0xA573}};  // 365B
constexpr Curve rising{{0x0E, 0xA909}, {0x0E, 0xAC6D}};    // 35B7
// ADE6 (Sonic), AE4C (Tails): pairs of frames by step / 13.
constexpr Table sonic_path_frames{0x0C, 0xADE6};
constexpr Table tails_path_frames{0x0C, 0xAE4C};

std::uint16_t negate(std::uint16_t v) {
    return std::uint16_t(-v);
}
} // namespace

std::uint16_t Game::path_step() const {
    return std::uint16_t(player.path[1] | player.path[2] << 8);
}

// 34F1 / 365B / 35B7: `distance` further along the curve (the 24-bit
// position takes the 16-bit distance's carry); the position from the
// curve's offsets at the new step.
void Game::follow_curve(Object& o, const Table& y, const Table& x, std::uint16_t distance) {
    const unsigned low = unsigned(player.path[0] | player.path[1] << 8) + distance;
    player.path[0] = u8(low);
    player.path[1] = u8(low >> 8);
    if (low > 0xFFFF) {
        ++player.path[2];
    }
    const unsigned step = path_step() * 2u;
    o.y = std::uint16_t(o.origin_y + rom.word(y, step));
    o.x = std::uint16_t(o.origin_x + rom.word(x, step));
}

// 3542 (03E4, state 0C): rightward. For the first 90 steps the speed drops
// by 0A per frame (too slow: he falls out, 3591); after them it rises by
// 0C and the path byte becomes 1. At step 180 he leaves running (rolling
// with flags bit 1) at the top speed.
void Game::path_right(Object& o) {
    follow_curve(o, rightward.y, rightward.x, o.vx); // 34F1
    if (path_step() < 0x90) {
        if (o.vx < 0x0A) {
            fall_off_path(o); // 3591
            return;
        }
        o.vx = std::uint16_t(o.vx - 0x0A);
    } else {
        o.path = 1;
        o.vx = std::uint16_t(o.vx + 0x0C);
    }
    if (path_step() < 0x180) {
        return;
    }
    o.next_state = (o.flags & 0x02) ? 0x09 : 0x06;
    o.vx = player.top_speed;
    player.path_end = 0x10;
}

// 36B3 (03EA, state 0D): the same leftward (speeds negative; the path byte
// becomes 0).
void Game::path_left(Object& o) {
    follow_curve(o, leftward.y, leftward.x, negate(o.vx)); // 365B
    if (path_step() < 0x90) {
        const std::uint16_t speed = negate(o.vx);
        if (speed < 0x0A) {
            fall_off_path(o); // 3717
            return;
        }
        o.vx = negate(std::uint16_t(speed - 0x0A));
    } else {
        o.path = 0;
        o.vx = std::uint16_t(o.vx - 0x0C);
    }
    if (path_step() < 0x180) {
        return;
    }
    o.next_state = (o.flags & 0x02) ? 0x09 : 0x06;
    o.vx = negate(player.top_speed);
    player.path_end = 0x10;
}

// 35B7 (03E7, state 13 in zone 2): a curve upward; at step 1A0 he leaves
// it jumping (state 0A) straight up at the top speed.
void Game::path_rise(Object& o) {
    follow_curve(o, rising.y, rising.x, o.vx);
    if (path_step() < 0x90) {
        if (o.vx < 0x0A) {
            fall_off_path(o);
            return;
        }
        o.vx = std::uint16_t(o.vx - 0x0A);
    } else {
        o.path = 1;
        o.vx = std::uint16_t(o.vx + 0x0C);
    }
    if (path_step() < 0x1A0) {
        return;
    }
    o.next_state = 0x0A;
    o.vy = player.top_speed;
    o.vx = 0;
    o.flags |= 0x03;
    o.terrain &= u8(~0x02);
}

// 3591 / 3717 with 3B9F: too slow: back toward the entry (8 pixels when at
// or left of the origin, otherwise 2 back) and falling (state 1D).
void Game::fall_off_path(Object& o) {
    if (o.origin_x >= o.x) {
        o.x = std::uint16_t(o.x + 8);
    } else {
        o.x = std::uint16_t(o.x - 2);
    }
    o.next_state = 0x1D;
    o.vy = 0x0080;
    o.flags = u8((o.flags | 0x01) & ~0x02);
    o.terrain &= u8(~0x02);
}

// AD85 (state 1D): fallen out of a path: the frame by the height fallen
// (16 pixels per pair, low byte only).
void Game::animate_path_fall(Object& o) {
    const unsigned pair = (u8(o.origin_y - o.y) >> 4) * 2u;
    const Table frames = o.type == 0x01 ? sonic_path_frames : tails_path_frames;
    const u8 first = rom.byte(frames, pair);
    o.frame = first == o.frame ? rom.byte(frames, pair + 1) : first;
    o.frame_ticks = 0x06;
}

// 9CBE (state 1D): shown, drifting left at 1 while moving as usual (B05B);
// landing walks.
void Game::path_fall(Object& o) {
    o.display &= u8(~display_flag::hidden);
    o.vx = 0xFF00;
    player_move(o); // B05B
    if (o.contact & 0x02) {
        to_walking(o); // 03F6
    }
}

// ADB7: on a path the frame follows the step (one pair of frames per 13
// steps, alternating within the pair); rolling, the spinning animation.
void Game::animate_path(Object& o) {
    if (o.flags & 0x02) {
        animate_spinning(o); // ACF0
        return;
    }
    const unsigned pair = u8(path_step() / 0x0D) * 2u;
    const Table frames = o.type == 0x01 ? sonic_path_frames : tails_path_frames;
    const u8 first = rom.byte(frames, pair);
    o.frame = first == o.frame ? rom.byte(frames, pair + 1) : first;
    o.frame_ticks = 0x06;
}

} // namespace triple_trouble_gg
