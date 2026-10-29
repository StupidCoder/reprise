// Silly's critters and birds ($11C78C-$11DA6C).
//
// Critters: nine creatures (three each of kinds 7, 8 and 9) in a pen beside
// the start, there while the view is in bands $1D-$38 ($10DAAC). They
// wander in steps along sixteen directions (kind 7 turns a little at
// random, kinds 8 and 9 go along x or y and kind 9 now and then stands),
// avoiding each other and the pen's walls (a 16 x 16 bit map of cells,
// $11D478); with a player's marble in the pen (zone 1, at the pen's floor)
// they flee from the nearest one. A player who reaches one in the pen
// catches it: three seconds, a bonus picture and 500 points.
//
// Birds: while the view is in bands 3-$1B, every eighth update up to two
// birds drop in at the top of five columns ($11DA7C) and walk down them
// (towards -y) in short and long runs; one that meets a wall is squashed,
// one that meets a player's marble eats it (mode 11, then a respawn).
// Track header +$24 holds their animations and the critters'.
#include "marble_madness_amiga/game/race.hpp"

namespace marble_madness::game {
namespace {

std::int16_t s16(std::int32_t v) { return std::int16_t(v); }

// Track header +$24's lists.
enum SillyList : std::uint32_t {
    bird_drop = 0x00, bird_walk = 0x04, bird_squash = 0x08, bird_eat = 0x0C,
    kind8_caught = 0x10, kind8_x_plus = 0x14, kind8_y_plus = 0x18, kind8_y_minus = 0x1C,
    kind8_x_minus = 0x20, kind8_still = 0x24,
    kind7_moving = 0x28, kind7_touched = 0x2C, kind7_caught = 0x30,
    kind9_caught = 0x34, kind9_x_plus = 0x38, kind9_y_plus = 0x3C, kind9_y_minus = 0x40,
    kind9_x_minus = 0x44, kind9_still = 0x48,
};

constexpr std::uint32_t pen_map = 0x11D478;      // 16 words: bit per cell, set = wall
constexpr std::uint32_t step_x = 0x11D498, step_y = 0x11D4B8; // words by direction (1024 = 4 pixels)
constexpr std::uint32_t bird_columns = 0x11DA7C; // 5 x (cell x, cell y) where birds drop in
constexpr std::uint32_t bird_sounds = 0x11DA86;  // longs by column
constexpr int pen_x = 0x2CA, pen_y = 0x2D2;      // a critter starts at pen + 4 * random(32)
constexpr std::int32_t pen_floor = 0x3F6E0000;

} // namespace

std::uint32_t Race::silly_list(std::uint32_t entry) const {
    const auto& t = files_->track(course_);
    return t.u32(t.u32(files_->track_header(course_) + 0x24) + entry);
}

// $10DAAC: the critters come with bands $1D-$38, the birds with 3-$1B.
void Race::silly_bands(int from, int to) {
    auto within = [](int band, int first, int last) { return band >= first && band <= last; };
    if (!within(from, 0x1D, 0x38)) {
        if (within(to, 0x1D, 0x38)) {
            spawn_critters();
        }
    } else if (!within(to, 0x1D, 0x38)) {
        for (auto& c : critters) { // $11C894
            c.status = 0;
            hide(c.kind, c.index);
        }
    }
    if (!within(from, 3, 0x1B)) {
        if (within(to, 3, 0x1B)) {
            birds_on_ = true;
        }
    } else if (!within(to, 3, 0x1B)) {
        birds_on_ = false;
    }
}

// $11C78C: unless some are out already, all nine at random free places.
void Race::spawn_critters() {
    for (const auto& c : critters) {
        if (c.status == 1) {
            return;
        }
    }
    for (std::size_t i = 0; i < critters.size(); ++i) {
        Critter& c = critters[i];
        c.kind = std::uint8_t(i < 3 ? 7 : i < 6 ? 8 : 9);
        c.status = 1;
        c.state = Critter::wandering;
        do {
            c.x = std::int32_t(random.next(0x20) * 4 + pen_x) << 16;
            c.y = std::int32_t(random.next(0x20) * 4 + pen_y) << 16;
        } while (critter_blocked(c));
        c.z = pen_floor;
        c.vx = c.vy = c.vz = 0;
        c.steps = 0;
        critter_choose(c);
        critter_place(c);
        show(c.kind, c.index);
    }
}

// $11CBCA: near another critter that is not caught (within 12 on both
// axes), or not on a free cell of the pen.
bool Race::critter_blocked(const Critter& c) const {
    const std::int16_t x = s16(c.x >> 16), y = s16(c.y >> 16);
    for (const auto& o : critters) {
        if (&o == &c || o.status == 0 || o.state == Critter::caught) {
            continue;
        }
        const auto dx = std::uint16_t(std::abs(s16(s16(o.x >> 16) - x))), dy = std::uint16_t(std::abs(s16(s16(o.y >> 16) - y)));
        if (dx < 0xC && dy < 0xC) {
            return true;
        }
    }
    const int cx = std::int8_t((x >> 3) - 0x59), cy = std::int8_t((y >> 3) - 0x5A);
    if (cx < 0 || cx >= 0x10 || cy < 0 || cy >= 0x10) {
        return true;
    }
    return ((1 << cx) & files_->program().s16(pen_map + std::uint32_t(cy) * 2)) != 0;
}

// $11C8DA: one update. A player's marble on the pen's floor in zone 1 makes
// them flee; a step every update while fleeing, every third otherwise.
void Race::update_critters() {
    if (course_ != data::Course::silly) {
        return;
    }
    bool chased = false;
    for (int i = 0; i < players_ && !chased; ++i) {
        const Marble& m = marbles[std::size_t(i)];
        chased = m.status == 1 && s16(m.z >> 16) == 0x3F6E && m.feature == 1;
    }
    for (auto& c : critters) {
        if (c.status == 0) {
            continue;
        }
        ++c.tick;
        if (c.state != Critter::caught) {
            c.state = chased ? Critter::fleeing : Critter::wandering;
        }
        const int period = c.state == Critter::fleeing ? 1 : 3;
        if (c.tick >= period) {
            c.tick = 0;
            const auto& t = files_->track(course_);
            if (c.kind == 7) {
                if (c.state == Critter::caught) {
                    c.animation += 4;
                    if (t.u32(c.animation) == 0xFFFFFFFF) {
                        critter_choose(c);
                    }
                } else {
                    c.x += c.vx;
                    c.y += c.vy;
                    if (++c.steps > 3) {
                        critter_choose(c);
                        c.steps = 0;
                    }
                }
            } else {
                c.animation += 4;
                if (t.u32(c.animation) == 0xFFFFFFFF) {
                    c.x += c.vx;
                    c.y += c.vy;
                    critter_choose(c);
                }
            }
        }
        critter_place(c);
    }
}

// $11CD5E: what next, by the state.
void Race::critter_choose(Critter& c) {
    if (c.state == Critter::caught) {
        c.animation = silly_list(c.kind == 7 ? kind7_caught : c.kind == 8 ? kind8_caught : kind9_caught);
    } else if (c.state == Critter::fleeing) {
        critter_flee(c);
        critter_animation(c);
    } else {
        critter_wander(c);
        critter_animation(c);
    }
}

// $11CDFC: the animation for the way it moves.
void Race::critter_animation(Critter& c) {
    const std::int16_t vx = s16(c.vx >> 16), vy = s16(c.vy >> 16);
    auto by_motion = [&](std::uint32_t x_plus, std::uint32_t y_plus, std::uint32_t y_minus, std::uint32_t x_minus,
                         std::uint32_t still) {
        if (vy > 0) return y_plus;
        if (vy < 0) return y_minus;
        if (vx > 0) return x_plus;
        return vx < 0 ? x_minus : still;
    };
    if (c.kind == 9) {
        c.animation = silly_list(by_motion(kind9_x_plus, kind9_y_plus, kind9_y_minus, kind9_x_minus, kind9_still));
    } else if (c.kind == 8) {
        c.animation = silly_list(by_motion(kind8_x_plus, kind8_y_plus, kind8_y_minus, kind8_x_minus, kind8_still));
    } else if (c.kind == 7) {
        c.animation = silly_list(kind7_moving);
    }
}

// $11D002: a new direction at random: kind 7 turns up to two sixteenths;
// the others go along y (or along x when they were moving along x), either
// way, and kind 9 stands one time in four.
void Race::critter_wander(Critter& c) {
    if (c.kind == 7) {
        c.direction = std::uint8_t((random.next(5) + c.direction - 2) & 0xF);
    } else {
        const std::uint8_t half = std::uint8_t(random.next(2) * 8);
        c.direction = c.vx != 0 ? std::uint8_t(half + 4) : half;
        if (random.next(4) == 0 && c.kind == 9) {
            c.direction = 0x10;
            c.vx = c.vy = 0;
        }
    }
    critter_walk(c);
}

// $11CF30: a step that way; when blocked, the next free direction (any for
// kind 7, quarter turns for the others), trying from where it was; none
// free: it stands. (A first step that is free is kept.)
void Race::critter_walk(Critter& c) {
    if (c.direction == 0x10) {
        return;
    }
    const std::int32_t x = c.x, y = c.y;
    const int turn = c.kind == 7 ? 1 : 4;
    critter_step(c);
    if (!critter_blocked(c)) {
        return;
    }
    for (int i = 0; i < 12; ++i) {
        if (c.kind == 7 || (i & 3) == 0) {
            c.x = x;
            c.y = y;
            c.direction = std::uint8_t((turn + c.direction) & 0xF);
            critter_step(c);
            if (!critter_blocked(c)) {
                c.x = x;
                c.y = y;
                return;
            }
        }
    }
    c.direction = 0x10;
    c.vx = c.vy = 0;
}

// $11D182: away from the nearest racing marble (within $140, octagonal):
// kind 7 by the angle in sixteenths, the others along the axis it is
// further along.
void Race::critter_flee(Critter& c) {
    const Marble* nearest = &marbles[0];
    std::uint16_t best = 0x140;
    for (int i = 0; i < players_; ++i) {
        const Marble& m = marbles[std::size_t(i)];
        if (m.status == 0) {
            continue;
        }
        const auto dx = std::uint16_t(std::abs(s16(s16(c.x >> 16) - s16(m.x >> 16))));
        const auto dy = std::uint16_t(std::abs(s16(s16(c.y >> 16) - s16(m.y >> 16))));
        const std::uint16_t d = std::uint16_t(dx * 16) < std::uint16_t(dy * 16) ? std::uint16_t((dx & 0xFFF) * 6 + dy * 16)
                                                                              : std::uint16_t((dy & 0xFFF) * 6 + dx * 16);
        if (d < best) {
            best = d;
            nearest = &m;
        }
    }
    const std::int16_t ux = s16(s16(c.x >> 16) - s16(nearest->x >> 16));
    const std::int16_t uy = s16(s16(c.y >> 16) - s16(nearest->y >> 16));
    if (c.kind == 7) {
        const std::uint16_t ax = std::uint16_t(std::abs(ux)) >> 4, ay = std::uint16_t(std::abs(uy)) >> 4;
        c.direction = ay == 0 ? 0 : ay < ax ? 1 : ax < ay ? 3 : 2;
        if (ux >= 1 && uy < 0) {
            c.direction += 12;
        } else if (ux < 0 && uy < 0) {
            c.direction += 8;
        } else if (ux < 0 && uy > 0) {
            c.direction += 4;
        }
    } else if ((uy >> 4) < 1) {
        c.direction = (uy >> 4) < 0 ? 0xC : (ux >> 4) < 1 ? 8 : 0;
    } else {
        c.direction = 4;
    }
    critter_dodge(c);
}

// $11D08E: as critter_walk, but trying the other directions in turn from a
// quarter before (not its first one again), and in the end it stays where
// it was.
void Race::critter_dodge(Critter& c) {
    if (c.direction == 0x10) {
        return;
    }
    const std::int32_t x = c.x, y = c.y;
    const int turn = c.kind == 7 ? 1 : 4;
    critter_step(c);
    if (critter_blocked(c)) {
        const std::uint8_t first = c.direction;
        c.direction = std::uint8_t(first - 4);
        bool found = false;
        for (int i = 0; i < 9 && !found; ++i) {
            if (c.kind == 7 || (i & 3) == 0) {
                c.x = x;
                c.y = y;
                c.direction = std::uint8_t((turn + c.direction) & 0xF);
                if (c.direction != first) {
                    critter_step(c);
                    found = !critter_blocked(c);
                }
            }
        }
        if (!found) {
            c.direction = 0x10;
            c.vx = c.vy = 0;
        }
    }
    c.x = x;
    c.y = y;
}

// $11D3D2: a step along its direction; the velocity is that step (a
// quarter of it for kind 7).
void Race::critter_step(Critter& c) {
    const auto& p = files_->program();
    const std::int32_t dx = std::int32_t(p.s16(step_x + std::uint32_t(c.direction) * 2)) << 8;
    const std::int32_t dy = std::int32_t(p.s16(step_y + std::uint32_t(c.direction) * 2)) << 8;
    c.x += dx;
    c.y += dy;
    c.vx = c.kind == 7 ? dx >> 2 : dx;
    c.vy = c.kind == 7 ? dy >> 2 : dy;
}

// $11D42E: the screen place ($106A00, which leaves the point in the probe).
void Race::critter_place(Critter& c) {
    const auto x = s16(c.x >> 16), y = s16(c.y >> 16), z = s16(c.z >> 16);
    probe_.set_point(x, y);
    c.screen = std::int32_t(std::uint32_t(std::uint16_t(s16(y - x + 0x88))) << 16 |
                            std::uint16_t(s16(s16(view_) + z + 0x6C - s16((x + y) >> 1))));
}

// $11CA44: a player's marble in zone 1 within 4 (kind 7: 5) of a critter
// catches it.
void Race::touch_critters(Marble& m) {
    if (course_ != data::Course::silly || m.feature != 1) {
        return;
    }
    for (auto& c : critters) {
        if (c.status == 0 || c.state == Critter::caught) {
            continue;
        }
        const int reach = c.kind == 7 ? 5 : 4;
        const auto dx = std::uint16_t(std::abs(s16(probe_.x() - s16(c.x >> 16))));
        const auto dy = std::uint16_t(std::abs(s16(probe_.y() - s16(c.y >> 16))));
        if (dx >= reach || dy >= reach) {
            continue;
        }
        c.state = Critter::caught;
        c.vx = c.vy = 0;
        critter_choose(c);
        if (c.kind == 9) {
            c.animation = silly_list(kind9_caught);
            sound(0x22);
        } else if (c.kind == 7) {
            c.animation = silly_list(kind7_touched);
            sound(0x23);
        } else {
            sound(0x24);
        }
        m.time = s16(m.time + 3);
        award(m, 0xE);
        m.score += 500; // $10A7F8
        score_changed_ |= std::uint8_t(1 << m.player);
    }
}

// $11D506: up to two birds: in each of two rounds the first column with no
// more birds than the round's number (and, with one, that one at least 4
// cells down) gets a new one, with its column's sound.
void Race::spawn_birds() {
    const auto& p = files_->program();
    for (int round = 0; round < 2; ++round) {
        for (std::uint32_t column = 0; column < 5; ++column) {
            const int cx = p.s8(bird_columns + column * 2), cy = p.s8(bird_columns + column * 2 + 1);
            int count = 0;
            const Bird* first = nullptr;
            for (const auto& b : birds) {
                if (b.status == 1 && (s16(b.x >> 16) >> 3) == cx && ++count < 2) {
                    first = &b;
                }
            }
            if (count > round || (count == 1 && cy - (s16(first->y >> 16) >> 3) <= 3)) {
                continue;
            }
            Bird* bird = nullptr;
            for (auto& b : birds) {
                if (b.status != 1) {
                    bird = &b;
                    break;
                }
            }
            if (bird == nullptr) {
                return;
            }
            Bird& b = *bird;
            b.status = 1;
            b.state = Bird::dropping;
            b.x = cx * 0x80000 + 0x40000;
            b.y = cy * 0x80000 + 0x40000;
            b.z = 0x3FD80000;
            b.tick = 0;
            b.count = 0;
            b.period = 8;
            b.animation = silly_list(bird_drop);
            b.vy = -0x40000;
            bird_place(b);
            show(0xF, b.index);
            sound(int(p.u32(bird_sounds + column * 4)), true);
            break;
        }
    }
}

// $11D6B0: one update.
void Race::update_birds() {
    if (course_ != data::Course::silly) {
        return;
    }
    if (birds_on_ && (frame_ & 7) == 0) {
        spawn_birds();
    }
    const auto& t = files_->track(course_);
    for (auto& b : birds) {
        if (b.status == 0) {
            continue;
        }
        bool squashed = false;
        if (++b.tick >= b.period) {
            b.tick = 0;
            b.animation += 4;
            if (t.u32(b.animation) == 0xFFFFFFFF) {
                if (b.state == Bird::gone) {
                    b.status = 0;
                    hide(0xF, b.index);
                } else {
                    // A walk: the next one fast and long when no bird is
                    // ahead and this one was slow, else slow and short.
                    bool clear = true;
                    for (const auto& o : birds) {
                        if (&o != &b && o.status == 1 && s16(o.x >> 16) == s16(b.x >> 16) && s16(o.y >> 16) < s16(b.y >> 16)) {
                            clear = false;
                            break;
                        }
                    }
                    b.animation = silly_list(bird_walk);
                    b.state = Bird::walking;
                    if (b.count != 0) {
                        --b.count;
                    }
                    if (b.count == 0) {
                        if (clear && b.vy == -0x20000) {
                            b.period = 1;
                            b.vy = -0x40000;
                            b.count = std::uint8_t(random.next(2) + 4);
                        } else {
                            b.period = 4;
                            b.vy = -0x20000;
                            b.count = std::uint8_t(random.next(2) + 1);
                        }
                    }
                }
            }
        }
        if (b.state == Bird::walking) {
            const std::int32_t y = b.y;
            b.y += b.vy;
            probe_.place(b.x, b.y);
            if (b.z < probe_.ground_height_here(terrain_)) {
                b.y = y & ~0x3FFFF;
                b.animation = silly_list(bird_squash);
                b.tick = 0;
                b.period = 1;
                b.state = Bird::gone;
                squashed = true;
            }
        }
        bird_place(b);
        if (squashed && (s16(b.x >> 16) >> 3) < 0x35) {
            const std::int16_t line = s16(b.screen);
            if (line >= 0 && line < 0xF0) {
                sound(0x2B, false);
            }
        }
    }
}

// $11D9CC: the screen place; covers in the three columns where walls stand
// in front of the birds' path.
void Race::bird_place(Bird& b) {
    const auto x = s16(b.x >> 16), y = s16(b.y >> 16), z = s16(b.z >> 16);
    probe_.set_point(x, y);
    const auto sx = s16(y - x + 0x88), sy = s16(s16(view_) + z + 0x6C - s16((x + y) >> 1));
    b.screen = std::int32_t(std::uint32_t(std::uint16_t(sx)) << 16 | std::uint16_t(sy));
    const int cx = x >> 3;
    for (auto& c : b.covers) {
        c.cell = -1;
    }
    if ((y >> 3) < 0x34 || (cx != 0x39 && cx != 0x31 && cx != 0x29)) {
        return;
    }
    const auto& t = files_->track(course_);
    const std::uint32_t record = t.u32(b.animation);
    if (!t.contains(record, 4)) {
        return;
    }
    const auto left = s16(t.s8(record) + sx), top = s16(t.s8(record + 1) + sy - 0x10);
    int columns = 3;
    std::array<Marble::Cover, 5> found{};
    find_covers({b.x, b.y, b.z, left, top, s16(top + 0x10), 0, false}, columns, found);
    for (std::size_t i = 0; i < 3; ++i) {
        b.covers[i] = found[i];
    }
}

// $11D8EA: a walking bird meets a player's marble (its point within 6
// across and from 4 behind to 8 ahead): the bird eats it.
void Race::touch_birds(Marble& m) {
    if (course_ != data::Course::silly) {
        return;
    }
    for (auto& b : birds) {
        if (b.status == 0 || b.state != Bird::walking) {
            continue;
        }
        const std::int16_t bx = s16(b.x >> 16), by = s16(b.y >> 16);
        if (s16(bx - 6) <= probe_.x() && probe_.x() < s16(bx - 6) + 0xC && s16(by - 4) <= probe_.y() &&
            probe_.y() < s16(by - 4) + 0xC) {
            b.state = Bird::gone;
            b.animation = silly_list(bird_eat);
            b.period = 4;
            b.tick = 0;
            m.mode = Marble::eaten;
            m.delay = 0x66;
            sound(0x14);
        }
    }
}

} // namespace marble_madness::game
