#include "bruce_lee_c64/game/actor.hpp"
#include <algorithm>
#include <cstdlib>

namespace bruce_lee::game {
namespace {

// Sound effects the moves request ($24F6, Y = effect).
constexpr std::uint8_t sound_step = 0x80, sound_strike = 0x81, sound_kick = 0x84, sound_chop = 0x85;

// The edges a step may not cross ($1E06, $1E4D, $1E9A, $1ED8).
constexpr int top_limit = 0x2E, bottom_limit = 0xC7, left_limit = 0x0D, right_limit = 0xA3;

// The steps-moved counters of this pass (Actor::steps 0-3).
enum Counter { moved_up = 0, moved_down = 1, moved_left = 2, moved_right = 3 };

bool negative(std::uint8_t v) {
    return v & 0x80;
}

// $10C7: the room clock against two marks. late = the clock reached $20
// (the original returns it in V), past = it reached $19 (in C).
struct ClockMarks {
    bool late{}, past{};
};
ClockMarks clock_marks(const ActorWorld& world) {
    if (world.clock >= 0x20) {
        return {true, true};
    }
    return {false, world.clock >= 0x19};
}

} // namespace

void Actors::start(Actor& a, const ActorWorld& world, std::uint8_t move, std::uint8_t step) {
    a.move = move;
    a.delay = world.assets.move_delays[move];
    a.step = step;
}

void Actors::carry_on(Actor& a, const ActorWorld& world, std::uint8_t move, int step) {
    a.move = move;
    a.delay = world.assets.move_delays[move];
    a.step = std::uint8_t(step - 1); // $24EA: one step used
}

void Actors::stand(Actor& a, ActorWorld& world) {
    if (a.last_move == move::stand) {
        ++a.posed; // standing still: the pose stays
    }
    start(a, world, move::stand, 0);
}

void Actors::step_up(Actor& a, ActorWorld& world) {
    if (a.y < top_limit || (a.climb_lock & 1)) {
        stand(a, world);
        return;
    }
    --a.y;
    ++a.steps[moved_up];
    start(a, world, move::climb_up, 0);
}

void Actors::step_down(Actor& a, ActorWorld& world) {
    if (a.y >= bottom_limit) {
        stand(a, world);
        return;
    }
    ++a.y;
    ++a.steps[moved_down];
    start(a, world, move::climb_down, 0);
}

void Actors::step_left(Actor& a, ActorWorld& world) {
    if (a.x < left_limit) {
        stand(a, world);
        return;
    }
    --a.x;
    ++a.steps[moved_left];
    a.facing = 0;
    start(a, world, move::walk_left, 0);
}

void Actors::step_right(Actor& a, ActorWorld& world) {
    if (a.x >= right_limit) {
        stand(a, world);
        return;
    }
    ++a.x;
    ++a.steps[moved_right];
    a.facing = 1;
    start(a, world, move::walk_right, 0);
}

// A step left, or a flying kick when the actor was already walking left
// for a while (fewer than 4 repeats left) and fire is pressed.
void Actors::step_left_or_kick(Actor& a, ActorWorld& world) {
    if (a.last_move == move::walk_left && a.repeat < 4 && a.fire == 0) {
        a.facing = 0;
        start(a, world, move::kick_left, 0x0C);
        return;
    }
    step_left(a, world);
}

void Actors::step_right_or_kick(Actor& a, ActorWorld& world) {
    if (a.last_move == move::walk_right && a.repeat < 4 && a.fire == 0) {
        a.facing = 1;
        start(a, world, move::kick_right, 0x0C);
        return;
    }
    step_right(a, world);
}

// $1DFE: on a ladder a step up; elsewhere a jump, unless blocked.
void Actors::climb_or_jump(Actor& a, ActorWorld& world) {
    if (a.terrain.feet & terrain::ladder) {
        step_up(a, world);
    } else if (a.terrain.feet & terrain::solid) {
        stand(a, world);
    } else {
        start(a, world, move::jump, 0x0F);
    }
}

// $2279: a move ends; the actor waits a while before its next decision.
// Bruce waits 12 passes once the room clock is past $19, else 3; the
// enemies wait 1 when it is late (or with $40 set), 3 when past, else 6.
void Actors::finish(Actor& a, int slot, const ActorWorld& world) {
    a.move = move::stand;
    a.step = 6;
    a.delay = 0;
    const auto marks = clock_marks(world);
    if (slot == 0) {
        a.step = marks.late || marks.past ? 12 : 3;
    } else if (marks.late || (!marks.past && world.flag40)) {
        a.step = 1;
    } else if (marks.past) {
        a.step = 3;
    }
}

// $2231: an enemy turns to Bruce and strikes.
void Actors::begin_strike(Actor& a, ActorWorld& world) {
    a.facing = a.x >= slots[0].x ? 0 : 1;
    a.move = move::strike;
    a.step = 0x10;
    a.delay = 0;
    world.sounds.push_back(sound_strike);
}

void Actors::update(int slot, ActorWorld& world) {
    Actor& a = slots[std::size_t(slot)];
    budget_ = 8;
    if (a.delay != 0) {
        --a.delay;
        return;
    }
    run(a, slot, world);
}

// From $1C7A: a decision pass.
void Actors::run(Actor& a, int slot, ActorWorld& world) {
    for (;;) {
        // $1C7A
        a.posed = 0;
        if (a.steps[moved_up] | a.steps[moved_down] | a.steps[moved_left] | a.steps[moved_right]) {
            for (int j = 39; j >= 0; --j) {
                a.steps[std::size_t(j + 4)] = a.steps[std::size_t(j)];
            }
        }
        a.steps[moved_up] = a.steps[moved_down] = a.steps[moved_left] = a.steps[moved_right] = 0;
        // $1CAF: the same move again counts down the repeat counter (from 6).
        if (a.move != move::stand && a.move == a.last_move) {
            if (a.repeat != 0) {
                --a.repeat;
                if (negative(a.repeat)) {
                    a.repeat = 6;
                }
            }
        } else {
            a.repeat = 6;
        }
        a.last_move = a.move;
        if (a.move == move::dying) {
            if (a.step != 0) {
                timed(a, slot, world);
            } else {
                decide(a, slot, world);
            }
            return;
        }
        if (a.move == move::bump && a.step != 0) {
            timed(a, slot, world);
            return;
        }
        if (!(a.terrain.feet & terrain::solid)) {
            a.climb_lock = 0; // $1D81 ($C5 = $FF is not used by these rules)
            if (a.step != 0) {
                timed(a, slot, world);
            } else {
                decide(a, slot, world);
            }
            return;
        }
        // $1CE2: something solid.
        a.climb_lock = 0;
        if (undo_and_bump(a, world)) {
            continue; // $1D60: decide again from $1C7A
        }
        return;
    }
}

// $1CE6-$1D80: undo the last pass's steps (the newest history entry after
// this pass's), drop both from the history, and bump. Returns true when the
// actor is to decide again instead ($1D60, at most 8 rounds per pass).
bool Actors::undo_and_bump(Actor& a, ActorWorld& world) {
    bool no_side_or_down_undo = true; // $85
    auto undo = [&](int entry, void (Actors::*step)(Actor&, ActorWorld&), bool counts) {
        while (!negative(--a.steps[std::size_t(entry)])) {
            if (counts) {
                no_side_or_down_undo = false;
            }
            (this->*step)(a, world);
        }
    };
    undo(4, &Actors::step_down, true);
    undo(5, &Actors::step_up, false);
    undo(6, &Actors::step_right, true);
    undo(7, &Actors::step_left, true);
    for (int j = 0; j < 36; ++j) {
        a.steps[std::size_t(j)] = a.steps[std::size_t(j + 8)];
    }
    a.delay = 0;
    for (int j = 36; j < 44; ++j) {
        a.steps[std::size_t(j)] = 0;
    }
    --budget_;
    if (budget_ >= 0 && !(a.terrain.feet & terrain::ladder)) {
        if (no_side_or_down_undo) {
            if (a.last_move != move::duck) {
                return true;
            }
        } else if (budget_ != 7) {
            if (a.facing == 0) {
                step_right(a, world);
            } else {
                step_left(a, world);
            }
        }
    }
    a.move = move::bump; // $1D78
    a.step = 0x40;
    return false;
}

// $1D90: no move in progress.
void Actors::decide(Actor& a, int slot, ActorWorld& world) {
    const auto feet = a.terrain.feet;
    if (a.last_move == move::fall) {
        bool keep_falling;
        if (slot != 0) {
            if (feet & (terrain::ground | terrain::ladder)) {
                if (a.facing == 1) {
                    step_right(a, world);
                } else {
                    step_left(a, world);
                }
                return;
            }
            keep_falling = true;
        } else if (a.terrain.body & terrain::ground) {
            a.fall_steps >>= 1;
            if (a.fall_steps != 0) {
                if (a.facing != 0) {
                    step_right(a, world);
                } else {
                    step_left(a, world);
                }
            }
            keep_falling = true;
        } else {
            keep_falling = !(feet & (terrain::ground | terrain::ladder));
        }
        if (!keep_falling) {
            stand(a, world);
            return;
        }
        step_down(a, world); // $1DCB
        step_down(a, world);
        start(a, world, move::fall, 0);
        return;
    }
    if (!(feet & (terrain::ground | terrain::ladder))) {
        a.fall_steps = 2; // $1DD6: nothing below: start falling
        step_down(a, world);
        start(a, world, move::fall, 0);
        return;
    }
    switch (a.stick) {
    case stick::up:
        if (a.y < top_limit) {
            stand(a, world);
            return;
        }
        world.sounds.push_back(sound_step);
        if (a.repeat == 0) {
            climb_or_jump(a, world);
        }
        climb_or_jump(a, world);
        climb_or_jump(a, world);
        climb_or_jump(a, world);
        return;
    case stick::down:
        if (!(feet & terrain::ladder) && (feet & terrain::ground)) {
            // $1E5B: ducking towards where Bruce faces (the enemies: $1F0C)
            if (slot != 0) {
                enemy_down(a, slot, world);
                return;
            }
            if (slots[0].facing != 0) {
                if (a.x < 0x10) {
                    stand(a, world);
                    return;
                }
                a.facing = 1;
            } else {
                if (a.x >= 0xA2) {
                    stand(a, world);
                    return;
                }
                a.facing = 0;
            }
            start(a, world, move::duck, 0);
            return;
        }
        if (a.y >= bottom_limit) {
            stand(a, world);
            return;
        }
        world.sounds.push_back(sound_step);
        if (a.repeat == 0) {
            step_down(a, world);
        }
        step_down(a, world);
        step_down(a, world);
        step_down(a, world);
        return;
    case stick::left:
        if (a.x < left_limit) {
            stand(a, world);
            return;
        }
        world.sounds.push_back(sound_step);
        if (a.repeat == 0) {
            step_left_or_kick(a, world);
        }
        step_left_or_kick(a, world);
        step_left_or_kick(a, world);
        return;
    case stick::right:
        if (a.x >= right_limit) {
            stand(a, world);
            return;
        }
        world.sounds.push_back(sound_step);
        if (a.repeat == 0) {
            step_right_or_kick(a, world);
        }
        step_right_or_kick(a, world);
        step_right_or_kick(a, world);
        return;
    case stick::down_left:
    case stick::down_right:
        if (slot != 0) {
            enemy_down(a, slot, world);
            return;
        }
        if ((feet & terrain::ladder) || !(feet & terrain::ground)) {
            stand(a, world);
            return;
        }
        if (a.stick == stick::down_left) {
            if (a.x >= 0xA2) {
                stand(a, world);
                return;
            }
            a.facing = 0;
        } else {
            if (a.x < 0x10) {
                stand(a, world);
                return;
            }
            a.facing = 1;
        }
        start(a, world, move::duck, 0);
        return;
    default:
        break;
    }
    if (!(feet & terrain::solid)) {
        if (a.stick == stick::up_left || a.stick == stick::up_right) {
            if (feet & terrain::ladder) {
                stand(a, world);
                return;
            }
            a.facing = a.stick == stick::up_right ? 1 : 0;
            world.sounds.push_back(sound_step);
            start(a, world, a.stick == stick::up_right ? move::jump_right : move::jump_left, 0x0F);
            return;
        }
    }
    // $1F88
    if (a.stick == stick::none && a.fire == 0) {
        if (feet & terrain::ladder) {
            stand(a, world);
            return;
        }
        start(a, world, move::chop, 0x04);
        return;
    }
    stand(a, world);
}

// $1F0C: an enemy's down command on the floor. The code compares the slot
// number (it arrives in A from TXA), so only the Yamo (slot 1) strikes, and
// only off ladders; the Ninja stands.
void Actors::enemy_down(Actor& a, int slot, ActorWorld& world) {
    if (slot == 1 && !(a.terrain.feet & terrain::ladder)) {
        begin_strike(a, world);
        return;
    }
    stand(a, world);
}

// $1FF5: a move in progress, `step` steps left.
void Actors::timed(Actor& a, int slot, ActorWorld& world) {
    const int y = a.step; // $52
    const auto feet = a.terrain.feet;
    auto stop = [&] { start(a, world, move::stand, 0); }; // $24F1
    switch (a.move) {
    case move::jump:
        if (feet & terrain::ladder) {
            stop();
            return;
        }
        if (y < 0x0D && y >= 2) {
            if (y >= 8) {
                if (y == 0x0C) {
                    step_up(a, world);
                    step_up(a, world);
                }
                step_up(a, world);
                step_up(a, world);
            } else {
                if (feet & terrain::ground) {
                    stop();
                    return;
                }
                step_down(a, world);
                step_down(a, world);
            }
        }
        carry_on(a, world, move::jump, y);
        return;
    case move::kick_left:
    case move::kick_right: {
        const bool left = a.move == move::kick_left;
        auto side = [&] {
            if (left) {
                step_left(a, world);
            } else {
                step_right(a, world);
            }
        };
        if (feet & (terrain::ladder | terrain::solid)) {
            stop();
            return;
        }
        if (y == 0x0C) {
            world.sounds.push_back(sound_kick);
        }
        if (y >= 0x0A) {
            step_up(a, world);
            side();
        } else if (y >= 3) {
            side();
        } else {
            if (feet & terrain::ground) {
                stop();
                return;
            }
            step_down(a, world);
            side();
        }
        carry_on(a, world, left ? move::kick_left : move::kick_right, y);
        return;
    }
    case move::jump_left:
    case move::jump_right: {
        const bool left = a.move == move::jump_left;
        auto side = [&] {
            if (left) {
                step_left(a, world);
            } else {
                step_right(a, world);
            }
        };
        if (feet & terrain::ladder) {
            stop();
            return;
        }
        if (y >= 0x0A) {
            if (y != 0x0A) {
                step_up(a, world);
            }
            step_up(a, world);
            side();
            side();
        } else if (y >= 7) {
            side();
            side();
        } else {
            if (feet & terrain::ground) {
                stop();
                return;
            }
            side();
            side();
            // $20FE / $2149 compare Y with 6 to drop only one line, but the
            // steps above leave their move code in Y ($1FA4: TAY), so the
            // test never holds: always two lines.
            step_down(a, world);
            step_down(a, world);
        }
        carry_on(a, world, left ? move::jump_left : move::jump_right, y);
        return;
    }
    case move::dying:
        if (slot == 0 && y == 1) {
            world.death_finished = true; // $1830
            return;
        }
        carry_on(a, world, move::dying, y);
        return;
    case move::ride: {
        if (!(feet & terrain::ladder)) {
            stop();
            return;
        }
        a.ride_flag = 1;
        if (slot != 0 && !world.two_players) {
            a.stick = a.facing == 1 ? stick::right : stick::left;
        }
        if (a.ride_wait != 0) {
            --a.ride_wait;
        } else {
            bool moved = true;
            switch (a.stick) {
            case stick::up:
                step_up(a, world);
                step_up(a, world);
                break;
            case stick::down:
                step_down(a, world);
                step_down(a, world);
                break;
            case stick::left:
                step_left(a, world);
                step_left(a, world);
                break;
            case stick::right:
                step_right(a, world);
                step_right(a, world);
                break;
            default:
                moved = false;
            }
            if (moved) {
                a.ride_flag >>= 1;
                a.ride_wait = 4;
                world.sounds.push_back(sound_step);
            }
        }
        if (world.lift == 1) {
            if (world.lift_down) {
                step_down(a, world);
                step_down(a, world);
            } else {
                step_up(a, world);
                step_up(a, world);
            }
        }
        a.stick = stick::none;
        a.fire = 1;
        // $21FE: keep only the net vertical movement of this pass.
        const int net = int(a.steps[moved_up]) - int(a.steps[moved_down]);
        if (std::int8_t(net) < 0) {
            a.steps[moved_down] = std::uint8_t(-net);
            a.steps[moved_up] = 0;
        } else {
            a.steps[moved_up] = std::uint8_t(net);
            a.steps[moved_down] = 0;
        }
        carry_on(a, world, move::ride, y + 1); // the step count stays
        return;
    }
    case move::turn_to_strike:
        if (a.step != 1) {
            carry_on(a, world, move::turn_to_strike, y);
            return;
        }
        begin_strike(a, world);
        return;
    case move::chop:
        if (feet & (terrain::ladder | terrain::solid)) {
            stop();
            return;
        }
        if (y == 8) {
            world.sounds.push_back(sound_chop);
        }
        if (y == 1) {
            finish(a, slot, world);
            return;
        }
        carry_on(a, world, move::chop, y);
        return;
    case move::low_strike:
        if (feet & terrain::solid) {
            stop();
            return;
        }
        if (y == 0x0C) {
            world.sounds.push_back(sound_chop);
        }
        if (y == 1) {
            finish(a, slot, world);
            return;
        }
        ++a.steps[a.facing == 1 ? moved_right : moved_left];
        carry_on(a, world, move::low_strike, y);
        return;
    case move::strike:
        carry_on(a, world, move::strike, y);
        return;
    case move::pushed_back:
        if (slot != 0 && y < 2) {
            finish(a, slot, world);
            return;
        }
        if (a.facing == 1) {
            step_left(a, world);
            step_left(a, world);
            a.facing = 1;
        } else {
            step_right(a, world);
            step_right(a, world);
            a.facing = 0;
        }
        carry_on(a, world, move::pushed_back, y);
        return;
    case move::thrown: {
        if (slot != 0 && !negative(a.d1) && y < 2) {
            finish(a, slot, world);
            return;
        }
        // $244B compares Y with $0A; after a step down Y holds that step's
        // move code instead of the step count ($1FA4: TAY).
        int y_register = y;
        if (!(feet & (terrain::ground | terrain::ladder))) {
            step_down(a, world);
            y_register = a.move;
        }
        if (a.facing == 1) {
            if (y_register == 0x0A && slot == 2) {
                for (int i = 0; i < 8; ++i) {
                    step_left(a, world);
                }
            }
            step_left(a, world);
            step_left(a, world);
            a.facing = 1;
        } else {
            if (y_register == 0x0A && slot == 2) {
                for (int i = 0; i < 4; ++i) {
                    step_right(a, world);
                }
            }
            step_right(a, world);
            step_right(a, world);
            a.facing = 0;
        }
        carry_on(a, world, move::thrown, y);
        return;
    }
    case move::bump:
        if (!(feet & terrain::solid)) {
            carry_on(a, world, move::bump, 1); // $24AA: step 0
            return;
        }
        // $24B3: still blocked. With no steps in the history the room's
        // restart position applies (not rebuilt yet); otherwise undo again
        // ($1CE6, with a fresh budget).
        {
            bool any = false;
            for (int j = 4; j < 44; ++j) {
                any = any || a.steps[std::size_t(j)] != 0;
            }
            if (!any) {
                world.restart_position = true;
                return;
            }
            if (undo_and_bump(a, world)) {
                run(a, slot, world);
            }
        }
        return;
    case move::enemy_think:
        think(a, slot, world, y);
        return;
    case move::enemy_walk:
        // $23BA: walk to the spot chosen by think(); then turn and think again.
        if (!(feet & terrain::ground)) {
            start_fall(a, world);
            return;
        }
        if (a.step == 1) {
            a.facing ^= 1;
            a.move = move::enemy_think;
            a.delay = 0;
            a.step = 0x80;
            return;
        }
        walk_on(a, world);
        carry_on(a, world, move::enemy_walk, y);
        return;
    default:
        carry_on(a, world, a.move, y);
        return;
    }
}

// $1DD6: nothing below: start falling.
void Actors::start_fall(Actor& a, ActorWorld& world) {
    a.fall_steps = 2;
    step_down(a, world);
    start(a, world, move::fall, 0);
}

// $1EC0 / $1E7F: two steps the way the actor faces.
void Actors::walk_on(Actor& a, ActorWorld& world) {
    if (a.facing == 1) {
        step_right_or_kick(a, world);
        step_right_or_kick(a, world);
    } else {
        step_left_or_kick(a, world);
        step_left_or_kick(a, world);
    }
}

// $2DCA: Bruce's distance from the actor, each axis capped at $30, bit 7
// set when Bruce is further down (y) or further right (x).
Actors::Distance Actors::distance_to_bruce(const Actor& a) const {
    const Actor& b = slots[0];
    auto axis = [](int mine, int his) {
        const int d = std::abs(mine - his);
        return std::uint8_t(std::min(d, 0x30) | (mine < his ? 0x80 : 0));
    };
    return {axis(a.y, b.y), axis(a.x, b.x)};
}

// $22E7: an enemy's decision, made while standing on the ground.
void Actors::think(Actor& a, int slot, ActorWorld& world, int y) {
    const auto feet = a.terrain.feet;
    auto& observed = decisions[std::size_t(slot)];
    observed = {true, bool(feet & terrain::ground),
                std::abs(int(a.y) - slots[0].y) < 16,
                (slots[0].x > a.x) == (a.facing == 1),
                std::abs(int(a.x) - slots[0].x) * 2,
                std::abs(int(a.y) - slots[0].y), "Walk on"};
    auto walk = [&] { // $233F: two steps on, the step count stays
        walk_on(a, world);
        carry_on(a, world, move::enemy_think, y + 1);
    };
    auto approach = [&](const Distance& d) { // $2314: walk to 15 beside Bruce
        // 8-bit arithmetic as in the original; a target of 0 (Bruce at x 15)
        // falls through into the addition and becomes 15.
        observed.outcome = "Reposition";
        std::uint8_t target = std::uint8_t(slots[0].x - 0x0F);
        if (!(d.x & 0x80) || target == 0) {
            target = std::uint8_t((d.x & 0x80 ? target : slots[0].x) + 0x0F);
        }
        int steps = target >= a.x ? target - a.x : a.x - target;
        if (steps < 5) {
            steps = 5;
        }
        a.step = std::uint8_t(steps);
        a.move = move::enemy_walk;
        a.delay = 0;
        a.climb_lock = 0;
    };
    if (!(feet & terrain::ground)) {
        if (feet & terrain::ladder) {
            walk();
        } else {
            observed.outcome = "Fall";
            start_fall(a, world);
        }
        return;
    }
    const auto d = distance_to_bruce(a);
    if ((d.y & 0x7F) >= 0x10) {
        // Not on Bruce's level: walk on while above him, else face him and wait.
        if (d.y & 0x80) {
            walk();
            return;
        }
        observed.outcome = "Face target and wait";
        a.facing = d.x & 0x80 ? 1 : 0;
        finish(a, slot, world);
        return;
    }
    const bool towards = ((d.x & 0x80) ? a.facing : a.facing ^ 1) == 1;
    const int dx = d.x & 0x7F;
    if (!towards) {
        if (dx >= 0x21) {
            observed.outcome = "Turn and walk";
            a.facing ^= 1; // turn round, walk on
            walk();
        } else {
            approach(d);
        }
        return;
    }
    if ((a.flags & 0x81) == 0x81) {
        // $2368: the Yamo: chop from close, flying kick from further.
        if (dx < 5) {
            approach(d);
        } else if (dx < 0x0C) {
            if (feet & terrain::ladder) {
                observed.outcome = "Wait on support";
                stand(a, world);
            } else {
                observed.outcome = "Chop";
                start(a, world, move::chop, 0x0C);
                a.step = 4;
            }
            if (a.move != move::chop) {
                finish(a, slot, world);
            }
        } else if (dx < 0x13) {
            observed.outcome = "Flying kick";
            a.move = a.facing == 1 ? move::kick_right : move::kick_left;
            a.step = 0x0C;
            a.delay = 0;
        } else {
            walk();
        }
        return;
    }
    // $23A5: the Ninja: a low strike from close.
    if (dx >= 0x0B) {
        walk();
    } else if (dx < 5) {
        approach(d);
    } else {
        observed.outcome = "Low strike";
        a.move = move::low_strike;
        a.step = 0x13;
    }
}

// $2D69: before the actors move, enemies the computer controls that stand
// idle on the ground start thinking (after a kick they rest first).
void Actors::wake_enemies(ActorWorld& world) {
    for (int slot = 2; slot >= 0; --slot) {
        Actor& a = slots[std::size_t(slot)];
        if (negative(a.d1)) {
            continue;
        }
        const bool computer = (!world.two_players && (a.flags & 0x81) == 0x81) || (a.flags & 0x41) == 0x41;
        if (!computer || a.move != move::stand || a.step != 0 || !(a.terrain.feet & (terrain::ground | terrain::ladder))) {
            continue;
        }
        if (a.last_move == move::kick_right || a.last_move == move::kick_left) {
            finish(a, slot, world);
            a.step = 0x18;
            const auto marks = clock_marks(world);
            if (marks.late || (!marks.past && world.flag40)) {
                a.step = 1;
            } else if (marks.past) {
                a.step = 3;
            }
            continue;
        }
        a.move = move::enemy_think;
        a.step = 0x14;
        a.delay = 0;
    }
}

// $276D: the animation frame for the move, as an even number; the pose is
// the character kind's first pose plus the frame.
void Actors::choose_pose(int slot, const ActorWorld& world) {
    Actor& a = slots[std::size_t(slot)];
    if (a.posed != 0) {
        return;
    }
    a.posed = 1;
    a.previous_frame = a.frame;
    const bool right = a.facing == 1;
    const auto feet = a.terrain.feet;
    auto climbing = [&]() -> std::uint8_t { return a.previous_frame == 0x0A ? 0x0C : 0x0A; }; // $2972
    auto walk = [&](std::uint8_t first, std::uint8_t second) -> std::uint8_t {
        if (feet & terrain::ladder) {
            return climbing();
        }
        return a.frame == first ? second : first; // alternates with the last frame
    };
    std::uint8_t frame;
    switch (a.move) {
    case move::fall:
        frame = feet & terrain::solid ? 0x08 : 0x44;
        break;
    case move::duck:
        frame = right ? 0x42 : 0x40;
        break;
    case move::dying:
        frame = 0x08;
        break;
    case move::bump:
        frame = a.previous_frame == 0x44 || a.sprite_wide[0] || a.sprite_wide[1] ? 0x08 : a.previous_frame;
        break;
    case 0x1B:
        frame = a.facing == 0 ? (a.step >= 4 ? 0x3C : 0x3E) : (a.step >= 4 ? 0x38 : 0x3A);
        break;
    case move::thrown:
        frame = a.step >= 0x0A ? (a.facing == 0 ? 0x3C : 0x38) : (a.facing == 0 ? 0x3E : 0x3A);
        break;
    case move::turn_to_strike:
        frame = 0x2A;
        break;
    case move::chop:
        frame = right ? 0x06 : 0x04;
        break;
    case move::strike:
        frame = right ? 0x2E : 0x2C;
        break;
    case move::low_strike:
        frame = a.step >= 0x0B ? (right ? 0x34 : 0x30) : (right ? 0x36 : 0x32);
        break;
    case move::pushed_back:
        frame = right ? 0x02 : 0x00;
        break;
    case move::kick_left:
        frame = a.step >= 0x0A ? 0x16 : 0x18;
        break;
    case move::kick_right:
        frame = a.step >= 0x0A ? 0x1A : 0x1C;
        break;
    case move::enemy_think:
    case move::enemy_walk:
        frame = right ? walk(0x12, 0x14) : walk(0x0E, 0x10);
        break;
    case move::walk_left:
        frame = walk(0x0E, 0x10);
        break;
    case move::walk_right:
        frame = walk(0x12, 0x14);
        break;
    case move::jump:
        frame = a.step >= 0x0F ? 0x08 : a.step >= 0x0C ? 0x1E : a.step >= 2 ? 0x20 : a.step >= 1 ? 0x1E : 0x08;
        break;
    case move::jump_left:
        frame = a.step >= 0x0C || a.step < 3 ? 0x22 : 0x24;
        break;
    case move::jump_right:
        frame = a.step >= 0x0C || a.step < 3 ? 0x26 : 0x28;
        break;
    case move::ride:
        frame = a.ride_flag == 0 ? climbing() : a.previous_frame;
        break;
    case move::stand:
        if (feet & terrain::ladder) {
            frame = a.previous_frame == 0x0A || a.previous_frame == 0x0C ? a.previous_frame : climbing();
        } else {
            frame = right ? 0x02 : 0x00;
        }
        break;
    default:
        frame = climbing();
        break;
    }
    a.frame = frame;
    a.pose = std::uint8_t(world.assets.pose_base[std::size_t(a.kind())] + frame);
}

} // namespace bruce_lee::game
