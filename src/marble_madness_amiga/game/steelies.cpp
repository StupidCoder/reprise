// The steelies: black enemy marbles on Beginner, Intermediate and Ultimate
// ($119C7C-$11A7D6, $11365A) and the collisions between marbles ($1169C4).
//
// A steelie is a marble like the players' (the same rolling rules) whose
// push comes from a small mind instead of a joystick. The Track lists them
// (header +$18: a list of 8-byte entries {first band, last band, path,
// zone}, and a path whose last band is 16 earlier); one appears when the
// view scrolls into its bands and goes when it leaves them. Its path is a
// graph of waypoints (6 bytes: cell x, cell y, four neighbours by index).
//
// Modes: patrolling (no player in its zone: follow neighbour 0 from
// waypoint to waypoint), hunting (a player in its zone: at each waypoint
// take the neighbour nearest that player), chasing (the player within
// $280: roll straight at the player), returning (after a chase or a knock:
// head for the nearest waypoint), knocked (bumped by a marble).
#include "marble_madness_amiga/game/race.hpp"

namespace marble_madness::game {
namespace {

constexpr std::uint32_t steelie_picture = 0x8A;      // marbdat table: the steelie's draw record

std::int16_t s16(std::int32_t v) { return std::int16_t(v); }
std::int32_t abs32(std::int32_t v) { return v < 0 ? -v : v; }
std::uint16_t abs16(std::int16_t v) { return std::uint16_t(v < 0 ? -v : v); }

// The game's distance estimate between cells or points: the longer leg
// plus 3/8 of the shorter one (in 16 times the units).
std::uint16_t octagonal(std::uint16_t a, std::uint16_t b) {
    return std::uint16_t(b * 16) < std::uint16_t(a * 16) ? std::uint16_t((b & 0xFFF) * 6 + a * 16)
                                                         : std::uint16_t((a & 0xFFF) * 6 + b * 16);
}

bool steerable_mode(std::uint8_t mode) {
    return mode == Marble::rolling || mode == Marble::landing || mode == Marble::bumped;
}

} // namespace

Steelie* Race::steelie_of(Marble& m) {
    for (auto& s : steelies) {
        if (&s.body == &m) {
            return &s;
        }
    }
    return nullptr;
}

// $10D9BC: the view moved into another 16-line band: steelies, slinkies,
// the acid, then the regions.
void Race::bands_changed(int from, int to) {
    spawn_steelies(from, to);
    spawn_slinkies(from, to);
    spawn_acid(from, to);
    regions_.bands_changed(from, to, random);
    take_region_display_events();
    if (course_ == data::Course::silly) {
        silly_bands(from, to);
    }
}

// $119E5A.
void Race::spawn_steelies(int from, int to) {
    const auto& t = files_->track(course_);
    const std::uint32_t table = t.u32(files_->track_header(course_) + 0x18);
    const std::uint32_t short_path = t.u32(table + 4);
    for (std::uint32_t e = t.u32(table); t.s8(e) != -1; e += 8) {
        // The last steelie not in play.
        Steelie* free = nullptr;
        for (auto& s : steelies) {
            if (s.body.status == 0) {
                free = &s;
            }
        }
        if (free == nullptr) {
            break;
        }
        const std::uint32_t path = t.u32(e + 2);
        bool running = false;
        for (const auto& s : steelies) {
            running |= s.body.status != 0 && s.path == path;
        }
        const int first = t.s8(e), last = t.s8(e + 1);
        if (running || (first != to && last != to) || (from >= first && from <= last)) {
            continue;
        }
        auto& s = *free;
        auto& m = s.body;
        s.waypoint = s.path = path;
        m.feature = t.s8(e + 6);
        s.band_from = std::int16_t(first);
        s.band_to = std::int16_t(path == short_path ? last - 0x10 : last);
        m.x = t.s8(path) * 0x80000 + 0x40000;
        m.y = t.s8(path + 1) * 0x80000 + 0x40000;
        probe_.invalidate();
        locate(m);
        m.z = probe_.ground_height();
        s.chase_time = -1;
        m.vx = m.vy = 0;
        m.status = 1;
        m.air = 0;
        m.mode = Marble::patrolling;
        m.screen[1] = m.screen[0] = 0;
        m.dust_shown = 0;
        m.bounce = 0;
        m.region_code = 0;
        for (auto& c : m.covers) {
            c.cell = -1;
        }
        project(m, false);
        show(2, std::uint8_t(&s - steelies.data()));
    }
    // Steelies whose bands the view has left go.
    for (auto& s : steelies) {
        if (s.body.status != 0 && ((from == s.band_from && to < s.band_from) || (from == s.band_to && s.band_to < to))) {
            remove(s, false, true);
        }
    }
}

// $119C7C: one update of a steelie.
void Race::update_steelie(Steelie& s) {
    auto& m = s.body;
    if (m.status == 0) {
        return;
    }
    if (s.chase_time > 0 && --s.chase_time == 0 && (m.mode == Marble::chasing || m.mode == Marble::hunting)) {
        return_to_path(s);
    }
    if (m.mode == Marble::knocked) {
        if (m.bounce != 0) {
            --m.bounce;
        }
        if (m.bounce == 0) {
            return_to_path(s);
        }
    } else if (m.status == 2) {
        // Broken: the animation plays out.
        animate(m);
        project(m, true);
        check_visible(m);
        return;
    }
    save_grounded(m);
    steer(s);
    roll(m);
}

// $11365A: the steelie's push: towards its waypoint's centre, or straight
// at the player it chases, at its speed; knocked steelies push less.
void Race::steer(Steelie& s) {
    auto& m = s.body;
    think(s);
    if (m.air == 2) {
        m.vz -= 0x6000;
        if (m.vz < -0x50000) {
            m.vz = -0x50000;
        }
    } else {
        const auto& t = files_->track(course_);
        const Marble& p = marbles[std::size_t(s.target & 1)];
        std::int32_t dx, dy;
        if (m.mode == Marble::chasing && p.status == 1 && p.feature == m.feature && steerable_mode(p.mode)) {
            dx = p.x - m.x;
            dy = p.y - m.y;
        } else {
            dx = t.s8(s.waypoint) * 0x80000 - m.x + 0x40000;
            dy = t.s8(s.waypoint + 1) * 0x80000 - m.y + 0x40000;
        }
        const auto ax = std::uint16_t(abs32(dx) >> 12), ay = std::uint16_t(abs32(dy) >> 12);
        const auto distance =
            ay < ax ? std::uint16_t((ay >> 3) * 3 + ax) : std::uint16_t((ax >> 3) * 3 + ay);
        std::int16_t ux = 0, uy = 0;
        if (distance != 0) {
            ux = s16(dx / distance);
            uy = s16(dy / distance);
        }
        if (distance < 0x40) {
            s.speed = 0x10000;
        }
        const auto strength = s16(s.speed >> 8);
        std::int32_t fx = ((std::int32_t(ux) * strength >> 4) - m.vx) >> 5;
        std::int32_t fy = ((std::int32_t(uy) * strength >> 4) - m.vy) >> 5;
        if (m.mode == Marble::knocked) {
            int k = 0x1F - std::int8_t(m.bounce);
            if (k < 0) {
                k = 0;
            }
            fx = std::int32_t(s16(fx >> 8)) * k * 8;
            fy = std::int32_t(s16(fy >> 8)) * k * 8;
        }
        m.vx += fx;
        m.vy += fy;
    }
    cap_speed(m);
}

// $11A378: at its waypoint (its cell), a returning steelie patrols again;
// a returning one keeps looking for the nearest waypoint, others decide.
void Race::think(Steelie& s) {
    auto& m = s.body;
    const auto& t = files_->track(course_);
    const bool at = (m.x >> 19) == t.s8(s.waypoint) && (m.y >> 19) == t.s8(s.waypoint + 1);
    if (at && m.mode == Marble::returning) {
        m.mode = Marble::patrolling;
    }
    if (m.mode == Marble::returning) {
        nearest_waypoint(s);
    } else {
        choose_mode(s, at);
    }
}

// $11A49A.
void Race::choose_mode(Steelie& s, bool at_waypoint) {
    auto& m = s.body;
    const auto& t = files_->track(course_);
    const auto old = m.mode;
    bool step = at_waypoint;
    // The players it may go for: racing in its zone, on the ground.
    int left = players_;
    Marble* target = nullptr;
    for (int i = 0; i < players_; ++i) {
        auto& p = marbles[std::size_t(i)];
        if (p.status == 1 && p.feature == m.feature && p.air != 2 && steerable_mode(p.mode)) {
            --left;
            target = &p;
        }
    }
    bool decided = false;
    if (left != players_) {
        if (players_ == 2 && left == 0) {
            // Both: the one further down the course ($11A402).
            const auto& a = marbles[1];
            const auto& b = marbles[0];
            bool second = false;
            if (a.status == 1 && b.status == 1) {
                second = a.feature == b.feature ? s16((b.y >> 19) + (b.x >> 19)) < s16((a.y >> 19) + (a.x >> 19))
                                                : b.feature < a.feature;
            }
            target = second ? &marbles[1] : &marbles[0];
        }
        const auto ax = std::uint32_t(abs32(target->x - m.x) >> 12 & 0xFFFF);
        const auto ay = std::uint32_t(abs32(target->y - m.y) >> 12 & 0xFFFF);
        const std::uint32_t distance = ay < ax ? 3 * (ay >> 3) + ax : 3 * (ax >> 3) + ay;
        s.target = target->player;
        if (distance < 0x280 && (s.chase_time != 0 || m.feature == 7 || m.feature == 9) && m.air == 0) {
            m.mode = Marble::chasing;
            decided = true;
        } else if (s.chase_time != 0) {
            m.mode = Marble::hunting;
            decided = true;
        }
    }
    if (!decided && m.mode != Marble::returning) {
        m.mode = Marble::patrolling;
    }
    if (m.mode != old) {
        if (old == Marble::chasing) {
            return_to_path(s);
        } else {
            step = true;
        }
    }
    if (!step) {
        return;
    }
    int strength = 0;
    if (m.mode == Marble::chasing) {
        strength = 0xC0;
    } else if (m.mode == Marble::hunting) {
        strength = 0x80;
    } else if (m.mode == Marble::patrolling || m.mode == Marble::returning) {
        strength = 0x70;
    }
    s.speed = strength << 12;
    // $11A72C: the next waypoint.
    int link = -1;
    if (m.mode == Marble::hunting) {
        // $11A11C: the neighbour nearest the player.
        const Marble& p = marbles[std::size_t(s.target & 1)];
        const auto px = s16(p.x >> 19), py = s16(p.y >> 19);
        std::uint16_t best = 0x800;
        link = 0;
        for (int k = 0; k < 4; ++k) {
            const std::uint32_t n = s.path + std::uint32_t(t.s8(s.waypoint + 2 + std::uint32_t(k)) * 6);
            const auto d = octagonal(abs16(s16(px - t.s8(n))), abs16(s16(py - t.s8(n + 1))));
            if (d < best) {
                link = k;
                best = d;
            }
        }
    } else if (m.mode == Marble::patrolling) {
        link = 0;
    }
    if (link >= 0) {
        s.waypoint = s.path + std::uint32_t(t.s8(s.waypoint + 2 + std::uint32_t(link)) * 6); // $11A77C
    }
}

// $11A250: the path's waypoint nearest the steelie.
void Race::nearest_waypoint(Steelie& s) {
    const auto& t = files_->track(course_);
    const auto mx = s16(s.body.x >> 19), my = s16(s.body.y >> 19);
    std::uint16_t best = 0x400;
    for (std::uint32_t e = s.path; t.s8(e) != -1 && t.s8(e + 1) != -1; e += 6) {
        const auto d = octagonal(abs16(s16(mx - t.s8(e))), abs16(s16(my - t.s8(e + 1))));
        if (d < best) {
            s.waypoint = e;
            best = d;
        }
    }
}

// $11A7AE.
void Race::return_to_path(Steelie& s) {
    s.body.mode = Marble::returning;
    nearest_waypoint(s);
    s.speed = 0x70000;
}

// $11A09C: a steelie leaves play (`gone`), and/or the racing players get
// bonus 3 for it (`bonus`: it fell off the course or was broken).
void Race::remove(Steelie& s, bool bonus, bool gone) {
    if (gone) {
        s.body.status = 0;
        hide(2, std::uint8_t(&s - steelies.data()));
    }
    if (bonus) {
        for (int i = 0; i < players_; ++i) {
            auto& p = marbles[std::size_t(i)];
            if (p.status != 2 && p.status != 0) {
                award(p, 3);
            }
        }
    }
}

// $1169C4: a marble meets another within $70 (octagonal, 16ths) and 15
// height units: it goes back to where it started this update, the two swap
// velocities and push apart (by $116EC8 by speed; four times as hard between
// a player and a steelie). A steelie knocked so goes back to its path. A
// marble that comes down on another breaks it.
bool Race::collide(Marble& m) {
    const auto& p = files_->program();
    constexpr std::uint32_t push_by_speed = 0x116EC8;
    const int count = (course_ == data::Course::beginner || course_ == data::Course::intermediate ||
                       course_ == data::Course::ultimate)
                          ? 4
                          : 2;
    std::array<Marble*, 4> all{&marbles[0], &marbles[1], &steelies[0].body, &steelies[1].body};
    const auto px = probe_.x(), py = probe_.y(), pz = probe_.z();
    const bool right = px < py;
    bool hit = false;
    for (int i = 0; i < count; ++i) {
        Marble& o = *all[std::size_t(i)];
        if (&o == &m || o.status != 1) {
            continue;
        }
        const auto dx = s16(px - s16(o.x >> 16)), dy = s16(py - s16(o.y >> 16)), dz = s16(pz - s16(o.z >> 16));
        if (dx <= -8 || dx >= 8 || dy <= -8 || dy >= 8 || dz <= -0xF || dz >= 0xF ||
            o.mode == Marble::respawning || o.mode == Marble::breaking || o.mode == Marble::eaten ||
            m.region_code == 10) {
            continue;
        }
        if (octagonal(abs16(dx), abs16(dy)) >= 0x70) {
            continue;
        }
        hit = true;
        const bool a_player = is_player(m), b_player = is_player(o);
        m.x = start_x_;
        m.y = start_y_;
        m.z = start_z_;
        std::swap(m.vx, o.vx);
        std::swap(m.vy, o.vy);
        const int shift = a_player != b_player ? 14 : 12;
        auto kick = [&](std::int32_t v, bool flip) {
            std::int32_t k = std::int32_t(p.s8(push_by_speed + abs16(s16(v >> 16)))) << shift;
            return flip ? -k : k;
        };
        const std::int32_t mkx = kick(m.vx, o.x < m.x), mky = kick(m.vy, o.y < m.y);
        m.vx -= mkx;
        m.vy -= mky;
        const std::int32_t okx = kick(o.vx, o.x < m.x), oky = kick(o.vy, o.y < m.y);
        o.vx += okx;
        o.vy += oky;
        for (Marble* knocked : {&m, &o}) {
            if (!is_player(*knocked)) {
                auto& s = *steelie_of(*knocked);
                if (s.chase_time == -1) {
                    s.chase_time = 0x96;
                }
                knocked->mode = Marble::knocked;
                knocked->bounce = std::uint8_t(knocked->bounce + 0x14);
                return_to_path(s);
            }
        }
        sound(a_player && b_player ? 8 : 0x1B, right);
        if (dz >= 0 && m.air != 0) {
            // Came down on it.
            if (!b_player) {
                remove(*steelie_of(o), true, false);
                o.status = 2;
            } else {
                sound_stop();
            }
            set_mode(o, Marble::breaking);
        }
    }
    return hit;
}

} // namespace marble_madness::game
