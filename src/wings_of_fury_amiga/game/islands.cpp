#include "wings_of_fury_amiga/game/islands.hpp"
#include "wings_of_fury_amiga/game/flight.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace wings::game {
namespace {

constexpr std::uint32_t a4 = 0x11AF52;

// Scenery indices of the map columns.
constexpr int hut_index = 4, wrecked_hut_index = 5, dugout_index = 3;
constexpr int pillbox_first = 0xF, pillbox_last = 0x1E;
constexpr int island_end_index = 2; // `bchr`

constexpr int soldier_score = 0x19, hut_score = 0x96, dugout_score = 0xC8, pillbox_score = 0xC8;

int word(int v) {
    return std::int16_t(v);
}
int index_of(std::uint16_t w) {
    return w >> 2 & 0x1FF;
}

} // namespace

IslandTables read_island_tables(const amiga::LoadedImage& wings) {
    IslandTables t;
    for (std::size_t i = 0; i < t.missions_per_rank.size(); ++i) {
        t.missions_per_rank[i] = wings.s16(a4 - 0x5AB6 + std::uint32_t(2 * i));
    }
    for (std::uint32_t i = 0; i < 28; ++i) t.bonus_row.push_back(wings.u8(a4 - 0x7B9F + i));
    for (std::uint32_t i = 0; i < 15 * 4; ++i) t.bonus.push_back(wings.s16(a4 - 0x7B82 + 2 * i));
    for (std::size_t i = 0; i < t.smoke_shift.size(); ++i) t.smoke_shift[i] = wings.u8(a4 - 0x640E + std::uint32_t(i));
    for (std::size_t i = 0; i < t.damage_smoke.size(); ++i) t.damage_smoke[i] = wings.u8(a4 - 0x5A34 + std::uint32_t(i));
    return t;
}

Islands find_islands(const data::MissionMap& map) {
    // $102DE8 counts, $103140 fills the lists (drawn columns only); the
    // island index counts the islands' right ends passed so far.
    Islands is;
    int island = 0;
    bool has_targets = false;
    const auto targets = [&]() -> IslandTargets& {
        if (std::size_t(island) >= is.targets.size()) is.targets.resize(std::size_t(island) + 1);
        return is.targets[std::size_t(island)];
    };
    for (std::size_t c = 0; c < map.words.size(); ++c) {
        const auto w = map.words[c];
        if ((w & 0x8000) == 0) continue;
        const int offset = int(c) * 2, x = offset * 4, index = index_of(w);
        if (index == hut_index || index == dugout_index) {
            Shelter s;
            s.dugout = index == dugout_index;
            s.offset = offset;
            s.island = island;
            s.left_x = s.dugout ? x - 0x2C : x;
            s.right_x = s.dugout ? x + 0x10 : x;
            (s.dugout ? is.dugouts : is.huts).push_back(s);
            auto& t = targets();
            t.soldiers += 5;
            if (s.dugout) {
                if (t.first_dugout == 0) t.first_dugout = offset;
                t.last_dugout = offset;
            }
            has_targets = true;
        } else if (index == pillbox_first) {
            is.pillboxes.push_back({offset, island});
            targets().pillboxes += 1;
            has_targets = true;
        } else if (index == island_end_index) {
            targets();
            if (has_targets) ++is.islands_left;
            has_targets = false;
            ++island;
        }
    }
    is.soldiers.resize((is.huts.size() + is.dugouts.size()) * 5);
    return is;
}


namespace {

// The island work done for one tick or frame.
class IslandWork {
public:
    IslandWork(PlayerFlight& p, FlightWorld& w, const FlightTables& t) : p(p), w(w), t(t), is(w.islands) {}

    // $102306: louder the nearer to the plane.
    int volume_at(int x) const {
        const int distance = word(std::abs(word(x - p.x)) + std::abs(word(0x14 - p.altitude)));
        const int v = std::uint16_t(distance) >> 5;
        return v > 0x40 ? 0 : 0x40 - v;
    }

