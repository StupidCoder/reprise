#include "spindizzy_worlds_amiga/play/explorer.hpp"
#include <algorithm>

namespace spindizzy::play {

namespace {

constexpr std::int32_t push = 0x10000;      // $24E4: steering push per tick
constexpr std::int32_t fire_push = 0x20000; // $24E8: with fire held
constexpr std::int32_t gravity = 0x80000;   // $12BB0
constexpr int footprint = 0x5A;             // the explorer's half width inside a cell ($135D2)
constexpr int step_height = 0x3C;           // a neighbour this much higher is no wall ($13838)
constexpr int grounded_step = 0x1E;         // and this much more while touching a top
constexpr int ice_time = 0xFA;              // ticks on ice ($1348E)

// Long arithmetic with the 68000's wrap-around.
std::int32_t wrap(std::int64_t value) { return std::int32_t(std::uint32_t(std::uint64_t(value))); }
std::int32_t negate(std::int32_t value) { return wrap(-std::int64_t(value)); }
std::int16_t high(std::int32_t value) { return std::int16_t(std::uint32_t(value) >> 16); }
std::int32_t with_high(std::int32_t value, int word) {
    return std::int32_t((std::uint32_t(std::uint16_t(word)) << 16) | (std::uint32_t(value) & 0xFFFF));
}
std::int16_t word(int value) { return std::int16_t(value); }
std::int16_t with_low_byte(std::int16_t value, int low) {
    return std::int16_t((std::uint16_t(value) & 0xFF00) | (low & 0xFF));
}
// The rounded whole part of a 16.16 value (add $8000, swap).
std::int16_t rounded(std::int32_t value) { return high(wrap(std::int64_t(value) + 0x8000)); }

// A position inside a cell, turned to a block's orientation: each quarter
// turn maps (u, v) to (255 − v, u) ($129D2).
struct Local {
    int u, v;
};
Local turn_position(int u, int v, int turns) {
    for (int i = 0; i < (turns & 3); ++i) {
        const int next_u = 255 - (v & 0xFF);
        v = u;
        u = next_u;
    }
    return {u & 0xFF, v & 0xFF};
}

// A velocity turned the same way: (a, b) → (−b, a) per quarter turn ($129F2).
struct Planar {
    std::int32_t a, b;
};
Planar turn_velocity(Planar velocity, int turns) {
    for (int i = 0; i < (turns & 3); ++i) velocity = {negate(velocity.b), velocity.a};
    return velocity;
}

struct Found {
    int block{};
    std::int16_t base{-256}; // $FF00: nothing found
    bool any{};
};

// The first block at or below `level` in a column, passing $22/$23 cells
// ($1298E).
Found look_down(const world::LevelGrid& grid, int column_x, int column_y, int level, std::uint16_t& contact) {
    for (; level >= 0; --level) {
        const int block = grid.at(column_x, column_y, level);
        if (block == 0) continue;
        if (block == 0x22 || block == 0x23) {
            contact |= contact::looked_through;
            continue;
        }
        return {block, std::int16_t(level << 8), true};
    }
    return {};
}

class Tick {
public:
    Tick(Explorer& explorer, Ground& ground, TickEvents& events) : e_(explorer), ground_(ground), events_(events) {}

    void run(std::uint8_t joystick) {
        count_down_timers();
        steer(joystick);
        if (e_.contact & contact::traction) apply_friction();
        e_.vz = wrap(std::int64_t(e_.vz) - gravity);
        move();
        find_floor();
        falling_ = high(e_.vz); // $24D2: kept for the landing test
        touch_surface();
        push_out_of_walls();
    }

    std::int16_t falling() const { return falling_; }

private:
    void die(int hint, int becomes, bool costs_energy) { events_.deaths.push_back({hint, becomes, costs_energy}); }

    void count_down_timers() {
        // $12B8C
        if (--e_.timer_a <= 0) e_.timer_a = e_.timer_b = 0;
        if (e_.arrival_ticks != 0) --e_.arrival_ticks;
    }

