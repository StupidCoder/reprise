#include "wings_of_fury_amiga/game/enemies.hpp"
#include "wings_of_fury_amiga/game/flight.hpp"
#include <algorithm>
#include <cstdlib>

namespace wings::game {
namespace {

// State and mode numbers (the plane's +0 and +2).
constexpr int flying = 2;
constexpr int falling = 4;
constexpr int burning = 0x10;
constexpr int closing_in = 1;
constexpr int attacking = 2;
constexpr int torpedo_run = 4;
constexpr int leaving = 0x10;
constexpr int turning = 8;

// The player record's +$C.
constexpr int player_flying = 0;
constexpr int player_on_deck = 1;

constexpr int torpedo_picture = 0x1A; // jt1a: a torpedo bomber in level flight
constexpr int right_pictures = 0x1C;
constexpr int wreck_left = 0x1B, wreck_right = 0x37; // jc1a / jc10

int word(int v) {
    return std::int16_t(v);
}

class EnemyWork {
public:
    EnemyWork(PlayerFlight& p, FlightWorld& w, const FlightTables& t) : p(p), w(w), t(t), e(w.enemies), et(t.enemies) {}

    // $10CAC8: the beam position's remainder by n.
    int rand(int n) { return int(std::uint16_t(w.beam.next()) % std::uint16_t(n)); }

    // $10E4D0: a plane appears at x. A formation's plane is a torpedo
    // bomber, and only one flies at a time; a lone fighter flies at
    // `altitude`.
    void launch(bool formation, int x, int altitude, int facing) {
        int free = -1;
        bool bomber = false;
        for (int i = 0; i < 4; ++i) {
            const auto& o = e.planes[std::size_t(i)];
            if (o.state == 0) free = i;
            if ((o.mode & torpedo_run) && o.state != 0) bomber = true;
        }
        if (free == -1 || (formation && bomber)) return;
        auto& n = e.planes[std::size_t(free)];
        n.state = flying;
        n.facing = facing;
        n.x = x;
        n.target_altitude = 0x32;
        n.target_speed = n.speed = et.min_speed;
        n.oil = 0xF0;
        n.hit_timer = rand(3) + 5;
        n.hit = n.side = n.turn_wait = n.turn = n.order = 0;
        if (formation) {
            n.mode = torpedo_run;
            n.altitude = 0x32;
        } else {
            ++e.fighters_out;
            n.mode = closing_in;
            n.altitude = altitude;
        }
    }

    // $10BC02 (at the start of the flight code): the record's timer sends
    // a torpedo bomber when it runs out, from $1800 behind the plane's way
    // to the carrier (or, over the deck, either side). Firing the guns
    // keeps it at $2EE or more; it waits while the carrier is down or out
    // of torpedo hits.
    void timer(int command) {
        const int far = std::abs(word(0x7FFF - p.x));
        if (command & (command::fire_held | command::fire_tapped)) {
            if (p.enemy_timer != 0 && p.enemy_timer < 0x2EE) p.enemy_timer = 0x2EE;
            return;
        }
        const auto& carrier = w.ships[4];
        if (p.enemy_timer == 0 || !carrier.afloat || carrier.ready == 0) return;
        if (--p.enemy_timer != 0 || far <= 0x1A00) return;
        bool left = p.x < p.deck_low;
        if (!left && p.x <= p.deck_high) left = (w.beam.next() & 0x8000) != 0;
        if (left) launch(true, word(p.x - 0x1800), 0, 1);
        else launch(true, word(p.x + 0x1800), 0, -1);
    }

    // $10E7D6: each plane's tick.
    void tick() {
        e.attacker_step = 0;
        for (auto& o : e.planes) {
            if (o.state == 0) continue;
            find_side(o);
            find_order();
            if (o.state == flying) {
                fly(o);
                if (!(o.mode & torpedo_run) && o.target_altitude < 0x21) o.target_altitude = 0x21;
            } else if (o.state == falling) {
                fall(o);
            } else if (o.state == burning) {
                burn(o);
            }
            ease_speed(o);
            if (o.state != burning) move(o);
            set_picture(o);
        }
    }