    std::uint16_t column_word(int offset) const {
        const auto c = std::size_t(offset / 2);
        return offset >= 0 && c < w.map->words.size() ? w.map->words[c] : 0;
    }
    void set_column_word(int offset, std::uint16_t v) {
        const auto c = std::size_t(offset / 2);
        if (offset >= 0 && c < w.mission_map.words.size()) w.mission_map.words[c] = v;
    }
    // $1050C8: surface and scenery index under a world x.
    int surface_at(int x) const {
        if (x < 0 || x >= int(w.map->words.size()) * 8) return 0;
        return column_word((x >> 3) * 2) & 3;
    }
    int index_at(int x) const {
        if (x < 0 || x >= int(w.map->words.size()) * 8) return 0;
        return index_of(column_word((x >> 3) * 2));
    }

    // $104AE4: the four columns of a building at x: the drawn one (the first
    // drawn among the four from one column left of x) two before it and one
    // after; none found: all at the map's start.
    std::array<int, 4> building_columns(int x) const {
        int at = ((std::uint16_t(x) >> 2) & 0xFFFE) - 2;
        for (int i = 0; i < 4; ++i, at += 2) {
            if (column_word(at) & 0x8000) return {at - 4, at - 2, at, at + 2};
        }
        return {0, 0, 0, 0};
    }
    // $104B54: the hut, dugout or pillbox standing at x (by the drawn
    // column's offset and the scenery under x).
    struct Found {
        Shelter* shelter{};
        Pillbox* pillbox{};
    };
    Found building_at(int x) {
        const auto columns = building_columns(x);
        int drawn = -1;
        for (int c : columns) {
            if (column_word(c) & 0x8000) {
                drawn = c;
                break;
            }
        }
        if (drawn < 0) return {};
        const int index = index_at(x) & 0x1FFF;
        if (index == wrecked_hut_index) {
            for (auto& h : is.huts) {
                if (h.offset == drawn) return {&h, nullptr};
            }
        } else if (index == dugout_index) {
            for (auto& d : is.dugouts) {
                if (d.offset == drawn) return {&d, nullptr};
            }
        } else if (index >= pillbox_first && index <= pillbox_last) {
            for (auto& b : is.pillboxes) {
                if (b.offset == drawn) return {nullptr, &b};
            }
        }
        return {};
    }

    // $104B40: everyone inside runs out, one every 60 ticks.
    static void empty(Shelter& s) {
        s.leaving += s.inside;
        s.inside = 0;
        s.leave_timer = 0x3C;
    }

    // $101E82: a soldier comes out of a hut (1) or dugout (2) into the first
    // free slot. A hut's soldier runs by the low bits of the frame clock; a
    // dugout's away from the island's middle (the outermost dugouts) or
    // either way, now and then the other way.
    void come_out(const Shelter& s, int direction) {
        if (s.dugout) {
            const auto& t = is.targets[std::size_t(s.island)];
            direction = s.offset == t.first_dugout ? 1 : s.offset == t.last_dugout ? -1 : 0;
            int r = 0;
            for (int i = 0; i < 4; ++i) r = w.beam.next();
            if ((((r << 1) | (r >> 15)) & 0x1F) == 0) direction = -direction;
        }
        for (auto& soldier : is.soldiers) {
            if (soldier.state != 0) continue;
            soldier.state = 1;
            soldier.island = s.island;
            int d = direction;
            if (d == 0) d = std::int8_t(w.vblank_clock & 0xFF);
            soldier.direction = std::int8_t(d);
            int x = soldier.direction < 0 ? s.left_x : s.right_x;
            const int spread = d & 7;
            soldier.step = spread & 3;
            soldier.x = word(x + spread * 4);
            return;
        }
    }