    // $12A1E: the stick pushes along x and y; pushing against the current
    // motion brakes by the plain push on top. Without traction (in the air,
    // on ice, an unheld lift top) the pushes are a sixteenth; with no contact at all
    // (and not on ice) there is no steering.
    void steer(std::uint8_t joystick) {
        e_.brake_lock = (e_.brake_lock & 0x1F) != 0 ? (e_.brake_lock & 0x1F) - 1 : 0;
        if (!(e_.contact & contact::ice) && (e_.contact & 0xFF) == 0) return;
        if (e_.brake_requested) {
            e_.brake_requested = false;
            e_.vx = e_.vy = 0;
            e_.brake_lock = 0x32;
            return;
        }
        std::int32_t forward = (joystick & stick::fire) ? fire_push : push;
        std::int32_t against = push;
        if (!(e_.contact & contact::traction)) {
            forward = std::int32_t(std::uint32_t(forward) >> 4);
            against = std::int32_t(std::uint32_t(against) >> 4);
        }
        const auto toward_minus = [&](std::int32_t& v) {
            v = wrap(std::int64_t(v) - forward);
            if (v <= 0) return;
            v = wrap(std::int64_t(v) - against);
            if (v < 0) v = 0;
        };
        const auto toward_plus = [&](std::int32_t& v) {
            v = wrap(std::int64_t(v) + forward);
            if (v >= 0) return;
            v = wrap(std::int64_t(v) + against);
            if (v >= 0) v = 0;
        };
        if (joystick & stick::up) toward_minus(e_.vx);
        if (joystick & stick::down) toward_plus(e_.vx);
        if (joystick & stick::left) toward_minus(e_.vy);
        if (joystick & stick::right) toward_plus(e_.vy);
    }

    // $129B6: v −= (v >> 8) × friction, the product of words.
    void apply_friction() {
        for (auto* v : {&e_.vx, &e_.vy}) {
            const auto scaled = std::int16_t(*v >> 8);
            *v = wrap(std::int64_t(*v) - std::int32_t(scaled) * e_.friction);
        }
    }

    // $12910: add the rounded velocities; too high up is a death.
    void move() {
        e_.z = word(e_.z + rounded(e_.vz));
        e_.y = word(e_.y + rounded(e_.vy));
        e_.x = word(e_.x + rounded(e_.vx));
        if (word(word(e_.z + 0x1000) - 0x2800) >= 0) lose_energy_and_die(6);
    }

    // $131F4 / $134EE
    void lose_energy_and_die(int hint) {
        e_.contact = 0;
        die(hint, -2, true);
    }

    // $12958: the block under the explorer, looking down from the cell its
    // feet are in (z + $FF, at most level 7).
    void find_floor() {
        e_.floor_block = 0;
        e_.floor_base = -256;
        int top = e_.z + 0xFF;
        if (word(top) < 0) return;
        if (top > 0x700) top = 0x700;
        if (std::uint16_t(e_.x) >= 0x2000 || std::uint16_t(e_.y) >= 0x2000) return;
        e_.column_x = e_.x >> 8;
        e_.column_y = e_.y >> 8;
        const auto found = look_down(ground_.grid, e_.column_x, e_.column_y, top >> 8, e_.contact);
        e_.floor_block = found.block;
        e_.floor_base = found.base;
    }

    // $12F7A: the found block's top acts on the explorer.
    void touch_surface() {
        e_.previous_contact = e_.contact;
        e_.contact = 0;
        e_.friction = ground_.surfaces.friction[std::size_t(e_.floor_block & 0xFF)];
        const auto at = turn_position(e_.x, e_.y, e_.floor_block);
        contact_with(e_.floor_block, at);
        e_.on_surface = e_.contact != 0;
    }

    int height(int block, Local at) const {
        return surface_height(ground_.surfaces, block, e_.floor_base, at.u, at.v);
    }
    // $131E6: whether a top at `top` holds the explorer (it is not above it).
    bool touches(int top) const { return word(top - e_.z) >= 0; }

    void land(int top, std::uint16_t flags = contact::grounded | contact::traction) {
        e_.vz = with_high(e_.vz, 0); // clr.w: the fraction stays
        e_.contact |= flags;
        e_.z = word(top);
    }