    // $10B682: the player's guns hit a plane ahead flying the same way,
    // within $A0 and $14, while the plane flies level.
    void guns() {
        if (!w.guns) return;
        for (auto& o : e.planes) {
            if (o.side != 3) continue;
            const int dx = std::abs(word(p.x - o.x));
            const int dy = std::abs(word(p.altitude - o.altitude));
            if (dx >= 0xA0 || dy >= 0x14 || p.climb != 0) continue;
            o.hit = 1;
            if (--o.hit_timer > 0) continue;
            w.effects.start_smoke(word(o.x - 0x10), word(o.altitude + 0xA), 6, w.beam);
            o.oil = word(o.oil - 8);
            if (o.oil < 0x60) {
                w.score += 0x15E;
                o.state = falling;
                o.target_altitude = -3;
                o.turn = 0;
                o.mode &= ~turning;
                set_picture(o);
            }
            o.hit_timer = rand(4) + 6;
        }
    }

private:
    PlayerFlight& p;
    FlightWorld& w;
    const FlightTables& t;
    Enemies& e;
    const EnemyTables& et;

    int map_columns() const { return int(w.map->words.size()); }
    std::uint16_t word_at(int c) const { return c < map_columns() ? w.map->words[std::size_t(c)] : 0; }
    // $10C982: the map column under x (as flight.cpp's column()).
    int column(int x) const {
        if (x < 0) x = 0;
        return std::min(word(x / 8), map_columns() + 2);
    }
    bool inside(int c) const { return c > 0 && c < map_columns() - 1; }
    // $10CB74 / $10CBB2: over open water / over an island.
    bool over_water(int c) const { return !inside(c) || (word_at(c) & 3) == 0; }
    bool over_island(int c) const { return inside(c) && (word_at(c) & 3) == 2; }
    int surface_at(int x) const {
        if (x < 0 || x >= map_columns() * 8) return 0;
        return word_at(x >> 3) & 3;
    }
    // $10CAE0: a puff of smoke.
    void smoke(int x, int y, int size) { w.effects.start_smoke(x, y, size, w.beam); }
    // The beam draw that decides whether a damaged plane smokes this tick.
    void damage_smoke(const EnemyPlane& o, int x, int y) {
        const int damage = word(0x80 - o.oil);
        if (damage > (w.beam.next() & 0x3F)) smoke(x, y, (damage >> 3) + 1);
    }

    // $10D18C: a leaving plane is gone once it is $A28 from the player.
    bool gone(EnemyPlane& o) {
        if (!(o.mode & leaving)) return false;
        if (std::abs(word(p.x - o.x)) <= 0xA28) return false;
        o.state = o.mode = 0;
        return true;
    }

    // $10D35A.
    void set_picture(EnemyPlane& o) {
        int v = o.turn;
        if ((o.mode & torpedo_run) && o.turn == 0) v = torpedo_picture;
        o.picture = o.facing == -1 ? v : v + right_pictures;
    }

    // $10D3B4: where the plane is from the player's point of view.
    void find_side(EnemyPlane& o) {
        const int ahead = word(o.x - p.x);
        if (p.facing == o.facing) {
            o.side = 1;
            if (word(ahead ^ p.facing) >= 0) {
                o.side = 3;
                if (o.attack_timer == 0) o.attack_timer = 0x226;
            }
        } else {
            o.side = ahead < 0 ? 4 : 2;
        }
        o.distance = std::abs(ahead);
    }

    // $10D476: the planes behind the player numbered by distance.
    void find_order() {
        for (int i = 0; i < 4; ++i) {
            auto& o = e.planes[std::size_t(i)];
            if (o.mode == 0 || o.side != 1) continue;
            int place = 1;
            for (int j = 0; j < i; ++j) {
                auto& other = e.planes[std::size_t(j)];
                if (other.side != 1) continue;
                if (other.distance < o.distance) ++place;
                else ++other.order;
            }
            o.order = place;
        }
    }

    // $10D530: a turn round's step, every third call.
    void turn_step(EnemyPlane& o) {
        o.attack_timer = 0;
        if (o.turn_delay-- < 1) {
            o.turn_delay = 2;
            ++o.turn;
        }
    }