    // $101DE4 (each tick): huts and dugouts let their soldiers out.
    void release() {
        const auto run = [&](std::vector<Shelter>& list) {
            for (auto& s : list) {
                if (s.leave_timer == 0 || --s.leave_timer != 0) continue;
                come_out(s, 0);
                s.leaving = (s.leaving - 1) & 0xFF; // (a byte)
                if (s.leaving == 0) continue;
                const int next = int(w.vblank_clock >> 4 & 0x1F);
                s.leave_timer = next != 0 ? next : 3;
            }
        };
        run(is.dugouts);
        run(is.huts);
    }

    // The bonus for an island taken ($105AE8) and what follows.
    void island_taken(int island) {
        const std::size_t row_at = std::size_t(w.rank * 4 + w.mission);
        const int row = row_at < t.islands.bonus_row.size() ? t.islands.bonus_row[row_at] : 0;
        const std::size_t at = std::size_t(row * 4 + island);
        const int bonus = at < t.islands.bonus.size() ? t.islands.bonus[at] : 0;
        w.score += bonus;
        if (--is.islands_left > 0 || is.ships_left != 0) {
            w.say(format(t.messages.island_taken, bonus)); // ($105624: only when no message runs)
        } else {
            // ($105694 appends over the first message's last character.)
            std::string text = format(t.messages.island_taken, bonus);
            if (!text.empty()) text.pop_back();
            mission_accomplished(text);
        }
    }

    static std::string format(const std::string& text, long value) {
        char buffer[256];
        std::snprintf(buffer, sizeof buffer, text.c_str(), value);
        return buffer;
    }

    // $105694: the next mission is set; the plane is sent home.
    void mission_accomplished(std::string text) {
        ++w.mission;
        if (w.mission > t.islands.missions_per_rank[std::size_t(std::clamp(w.rank, 0, 6))]) {
            w.mission = 1;
            w.rank = std::min(w.rank + 1, 6);
            w.celebration = true;
            text += t.messages.promoted;
        } else {
            text += t.messages.next_mission;
        }
        w.say(text, true);
        w.mission_accomplished = true;
    }

    // $1046DC (land): what a weapon (+$22: 0 rocket) or a crash at x hits.
    // $104986: in the water or on a ship: the ship whose columns cover x
    // ($104A4E: the enemy ships, then the US carrier). A running torpedo
    // flashes the screen red and takes one of the ship's torpedo hits; the
    // last starts it sinking. Rockets (and crashes) knock out a ship's guns
    // (none on the ships rebuilt so far); bombs do nothing.
    void ship_hit(int x, int weapon, bool running) {
        const int offset = (std::uint16_t(x) >> 3) * 2;
        ShipState* ship = nullptr;
        for (auto& s : w.ships) {
            if (offset >= s.first && offset <= s.last && (&s == &w.ships[4] || s.afloat)) {
                ship = &s;
                break;
            }
        }
        if (ship == nullptr || weapon == 1) return;
        is.flash_frames = 5;
        is.flash_colour = 0xFFF;
        if (weapon != 2 || !running) return;
        is.flash_colour = 0xF00;
        if (ship->ready <= 0) return;
        if (--ship->ready == 0) ship->sink_delay = 0x14;
    }