    void contact_with(int block, Local at) {
        const auto& surfaces = ground_.surfaces;
        const int top = height(block, at);
        switch (surfaces.kind[std::size_t(block & 0xFF)]) {
        case Surface::none: return;
        case Surface::lost:
            e_.vz = wrap(std::int64_t(e_.vz) + 0x100000);
            events_.deaths.push_back({-1, -2, false});
            return;
        case Surface::flat:
            if (touches(top)) land(top);
            return;
        case Surface::slippery:
            if (touches(top)) land(top, contact::grounded);
            return;
        case Surface::switch_pad:
            if (!touches(top)) return;
            if (word(top - e_.z) < 0x20) press_switch(block);
            land(top);
            return;
        case Surface::slope: slide(block, top, 1); return;
        case Surface::steep_slope: slide(block, top, 2); return;
        case Surface::corner_ramp:
        case Surface::corner_slope: {
            const bool far_half = at.u + at.v >= 256;
            const bool sloped = surfaces.kind[std::size_t(block & 0xFF)] == Surface::corner_ramp ? far_half : !far_half;
            if (!sloped) {
                if (touches(top)) land(top);
                return;
            }
            slide_diagonally(block, top, 0xFFFE924A);
            return;
        }
        case Surface::diagonal_ramp: slide_diagonally(block, top, 0xFFFE924A); return;
        case Surface::valley:
        case Surface::ridge: {
            const bool along_u = surfaces.kind[std::size_t(block & 0xFF)] == Surface::valley ? at.v >= at.u : at.v < at.u;
            if (along_u) {
                slide(block, top, 1);
            } else {
                // $1346A: the other face, as a slope turned back a quarter.
                slide(block - 1, top, 1);
            }
            return;
        }
        case Surface::ice: touch_ice(top); return;
        case Surface::bouncy: bounce(top); return;
        case Surface::conveyor:
            if (!touches(top)) return;
            e_.vy = with_high(e_.vy, high(e_.vy) + high(surfaces.conveyor_y[std::size_t(block & 3)]));
            e_.vx = with_high(e_.vx, high(e_.vx) + high(surfaces.conveyor_x[std::size_t(block & 3)]));
            land(top);
            return;
        case Surface::deadly:
            if (!touches(top)) return;
            e_.contact = 0;
            die(8, -2, false);
            return;
        case Surface::lift_top:
            if (!touches(top) || (e_.previous_contact & contact::on_lift)) return;
            e_.contact |= contact::unheld_lift_top;
            if (e_.previous_contact & contact::unheld_lift_top) lose_energy_and_die(6);
            return;
        }
    }

    // $13228: the pads $2C-$33 (and their copies every $40): the first one
    // clears the push-button word, the next four set the secondary value,
    // the last three the primary one.
    void press_switch(int block) {
        auto& switches = ground_.switches;
        const int pad = ((block - 4) & 7) - 1;
        if (pad < 0 || pad < 4) {
            const int value = pad < 0 ? 0xFFFF : pad;
            const int now = switches.primary << 8 | switches.secondary;
            if (now == value) return;
            events_.switch_sound = pad < 0 ? 0x15 : 0x15 + pad;
            switches.primary = std::uint8_t(value >> 8);
            switches.secondary = std::uint8_t(value);
            return;
        }
        const int value = (pad & 3) + 1;
        if (switches.primary == value) return;
        events_.switch_sound = 0x15 + pad;
        switches.primary = std::uint8_t(value);
    }

    // $13312 / $1335E: a slope one or two cells high along −u. Gravity
    // pulls the explorer down it (`pull` units per tick); it follows the
    // surface unless it falls faster than the slope drops.
    void slide(int block, int top, int steepness) {
        if (!touches(top)) return;
        auto local = turn_velocity({e_.vx, e_.vy}, block);
        local.a = wrap(std::int64_t(local.a) + std::int64_t(steepness) * 0x10000);
        std::int32_t follow = negate(local.a);
        if (steepness == 2) follow = wrap(std::int64_t(follow) * 2);
        stay_on_slope(block, local, follow, top);
    }

    // $133A6: a diagonal slope; the pull is split over both axes.
    void slide_diagonally(int block, int top, std::uint32_t pull) {
        if (!touches(top)) return;
        auto local = turn_velocity({e_.vx, e_.vy}, block);
        local.a = wrap(std::int64_t(local.a) - std::int32_t(pull));
        local.b = wrap(std::int64_t(local.b) - std::int32_t(pull));
        const std::int32_t follow = negate(wrap(std::int64_t(local.a) + local.b)) >> 1;
        stay_on_slope(block, local, follow, top);
    }