    // $10D562: turning round (mode bit 3). A fighter chasing a level player
    // may break off two thirds through (at step $14) and loop back, up to
    // three times.
    void turn_round(EnemyPlane& o) {
        const int player_mode = p.mode;
        if (p.mode == player_flying && (o.mode & 3)) o.target_altitude = p.altitude;
        const bool keep_on = p.turn != 0 || player_mode == player_on_deck || p.mode != player_flying ||
                             o.turn < 0x13 || (o.side != 1 && o.side != 3) || (o.mode & 3) == 0;
        if (!keep_on) {
            o.turn = 0;
            o.mode &= ~turning;
            return;
        }
        turn_step(o);
        if (o.turn >= 0x1A) {
            o.turn = 0;
            o.mode &= ~turning;
            return;
        }
        if (o.turn == 0xE) {
            o.facing = -o.facing;
            ++o.turn;
            return;
        }
        if (o.turn != 0x14 || (o.mode & 3) == 0 || p.mode != player_flying) return;
        if (o.loops >= 3) {
            o.loops = 0;
            o.attack_timer = 0x226;
            return;
        }
        const bool player_turning = p.turn > 0 && p.turn < 6;
        bool loop = false;
        switch (o.side) {
        case 1:
            if (player_turning && o.attack_timer == 0) o.turn_wait = word(word(o.distance * 100) / o.speed);
            loop = true;
            break;
        case 2: loop = player_turning; break;
        case 3: loop = true; break;
        case 4:
            if (o.attack_timer == 0) o.turn_wait = word(word(o.distance * 100) / word(p.speed + o.speed));
            loop = true;
            break;
        default: break;
        }
        if (loop && player_mode != player_on_deck && o.attack_timer == 0) {
            const int back = word(o.turn - 0xD);
            o.turn = word(o.turn - back * 2);
            ++o.loops;
        }
    }

    // $10D796: along at its speed (the player's turn table, FFP as the game
    // calls it), toward its target height, and the turn-round timer.
    void move(EnemyPlane& o) {
        if ((o.state & (falling | burning)) == 0 && (p.mode == 4 || p.mode == 8 || p.mode == 6)) {
            o.target_speed = 0x6A4; // (the player is down: it flies off fast)
        }
        const int along = word(word(o.speed / 100) * o.facing);
        o.x = word(o.x + word(ffp::to_int(ffp::multiply(t.turn_speed[std::size_t(o.turn)], ffp::from_int(along)))));
        if ((o.state & (falling | burning)) == 0 && !(o.mode & torpedo_run)) {
            if (p.mode == player_on_deck) o.target_altitude = 0x46;
            if (o.target_altitude < 0x21) o.target_altitude = 0x21;
        }
        const int gap = std::min(std::abs(word(o.target_altitude - o.altitude)), 100) / 20;
        if (o.state & falling) {
            if (o.altitude > o.target_altitude) {
                o.altitude = word(o.altitude + o.fall_speed / 100);
                o.fall_speed = word(o.fall_speed - 10);
            } else if (o.altitude < o.target_altitude) {
                o.altitude = o.target_altitude;
            }
        }
        if (o.altitude > o.target_altitude) {
            o.altitude = word(o.altitude - (et.climb_step[std::size_t(gap)] + rand(2)));
        } else if (o.altitude < o.target_altitude) {
            o.altitude = word(o.altitude + et.climb_step[std::size_t(gap)] + rand(2));
        }
        if ((o.state & (falling | burning)) == 0 && o.turn_wait != 0 && --o.turn_wait < 1) {
            o.mode |= turning;
            o.turn_wait = 0;
        }
        if ((o.mode & turning) && o.state == flying) turn_round(o);
    }