    void hit(int x, int weapon, bool running) {
        const int surface = surface_at(x);
        if (surface == 0 || surface == 1) {
            ship_hit(x, weapon, running);
            return;
        }
        const int index = index_at(x) & 0x1FFF;
        if (weapon == 0) {
            is.flash_frames = 5;
            is.flash_colour = 0xFFF;
        }
        if (index == dugout_index) {
            auto found = building_at(x);
            if (found.shelter == nullptr) return;
            if (weapon == 0) is.flash_colour = 0xF00;
            if (found.shelter->inside == 0) return;
            found.shelter->refill = 0xC8;
            w.score += dugout_score;
            empty(*found.shelter);
        } else if (index == hut_index) {
            for (int c : building_columns(x)) set_column_word(c, std::uint16_t((column_word(c) & 0x8000) | 0x16));
            auto found = building_at(x);
            if (found.shelter == nullptr) return;
            if (weapon == 0) is.flash_colour = 0xF00;
            found.shelter->gun_delay = 0x32;
            found.shelter->refill = 1;
            w.score += hut_score;
            empty(*found.shelter);
        } else if (index >= pillbox_first && index <= pillbox_last - 1 && weapon == 0) {
            auto found = building_at(x);
            if (found.pillbox == nullptr) return;
            is.flash_colour = 0xF00;
            auto& b = *found.pillbox;
            // Each rocket breaks the part of the pillbox it hits.
            int damage = index - pillbox_first;
            const int part = word((word((std::uint16_t(x) >> 2) - b.offset) >> 1) + 3);
            const int shift = (4 - part) & 63; // (lsl.w by a register: 16 and more leave 0)
            if (shift < 16) damage |= 1 << shift;
            const auto columns = building_columns(x);
            for (int c : columns) {
                set_column_word(c, std::uint16_t((column_word(c) & 0x8000) | ((damage + pillbox_first) << 2) | 2));
            }
            if (b.destroyed) return;
            b.destroyed = true;
            b.smoke_left = 0x32;
            b.smoke_timer = 1;
            w.score += pillbox_score;
            auto& tg = is.targets[std::size_t(b.island)];
            if (--tg.pillboxes == 0 && tg.soldiers == 0) island_taken(b.island);
        }
    }

    // $101A8C: soldiers within reach of x fall (and scream, $1023AC).
    void kill_near(int x, int reach) {
        const int from = word(x - reach);
        for (auto& s : is.soldiers) {
            if (s.state != 1) continue;
            if (std::uint16_t(word(s.x - from)) > std::uint16_t(reach * 2)) continue;
            s.state = 2;
            s.step = 5;
            s.timer = 2;
            scream(from); // (the distance from the blast's left edge)
        }
    }
    void scream(int x) {
        auto& c = w.sound.channels[3];
        c.request[1] = {true, Sample::scream, 0x19AC, 0x17C, volume_at(x) >> 1, 1};
        c.request[0].active = false;
        c.playing = Sample::none;
    }

    // $103D78 (each frame): the dugouts' guns.
    void dugout_guns() {
        for (auto& d : is.dugouts) {
            if (d.inside == 0) {
                if (--d.refill <= 0) {
                    d.refill = 0xC8;
                    call_soldier(d);
                }
                continue;
            }
            if (d.gun_delay != 0 && --d.gun_delay != 0) continue;
            const int x = d.offset * 4;
            const int entry = gun_picture(x);
            if (entry < 0) continue;
            is.sprites.push_back({entry, x, 0x14, true});
            flak(x);
        }
    }

    // $103DE8 (each frame): the pillboxes. A standing one aims and fires its
    // gun as a dugout does (drawn $16 up); a destroyed one smokes: 50 puffs,
    // each a frame later than the last.
    void pillboxes() {
        for (auto& b : is.pillboxes) {
            const int x = b.offset * 4;
            if (b.destroyed) {
                if (b.smoke_left == 0 || --b.smoke_timer > 0 || --b.smoke_left == 0) continue;
                b.smoke_timer = 0x32 - b.smoke_left;
                w.effects.start_puff(std::int32_t(std::uint32_t(x + 8) << 16), 0x110000, 5, w.beam);
                continue;
            }
            const int entry = gun_picture(x);
            if (entry < 0) continue;
            is.sprites.push_back({entry, x, 0x16, true});
            flak(x);
        }
    }

