// The slinkies ($11A81E-$11BD32): green worms on Beginner, Intermediate
// and Ultimate.
//
// The Track lists them (header +$1C: a table whose first entry is a list of
// 8-byte entries {first band, last band, path, zone}, the rest animation
// lists by state and direction); one appears when the view scrolls into its
// bands and goes when it leaves them. A slinky moves a cell per round of its
// animation along a path of waypoints (3 bytes: cell x, y, the next one's
// index). When a player of its zone rolls fast at the right distance it
// hops after it on a height curve; landing within reach it eats the marble
// (the player starts again), close by it throws the marble off. A marble
// that runs into a slinky is stopped and bumped, and knocks the slinky
// sliding.
#include "marble_madness_amiga/game/race.hpp"

namespace marble_madness::game {
namespace {

constexpr std::uint32_t hop_heights = 0x11B2AA; // bytes by hop update (6-$25): the hop's height

std::int16_t s16(std::int32_t v) { return std::int16_t(v); }
std::int32_t abs32(std::int32_t v) { return v < 0 ? -v : v; }
std::uint16_t abs16(std::int16_t v) { return std::uint16_t(v < 0 ? -v : v); }

std::uint16_t octagonal(std::uint16_t a, std::uint16_t b) {
    return std::uint16_t(b * 16) < std::uint16_t(a * 16) ? std::uint16_t((b & 0xFFF) * 6 + a * 16)
                                                         : std::uint16_t((a & 0xFFF) * 6 + b * 16);
}

// The animation table's lists (offsets into header +$1C's table).
enum Animation : std::uint32_t {
    still = 0x04,
    eating_list = 0x08,
    missed_list = 0x0C,
    hopping_list = 0x10,
    landing_x_plus = 0x14,
    landing_x_minus = 0x18,
    landing_y_plus = 0x1C,
    landing_y_minus = 0x20,
    knocked_list = 0x24,
    walk_y_plus = 0x28,
    walk_y_minus = 0x2C,
    walk_x_plus = 0x30,
    walk_x_minus = 0x34,
};

} // namespace

// $11ABF8.
void Race::spawn_slinkies(int from, int to) {
    const auto& t = files_->track(course_);
    const std::uint32_t table = t.u32(files_->track_header(course_) + 0x1C);
    for (std::uint32_t e = t.u32(table); t.s8(e) != -1; e += 8) {
        Slinky* free = nullptr;
        for (auto& s : slinkies) {
            if (s.status == 0) {
                free = &s;
                break;
            }
        }
        if (free == nullptr) {
            break;
        }
        const std::uint32_t path = t.u32(e + 2);
        bool running = false;
        for (const auto& s : slinkies) {
            running |= s.status != 0 && s.path == path;
        }
        const int first = t.s8(e), last = t.s8(e + 1);
        if (running || (first != to && last != to) || (from >= first && from <= last)) {
            continue;
        }
        auto& s = *free;
        s.waypoint = s.path = path;
        s.zone = t.s8(e + 6);
        s.band_from = std::int16_t(first);
        s.band_to = std::int16_t(last);
        s.x = t.s8(path) * 0x80000 + 0x40000;
        s.y = t.s8(path + 1) * 0x80000 + 0x40000;
        probe_.place(s.x, s.y);
        s.z = probe_.ground_height_here(terrain_);
        s.vx = s.vy = 0;
        s.status = 1;
        s.state = Slinky::wandering;
        s.screen = 0;
        for (auto& c : s.covers) {
            c.cell = -1;
        }
        s.frame = s.animation = t.u32(table + still);
        s.step = 1;
        s.tick = 0;
        s.period = 2;
        slinky_place(s);
        s.index = std::uint8_t(&s - slinkies.data());
        show(4, s.index);
    }
    for (auto& s : slinkies) {
        if (s.status != 0 && ((from == s.band_from && to < s.band_from) || (from == s.band_to && s.band_to < to))) {
            s.status = 0;
            hide(4, std::uint8_t(&s - slinkies.data()));
        }
    }
}

// $11A81E: one update: step the animation (a round's end moves a wandering
// slinky on and decides what next), the hop's flight, its screen place.
void Race::update_slinky(Slinky& s) {
    if (s.status == 0) {
        return;
    }
    const auto& t = files_->track(course_);
    if (++s.tick < s.period) {
        if (s.state == Slinky::knocked) {
            slinky_step(s);
        }
    } else {
        s.tick = 0;
        s.frame = std::uint32_t(std::int32_t(s.frame) + s.step * 4);
        if (t.u32(s.frame) == 0xFFFFFFFF || s.frame == s.animation) {
            s.frame = s.animation;
            if (s.step > 0 && (s.state == Slinky::wandering || s.state == Slinky::returning)) {
                s.x += s.vx;
                s.y += s.vy;
            }
            slinky_step(s);
        } else if (s.state == Slinky::knocked) {
            slinky_step(s);
        }
    }
    if (s.state == Slinky::hopping || s.state == Slinky::eating || s.state == Slinky::missed) {
        if (s.hop > 5 && s.hop < 0x26) {
            probe_.place(s.x, s.y);
            const std::int32_t ground = probe_.ground_height_here(terrain_);
            s.z = (std::int32_t(files_->program().s8(hop_heights + s.hop)) << 16) + ground;
            const Marble& p = marbles[std::size_t(s.target & 1)];
            if (p.status == 1 && p.feature == s.zone && p.air == 0 && p.z == ground &&
                (p.mode == Marble::rolling || p.mode == Marble::landing || p.mode == Marble::bumped ||
                 p.mode == Marble::frozen)) {
                // Fly at the player.
                const std::int32_t dx = p.x - s.x, dy = p.y - s.y;
                s.vx = dx >> 4;
                s.vy = dy >> 4;
                const auto ax = std::uint16_t(abs32(dx) >> 12), ay = std::uint16_t(abs32(dy) >> 12);
                const auto distance = ay < ax ? std::uint16_t((ay >> 3) * 3 + ax) : std::uint16_t((ax >> 3) * 3 + ay);
                std::int16_t ux = 0, uy = 0;
                if (distance != 0) {
                    ux = s16(dx / distance);
                    uy = s16(dy / distance);
                }
                s.vx = (((std::int32_t(ux) * 0x800 >> 4) - s.vx) >> 5) + s.vx;
                s.vy = (((std::int32_t(uy) * 0x800 >> 4) - s.vy) >> 5) + s.vy;
            } else {
                s.vx = s.vy = 0;
            }
            start_x_ = s.x;
            start_y_ = s.y;
            s.x += s.vx;
            s.y += s.vy;
            probe_.place(s.x, s.y);
            if (probe_.ground_height_here(terrain_) != ground) {
                s.x = start_x_;
                s.y = start_y_;
            }
        }
        ++s.hop;
    }
    slinky_place(s);
}

// $11B248: the screen place and the wall covers ($11EC42 with 2: the
// frame record's size, no hand-placed covers).
void Race::slinky_place(Slinky& s) {
    const auto& t = files_->track(course_);
    probe_.set_point(s16(s.x >> 16), s16(s.y >> 16));
    const auto x = s16(s.x >> 16), y = s16(s.y >> 16), z = s16(s.z >> 16);
    const auto screen_x = s16(y - x + 0x88);
    const auto screen_y = s16(s16(view_) + z + 0x6C - s16((x + y) >> 1));
    s.screen = std::int32_t(std::uint32_t(std::uint16_t(screen_x)) << 16 | std::uint16_t(screen_y));
    const std::uint32_t record = t.u32(s.frame);
    if (!t.contains(record, 4)) {
        return;
    }
    int columns = t.s8(record + 2) > 1 ? 5 : 3;
    const auto left = s16(t.s8(record) + screen_x), top = s16(t.s8(record + 1) + screen_y);
    find_covers({s.x, s.y, s.z, left, top, s16(top + t.s8(record + 3) * 8), columns == 3 ? 1 : 2, false}, columns,
                s.covers);
}

// $11B904: at the end of an animation round (every update while knocked).
void Race::slinky_step(Slinky& s) {
    const auto& t = files_->track(course_);
    const std::uint32_t table = t.u32(files_->track_header(course_) + 0x1C);
    switch (s.state) {
    case Slinky::wandering:
    case Slinky::returning: {
        const auto cx = s16(s.x >> 19), cy = s16(s.y >> 19);
        if (cx == t.s8(s.path) && cy == t.s8(s.path + 1) && s.animation != t.u32(table + still)) {
            // Back at the path's start: a pause.
            if (s.state == Slinky::returning) {
                s.state = Slinky::wandering;
            }
            s.animation = t.u32(table + still);
            s.vx = s.vy = 0;
            s.step = 1;
            s.frame = s.animation;
            s.tick = 0;
            s.period = 2;
            s.waypoint = s.path + std::uint32_t(t.s8(s.waypoint + 2) * 3);
        } else if (cx == t.s8(s.waypoint) && cy == t.s8(s.waypoint + 1)) {
            if (s.state == Slinky::returning) {
                s.state = Slinky::wandering;
            }
            s.waypoint = s.path + std::uint32_t(t.s8(s.waypoint + 2) * 3);
            slinky_animation(s);
        } else {
            slinky_animation(s);
        }
        if (s.state == Slinky::wandering) {
            slinky_lookout(s);
        }
        break;
    }
    case Slinky::hopping: {
        Marble& p = marbles[std::size_t(s.target & 1)];
        const std::int32_t dx = p.x - s.x, dy = p.y - s.y;
        const auto ax = s16(abs32(dx) >> 12), ay = s16(abs32(dy) >> 12);
        if (ax <= 0x100 && ay <= 0x100) {
            const auto distance = ax > ay ? s16((ay >> 3) * 3 + ax) : s16((ax >> 3) * 3 + ay);
            if (distance <= 0x6F) {
                // Caught.
                p.x = s.x;
                p.y = s.y;
                p.mode = Marble::frozen;
                s.state = Slinky::eating;
                sound(0x1A);
                slinky_animation(s);
                break;
            }
            if (distance < 0xC0) {
                // Close: the marble is thrown off.
                p.vx = (std::int32_t(s16(dx / distance)) * 0x400) >> 4;
                p.vy = (std::int32_t(s16(dy / distance)) * 0x400) >> 4;
            }
        }
        s.state = Slinky::missed;
        slinky_animation(s);
        break;
    }
    case Slinky::knocked:
        friction(s.vx, s.vy, 2);
        if (s.vx == 0 && s.vy == 0) {
            s.state = Slinky::landing;
            slinky_animation(s);
        } else {
            const std::int32_t x = s.x, y = s.y, z = s.z;
            s.x += s.vx;
            s.y += s.vy;
            probe_.place(s.x, s.y);
            const std::int32_t ground = probe_.ground_height_here(terrain_);
            s.z = ground;
            if (ground != z) {
                s.state = ground < z ? Slinky::landing : Slinky::hopping;
                s.x = x;
                s.y = y;
                s.z = z;
            }
            slinky_animation(s);
        }
        break;
    case Slinky::landing:
        s.state = Slinky::hopping;
        slinky_animation(s);
        break;
    case Slinky::eating:
    case Slinky::missed:
        s.x = (s.x & std::int32_t(0xFFF80000u)) + 0x40000;
        s.y = (s.y & std::int32_t(0xFFF80000u)) + 0x40000;
        if (s.state == Slinky::eating) {
            Marble& p = marbles[std::size_t(s.target & 1)];
            sound_stop();
            p.delay = 0x65;
            set_mode(p, Marble::respawning);
        }
        s.state = Slinky::returning;
        slinky_nearest_waypoint(s);
        slinky_animation(s);
        break;
    default:
        break;
    }
}

// $11B2E0: the animation (and, wandering, the step) for the state.
void Race::slinky_animation(Slinky& s) {
    const auto& t = files_->track(course_);
    const std::uint32_t table = t.u32(files_->track_header(course_) + 0x1C);
    switch (s.state) {
    case Slinky::wandering:
    case Slinky::returning: {
        // Towards the waypoint along one axis (moving: y first).
        const int wx = t.s8(s.waypoint), wy = t.s8(s.waypoint + 1);
        const int cx = s16(s.x >> 19), cy = s16(s.y >> 19);
        int dx = 0, dy = 0;
        if (s.vx == 0) {
            if (wx < cx) dx = -8;
            else if (cx < wx) dx = 8;
            else if (wy < cy) dy = -8;
            else if (cy < wy) dy = 8;
        } else {
            if (wy < cy) dy = -8;
            else if (cy < wy) dy = 8;
            else if (wx < cx) dx = -8;
            else if (cx < wx) dx = 8;
        }
        std::uint32_t list = still;
        if (dy > 0) list = walk_y_plus;
        else if (dy < 0) list = walk_y_minus;
        else if (dx > 0) list = walk_x_plus;
        else if (dx < 0) list = walk_x_minus;
        s.animation = t.u32(table + list);
        s.vx = dx << 16;
        s.vy = dy << 16;
        s.step = 1;
        break;
    }
    case Slinky::hopping:
        s.animation = t.u32(table + hopping_list);
        s.hop = 0;
        s.step = 1;
        break;
    case Slinky::knocked:
        if (t.u32(table + knocked_list) != s.animation) {
            if (s.frame == s.animation) {
                s.animation = t.u32(table + knocked_list);
                s.step = 1;
            } else {
                s.step = s16(std::int32_t(s.frame - s.animation) >> 2) < 4 ? -1 : 1;
            }
        }
        break;
    case Slinky::landing:
        if (abs32(s.knock_vy) < abs32(s.knock_vx)) {
            s.animation = t.u32(table + (s.knock_vx < 1 ? landing_x_minus : landing_x_plus));
        } else {
            s.animation = t.u32(table + (s.knock_vy < 1 ? landing_y_minus : landing_y_plus));
        }
        s.step = 1;
        break;
    case Slinky::eating:
        s.animation = t.u32(table + eating_list);
        break;
    case Slinky::missed:
        s.animation = t.u32(table + missed_list);
        break;
    default:
        break;
    }
    s.frame = s.animation;
    s.tick = 0;
    s.period = (s.state == Slinky::wandering || s.state == Slinky::landing) ? 2 : 1;
}

// $11B5F6: a wandering slinky watches for a player of its zone rolling
// fast ($C000) on the ground that no other slinky is after; between $180
// and $280 away it hops.
void Race::slinky_lookout(Slinky& s) {
    int left = players_;
    Marble* target = nullptr;
    for (int i = 0; i < players_; ++i) {
        auto& p = marbles[std::size_t(i)];
        if (p.status != 1 || p.feature != s.zone) {
            continue;
        }
        if (abs32(p.vy) + abs32(p.vx) > 0xC000 && p.air != 2 &&
            (p.mode == Marble::rolling || p.mode == Marble::landing || p.mode == Marble::bumped)) {
            bool taken = false;
            for (const auto& o : slinkies) {
                taken |= &o != &s && o.status == 1 && o.state == Slinky::hopping && o.target == p.player;
            }
            if (!taken) {
                --left;
                target = &p;
            }
        }
    }
    if (left == players_) {
        return;
    }
    if (players_ == 2 && left == 0) {
        const auto& a = marbles[1];
        const auto& b = marbles[0];
        bool second = false;
        if (a.status == 1 && b.status == 1) {
            second = a.feature == b.feature ? s16((b.y >> 19) + (b.x >> 19)) < s16((a.y >> 19) + (a.x >> 19))
                                            : b.feature < a.feature;
        }
        target = second ? &marbles[1] : &marbles[0];
    }
    const auto ax = std::uint32_t(abs32(target->x - s.x) >> 12 & 0xFFFF);
    const auto ay = std::uint32_t(abs32(target->y - s.y) >> 12 & 0xFFFF);
    const std::uint32_t distance = ay < ax ? 3 * (ay >> 3) + ax : 3 * (ax >> 3) + ay;
    s.target = target->player;
    if (distance > 0x180 && distance < 0x280) {
        s.state = Slinky::hopping;
        slinky_animation(s);
    }
}

// $11AE1E: the path's waypoint nearest the slinky.
void Race::slinky_nearest_waypoint(Slinky& s) {
    const auto& t = files_->track(course_);
    const auto mx = s16(s.x >> 19), my = s16(s.y >> 19);
    std::uint16_t best = 0x400;
    for (std::uint32_t e = s.path; t.s8(e) != -1 && t.s8(e + 1) != -1; e += 3) {
        const auto d = octagonal(abs16(s16(mx - t.s8(e))), abs16(s16(my - t.s8(e + 1))));
        if (d < best) {
            s.waypoint = e;
            best = d;
        }
    }
}

// $11AF24: a player's marble against the slinkies (their frame record's box,
// 16 high): the marble goes back, stops and is bumped (mode 5); a slinky
// standing or wandering is knocked sliding with the marble's push.
bool Race::touch_slinkies(Marble& m) {
    if (course_ != data::Course::beginner && course_ != data::Course::intermediate &&
        course_ != data::Course::ultimate) {
        return false;
    }
    const auto& t = files_->track(course_);
    const auto& md = files_->marble_data();
    const auto px = probe_.x(), py = probe_.y(), pz = probe_.z();
    for (auto& s : slinkies) {
        if (s.status == 0) {
            continue;
        }
        const std::uint32_t record = t.u32(s.frame);
        int x0 = -4, y0 = -4, w = 8, h = 8;
        if (record != 0xFFFFFFFF) {
            x0 = t.s8(record + 4);
            y0 = t.s8(record + 5);
            w = t.s8(record + 6);
            h = t.s8(record + 7);
        }
        x0 += s16(s.x >> 16);
        y0 += s16(s.y >> 16);
        const auto sz = s16(s.z >> 16);
        auto within = [](int v, int from, int size) { return from <= v && v <= s16(size + from); };
        const bool z_hit = (sz <= pz && pz <= s16(sz + 0x10)) || (sz <= s16(pz + 0xE) && s16(pz + 0xE) <= s16(sz + 0x10));
        const bool x_hit = within(s16(px - 3), x0, w) || within(s16(px + 3), x0, w);
        const bool y_hit = within(s16(py - 3), y0, h) || within(s16(py + 3), y0, h);
        if (!(z_hit && x_hit && y_hit)) {
            continue;
        }
        if (s.state == Slinky::wandering || s.state == Slinky::returning) {
            s.state = Slinky::knocked;
            slinky_animation(s);
            s.vx = s.knock_vx = m.vx;
            s.vy = s.knock_vy = m.vy;
        }
        if ((s.state == Slinky::hopping || s.state == Slinky::missed) && m.player == s.target) {
            return true; // its own prey: the hop decides
        }
        if (m.mode != Marble::bumped && s.state != Slinky::eating && !is_bot(m)) { // hunts players only
            s.target = m.player;
        }
        m.x = start_x_;
        m.y = start_y_;
        m.vx = m.vy = 0;
        if (m.mode == Marble::landing) {
            m.animation_tick = 0;
            m.animation_period = 2;
            m.animation = md.u32(marble_data_ + 4);
        } else if (m.mode != Marble::bumped) {
            sound(0xE);
            m.animation_tick = 0;
            m.animation_period = 2;
            m.animation = md.u32(marble_data_);
        }
        if (m.mode != Marble::bumped && m.mode != Marble::frozen) {
            m.mode = Marble::bumped;
            m.bounce = 0x32;
        }
        return true;
    }
    return false;
}

} // namespace marble_madness::game