    // $10D9C6: a fighter closing in on the player.
    void close_in(EnemyPlane& o) {
        const bool player_turning = p.turn > 0 && p.turn < 6;
        switch (o.side) {
        case 1:
            if (o.distance < 0xA0) {
                if (!player_turning || p.mode == player_on_deck || o.turn_wait != 0) {
                    if (o.distance < 0xA0 && o.turn_wait == 0 && o.order == 1) {
                        o.mode = (o.mode & turning) | attacking;
                    } else {
                        o.firing = false;
                        o.target_altitude = p.altitude;
                        o.target_speed = word(p.speed - o.order * 0x46);
                    }
                } else {
                    o.turn_wait = word(o.order * 2 + word(word(o.distance * 100) / o.speed));
                }
            } else if (o.turn == 0) {
                o.target_speed = word(p.speed + o.distance);
                o.target_speed = word(o.order == 1 ? o.target_speed + o.order * 0x50 : o.target_speed - o.order * 0x50);
            }
            break;
        case 2:
            if (p.mode == player_flying && p.turn > 0 && p.turn < 0xB) o.target_speed = et.min_speed;
            else o.mode |= turning;
            break;
        case 3:
            if (o.distance >= 0x600) {
                o.mode |= turning;
                break;
            }
            if (--o.attack_timer < 1 || player_turning || p.mode == player_on_deck) {
                o.mode |= turning;
                o.attack_timer = 0;
            } else if (o.hit == 1) {
                dodge(o, 0x226);
            } else {
                o.target_speed = word(p.speed - o.distance / 4);
                o.target_speed = word(o.target_speed + e.attacker_step++ * 0x46);
                if (o.altitude == o.target_altitude) o.target_altitude = rand(7) * 10 + 0x23;
            }
            break;
        case 4:
            if (p.mode == player_flying) {
                o.target_speed = et.min_speed;
                o.target_altitude = o.altitude < p.altitude ? word(p.altitude - 0x20) : word(p.altitude + 0x20);
                o.turn_wait = word(word(o.distance * 100) / word(p.speed_x + o.speed));
                o.mode |= turning;
            }
            break;
        default: break;
        }
    }

    // Hit by the guns while ahead of the player: it turns round soon.
    void dodge(EnemyPlane& o, int attack_time) {
        o.hit = 0;
        if (o.turn_wait == 0) o.turn_wait = rand(6) + 8;
        o.attack_timer = attack_time;
    }

    // $10DCCC: a fighter on the player's tail, its guns firing when level
    // with the player close behind. A hit on the player every few ticks:
    // oil -8 and some fuel.
    void attack(EnemyPlane& o) {
        if (o.side != 1) {
            o.mode = closing_in;
            return;
        }
        if (p.turn == 0 && p.mode != player_on_deck) {
            if (o.distance >= 0x83) o.target_speed = word(p.speed + 0x32);
            else if (o.distance < 0x82) o.target_speed = word(p.speed - 0x32);
        } else {
            o.mode = closing_in | turning;
            o.turn_wait = word(o.distance / word(o.speed / 100));
        }
        o.target_altitude = p.altitude;
        const int dy = std::abs(word(p.altitude - o.altitude));
        const bool level = p.mode == player_flying && o.mode == attacking;
        if (level && o.order == 1 && dy < 8 && o.turn == 0 && o.distance < 0xA0 && p.facing == o.facing &&
            word(word(o.x - p.x) ^ p.facing) < 0) {
            o.firing = true;
            if (!e.invulnerable && o.altitude == p.altitude && w.smoke_tick && --p.spare < 1) {
                p.oil = word(p.oil - 8);
                p.fuel = word(p.fuel - rand(0x20));
                p.spare = rand(5) + 6;
            }
        } else {
            o.firing = false;
        }
    }

    // $10DEA4: a torpedo bomber heads for the carrier, comes down to $14
    // within 1000 of the deck and drops its torpedo (into the slot -$5A6A)
    // while still short of it; then it climbs away (mode $10).
    void torpedo(EnemyPlane& o) {
        if (o.side == 3) {
            if (o.hit == 1) dodge(o, 0x113);
            if (o.distance > 0x96) o.distance = 0xF0;
            if (o.distance < 0x96) {
                o.distance = word(o.distance - 0x96);
                if (o.altitude == o.target_altitude) o.target_altitude = rand(7) * 10 + 0x23;
            }
            o.target_speed = word(p.speed - o.distance);
        } else if (o.facing == -1 && o.x < word(p.deck_low - 500)) {
            o.mode |= turning;
        } else if (o.facing == 1 && word(p.deck_high + 500) < o.x) {
            o.mode |= turning;
        }
        if (o.facing == -1 && p.deck_high < o.x && word(o.x - p.deck_high) < 1000) o.target_altitude = 0x14;
        else if (o.facing == 1 && o.x < p.deck_low && word(p.deck_low - o.x) < 1000) o.target_altitude = 0x14;
        const bool short_of_deck = (o.facing == -1 && p.deck_high < o.x) || (o.facing == 1 && o.x < p.deck_low);
        if (!short_of_deck) {
            if (--o.attack_timer < 1) {
                o.mode |= turning;
                o.attack_timer = 0;
            }
            return;
        }
        if (o.altitude != 0x14 || o.turn != 0) return;
        auto& torpedo = w.effects.enemy_torpedo;
        torpedo.weapon = 2;
        torpedo.dy = 0;
        torpedo.dx = std::int32_t(word(o.speed)) * 0x28F;
        torpedo.y = std::int32_t(std::uint32_t(std::uint16_t(o.altitude + 0xF)) << 16);
        torpedo.x = std::int32_t(std::uint32_t(std::uint16_t(o.x)) << 16);
        torpedo.surface = std::int8_t(o.facing);
        if (torpedo.surface == -1) torpedo.dx = -torpedo.dx;
        torpedo.kind = FlyingObject::Kind::torpedo;
        torpedo.picture = 0;
        o.state = flying;
        o.target_speed = et.max_speed / 2;
        o.target_altitude = 0x3C;
        o.mode = leaving;
        p.enemy_timer = 500;
    }