    // $104FEE: an empty dugout calls a soldier from the nearest hut of its
    // island that still has two or more inside.
    void call_soldier(const Shelter& d) {
        Shelter* nearest = nullptr;
        int best = 0x2710;
        for (auto& h : is.huts) {
            if (h.island != d.island || h.inside < 2) continue;
            const int distance = std::abs(word(d.left_x - h.left_x));
            if (distance == 0 || distance > best) continue;
            best = distance;
            nearest = &h;
        }
        if (nearest == nullptr) return;
        --nearest->inside;
        come_out(*nearest, word(nearest->left_x - d.left_x) < 0 ? 1 : -1);
    }

    // $104D50 / $104DB8: the gun's picture, aimed at the plane (`gun*` by
    // the angle table, `gnf*` when it fires); in the 1/8 view a random
    // flash; -1 when the plane is out of reach or not flying.
    int gun_picture(int x) {
        if (p.mode != 0) return -1;
        if (w.frame_eighth) return (w.beam.next() & 0x8000) ? 0x5A : -1;
        int dx = word(w.frame_camera - x);
        if (dx > 0x200 || dx < -0x200) return -1;
        const int row = word(((std::int16_t(w.frame_altitude) >> 5) - 1) << 3);
        const int column = word(dx / 0x28 + 3);
        int entry = t.gun_angle(row + column) + 0x81;
        if ((w.beam.next() >> 12 & 0xF) <= 5) entry += 7; // (rol.w #4: the top four bits)
        return entry;
    }

    // $104F5C: flak near the plane; every hit counts down +$10, and at 0 the
    // oil and fuel suffer.
    void flak(int x) {
        int d = std::abs(word(x - w.frame_camera));
        if (d > 0x1C0) return;
        // (The frame's guns are heard: the nearest sets the loudness.)
        is.gun_distance = std::min(is.gun_distance, d);
        is.guns_heard = true;
        int a = d, b = p.altitude;
        if (a <= b) std::swap(a, b);
        const int reach = word(a + (std::uint16_t(b) >> 2));
        if ((w.beam.next() & 0x1FF) < reach) return;
        if ((w.beam.next() & 0x7FF) > 0x199) return;
        smoke_at_plane(6);
        if (--p.spare > 0) return;
        --p.oil;
        p.fuel -= w.beam.next() & 3;
        p.spare = (w.beam.next() & 7) + 6;
    }

    // $1054E0: a puff of smoke at the plane's nose.
    void smoke_at_plane(int size) {
        std::uint32_t x = std::uint32_t(std::uint16_t(w.frame_camera)) << 16;
        x += p.facing < 0 ? std::uint32_t(-0xA0000) : 0xA0000u;
        std::uint32_t y = (std::uint32_t(std::uint16_t(w.frame_altitude)) << 16 | std::uint16_t(w.last.speed_y)) + 0xD0000u;
        if (p.turn > 8 && p.turn < 0x12) {
            std::uint32_t shift = t.islands.smoke_shift[std::size_t(p.turn - 8)] & 0xF;
            const bool back = p.facing >= 0 ? shift < 0xB : shift <= 0xB;
            if (back) shift = std::uint32_t(-std::int32_t(shift));
            x += (shift << 16) | (shift >> 16);
        }
        w.effects.start_puff(std::int32_t(x), std::int32_t(y), size, w.beam);
    }

    // $101BFC (every second tick): a damaged plane smokes, the more the
    // worse the oil.
    void damage_smoke(bool this_tick) {
        if (!this_tick || p.mode == 6 || p.mode == 8) return;
        const int damage = word(0x80 - p.oil);
        if (damage == 0) return;
        if (std::uint16_t(damage) <= 0x13) {
            const int bit = ((damage << 3) & 0xF0) + (w.beam.next() & 0xF);
            if ((t.islands.damage_smoke[std::size_t(bit >> 3) % 40] >> (bit & 7) & 1) == 0) return;
        }
        smoke_at_plane(5);
    }