    // $1332E: when the explorer falls no faster than the slope drops
    // (`follow`), it rides the surface with that vertical speed and the
    // pull added; otherwise it only stays on top.
    void stay_on_slope(int block, Planar local, std::int32_t follow, int top) {
        if (wrap(std::int64_t(follow) - e_.vz) < 0) {
            e_.z = word(top);
            return;
        }
        e_.vz = follow;
        const auto back = turn_velocity(local, -block);
        e_.vx = back.a;
        e_.vy = back.b;
        e_.contact |= contact::grounded | contact::traction;
        e_.z = word(top);
    }

    // $1349A: standing still kills, and so does staying 250 ticks (unless
    // the level's flag bit 11 is set).
    void touch_ice(int top) {
        if (!touches(top) || (e_.previous_contact & contact::on_lift)) return;
        if (!(ground_.level_flags & 0x0800)) {
            if (!(e_.previous_contact & contact::ice)) {
                e_.ice_ticks = ice_time;
            } else if (--e_.ice_ticks <= 0) {
                lose_energy_and_die(10);
                return;
            }
        }
        e_.contact |= contact::grounded | contact::ice;
        e_.vz = with_high(e_.vz, 0);
        if (rounded(e_.vx) == 0 && rounded(e_.vy) == 0) {
            lose_energy_and_die(7);
            return;
        }
        e_.z = word(top);
    }

    // $13504: falls faster than 8 units per tick come back up.
    void bounce(int top) {
        if (!touches(top)) return;
        e_.contact |= contact::bouncy | contact::traction;
        const auto falling = high(e_.vz);
        if (falling < 0) {
            if (falling >= -8) {
                e_.vz = with_high(e_.vz, 0);
            } else {
                e_.vz = with_high(e_.vz, -falling);
                events_.bounced = true;
            }
        }
        e_.z = word(top);
    }

    // $13808: how far a neighbour's top rises above the explorer's current
    // top, less the step it can climb. `here` and `there` are the points
    // compared (the cell edge on each side).
    int rise(int column_x, int column_y, int here_x, int here_y, int there_x, int there_y) {
        const auto here = turn_position(here_x, here_y, e_.floor_block);
        const int current =
            e_.on_surface ? surface_height(ground_.surfaces, e_.floor_block, e_.floor_base, here.u, here.v) : e_.z;
        const auto found = look_down(ground_.grid, column_x, column_y, e_.wall_level, e_.contact);
        if (!found.any) return 0;
        const auto at = turn_position(there_x, there_y, found.block);
        int top = surface_height(ground_.surfaces, found.block, found.base, at.u, at.v) - step_height;
        if (e_.contact != 0) top -= grounded_step;
        return word(top - current);
    }

    // $1384A: reverse and halve.
    static void rebound(std::int32_t& v) { v = negate(v) >> 1; }

