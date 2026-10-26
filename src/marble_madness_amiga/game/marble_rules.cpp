// The marble's rules: one world update per marble, as $114402 runs them.
//
// Arithmetic follows the original's widths where they change a result
// (16-bit products with muls.w, word wraparound, logical shifts); those
// places are commented.
#include "marble_madness_amiga/game/race.hpp"
#include <cstdlib>

namespace marble_madness::game {
namespace {

std::int16_t s16(std::int32_t value) { return std::int16_t(value); }
std::int32_t abs32(std::int32_t value) { return value < 0 ? -value : value; }

// Program tables (research layout addresses).
constexpr std::uint32_t speed_curve = 0x10204A;      // 17 words: friction and roll-sound by speed
constexpr std::uint32_t edge_depth = 0x119310;       // bytes: how far a marble sinks over an edge, by distance
constexpr std::uint32_t wall_push = 0x119588;        // words: push away from a wall, by distance (0-3)
constexpr std::uint32_t goal_push_x = 0x1134A0;      // bytes per course: the push past the goal
constexpr std::uint32_t goal_push_y = 0x1134A6;
constexpr std::uint32_t goal_sounds = 0x102104;      // longs per player
constexpr std::uint32_t cosine_quarter = 0x101F4C;   // 101 words: cosine of 0-100 (a quarter turn), 1.0 = $1000
constexpr std::uint32_t sparkle_offsets = 0x1020C4;  // 8 x (x, y) bytes: the respawn sparkles' offsets

// marbdat table offsets ($115448 points at it).
constexpr std::uint32_t anim_bounce = 0x00;
constexpr std::uint32_t anim_roll_fast = 0x04;
constexpr std::uint32_t anim_roll = 0x08;
constexpr std::uint32_t anim_break = 0x0C;
constexpr std::uint32_t anim_appear = 0x10;
constexpr std::uint32_t anim_break_dust = 0x14;
constexpr std::uint32_t anim_dizzy = 0x18;
constexpr std::uint32_t bonus_points = 0x24;         // words
constexpr std::uint32_t bonus_pictures = 0x46;       // longs

// The octagonal distance the game uses for "how close": both offsets
// times 16 (word arithmetic), the larger plus 3/8 of the smaller.
std::uint32_t octagonal(std::int16_t dx, std::int16_t dy) {
    const std::uint32_t a = std::uint16_t(std::uint16_t(std::abs(dx)) << 4);
    const std::uint32_t b = std::uint16_t(std::uint16_t(std::abs(dy)) << 4);
    return a > b ? a + (b >> 3) * 3 : b + (a >> 3) * 3;
}

} // namespace

std::int32_t Race::ground_at(std::int32_t x, std::int32_t y) {
    probe_.invalidate();
    probe_.locate(terrain_, x, y, 0);
    return probe_.ground_height();
}

// $1141A0: remember where the marble last was on the ground.
void Race::save_grounded(Marble& m) {
    if (m.air == 0) {
        m.cell_x = s16(m.x >> 19);
        m.cell_y = s16(m.y >> 19);
        m.fall_start_z = m.z;
        m.saved_feature = m.feature;
    }
}

// $10EADC: the marble's screen position, kept with the two before it.
void Race::project(Marble& m, bool reload) {
    if (reload) {
        probe_.invalidate();
    }
    locate(m);
    std::int16_t z = s16(m.z >> 16);
    const auto code = m.region_code;
    if ((code == 0x2F || code == 0x30 || code == 0x31) && (probe_.x() & 1) != 0 && abs32(m.vx) > 0x4000) {
        z = s16(z + 2); // the cog wheels' rattle
    }
    const auto screen_x = s16(probe_.y() - probe_.x() + 0x88);
    const auto screen_y = s16(s16(view_) + z + 0x6C - s16((probe_.x() + probe_.y()) >> 1));
    const std::int32_t packed = std::int32_t(std::uint32_t(std::uint16_t(screen_x)) << 16 | std::uint16_t(screen_y));
    if (m.screen[0] != packed && packed != m.screen[1]) {
        m.screen[2] = m.screen[1];
        m.screen[1] = m.screen[0];
        m.screen[0] = packed;
    }
}

// $1134C4: the player's push, or gravity while in the air.
void Race::push(Marble& m) {
    if (m.status == 2 || m.status == 3) {
        return;
    }
    if (game_mode == 1) {
        return; // the attract demo's driver ($1134BC) is not rebuilt yet
    }
    const auto code = m.region_code;
    const bool steerable = code == 0 || code == 0x3B || code == 0x2D || code == 0x2E || code == 0x38 ||
                           code == 0x39 || code == 0x3A || code == 0x2F || code == 0x30 || code == 0x31;
    if (m.air == 2 || !steerable) {
        if (m.air != 0) {
            m.vz -= 0x6000;
            if (m.vz < -0x50000) {
                m.vz = -0x50000;
            }
        }
    } else {
        const Push& p = push_[&m == &marbles[0] ? 0 : 1];
        std::int32_t ax = p.forward * 0x160;
        std::int32_t ay = -p.side * 0x160;
        if (m.mode == Marble::landing || m.mode == Marble::bumped) {
            // Less grip while bouncing: the bounce height takes it away.
            int grip = 0x1F - std::int8_t(m.bounce);
            if (m.mode == Marble::bumped && grip < 4) {
                grip = 4;
            }
            ax = s16(std::int32_t(std::uint32_t(ax) >> 8)) * grip * 8;
            ay = s16(std::int32_t(std::uint32_t(ay) >> 8)) * grip * 8;
        }
        m.vx += ax;
        m.vy += ay;
    }
    cap_speed(m);
}

// $1153E6 / $115290: the speed limit (faster while bouncing), and the
// rolling score: 10 points per distance step.
void Race::cap_speed(Marble& m) {
    const std::uint32_t limit = m.mode == Marble::rolling ? 0x40000 : 0x50000;
    const std::uint32_t ax = std::uint32_t(abs32(m.vx)), ay = std::uint32_t(abs32(m.vy));
    // muls.w: only the low word of the smaller component's eighth counts.
    const std::uint32_t speed = ay < ax ? ax + std::uint32_t(s16(std::int32_t(ay >> 3)) * 3)
                                        : ay + std::uint32_t(s16(std::int32_t(ax >> 3)) * 3);
    if (is_player(m)) {
        const auto& p = files_->program();
        const int index = int(speed >> 15) & 15;
        const int fraction = int(speed >> 12) & 7;
        const std::int16_t lo = p.s16(speed_curve + std::uint32_t(index) * 2);
        const std::int16_t hi = p.s16(speed_curve + std::uint32_t(index + 1) * 2);
        const std::int32_t step = (lo + ((fraction * s16(hi - lo)) >> 3)) >> 9;
        m.roll_sound = s16(m.roll_sound + step);
        if (m.roll_sound > 0xA0) {
            m.roll_sound = s16(m.roll_sound - 0xA0);
            m.score += 10;
            score_changed_ |= 1 << m.player;
        }
    }
    if (limit < speed) {
        const std::uint32_t scale = (limit << 6) / ((speed >> 8) & 0xFFFF);
        m.vx = (s16(m.vx >> 8) * s16(std::int32_t(scale))) >> 6;
        m.vy = (s16(m.vy >> 8) * s16(std::int32_t(scale))) >> 6;
    }
}

// $115088: rolling friction. Kind 0 normal, 2 slippery-sticky (x4), 3
// different along x and y, 4 a conveyor (speeds up).
void Race::friction(Marble& m, int kind) {
    const std::uint32_t ax = std::uint32_t(abs32(m.vx)), ay = std::uint32_t(abs32(m.vy));
    std::uint32_t speed = ax > ay ? ax + (ay >> 3) * 3 : ay + (ax >> 3) * 3;
    const auto& p = files_->program();
    const int index = int(speed >> 15) & 15;
    const int fraction = int(speed >> 12) & 7;
    const std::int16_t lo = p.s16(speed_curve + std::uint32_t(index) * 2);
    const std::int16_t hi = p.s16(speed_curve + std::uint32_t(index + 1) * 2);
    std::int32_t loss = lo + ((s16(hi - lo) * s16(fraction)) >> 3);
    if (speed < 0x100) {
        speed = 0x100;
    }
    auto reduced = [&](std::int32_t by) { return speed > std::uint32_t(by) ? speed - std::uint32_t(by) : 0u; };
    std::uint32_t new_x, new_y;
    if (kind == 2) {
        loss <<= 2;
        new_x = new_y = reduced(loss);
    } else if (kind == 3) {
        new_y = reduced(loss);
        loss *= 5;
        new_x = reduced(loss);
    } else if (kind == 4) {
        new_x = new_y = speed + std::uint32_t(loss >> 2);
    } else {
        new_x = new_y = reduced(loss);
    }
    const std::uint32_t divisor = (speed >> 8) & 0xFFFF;
    const auto scale_x = s16(std::int32_t((new_x << 6) / divisor));
    const auto scale_y = kind == 3 ? s16(std::int32_t((new_y << 6) / divisor)) : scale_x;
    m.vx = (s16(m.vx >> 8) * scale_x) >> 6;
    m.vy = (s16(m.vy >> 8) * scale_y) >> 6;
}

// $114FF0: the slope pulls the marble downhill (uphill on Silly). Steep
// slopes pull four times as hard; the probe keeps the scaled slope.
void Race::slope_force(Marble& m) {
    if (probe_.slope_x < -0xB || probe_.slope_x > 0xB) {
        probe_.slope_x = s16(probe_.slope_x << 2);
    }
    if (probe_.slope_y < -0xB || probe_.slope_y > 0xB) {
        probe_.slope_y = s16(probe_.slope_y << 2);
    }
    if (course_ == data::Course::silly) {
        m.vx += std::int32_t(probe_.slope_x) << 11;
        m.vy += std::int32_t(probe_.slope_y) << 11;
    } else {
        m.vx -= std::int32_t(probe_.slope_x) << 11;
        m.vy -= std::int32_t(probe_.slope_y) << 11;
    }
}

// $10EF4C: walls stop the marble: the velocity component toward a wall
// turns away from it (keeping its size) and the position along it goes
// back to where the update started.
void Race::bounce_off_edges(Marble& m) {
    const std::int16_t moved_x = s16(start_cell_x_ - probe_.cell_x());
    const std::int16_t moved_y = s16(start_cell_y_ - probe_.cell_y());
    const bool left_cell = start_cell_x_ != probe_.cell_x() && start_cell_y_ != probe_.cell_y();
    const auto& kind = probe_.side_kind;
    const auto& height = probe_.side_height;
    const auto& corner = probe_.corner_height;
    const int fx = probe_.fraction_x(), fy = probe_.fraction_y();
    const auto bits = probe_.corner_bits;
    auto positive = [](std::int32_t v) { return v < 0 ? -v : v; }; // $10EF14
    auto negative = [](std::int32_t v) { return v > 0 ? -v : v; }; // $10EF30
    std::int32_t vx = m.vx, vy = m.vy;
    if ((kind[0] != 0 && kind[0] < 3 && fx < 4 && height[0] > 3) || (kind[2] > 2 && moved_x == 1 && height[2] > 3)) {
        vx = positive(vx);
    }
    if ((kind[1] != 0 && kind[1] < 3 && fy > 4 && height[1] > 3) || (kind[3] > 2 && moved_y == -1 && height[3] > 3)) {
        vy = negative(vy);
    }
    if ((kind[2] != 0 && kind[2] < 3 && fx > 4 && height[2] > 3) || (kind[0] > 2 && moved_x == -1 && height[0] > 3)) {
        vx = negative(vx);
    }
    if ((kind[3] != 0 && kind[3] < 3 && fy < 4 && height[3] > 3) || (kind[1] > 2 && moved_y == 1 && height[1] > 3)) {
        vy = positive(vy);
    }
    if (((bits & 0x01) && moved_x != -1 && moved_y != 1 && fx < 4 && fy > 4 && corner[0] > 3) ||
        ((bits & 0x40) && left_cell && corner[2] > 3)) {
        vy = negative(vy);
        vx = positive(vx);
    }
    if (((bits & 0x02) && moved_x != 1 && moved_y != 1 && fx > 4 && fy > 4 && corner[1] > 3) ||
        ((bits & 0x80) && left_cell && corner[3] > 3)) {
        vy = negative(vy);
        vx = negative(vx);
    }
    if (((bits & 0x04) && moved_x != 1 && moved_y != -1 && fx > 4 && fy < 4 && corner[2] > 3) ||
        ((bits & 0x10) && left_cell && corner[0] > 3)) {
        vy = positive(vy);
        vx = negative(vx);
    }
    if (((bits & 0x08) && moved_x != -1 && moved_y != -1 && fx < 4 && fy < 4 && corner[3] > 3) ||
        ((bits & 0x20) && left_cell && corner[1] > 3)) {
        vy = positive(vy);
        vx = positive(vx);
    }
    if (m.vx != vx) {
        bounce_x_ = true;
        m.x = start_x_;
        m.vx = vx;
    }
    if (m.vy != vy) {
        bounce_y_ = true;
        m.y = start_y_;
        m.vy = vy;
    }
    if (bounce_x_ || bounce_y_) {
        sound(7);
    }
}

// $119328: a nudge away from walls the marble is close to.
void Race::push_off_walls(Marble& m) {
    const auto& p = files_->program();
    const int fx = probe_.fraction_x(), fy = probe_.fraction_y();
    const auto table = [&](int i) { return p.s16(wall_push + std::uint32_t(i) * 2); };
    const auto& kind = probe_.side_kind;
    const auto& height = probe_.side_height;
    const auto bits = probe_.corner_bits;
    const int rx = 7 - fx, ry = 7 - fy;
    // Only the entries for distances 0-3 are ever used.
    const std::int16_t push_x = fx < 4 ? table(fx) : 0, pull_x = rx < 4 ? s16(-table(rx)) : 0;
    const std::int16_t push_y = fy < 4 ? table(fy) : 0, pull_y = ry < 4 ? s16(-table(ry)) : 0;
    std::int32_t vx = m.vx, vy = m.vy;
    if (kind[0] != 0 && kind[0] < 3 && fx < 4 && height[0] > 0) {
        vx += push_x;
    }
    if (kind[1] != 0 && kind[1] < 3 && ry < 4 && height[1] > 0) {
        vy += pull_y;
    }
    if (kind[2] != 0 && kind[2] < 3 && rx < 4 && height[2] > 0) {
        vx += pull_x;
    }
    if (kind[3] != 0 && kind[3] < 3 && fy < 4 && height[3] > 0) {
        vy += push_y;
    }
    if ((bits & 1) && fx < 4 && ry < 4 && height[0] > 0) {
        vx += push_x;
        vy += pull_y;
    }
    if ((bits & 2) && ry < 4 && rx < 4 && height[1] > 0) {
        vy += pull_y;
        vx += pull_x;
    }
    if ((bits & 4) && rx < 4 && fy < 4 && height[2] > 0) {
        vx += pull_x;
        vy += push_y;
    }
    if ((bits & 8) && fy < 4 && fx < 4 && height[3] > 0) {
        vy += push_y;
        vx += push_x;
    }
    m.vx = vx;
    m.vy = vy;
}

// $118BF4: rolling over an edge. Near a drop the marble sinks by a small
// amount per distance (it rounds the edge) rather than falling at once;
// with no edge near and the ground more than 6 units below, it falls.
void Race::keep_on_ground(Marble& m, std::int32_t ground) {
    const std::int16_t moved_x = s16(start_cell_x_ - probe_.cell_x());
    const std::int16_t moved_y = s16(start_cell_y_ - probe_.cell_y());
    const auto& kind = probe_.side_kind;
    const auto& height = probe_.side_height;
    const auto& corner = probe_.corner_height;
    const int fx = probe_.fraction_x(), fy = probe_.fraction_y();
    const auto bits = probe_.corner_bits;
    const bool positional = probe_.x() < probe_.y();
    std::uint16_t edges = 0;
    auto low = [](std::int16_t h) { return h < 4 && h >= 0; };
    if (kind[0] != 0 && kind[0] < 3 && fx < 4 && moved_x != 1 && low(height[0])) edges |= 0x01;
    if (kind[1] != 0 && kind[1] < 3 && fy > 4 && moved_y != -1 && low(height[1])) edges |= 0x02;
    if (kind[2] != 0 && kind[2] < 3 && fx > 4 && moved_x != -1 && low(height[2])) edges |= 0x04;
    if (kind[3] != 0 && kind[3] < 3 && fy < 4 && moved_y != 1 && low(height[3])) edges |= 0x08;
    if ((bits & 1) && fx < 4 && fy > 4 && moved_x == -1 && moved_y == 1 && corner[0] < 4) edges |= 0x10;
    if ((bits & 2) && fx > 4 && fy > 4 && moved_x == 1 && moved_y == 1 && corner[1] < 4) edges |= 0x20;
    if ((bits & 4) && fx > 4 && fy < 4 && moved_x == 1 && moved_y == -1 && corner[2] < 4) edges |= 0x40;
    if ((bits & 8) && fx < 4 && fy < 4 && moved_x == -1 && moved_y == -1 && corner[3] < 4) edges |= 0x80;

    const auto code = m.region_code;
    const bool code_allows = code == 0 || code == 0x12 || code == 0x20 || code == 0x10 || code == 0x17 ||
                             code == 0x18 || code == 0x3B || code == 0x32 || code == 0x33 || code == 0x34 ||
                             code == 0x35 || code == 0x36 || code == 0x37 || code == 0x2D || code == 0x2E ||
                             code == 0x38 || code == 0x39 || code == 0x3A || code == 0x2F || code == 0x30 ||
                             code == 0x31;
    if (edges == 0 || !code_allows) {
        if (edges == 0 && m.air != 2) {
            m.air = 0;
            m.vz = 0;
            if (m.z - ground > 0x60000) {
                m.air = 2;
                m.edge_cell_x = probe_.cell_x();
                m.edge_cell_y = probe_.cell_y();
                m.vz = -0x6000;
                if (code != 0x12 && code != 0x20) {
                    sound(0xB, positional);
                }
            }
        }
    } else {
        if (m.air == 0) {
            m.edge_sides = 0;
        }
        if (edges != m.edge_sides && m.air != 2) {
            m.air = 1;
            m.edge_cell_x = probe_.cell_x();
            m.edge_cell_y = probe_.cell_y();
            m.edge_sides = std::uint8_t(edges);
            m.vz = -0x6000;
        }
    }
    if (m.air != 1) {
        return;
    }
    const std::int16_t dx = std::int16_t(std::abs(s16(probe_.cell_x() - m.edge_cell_x)));
    const std::int16_t dy = std::int16_t(std::abs(s16(probe_.cell_y() - m.edge_cell_y)));
    if (dx >= 2 || dy >= 2) {
        return;
    }
    int best = -1;
    std::int16_t step_height = 0;
    for (int bit = 0; bit < 8; ++bit) {
        const int side = (1 << bit) & m.edge_sides;
        int across = 5, along = 5;
        std::int16_t h = step_height;
        switch (side) {
        case 0x01: h = height[0]; if (dx == 0) across = fx; break;
        case 0x02: h = height[1]; if (dy == 0) across = 7 - fy; break;
        case 0x04: h = height[2]; if (dx == 0) across = 7 - fx; break;
        case 0x08: h = height[3]; if (dy == 0) across = fy; break;
        // The corner cases test the distances against -1 as well, which an
        // absolute value never is ($118DF8 on).
        case 0x10: h = corner[0]; if (dy != 1) { across = fx; along = 7 - fy; } break;
        case 0x20: h = corner[1]; if (dx != 1 && dy != 1) { across = 7 - fx; along = 7 - fy; } break;
        case 0x40: h = corner[2]; if (dx != 1) { across = 7 - fx; along = fy; } break;
        case 0x80: h = corner[3]; across = fx; along = fy; break;
        default: break;
        }
        if (side == 0) {
            continue;
        }
        if (across < 5 || along < 5) {
            const int index = ((along < across && across < 5) || along > 4) ? across : along;
            const int depth = files_->program().s8(edge_depth + std::uint32_t(index));
            if (best == -1 || depth < best) {
                step_height = h;
                best = depth;
            }
        }
    }
    if (best == -1) {
        m.air = 2;
        m.vz = -0x6000;
        if (code != 0x12 && code != 0x20) {
            sound(0xB, positional);
        }
    } else if (step_height - best > 0) {
        m.z += (step_height - best) * 0x10000;
    }
}

// $114DEC: the marble hit the ground after a fall of `impact`.
void Race::land(Marble& m, int impact) {
    const auto before = m.mode;
    const bool player = is_player(m);
    if (player) {
        if (m.mode == Marble::finishing) {
            m.status = 3;
        }
        m.mode = Marble::landing;
    } else {
        m.mode = 0x24;
    }
    int total = impact + m.delay;
    if (total > 0x7F) {
        total = 0x7F;
    }
    m.delay = std::int8_t(total);
    if (!player) {
        return; // creatures (not rebuilt yet)
    }
    const auto& md = files_->marble_data();
    const int bounce = std::int8_t(m.bounce);
    if (cheats.safe_falls && bounce + ((std::int8_t(m.delay) - bounce) >> 1) >= 0x20) {
        // Cheat: the hardest landing that still bounces.
        m.delay = std::int8_t(0x3E - bounce);
    }
    if (bounce + ((std::int8_t(m.delay) - bounce) >> 1) < 0x20) {
        if (before != Marble::landing) {
            m.animation_tick = 0;
            m.animation_period = 2;
            m.animation = md.u32(marble_data_ + anim_bounce);
            sound(0xE);
        }
        return;
    }
    // A hard landing breaks the marble (or takes it off when the view has
    // already left it).
    const std::int16_t screen_y = s16(m.screen[0]);
    const bool off_screen = screen_y < -0xF || screen_y > 0xEF;
    if (before == Marble::landing || before == Marble::bumped) {
        m.animation_tick = 0;
        m.bounce = 1;
        m.animation_period = 2;
        m.animation = md.u32(marble_data_ + anim_roll);
    }
    if (off_screen) {
        m.delay = 100;
        set_mode(m, Marble::respawning);
    } else {
        set_mode(m, Marble::breaking);
    }
}

// $114C6E: stop the marble and start a mode.
void Race::set_mode(Marble& m, int mode) {
    const bool right = probe_.x() < probe_.y(); // before the respawn moves the probe
    m.vx = m.vy = 0;
    if (m.mode == Marble::finishing) {
        m.status = 3;
    }
    const auto code = m.region_code;
    if (code == 0x10 || (code >= 0x32 && code <= 0x37)) {
        sound(10);
    }
    const auto& md = files_->marble_data();
    if (mode == Marble::breaking) {
        sound(0x11, right);
        m.animation_tick = 0;
        m.animation_period = 2;
        m.animation = md.u32(marble_data_ + anim_break);
        m.bounce = 2;
        m.mode = Marble::breaking;
    } else if (mode == Marble::appearing) {
        m.animation_tick = 0;
        m.animation_period = 4;
        m.animation = md.u32(marble_data_ + anim_appear);
        m.mode = Marble::appearing;
    } else if (mode == Marble::respawning) {
        respawn(m);
        sound(m.delay == 0x65 ? 0x13 : 0x15, right);
        m.animation = 0;
        m.mode = Marble::respawning;
        ++m.respawns;
    }
}

// $1149F6: put the marble on the respawn point for its zone.
void Race::respawn(Marble& m) {
    choose_respawn_point(m);
    m.x = std::int32_t(respawn_x_) << 16;
    m.y = std::int32_t(respawn_y_) << 16;
    probe_.invalidate();
    locate(m);
    m.z = probe_.ground_height();
    m.feature = respawn_feature_;
    m.vx = m.vy = m.vz = 0;
    m.bounce = 0;
    m.air = 0;
    m.region_code = 0;
    m.screen[1] = m.screen[2] = 0;
    reset_shine(m);
    project(m, false);
    zone_changed(m); // $112F20
}

// $112CA4 / $112D94: the nearest free respawn point of the marble's zone
// (within $300), else the last free one of the zone before it.
void Race::choose_respawn_point(Marble& m) {
    const auto& t = files_->track(course_);
    respawn_x_ = respawn_y_ = 0;
    respawn_feature_ = -1;
    const std::uint32_t list = t.u32(respawn_points_);
    std::uint32_t best = 0x300;
    for (std::uint32_t e = list; t.u8(e) != 0xFF; e += 3) {
        // A byte against the sign-extended zone: zone -1 matches nothing.
        if (std::uint16_t(t.u8(e + 2)) != std::uint16_t(std::int16_t(m.saved_feature))) {
            continue;
        }
        const int ex = t.u8(e), ey = t.u8(e + 1);
        const std::uint32_t d = octagonal(s16(m.cell_x - ex), s16(m.cell_y - ey));
        const auto px = s16(ex * 8 + 4), py = s16(ey * 8 + 4);
        if (occupied(&m, px, py, 0x180) == 0 && d < best) {
            respawn_x_ = px;
            respawn_y_ = py;
            respawn_feature_ = std::int8_t(t.u8(e + 2));
            best = d;
        }
    }
    if (respawn_feature_ != -1) {
        return;
    }
    std::uint32_t end = list;
    while (t.u8(end) != 0xFF) {
        end += 3;
    }
    auto zone = std::uint16_t(std::int16_t(m.saved_feature));
    if (zone != 0) {
        --zone;
    }
    // From the end marker itself back to the first entry.
    for (std::uint32_t e = end; e + 3 != list; e -= 3) {
        const auto px = s16(t.u8(e) * 8 + 4), py = s16(t.u8(e + 1) * 8 + 4);
        if (occupied(&m, px, py, 0x180) == 0 && std::uint16_t(t.u8(e + 2)) == zone) {
            respawn_x_ = px;
            respawn_y_ = py;
            respawn_feature_ = std::int8_t(t.u8(e + 2));
            return;
        }
    }
}

// $116578: is something within `radius` of the point? 1 another marble,
// 2/4 a creature (not rebuilt yet).
int Race::occupied(const Marble* self, int x, int y, int radius) const {
    for (int i = 0; i < players_; ++i) {
        const Marble& o = marbles[std::size_t(i)];
        if (&o == self || o.status == 0) {
            continue;
        }
        const std::int16_t dx = s16(s16(o.x >> 16) - x);
        const std::int16_t dy = s16(s16(o.y >> 16) - y);
        if ((std::uint16_t(std::abs(dx)) >> 3) < 4 && (std::uint16_t(std::abs(dy)) >> 3) < 4 &&
            octagonal(dx, dy) < std::uint32_t(radius)) {
            return 1;
        }
    }
    return 0;
}

// $1138D4: the sparkles that gather where the marble reappears; true when
// they are done. Eight sparkles sit on a circle that turns and shrinks as
// the countdown runs out, lifted by the same amount (the circle lies flat
// on the course: its y axis is halved). Angles run 0-$191 (a full turn,
// 64 per radian) through the quarter-wave cosine table.
bool Race::appear(Marble& m) {
    if (m.delay == 100) {
        m.delay = 0x30;
        m.edge_cell_x = 0;
    } else if (m.delay == 0x65) {
        m.delay = 0x18;
        m.edge_cell_x = 0;
    } else if (m.delay == 0x66) {
        m.delay = 0x24;
        m.edge_cell_x = 0;
    }
    int countdown = m.delay--;
    if (m.mode == Marble::settling) {
        countdown = 0x24 - countdown;
    }
    const int radius = countdown >> 1;
    constexpr int full_turn = 0x192;
    int angle = m.edge_cell_x + 10;
    if (angle >= full_turn) {
        angle -= full_turn;
    }
    m.edge_cell_x = s16(angle);

    const auto& p = files_->program();
    const auto table = [&](int i) { return int(p.s16(cosine_quarter + std::uint32_t(i) * 2)); };
    const int screen_x = s16(m.screen[0] >> 16);
    const int screen_y = int(std::uint32_t(m.screen[0]) & 0xFFFF);
    m.sparkle_count = 0;
    for (int i = 0; i < 8; ++i) {
        int c, s;
        if (angle <= 100) {
            c = table(angle);
            s = table(100 - angle);
        } else if (angle <= 200) {
            c = -table(200 - angle);
            s = table(angle - 100);
        } else if (angle <= 301) {
            c = -table(angle - 201);
            s = -table(301 - angle);
        } else {
            c = table(401 - angle);
            s = -table(angle - 301);
        }
        const int a = (c * radius) >> 12, b = (s * radius) >> 12;
        const auto offset = sparkle_offsets + std::uint32_t(i) * 2;
        const int x = screen_x + b - a + p.s8(offset);
        const int y = screen_y + countdown - ((a + b) >> 1) + p.s8(offset + 1);
        if (x > -8 && x < 0x120 && y > -8 && y < 0xF0) {
            m.sparkles[m.sparkle_count++] = {s16(0x44 + i), s16(x), s16(y)};
        }
        angle += 0x32;
        if (angle >= full_turn) {
            angle -= full_turn;
        }
    }
    m.sprite_flag = 1;
    return m.delay == 0;
}

// $1141E0: step the marble's animation; at the end of a list the mode
// decides what follows.
void Race::animate(Marble& m) {
    const auto& md = files_->marble_data();
    const std::uint32_t break_list = md.u32(marble_data_ + anim_break);
    if (m.mode == Marble::breaking && break_list < m.animation && m.animation < break_list + 0x200 &&
        s16(std::int32_t(m.animation - break_list) >> 2) > 9) {
        if (++m.dust_tick == 1) {
            m.dust_tick = 0;
            m.dust_animation += 4;
        }
    }
    if (++m.animation_tick < m.animation_period) {
        return;
    }
    m.animation_tick = 0;
    m.animation += 4;
    if (m.mode == Marble::breaking && s16(std::int32_t(m.animation - break_list) >> 2) == 9) {
        m.dust_animation = md.u32(marble_data_ + anim_break_dust);
        m.dust_tick = 0;
        m.dust_shown = 1;
        sound(0x12);
    }
    if (md.u32(m.animation) != 0xFFFFFFFF) {
        return;
    }
    if (m.mode == Marble::landing || m.mode == Marble::bumped) {
        m.animation_tick = 0;
        m.animation_period = 2;
        m.animation = md.u32(marble_data_ + (std::int8_t(m.bounce) < 7 ? anim_roll : anim_roll_fast));
    } else if (m.mode == Marble::breaking) {
        if (m.bounce == 0) {
            m.animation_tick = 0;
            m.animation_period = 2;
            m.animation = md.u32(marble_data_ + anim_roll);
            m.bounce = 1;
        } else if (m.bounce == 1) {
            set_mode(m, Marble::breaking);
        } else {
            m.dust_shown = 0;
            if (m.status == 2) {
                m.status = 0; // and its display object goes ($1120F4)
            } else {
                m.delay = 0x65;
                set_mode(m, Marble::respawning);
            }
        }
    } else {
        m.delay = 0x65;
        set_mode(m, Marble::respawning);
    }
}

// $1130BE: the course's zone lines. Leaving a line's cells sets the zone
// on the side the marble went to; zone -1 is the goal.
void Race::track_zones(Marble& m) {
    if (probe_.cell_x() == start_cell_x_ && probe_.cell_y() == start_cell_y_) {
        return;
    }
    const auto& t = files_->track(course_);
    const auto before = m.feature;
    for (std::uint32_t e = zones_;; e += 5) {
        int a = t.s8(e);
        const int b = std::abs(int(t.s8(e + 1)));
        const int end = t.s8(e + 2);
        const bool across_x = a < 0; // the line runs along x at y = b
        a = std::abs(a);
        const bool relevant = across_x ? (probe_.cell_y() == b || b == start_cell_y_)
                                       : (probe_.cell_x() == a || a == start_cell_x_);
        if (relevant) {
            bool now = false, before_on = false;
            for (int k = across_x ? a : b; k <= end; ++k) {
                const int px = across_x ? k : a, py = across_x ? b : k;
                now |= px == probe_.cell_x() && py == probe_.cell_y();
                before_on |= px == start_cell_x_ && py == start_cell_y_;
            }
            if (!now && before_on) {
                const bool first_side = across_x ? probe_.cell_y() <= b : probe_.cell_x() <= a;
                m.feature = t.s8(e + (first_side ? 3 : 4));
                if (m.feature == -1) {
                    reach_goal(m, before);
                }
                if (before != m.feature) {
                    zone_changed(m);
                }
                return;
            }
        }
        if (t.s8(e + 3) == -1 && t.s8(e + 4) == -1) {
            return;
        }
    }
}

// The goal line ($1131xx): the first marble home gets the course's bonus;
// the marble rolls on with the course's push and the timer stops.
void Race::reach_goal(Marble& m, std::int8_t zone_before) {
    if (first_home_ == -1 && m.status != 2) {
        first_home_ = m.player;
        colour_goal(m.player);
        award(m, int(course_) * 2 + 3);
    }
    m.finish_feature = m.timer_frames;
    m.timer_frames = -1;
    sound_stop();
    m.mode = Marble::finishing;
    const auto& p = files_->program();
    if (course_ == data::Course::practice && (zone_before == 3 || zone_before == 4)) {
        m.vx = m.vy = 0;
    } else {
        m.vx = std::int32_t(p.s8(goal_push_x + std::uint32_t(course_))) << 16;
        m.vy = std::int32_t(p.s8(goal_push_y + std::uint32_t(course_))) << 16;
    }
    m.delay = 0x1E;
    sound(int(p.u32(goal_sounds + std::uint32_t(m.player) * 4)));
}

// $112F20: what entering a zone starts: regions waiting for this zone go
// on with their scripts (and, on Aerial and Silly, sounds).
void Race::zone_changed(Marble& m) {
    for (auto& r : regions_.all()) {
        if (r.active && r.state == 4 && std::uint8_t(m.feature) == r.feature) {
            if (r.code == 11) {
                r.pc = regions_.block_entry(0x15);
            } else if (r.code == 13) {
                r.pc = regions_.block_entry(0x16);
            }
            regions_.resume(r, terrain_);
        }
    }
}

// $108710: the goal takes the winner's colours: the display block's goal
// copper list (+$C, +$A instructions) gets player 1's (+$10) or player 2's
// (+$14) colour list.
void Race::colour_goal(int player) {
    const auto& t = files_->track(course_);
    const std::uint32_t block = t.u32(files_->track_header(course_) + 0x10);
    const std::uint32_t list = t.u32(block + (player == 0 ? 0x10 : 0x14));
    const std::uint32_t instructions = t.u32(block + 0xC);
    const int count = t.u16(block + 0xA);
    for (int i = 0; i < count; ++i) {
        copper_colours_[instructions + std::uint32_t(i) * 6] = t.u16(list + std::uint32_t(i) * 2);
    }
}

// $10A82A: a bonus from marbdat's table (points and a picture).
void Race::award(Marble& m, int bonus) {
    const auto& md = files_->marble_data();
    m.score += md.s16(marble_data_ + bonus_points + std::uint32_t(bonus) * 2);
    score_changed_ |= 1 << m.player;
    m.bonus_picture = md.u32(marble_data_ + bonus_pictures + std::uint32_t(bonus) * 4);
    m.bonus_time = 0;
    m.bonus_wobble = 0;
    m.bonus_wobble_step = -1;
    m.bonus_on = 1;
}

} // namespace marble_madness::game