    // $104E18 (a wrecked hut's column drawn): its ruin smokes, a puff every
    // (50 - puffs left) frames.
    void hut_smoke(int offset) {
        for (auto& h : is.huts) {
            if (h.offset != offset || h.gun_delay == 0) continue;
            if (--h.refill > 0 || --h.gun_delay == 0) return;
            h.refill = 0x32 - h.gun_delay;
            w.effects.start_puff(std::int32_t(std::uint32_t(offset * 4 + 0x10) << 16), 0x110000, 5, w.beam);
            return;
        }
    }

    // $103EEE (each frame): the soldiers run, fall, lie dead; one who
    // reaches an empty dugout mans its gun.
    void soldiers() {
        for (auto& s : is.soldiers) {
            if (s.state == 0) continue;
            if (s.state == 2) {
                if (--s.timer < 0) {
                    s.timer = 2;
                    if (++s.step > 7) {
                        s.state = 3;
                        w.score += soldier_score;
                        ++is.soldiers_killed;
                        auto& tg = is.targets[std::size_t(s.island)];
                        if (--tg.soldiers == 0 && tg.pillboxes == 0) {
                            island_taken(s.island);
                            // (The mission accomplished ends this frame's soldiers.)
                            if (w.mission_accomplished) return;
                        }
                    }
                }
            } else if (s.state == 1) {
                if (++s.step > 4) s.step = 0;
                int dx = s.direction < 0 ? -3 : 3;
                if (surface_at(word(s.x + dx)) == 0) {
                    dx = -dx; // (the water's edge: back)
                    s.direction = std::int8_t(-s.direction);
                }
                s.x = word(s.x + dx);
            }
            // The picture: `guy*`, facing left 9 further; in the 1/8 view
            // two steps and no bodies.
            if (w.frame_eighth) {
                if (s.state != 3) is.sprites.push_back({0x6F + (s.step & 1), s.x, 0xC, false});
            } else {
                is.sprites.push_back({0x6F + s.step + (s.direction < 0 ? 9 : 0), s.x, 0xC, false});
            }
            if (s.state == 1 && index_at(s.x) == dugout_index && s.x >= 0) {
                auto found = building_at(s.x);
                if (found.shelter == nullptr) continue;
                if (found.shelter->gun_delay == 0 && found.shelter->inside == 0) found.shelter->gun_delay = 0x168;
                ++found.shelter->inside;
                s.state = 0;
            }
        }
    }

private:
    PlayerFlight& p;
    FlightWorld& w;
    const FlightTables& t;
    Islands& is;
};

} // namespace

void soldiers_come_out(PlayerFlight& p, FlightWorld& w, const FlightTables& t) {
    IslandWork(p, w, t).release();
}

void damaged_plane_smoke(PlayerFlight& p, FlightWorld& w, const FlightTables& t) {
    IslandWork(p, w, t).damage_smoke(w.smoke_tick);
}

void island_hit(PlayerFlight& p, FlightWorld& w, const FlightTables& t, int x, int weapon, bool running) {
    IslandWork(p, w, t).hit(x, weapon, running);
}

void kill_soldiers_near(PlayerFlight& p, FlightWorld& w, const FlightTables& t, int x, int reach) {
    IslandWork(p, w, t).kill_near(x, reach);
}

void islands_frame(PlayerFlight& p, FlightWorld& w, const FlightTables& t, const std::vector<int>& wrecked_huts) {
    IslandWork work(p, w, t);
    w.islands.sprites.clear();
    w.islands.guns_heard = false;   // ($103772)
    w.islands.gun_distance = 0x2710;
    for (int offset : wrecked_huts) work.hut_smoke(offset);
    work.dugout_guns();
    work.pillboxes();
    work.soldiers();
    // $1038EA (the view's end): the guns' sound for the next tick.
    w.islands.gun_sound = w.islands.guns_heard;
    if (w.islands.guns_heard) w.islands.gun_volume = 0x40 - std::min(std::uint16_t(w.islands.gun_distance) >> 3, 0x40);
}

} // namespace wings::game