    // $10E17A: leaving; gone once far enough.
    void leave(EnemyPlane& o) {
        if (gone(o) || o.turn != 0) return;
        if (o.side == 3) {
            if (o.hit == 1) dodge(o, 0x226);
            else if (o.altitude == o.target_altitude && o.distance < 0xA0) o.target_altitude = rand(7) * 10 + 0x19;
        } else if (o.side == 1) {
            o.mode |= turning;
        }
    }

    // $10E728: a flying plane (smoking once damaged).
    void fly(EnemyPlane& o) {
        damage_smoke(o, o.x, word(o.altitude + 0xB));
        switch (o.mode & ~turning) {
        case closing_in: close_in(o); break;
        case attacking: attack(o); break;
        case torpedo_run: torpedo(o); break;
        case leaving: leave(o); break;
        default: break;
        }
    }

    // $10E244: shot down. Once down at its target height (-3) it skids
    // along, slowing, splashing and blasting what it slides over; in the
    // water it sinks, on an island it burns.
    void fall(EnemyPlane& o) {
        damage_smoke(o, word(o.x - 0x10), word(o.altitude + 0xA));
        if (o.altitude != o.target_altitude) return;
        const int c = column(o.x);
        if (over_water(c) && o.speed < 300) {
            if (++e.skid_counter >= 2) {
                e.skid_counter = 0;
                o.target_altitude = word(o.target_altitude - 1);
            }
            o.speed = word(o.speed + 0x18);
        }
        o.speed = word(o.speed - 0x23);
        set_off_near(p, w, t, o.x, 0x14);   // $101A84
        if (o.speed > 500) island_hit(p, w, t, word(c * 8), 0); // $1046C6
        const int splash_x = word(o.x + o.facing * 4);
        w.effects.start_splash(splash_x, surface_at(splash_x)); // $1052AC
        if (o.speed >= 100) return;
        ++w.kills;
        if (over_island(c)) {
            o.state = burning;
            o.target_speed = 6;
            o.speed = 0x1E;
            o.mode = 0;
        } else {
            if (o.mode != torpedo_run && o.mode != leaving) --e.fighters_out;
            o.state = o.mode = 0;
        }
    }

    // $10E3E8: burning on an island, smoking less and less; then a wreck.
    void burn(EnemyPlane& o) {
        o.picture = o.facing == -1 ? wreck_left : wreck_right;
        if (--o.speed == 0) {
            --o.target_speed;
            o.speed = word(o.target_speed * 5);
        }
        if (o.target_speed < 2) {
            if (e.wrecks.size() < 40) e.wrecks.push_back(word(o.facing * o.x));
            if (o.mode != torpedo_run && o.mode != leaving) --e.fighters_out;
            o.state = o.mode = 0;
        } else if ((o.speed & 3) == 0) {
            smoke(word(o.x - 4), 6, o.target_speed);
        }
    }