    // $135D2: the explorer's footprint (±$5A around its position) may not
    // reach into a neighbour cell whose top is higher than a step. It is
    // pushed back to the edge and bounces off; corners are tested when both
    // axes are near an edge without a wall.
    void push_out_of_walls() {
        if (std::uint16_t(e_.x) >= 0x2000 || std::uint16_t(e_.y) >= 0x2000) return;
        const std::int16_t edge_x = with_low_byte(e_.x, (e_.x & 0x80) ? 0xFF : 0); // $2506
        const std::int16_t edge_y = with_low_byte(e_.y, (e_.y & 0x80) ? 0xFF : 0); // $2508
        const int feet = e_.z + 0x268;
        if (word(feet) < 0) return;
        e_.wall_level = std::min(feet >> 8, 7);

        int near_edges = 0; // $251A: edges reached without a wall
        int corner_x = 0, corner_y = 0;
        const int fx = e_.x & 0xFF;
        if (fx < footprint || fx > 0x100 - footprint) {
            const int side = fx < footprint ? -1 : 1;
            const int beyond = side < 0 ? ((e_.x & 0xFF00) - 1) : ((e_.x & 0xFF00) + 0x100);
            if (std::uint16_t(beyond) < 0x2000) {
                near_edges = 1;
                corner_x = side;
                if (rise(e_.column_x + side, e_.column_y, edge_x, e_.y, beyond, e_.y) > 0) {
                    near_edges = 0;
                    e_.x = with_low_byte(e_.x, side < 0 ? footprint : 0x100 - footprint);
                    if ((high(e_.vx) < 0) == (side < 0)) rebound(e_.vx);
                }
            }
        }

        const int fy = e_.y & 0xFF;
        if (fy >= footprint && fy < 0x100 - footprint) return; // (the original's y test: $A6 counts as the edge)
        const int side = fy < footprint ? -1 : 1;
        const int beyond = side < 0 ? ((e_.y & 0xFF00) - 1) : ((e_.y & 0xFF00) + 0x100);
        if (side < 0 ? word(beyond) < 0 : word(beyond - 0x2000) >= 0) return;
        ++near_edges;
        corner_y = side;
        if (rise(e_.column_x, e_.column_y + side, e_.x, edge_y, e_.x, beyond) > 0) {
            near_edges = 0;
            e_.y = with_low_byte(e_.y, side < 0 ? footprint : 0x100 - footprint);
            if ((high(e_.vy) < 0) == (side < 0)) rebound(e_.vy);
        }
        if (near_edges == 2) push_out_of_corner(corner_x, corner_y, edge_x, edge_y);
    }

    // $13714: the diagonal neighbour; the explorer is pushed out along the
    // axis it is less deep in, bouncing at half speed when moving inward.
    void push_out_of_corner(int side_x, int side_y, std::int16_t edge_x, std::int16_t edge_y) {
        if (rise(e_.column_x + side_x, e_.column_y + side_y, edge_x, edge_y, ~edge_x & 0xFF, ~edge_y & 0xFF) <= 0) return;
        const int u = e_.x & 0xFF, v = e_.y & 0xFF;
        const std::int32_t back_x = negate(e_.vx >> 1), back_y = negate(e_.vy >> 1);
        bool along_x;
        if (side_x == side_y) along_x = (std::int8_t(u - v) < 0) == (side_x > 0);
        else along_x = (u + v >= 256) == (side_x < 0);
        const auto bounce_axis = [](std::int32_t& v, std::int32_t back, int side) {
            if (side > 0 ? back < 0 : back >= 0) v = back;
        };
        if (along_x) {
            e_.x = with_low_byte(e_.x, side_x > 0 ? 0xA5 : footprint);
            bounce_axis(e_.vx, back_x, side_x);
        } else {
            e_.y = with_low_byte(e_.y, side_y > 0 ? 0xA5 : footprint);
            bounce_axis(e_.vy, back_y, side_y);
        }
    }

    Explorer& e_;
    Ground& ground_;
    TickEvents& events_;
    std::int16_t falling_{};
};

} // namespace

void move_explorer(Explorer& explorer, std::uint8_t joystick, Ground& ground, TickEvents& events) {
    Tick tick(explorer, ground, events);
    tick.run(joystick);
    explorer.falling_speed = tick.falling();
}

void settle_explorer(Explorer& explorer, TickEvents& events) {
    // $12F0A: a hard landing hurts; a harder one kills.
    const auto flags = explorer.contact;
    if (!(flags & contact::bouncy) && (flags & (contact::grounded | contact::traction | contact::on_lift))) {
        const int now = high(explorer.vz) < 0 ? high(explorer.vz) : 0;
        int impact = word(now - explorer.falling_speed);
        if (impact > 0 && (impact -= 0x30) > 0) {
            if (impact > 0x3C) {
                explorer.contact = 0;
                events.deaths.push_back({6, -2, true, true});
            } else {
                events.landing_volume = std::min(impact / 2 + 0x20, 0x40);
            }
        }
    }
    // $12C04: standing firmly (not on a lift) marks the respawn point.
    if ((explorer.contact & contact::grounded) && (explorer.contact & contact::traction) &&
        !(explorer.contact & contact::on_lift)) {
        explorer.respawn_x = explorer.x;
        explorer.respawn_y = explorer.y;
        explorer.respawn_z = explorer.z;
    }
    // $12C22: below the world.
    if (word(explorer.z + 0x1000) < 0) events.deaths.push_back({3, -3, false, true});
}

} // namespace spindizzy::play