    // $10E64E: a flying plane's speed eases toward its target.
    void ease_speed(EnemyPlane& o) {
        if (o.state != flying) return;
        o.target_speed = std::clamp(o.target_speed, et.min_speed, et.max_speed);
        if (o.speed < o.target_speed) {
            o.speed = std::min(word(o.speed + word(o.target_speed - o.speed) / 2 + 5), et.max_speed);
        } else if (o.target_speed < o.speed) {
            o.speed = std::max(word(o.speed - (word(o.speed - o.target_speed) / 2 - 5)), et.min_speed);
        }
    }
};

} // namespace

EnemyTables read_enemy_tables(const amiga::LoadedImage& wings) {
    EnemyTables t;
    t.min_speed = wings.s16(0x115EA6);
    t.max_speed = wings.s16(0x115EA8);
    for (std::size_t i = 0; i < t.climb_step.size(); ++i) t.climb_step[i] = wings.s16(0x11614C + std::uint32_t(i * 2));
    const auto name = [&](std::uint32_t at) {
        std::string s;
        for (std::uint32_t c = 0; c < 4; ++c) s += char(wings.u8(at + c));
        return s;
    };
    for (std::uint32_t i = 0; i < 28; ++i) {
        t.names[i] = name(0x115EAC + i * 4);
        t.names[i + 28] = name(0x115F1C + i * 4);
        t.eighth_names[i] = name(0x115F8C + i * 4);
        t.eighth_names[i + 28] = name(0x115FFC + i * 4);
    }
    for (std::uint32_t i = 0; i < 4; ++i) t.flash_names[i] = name(0x114520 + (0x10 + i) * 4);
    return t;
}

void enemy_timer(PlayerFlight& p, FlightWorld& w, const FlightTables& t, int command) {
    EnemyWork(p, w, t).timer(command);
}
void enemies_tick(PlayerFlight& p, FlightWorld& w, const FlightTables& t) {
    EnemyWork(p, w, t).tick();
}
void guns_at_planes(PlayerFlight& p, FlightWorld& w, const FlightTables& t) {
    EnemyWork(p, w, t).guns();
}
void launch_enemy(PlayerFlight& p, FlightWorld& w, const FlightTables& t, bool formation, int x, int altitude, int facing) {
    EnemyWork(p, w, t).launch(formation, x, altitude, facing);
}

namespace {
const data::Shape* find_shape(const data::Assets& assets, std::string_view library, const std::string& name) {
    for (const auto& l : assets.libraries) {
        if (l.name == library) return l.library.find(name);
    }
    return nullptr;
}
} // namespace

std::vector<EnemySprite> enemy_sprites(Enemies& e, const EnemyTables& t, const data::Assets& assets, const EnemyView& view) {
    std::vector<EnemySprite> out;
    const int shift = view.eighth_scale ? 3 : 0;
    const int origin = word(view.camera + word(0xFF60 << shift));
    const auto picture = [&](int entry) {
        const auto& names = view.eighth_scale ? t.eighth_names : t.names;
        if (entry < 0 || std::size_t(entry) >= names.size()) return static_cast<const data::Shape*>(nullptr);
        return find_shape(assets, view.eighth_scale ? "8thscale.shp" : "japplane.shp", names[std::size_t(entry)]);
    };
    // The wrecks on the islands, then the planes ($100DA6).
    for (const int wreck : e.wrecks) {
        const int sx = word(std::abs(wreck) - origin) >> shift;
        const int sy = word(view.ground_line - 4) >> shift;
        if (sx < -0x80 || sx > 0x1C0) continue;
        const auto* shape = picture(wreck < 0 ? wreck_left : wreck_right);
        if (shape != nullptr) out.push_back({shape, sx - shape->offset_x, sy - shape->offset_y, false});
    }
    for (auto& o : e.planes) {
        if (o.state == 0) continue;
        const int sx = word(o.x - origin) >> shift;
        const int sy = word(view.ground_line - o.altitude - 7) >> shift;
        if (sx < -0x80 || sx > 0x1C0) continue;
        const auto* shape = picture(o.picture);
        if (shape == nullptr) continue;
        out.push_back({shape, sx - shape->offset_x, sy - shape->offset_y, false});
        // The gun flash, XORed on every second frame while it fires.
        if (!o.firing || view.eighth_scale) continue;
        ++o.flash;
        if ((o.flash & 1) == 0) continue;
        const int entry = ((o.flash & 3) >> 1) + (o.facing < 0 ? 2 : 0);
        const auto* flash = find_shape(assets, "japplane.shp", t.flash_names[std::size_t(entry)]);
        if (flash != nullptr) out.push_back({flash, sx - flash->offset_x, sy - flash->offset_y, true});
    }
    return out;
}

void draw_enemies(IndexedCanvas& canvas, const std::vector<EnemySprite>& sprites) {
    for (const auto& s : sprites) {
        if (s.xor_drawn) canvas.draw_xor(*s.shape, s.x, s.y);
        else canvas.draw(*s.shape, s.x, s.y);
    }
}

} // namespace wings::game
