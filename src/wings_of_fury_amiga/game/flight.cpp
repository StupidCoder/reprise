#include "wings_of_fury_amiga/game/flight.hpp"
#include <algorithm>

namespace wings::game {
namespace {

using ffp::Ffp;

// Mode numbers (the player record's +$C).
constexpr int flying = 0;
constexpr int on_deck = 1;
constexpr int falling = 4;
constexpr int sinking = 6;
constexpr int caught = 7;
constexpr int burning = 8;
constexpr int going_below = 0xB;

constexpr int max_speed = 0x578;
constexpr int lift_off_speed = 600;
constexpr int stall_speed = 1000;
constexpr int climb_floor = -4500;
constexpr int sky_top = 0x44C;
constexpr Ffp fifty = 0xC8000046;   // 50.0
constexpr Ffp hundred = 0xC8000047; // 100.0
constexpr int carrier_reach = 1280; // $115E56: the wheels come down this close to the carrier

// The compiled code's word arithmetic.
int word(int v) {
    return std::int16_t(v);
}
int div16(int a, int b) { // C division (toward zero) of a word
    return word(word(a) / b);
}

class Tick {
public:
    Tick(PlayerFlight& p, FlightWorld& w, const FlightTables& t, int command) : p(p), w(w), t(t), cmd(command) {}

    void run() {
        timers();
        // $101402: the menu and the elevator wait out -$42C0 first.
        if (w.input_delay == 0 || --w.input_delay <= 0) {
            deck_menu();
            elevator();
        }
        fly();
        enemies_tick(p, w, t);        // $10E7D6
        sound_state();
        guns_at_planes(p, w, t);      // $10B682
        sound_driver();
        snapshot();
        damaged_plane_smoke(p, w, t); // $101BFC
        fly_objects();
        strafe();
        airfields_tick(p, w, t);      // $10143E, $101622
        // ($101510, the enemy carrier's fighters: not rebuilt yet.)
        ships_sinking(p, w, t);       // $101CAE
        soldiers_come_out(p, w, t);   // $101DE4
    }

private:
    using Kind = FlyingObject::Kind;
    PlayerFlight& p;
    FlightWorld& w;
    const FlightTables& t;
    int cmd;

    ShipState& carrier() { return w.ships[4]; }
    int map_columns() const { return int(w.map->words.size()); }

    // $101386 before the command is used: oil and fuel run down only while
    // flying (oil only after damage, when it is no longer $80); the sea's
    // motion steps through its table.
    void timers() {
        w.smoke_tick = !w.smoke_tick; // (-$3CB8, each tick)
        if (p.mode == flying) {
            if (p.oil != 0x80 && --p.oil_counter <= 0) {
                p.oil_counter = 0x50;
                --p.oil;
            }
            if (--p.fuel_counter <= 0) {
                --p.fuel;
                p.fuel_counter = p.fuel_period;
            }
        }
        if (--p.sea_counter < 0) {
            p.sea_counter = p.sea_period;
            p.sea_step = (p.sea_step + 1) & 7;
            p.sea = t.sea_motion[std::size_t(p.sea_step)] + 1;
        }
    }

    // $10C982: the map column under x (below 0 counts as 0). The game's map
    // buffer is the file's size (-$69D2, $102BB0), 8 bytes more than the
    // column words after the header, and an address at or past its end - 2
    // becomes end - 4: two columns past the data. Those words are whatever
    // the allocation held; read as 0 here (hypothesis).
    int column(int x) const {
        if (x < 0) x = 0;
        return std::min(div16(x, 8), map_columns() + 2);
    }
    std::uint16_t word_at(int c) const { return c < map_columns() ? w.map->words[std::size_t(c)] : 0; }
    bool inside(int c) const { return c > 0 && c < map_columns() - 1; }
    // $10CB34 / $10CB74: on a ship's deck / over open water (the first and
    // last columns count as outside the map: water).
    bool over_ship(int c) const { return inside(c) && (word_at(c) & 3) == 1; }
    bool over_water(int c) const { return !inside(c) || (word_at(c) & 3) == 0; }

    // $1050C8: the surface bits under a world x (0 off the map).
    int surface_at(int x) const {
        if (x < 0 || x >= map_columns() * 8) return 0;
        return word_at(x >> 3) & 3;
    }

    // $105710: the height of what stands in a column.
    int obstacle(int c) const {
        const int index = word_at(c) >> 2 & 0x1FF;
        const int drop = word_at(c) >> 11 & 7;
        const auto scenery = [&](int cls) { return t.obstacle_height[std::size_t(cls)] - drop; };
        const auto ship = [&](int slot, bool with_lift) {
            const auto& s = w.ships[std::size_t(slot)];
            return word(s.deck - s.sunk - p.sea - (with_lift ? w.lift : 0));
        };
        switch (index) {
        case 6: return scenery(0);
        case 7: return scenery(1);
        case 8: case 0xB: return scenery(2);
        case 3: return scenery(3);
        case 4: return scenery(4);
        default: break;
        }
        if (index >= 0xF && index <= 0x1E) return scenery(5);
        if (index == 0xCC) return ship(2, false);
        if (index == 0xF1 || index == 0xF2 || index == 0xF3 || index == 0xF6) return ship(3, false);
        if (index >= 0x10C && index <= 0x110) return ship(1, false);
        if (index >= 0xE4 && index <= 0xE7) return ship(0, false);
        switch (index) {
        case 0x1F: case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x26: case 0x27: case 0x9F:
            return ship(4, true);
        default:
            return 0;
        }
    }

    // $10AAEA: how high the plane's centre rides above what it stands on.
    int clearance() const {
        if (p.turn == 0 || p.mode == on_deck || p.mode == going_below) {
            int h = t.wheel_height[std::size_t(p.attitude)];
            if (p.gear != 0) h += t.gear_height[std::size_t(p.attitude)];
            return h;
        }
        if (p.turn < 6) return t.turn_height[std::size_t(p.turn)];
        if (p.turn > 0x13) return t.turn_height[std::size_t(0x19 - p.turn)];
        return 0xB;
    }
    int ground_level() { return word(obstacle(column(p.x)) + clearance()); }

    // $10AB80: one step of turning round, every second call; at step $E the
    // plane faces the other way. `pushing_back`: a step past $13 runs the
    // turn backwards unless the stick still points back.
    void turn_step(bool pushing_back) {
        // ($10AA6E would hold the turn while an enemy formation is lined up;
        // there are no enemies yet.)
        if (--p.turn_delay != 0) return;
        p.turn_delay = 2;
        int next = p.turn + 1;
        if (next < 0x1A) {
            if (next > 0x13 && !pushing_back) next += (p.turn - 0xC) * -2;
            p.turn = next;
            if (p.turn == 0xE) p.facing = -p.facing;
        } else {
            p.turn = 0;
        }
    }
    void unwind_turn(bool pushing_back) {
        if (p.turn == 0) return;
        if (p.turn < 7) --p.turn;
        else turn_step(pushing_back);
    }

    // $10B4DE: is the plane (on deck) inside the elevator's reach? Sets the
    // officer's signal when it is past either end.
    bool on_elevator_spot() {
        if (p.mode != on_deck) return false;
        const int front = t.deck_front[std::size_t(p.turn)], back = t.deck_back[std::size_t(p.turn)];
        const int low = p.facing < 0 ? p.x - front : p.x - back;
        const int high = p.facing < 0 ? p.x + back : p.x + front;
        if (low < word(w.carrier_x - 0x17)) {
            p.signal = 1;
            return false;
        }
        if (word(w.carrier_x + 0x21) < high) {
            p.signal = 0;
            return false;
        }
        return true;
    }

    // $1012B0: the weapon menu while the plane waits below deck. Fire sends
    // it up (mode $B, elevator rising); up and down step through the weapons
    // (again after two ticks).
    void deck_menu() {
        if (!w.menu) return;
        // (keys: cursor up/down and Return/Enter do the same when no stick
        // command is given; not rebuilt)
        if (cmd == 0) return;
        if (cmd & (command::fire_held | command::fire_tapped)) {
            w.menu = false;
            p.mode = going_below;
            w.elevator = 2;
            return;
        }
        if (w.menu_repeat > 0) {
            --w.menu_repeat;
            return;
        }
        if ((cmd & (command::up | command::down)) == 0) return;
        w.menu_repeat = 2;
        if (cmd & command::up) {
            if (--w.weapon == -1) w.weapon = 2;
        } else if (++w.weapon == 3) {
            w.weapon = 0;
        }
        if (w.weapon_load != 0xFF) w.weapon_load = t.weapon_loads[std::size_t(w.weapon)];
        w.gauges.set_load(w.weapon_load); // ($10136E: the digits jump to it)
    }

    // $101460: the elevator moves a pixel per drawn frame; at the top it is
    // up (state 0), at the bottom a new plane is made ready (state 1).
    void elevator() {
        if (w.draw_counter == w.elevator_seen) return;
        w.elevator_seen = w.draw_counter;
        if (w.elevator == 2) {
            if (--w.lift == 0) {
                w.elevator = 0;
                silence();
                clang();
                // $10B9CC: the engine ticks over.
                w.sound.engine_level = w.sound.engine_target = 0x28;
                w.sound.engine_period = w.sound.engine_base = 0x328;
                p.engine_loud = false;
            }
        } else if (w.elevator == 3) {
            if (++w.lift == 0x20) {
                silence();
                clang();
                new_plane(w.random_load);
            }
        }
    }

    // $1035CE: the plane is lost: the view goes black for 20 waits, a spare
    // is used, and ($1035D8) the next one comes up - unless none is left or
    // the carrier is lost.
    void plane_lost() {
        w.blackout = 0x14;
        --w.spare_planes;
        p.x = w.carrier_x;
        p.turn = 0;
        p.facing = -1;
        if (w.spare_planes <= 0 || carrier().ready <= 0) {
            w.game_over = true;
            w.the_end = true; // ($103612)
            w.spare_planes = 0;
            return;
        }
        new_plane(w.random_load);
    }

    // $10AED8: for its first 75 ticks the wreck keeps blowing up (sinking:
    // splashing at the waterline) every 0..12 ticks, up to a column either
    // side.
    void wreck_blasts() {
        w.sound.engine_target = 0; // ($10AF20)
        const bool sinking_now = p.mode == sinking;
        const int height = sinking_now ? 0 : word(p.altitude - clearance());
        if (p.wreck_time >= 0x4B || p.wreck_time < p.next_blast) return;
        p.next_blast = p.wreck_time + (w.random.next() & 0xC);
        blast(column(p.x + (w.random.next() & 0xF) - 8), height, sinking_now);
    }

    // $10AF7C: the wreck waits 150 ticks, or a fire press after 30.
    void wreck_timer() {
        if (w.spare_planes == 1 || w.spare_planes == 0) w.the_end = true; // ($10AF80)
        ++p.wreck_time;
        if (p.wreck_time == 0x96 || (p.wreck_time > 0x1E && (cmd & (command::fire_held | command::fire_tapped)))) {
            plane_lost();
        }
    }

    // $10AFBA: the plane falls. What it meets decides: land and water stop
    // it at wheel height, a ship's deck at the deck (unless it would run
    // off the deck within five columns: then it is pushed back and stops
    // dead). It slides on, $55 slower each tick; a turn in progress unwinds
    // to its nearer end; at rest it sinks (over water, or low on a ship) or
    // burns.
    void crash() {
        enum Surface { land = 1, water = 2, ship = 3 };
        Surface surface = land;
        bool falling_on = false, down = false;
        int resting = clearance();
        const int c = column(p.x);
        if (over_water(c)) surface = water;
        else if (over_ship(c)) surface = ship;
        const auto level_out = [&] {
            p.attitude = 0;
            p.smoothed_climb = 0;
            p.climb = 0;
        };
        // $1052AC at the plane's nose.
        const auto splash = [&] { w.effects.start_splash(word(p.x + p.facing * 8), surface_at(word(p.x + p.facing * 8))); };
        const auto slide = [&] { p.x = word(p.x + word(div16(p.speed, 100) * p.facing)); };
        switch (surface) {
        case water:
            if (p.altitude <= clearance()) {
                level_out();
                p.altitude = clearance();
                down = true;
                splash();
            }
            break;
        case land:
            if (word(p.altitude - clearance()) <= 0 && p.turn == 0) {
                level_out();
                p.altitude = clearance();
                shapes(0); // ($10ABDE with step 0: the attitude only)
                down = true;
                blast(c, p.altitude - 1, false);
            } else {
                falling_on = true;
            }
            break;
        case ship: {
            const int sliding = word(div16(p.speed, 100) * p.facing);
            resting = word(resting + obstacle(c));
            if (p.altitude <= resting) {
                // The column five back (ten map bytes) decides.
                if (over_water(c - p.facing * 5)) {
                    p.x = word(p.x - word(sliding + 4));
                    p.speed = 0;
                    resting = 2;
                    w.islands.flash_frames = 7; // ($10CAB4: the screen flashes red)
                    w.islands.flash_colour = 0xF00;
                    blast(c, p.altitude, false);
                } else {
                    down = true;
                    level_out();
                    p.altitude = word(clearance() + obstacle(c));
                }
            }
            break;
        }
        }
        if (p.turn < 0xE) {
            p.turn = std::max(p.turn - 2, 0);
        } else {
            if (p.turn == 0xE) p.facing = -p.facing;
            p.turn += 2;
            if (p.turn > 0x19) p.turn = 0;
        }
        if (p.turn != 0 && p.altitude <= resting) {
            int h = 0xB;
            if (p.turn < 6) h = t.turn_height[std::size_t(p.turn)];
            else if (p.turn > 0x13) h = t.turn_height[std::size_t(0x19 - p.turn)];
            if (over_ship(c) || over_water(c)) p.altitude = word(obstacle(c) + h);
            slide();
        } else if (down) {
            slide();
            p.speed -= 0x55;
            if (p.speed < 100) p.speed = 0;
            if (surface == water) {
                splash();
            } else {
                // $10B304: the blast here gets the plane's x and altitude
                // words where the column's map address belongs, so it lands
                // where (altitude - the map's address) x 4 points: with the
                // map at $xxxxFE04, as in the reference machine, (altitude +
                // $1FC) x 4 - x 2040 at rest, far from the crash. Its sound
                // starts the explosion again, quietly. On land the plane then
                // wrecks what stands in its column ($1046C6, as a rocket would)
                // and the soldiers within 8 ($101A84).
                constexpr int map_address_low = 0xFE04;
                const int x = std::int16_t(std::uint16_t((p.altitude - map_address_low) << 2));
                if (w.effects.start_blast_at(x, 0, false)) boom(x);
                if (surface == land) {
                    island_hit(p, w, t, c * 8, 0);
                    set_off_near(p.x, 8);
                }
            }
        } else {
            p.climb = std::max(p.climb - t.pitch_rate, -0xC1C);
            falling_on = true;
        }
        if (falling_on) {
            p.speed_y = std::max(p.speed_y - 1, -10);
            p.altitude = word(p.altitude + p.speed_y);
            if (word(p.altitude - clearance()) <= 0) {
                p.altitude = clearance();
                blast(c, p.altitude - 1, false);
            }
            slide();
        }
        if (p.speed == 0 && p.turn == 0 && p.altitude <= resting) {
            p.mode = (over_water(c) || (over_ship(c) && p.altitude < 0x14)) ? sinking : burning;
            p.oil = 0;
            p.extra = 0;
            p.sink_step = 0;
            p.wreck_time = 0;
            p.next_blast = 0;
        }
    }

    // $10B5B0: the fire button. On deck, a press while standing still on the
    // elevator spot takes the plane below (elevator state 3). In the air, a
    // tap drops or launches the load ($10107C) and a long press fires the
    // guns (their muzzle flash and the hits on the ground: strafe()).
    void fire() {
        if ((cmd & (command::fire_held | command::fire_tapped)) == 0) {
            w.guns = false;
            return;
        }
        if (p.mode == flying) {
            if (cmd & command::fire_tapped) {
                // Not while upside down in the middle of a turn.
                if (p.turn < 6 || p.turn > 0x10) release_weapon();
            } else {
                w.guns = p.turn == 0 && w.gun_supply > 0;
            }
            return;
        }
        if (p.mode == on_deck && w.elevator == 0 && carrier().ready != 0 && on_elevator_spot() && p.speed == 0) {
            w.sound.engine_target = 0; // ($10B646)
            p.signal = 3;
            w.elevator = 3;
            p.mode = going_below;
            p.altitude = ground_level();
        }
    }

    // $100820: a blast in a column; one on land also sounds ($102324).
    void blast(int column, int height, bool water) {
        if (w.effects.start_blast(column, height, water) && !water) boom(column * 8);
    }

    // ---- the sound effects ----

    SoundBoard::Channel& channel(int n) { return w.sound.channels[std::size_t(n)]; }

    // $102306 / $1022F6: louder the nearer to the plane (64 down to 0).
    int volume_at(int x) const {
        const int distance = word(std::abs(word(x - p.x)) + std::abs(word(0x14 - p.altitude)));
        const int v = std::uint16_t(distance) >> 5;
        return v > 0x40 ? 0 : 0x40 - v;
    }
    // $102324: the explosion on channel 2.
    void boom(int x) {
        auto& c = channel(2);
        c.request[0].active = true;
        c.request[0].volume = volume_at(x);
        c.playing = Sample::none;
    }
    // $10233E: the splash on channel 2.
    void splash_sound(int x) {
        auto& c = channel(2);
        c.request[0].active = false;
        c.request[1].active = true;
        c.request[1].volume = volume_at(x);
        c.playing = Sample::none;
    }
    // $102354 (the elevator stopping) and $102380 (wheels on the deck): a
    // one-shot on channel 3, at lengths given in the code.
    void deck_sound(Sample sample, int length, int period) {
        auto& c = channel(3);
        c.request[1] = {true, sample, length, period, 0x40, 1};
        c.request[0].active = false;
        c.playing = Sample::none;
    }
    void clang() { deck_sound(Sample::clang, 0x1646, 0x1C2); }
    // $101F4E: every request off, and the channels stopped at once.
    void silence() {
        for (auto& c : w.sound.channels) {
            for (auto& r : c.request) r.active = false;
        }
        sound_driver();
    }
    void screech() { deck_sound(Sample::screech, 0x1A5A, 0x15E); }
    // The engine's target loudness and pitch (the flight code sets them
    // with the engine flag: $40/$14F flat out, $31/$181 cruising).
    void engine_sound(bool loud) {
        w.sound.engine_target = loud ? 0x40 : 0x31;
        w.sound.engine_base = loud ? 0x14F : 0x181;
    }
    // On deck and on the elevator the engine ticks over.
    void engine_idle() {
        w.sound.engine_target = 0x28;
        w.sound.engine_base = 0x328;
    }

    // $102132: the requests the game state keeps up each tick.
    void sound_state() {
        auto& s = w.sound;
        auto& guns_or_engine = channel(0);
        guns_or_engine.request[1].active = false;
        // The engine eases up by 1, down by 2. The comparisons are unsigned:
        // from 1 toward 0 the level steps past 0 to -1 and on down (the
        // channel then keeps its last volume: negative ones are not set).
        const auto above = [](int a, int b) { return std::uint16_t(a) > std::uint16_t(b); };
        if (s.engine_target != s.engine_level) {
            if (above(s.engine_target, s.engine_level)) {
                s.engine_level = word(s.engine_level + 1);
            } else {
                const int down = word(s.engine_level - 2);
                s.engine_level = above(s.engine_target, down) ? s.engine_target : down;
            }
        }
        guns_or_engine.request[1].volume = s.engine_level;
        if (s.engine_level != 0) {
            guns_or_engine.request[1].active = true;
            const int pitch = word((p.climb >> 7) + s.engine_base + (p.altitude >> 4));
            if (pitch != s.engine_period) {
                if (above(pitch, s.engine_period)) {
                    const int up = word(s.engine_period + 0x14);
                    s.engine_period = above(pitch, up) ? up : pitch;
                } else {
                    const int down = word(s.engine_period - 0xA);
                    s.engine_period = above(pitch, down) ? pitch : down;
                }
            }
            guns_or_engine.request[1].period = s.engine_period;
        }
        guns_or_engine.request[0].active = w.guns;
        // Channel 1: the enemy planes' guns (any firing) and the engine of
        // the nearest one flying, louder the nearer ($1022CE).
        int nearest = 0xFFFF;
        bool enemy_guns = false;
        for (const auto& o : w.enemies.planes) {
            if (o.state == 0 || o.state > 2) continue;
            const int d = std::uint16_t(std::abs(word(o.x - p.x)) + std::abs(word(o.altitude - p.altitude)));
            nearest = std::min(nearest, d);
            enemy_guns = enemy_guns || o.firing;
        }
        auto& enemy = channel(1);
        enemy.request[1].active = false;
        const int v = nearest >> 4;
        const int loudness = v > 0x2C ? 0 : 0x40 - (v <= 5 ? v * 4 : v + 0x14);
        if (loudness != 0) {
            enemy.request[1].volume = loudness;
            enemy.request[1].active = true;
        }
        enemy.request[0].active = enemy_guns;
        auto& blasts = channel(2);
        auto& deck = channel(3);
        if (w.elevator == 2 || w.elevator == 3) {
            // The elevator's machinery grinds.
            deck.request[1] = {true, Sample::grind, w.sound.lengths[std::size_t(Sample::grind)], 0x1C2, 0x40, 0xFFFF};
            deck.request[0].active = false;
            blasts.request[1].active = false;
        }
        if (w.elevator == 1) {
            // Below deck: the sea, slow and quiet, on and on.
            blasts.request[1].period = 0x320;
            blasts.request[1].volume = 0x22;
            blasts.request[1].repeat = 0xFFFF;
            blasts.request[1].active = true;
        } else {
            blasts.request[1].period = 0x15E;
            blasts.request[1].repeat = 1;
        }
        // Channel 3's first request: the anti-aircraft guns the last frame
        // showed firing; they take over from the deck's sounds.
        deck.request[0].active = w.islands.gun_sound;
        if (w.islands.gun_sound) deck.request[1].active = false;
        deck.request[0].volume = w.islands.gun_volume;
    }

    // $102066: each channel plays its first active request, else its second.
    void sound_driver() {
        auto& s = w.sound;
        for (int n = 0; n < 4; ++n) {
            auto& c = channel(n);
            const SoundRequest* chosen = nullptr;
            for (const auto& r : c.request) {
                if (r.sample != Sample::none && r.active) {
                    chosen = &r;
                    break;
                }
            }
            if (chosen == nullptr) {
                if (c.playing != Sample::none) s.commands.push_back({SoundBoard::Command::Kind::stop, n, {}});
                c.playing = Sample::none;
            } else if (chosen->sample == c.playing) {
                if (chosen->period != c.playing_period || chosen->volume != c.playing_volume) {
                    c.playing_period = chosen->period;
                    c.playing_volume = chosen->volume;
                    s.commands.push_back({SoundBoard::Command::Kind::retune, n, *chosen});
                }
            } else {
                c.playing = chosen->sample;
                c.playing_period = chosen->period;
                c.playing_volume = chosen->volume;
                s.commands.push_back({SoundBoard::Command::Kind::start, n, *chosen});
            }
        }
    }

    // ---- the weapons ----

    // $105108 / $105104: sine and cosine of a 1024-step angle (x 32767).
    int sine(int a) const {
        a &= 0x3FF;
        if (a < 0x100) return t.sine_quarter[std::size_t(a)];
        if (a < 0x200) return t.sine_quarter[std::size_t(0x200 - a)];
        a -= 0x200;
        if (a >= 0x100) a = 0x200 - a;
        return -t.sine_quarter[std::size_t(a)];
    }
    int cosine(int a) const { return sine(a + 0x100); }

    // $105CA6: the angle of (x, y), by octant from the arctangent table.
    // (The division's low word comes from the caller's register: taken as 0.)
    int arctangent(int y, int x) const {
        int octant = 0;
        if (y < 0) {
            octant |= 0x10;
            y = -y;
        }
        if (x < 0) {
            octant |= 8;
            x = -x;
        }
        int a = 0;
        if (y == x) {
            if (y == 0) return 0;
            a = 0x80;
        } else {
            int small = y, big = x;
            if (y > x) {
                std::swap(small, big);
                octant |= 4;
            }
            const std::uint32_t q = (std::uint32_t(std::uint16_t(small)) << 16) / std::uint16_t(big);
            const int index = int((q & 0xFFFF) >> 8) + int(q >> 7 & 1); // rounded
            a = t.arctangent[std::size_t(index)];
        }
        switch (octant) {
        case 0x4: return 0x100 - a;
        case 0x8: return 0x200 - a;
        case 0xC: return a + 0x100;
        case 0x10: return -a;
        case 0x14: return a - 0x100;
        case 0x18: return a - 0x200;
        case 0x1C: return -(a + 0x100);
        default: return a;
        }
    }

    // $101274 (end of the tick): what the weapons read of the plane.
    void snapshot() {
        w.last.speed_y = p.speed_y;
        w.last.speed_x = p.speed_x;
        w.last.x = p.x;
        w.last.altitude = p.altitude;
        // (muls gives a long product; divs a word quotient.)
        w.last.pitch = word(word(p.smoothed_climb * 2) * 0x200 / 0x4650);
    }

    // $101A46: where a line from the plane at this (downward) angle meets the
    // ground; 0 when the angle does not point down.
    int ground_point(int angle) const {
        if (angle >= 0) return 0;
        if (angle == -0xFF) angle = -0xFE;
        const std::uint32_t height = std::uint32_t(std::int32_t(w.last.altitude) << 16 >> 8);
        const int slope = t.slope[std::size_t(-angle & 0xFF)];
        // divu: on overflow the dividend's low word stays.
        const std::uint32_t q = height / std::uint16_t(slope);
        const int along = q > 0xFFFF ? std::uint16_t(height) : std::uint16_t(q);
        return word(w.frame_camera - (p.facing < 0 ? along : -along));
    }

    // $1011A6: the first standing gun of an enemy ship still afloat with lo
    // <= x <= hi (world x); 0 when none.
    int ship_gun_between(int a, int b) const {
        const int lo = std::min(a, b), hi = std::max(a, b);
        static constexpr int order[4] = {0, 1, 2, 3}; // (-$5AA4: the US carrier has no value)
        for (const int slot : order) {
            const auto& s = w.ships[std::size_t(slot)];
            if (!s.present || !s.afloat || (s.value == 0 && s.sunk == 0)) continue;
            for (const auto& g : s.guns) {
                if (!g.destroyed && lo <= g.x && g.x <= hi) return g.x;
            }
        }
        return 0;
    }

    // $10115C: the first standing pillbox (list -$5AFA) with lo <= offset <
    // hi (map byte offsets); 0 when none.
    int gun_post_between(int a, int b) const {
        const int lo = std::min(a, b), hi = std::max(a, b);
        for (const auto& g : w.islands.pillboxes) {
            if (!g.destroyed && lo <= g.offset && g.offset < hi) return g.offset;
        }
        return 0;
    }

    // $1007F2 (through $10107C): a tap drops a bomb, launches a rocket or a
    // torpedo from where the plane was at the end of the last tick.
    void release_weapon() {
        if (w.weapon_load == 0) return;
        auto slot = std::find_if(w.effects.objects.begin(), w.effects.objects.end(),
                                 [](const FlyingObject& o) { return !o.active(); });
        if (slot == w.effects.objects.end()) return;
        auto& o = *slot;
        if (w.weapon_load != 0xFF) --w.weapon_load;
        o.weapon = w.weapon;
        o.dy = std::int32_t(std::uint32_t(std::uint16_t(w.last.speed_y)) << 16);
        o.dx = std::int32_t(std::uint32_t(std::uint16_t(w.last.speed_x)) << 16);
        if (p.facing < 0) o.dx = -o.dx;
        o.set_pixel_y(word(w.frame_altitude + 0xB));
        o.x = std::int32_t(std::uint32_t(std::uint16_t(w.last.x)) << 16);
        o.picture = o.dx < 0 ? 3 : 9;
        if (w.weapon == 1) {
            o.kind = Kind::bomb;
        } else if (w.weapon == 2) {
            o.surface = std::int8_t(p.facing); // (-$5F71: the facing word's low byte)
            o.kind = Kind::torpedo;
        } else {
            const int angle = w.last.pitch >> 1;
            o.angle = angle;
            o.speed = p.speed;
            o.thrust_y = sine(angle) * 0x10 >> 3;
            o.thrust_x = cosine(angle) * 0x10 >> 3;
            if (o.dx < 0) o.thrust_x = -o.thrust_x;
            // The motor lights after 4, 8 or 12 ticks (the fourth of four
            // beam-position numbers, rotated left by one: bits 2-3, 0 -> 8).
            int r = 0;
            for (int i = 0; i < 4; ++i) r = w.beam.next();
            const int rotated = (r << 1 | r >> 15) & 0xFFFF;
            o.ignition = (rotated & 0xC) != 0 ? rotated & 0xC : 8;
            o.picture = std::clamp(4 - (w.last.pitch >> 5), 0, 9) + (o.dx < 0 ? 10 : 0);
            o.kind = Kind::rocket;
        }
    }

    // $10099A: when its motor lights, a rocket pointing down turns toward a
    // gun post the plane's line of fire (± $14 angle steps) passes over.
    void aim(FlyingObject& o) {
        const int angle = o.angle >> 1;
        if (angle >= 0) return;
        const int mid = ground_point(angle), steep = ground_point(angle + 0x14), shallow = ground_point(angle - 0x14);
        if (mid == 0 || steep == 0 || shallow == 0) return;
        // The ships' guns first ($1011A6, in world x), then the pillboxes
        // ($10115C, in map offsets).
        int target = ship_gun_between(mid, steep);
        if (target == 0) target = ship_gun_between(mid, shallow);
        if (target == 0) target = gun_post_between(mid >> 2, steep >> 2) * 4;
        if (target == 0) target = gun_post_between(mid >> 2, shallow >> 2) * 4;
        if (target == 0) return;
        int heading = arctangent(o.pixel_y(), std::abs(word(target - o.pixel_x())));
        if (heading >= 0) heading = -heading;
        const int speed = int(std::uint16_t(o.speed) / 100u);
        const std::int32_t old_dx = o.dx;
        o.dx = cosine(heading) * speed;
        if (old_dx < 0) o.dx = -o.dx;
        o.dy = sine(heading) * speed;
    }

    // $101A8C: a blast here kills the soldiers within reach and ($101AE2)
    // sets off the torpedoes near it.
    void set_off_near(int x, int reach) { game::set_off_near(p, w, t, x, reach); }

    // A weapon goes off where it is, wrecking what stands there ($1046DC).
    void explode(FlyingObject& o) {
        island_hit(p, w, t, o.pixel_x(), o.weapon, o.picture == 0xA);
        o.kind = Kind::blast;
        o.frame = 1;
        set_off_near(o.pixel_x(), 0x10);
    }

    // $100D50: a torpedo running in the water, a splash of wake each tick.
    void run_torpedo(FlyingObject& o) {
        if (--o.run <= 0) {
            o.kind = Kind::none;
            o.frame = 0;
            return;
        }
        o.x = std::int32_t(std::uint32_t(o.x) + std::uint32_t(o.dx));
        if (surface_at(o.pixel_x()) != 0) {
            // What it hits ($1046DC); the blast keeps the water's frames.
            island_hit(p, w, t, o.pixel_x(), o.weapon, true);
            o.kind = Kind::blast;
            o.frame = 1;
            boom(o.pixel_x());
            // ($100D92 calls the splash with D0 as $102324 left it: the
            // boom's volume, taken as an x. Far from the plane: silent.)
            splash_sound(volume_at(o.pixel_x()));
            return;
        }
        w.effects.start_splash(o.pixel_x(), 0);
    }

    // $100AA6: one object's tick.
    void fly_object(FlyingObject& o) {
        int next_y = 0;
        if (o.kind == Kind::rocket) {
            bool burning = true;
            if (o.ignition != 0) {
                if (--o.ignition != 0) {
                    // Still dropping: it slips back a pixel and down a pixel.
                    o.set_pixel_x(o.pixel_x() - (p.facing >= 0 ? 1 : -1));
                    o.set_pixel_y(o.pixel_y() - 1);
                    burning = false;
                } else {
                    aim(o);
                }
            }
            if (burning) {
                o.dy = std::int32_t(std::uint32_t(o.dy) + std::uint32_t(o.thrust_y));
                o.dx = std::int32_t(std::uint32_t(o.dx) + std::uint32_t(o.thrust_x));
            }
            o.x = std::int32_t(std::uint32_t(o.x) + std::uint32_t(o.dx));
            // Lost once it is $500 from the camera ($1680 in the 1/8 view).
            const std::int32_t camera = std::int32_t(std::uint32_t(std::uint16_t(w.frame_camera)) << 16);
            std::int32_t distance = std::int32_t(std::uint32_t(camera) - std::uint32_t(o.x));
            if (distance < 0) distance = -distance;
            int beyond = word(int(std::uint32_t(distance) >> 16) - 0x280);
            if (w.frame_eighth) beyond = word(beyond - 0x1180);
            if (beyond > 0x280) {
                o.kind = Kind::none;
                return;
            }
            // (The next height counts the thrust twice.)
            next_y = word(int((std::uint32_t(o.dy) + std::uint32_t(o.thrust_y) + std::uint32_t(o.y)) >> 16));
        } else {
            if (o.kind == Kind::torpedo && o.picture == 0xA) {
                run_torpedo(o);
                return;
            }
            // Drag: a tenth of the horizontal speed's whole part; then gravity.
            const int drag = std::int16_t(o.dx >> 16) / 10;
            o.dx = std::int32_t(std::uint32_t(o.dx) - (std::uint32_t(std::uint16_t(drag)) << 16));
            o.x = std::int32_t(std::uint32_t(o.x) + std::uint32_t(o.dx));
            o.dy = std::int32_t(std::uint32_t(o.dy) - std::uint32_t(w.gravity));
            next_y = word(int((std::uint32_t(o.dy) + std::uint32_t(o.y)) >> 16));
        }
        // ($101126 would first test the barrage balloons' cables: none on the
        // maps rebuilt so far.)
        o.set_pixel_y(next_y);
        const int x = o.pixel_x(), surface = surface_at(x);
        int ground = 0xC;
        if (surface == 1) ground = word(obstacle(int(std::uint16_t(x) >> 3)) + 0xB);
        if (next_y <= ground) {
            o.set_pixel_y(ground);
            o.surface = surface;
            if (surface == 0) splash_sound(x);
            else boom(x);
            o.wake_frame = w.draw_counter;
            if (o.kind == Kind::torpedo) {
                // $100D00: into the water flat enough, it runs.
                if (surface != 0 || word(int(o.dy >> 16)) < -5) {
                    explode(o);
                    return;
                }
                o.dx = std::int16_t(o.dx >> 16) < 0 ? -0x45000 : 0x45000;
                o.picture = 0xA;
                o.run = 0xC8;
                run_torpedo(o);
                return;
            }
            explode(o);
            return;
        }
        // Falling bombs tumble a step every second drawn frame.
        if (o.kind == Kind::bomb && w.frame_drawn && (w.draw_counter & 1) && ++o.picture > 0xB) o.picture = 0;
    }

    // $100A72.
    void fly_objects() {
        for (auto& o : w.effects.objects) {
            if (o.active() && o.kind != Kind::blast) fly_object(o);
        }
        w.frame_drawn = false;
        auto& enemy = w.effects.enemy_torpedo;
        if (enemy.active() && enemy.kind != Kind::blast) fly_object(enemy);
    }

    // $1019BC: the guns firing in a dive below $A0 hit the ground where the
    // line of fire meets it: a splash, or dust on land.
    void strafe() {
        if (!w.guns || w.last.speed_y >= 0 || p.altitude >= 0xA0 || p.pulling_up) return;
        const bool room = std::any_of(w.effects.splashes.begin(), w.effects.splashes.end(),
                                      [](const Splash& s) { return s.left == 0; });
        if (!room) return;
        const int x = ground_point(w.last.pitch);
        set_off_near(x, 0x10);
        w.effects.start_splash(x, surface_at(x));
    }

    // $10C4E8: rolling on deck. Pushing the way the plane faces opens the
    // throttle (the step grows to 8); pushing the other way brakes and, at a
    // standstill, turns the plane round (six steps, then facing flips).
    void deck_throttle() {
        w.guns = false;
        if ((cmd & (command::left | command::right)) == 0) {
            engine_idle();
            p.engine_loud = false;
            p.throttle = 0;
            p.speed -= 8;
        } else {
            const int pushed = (cmd & command::left) ? -1 : 1;
            if (p.facing == pushed) {
                p.engine_loud = true;
                if (p.turn == 0) {
                    engine_sound(true);
                    p.throttle = std::min(p.throttle + 1, 8);
                } else {
                    engine_idle();
                    --p.turn;
                }
                p.speed += p.throttle;
            } else {
                engine_idle();
                p.engine_loud = false;
                p.throttle = 0;
                if (p.speed == 0 && ++p.turn > 6) {
                    p.facing = -p.facing;
                    p.turn = 5;
                }
                p.speed -= 8;
            }
        }
        p.lift = p.speed > lift_off_speed && (cmd & command::up) == 0;
        p.speed = std::clamp(p.speed, 0, max_speed);
    }

    // $10BDBA: on the ground the plane moves by its speed / 100, rounded.
    void ground_roll() {
        if (p.mode == flying) return;
        p.speed_x = div16(p.speed + 0x32, 100);
        p.x = word(p.x + p.speed_x * p.facing);
    }

    // $10C5F4: past either end of the deck the plane is in the air.
    void deck_edge() {
        if (p.x < p.deck_low || p.x > p.deck_high) {
            p.signal = 2;
            p.fresh_on_deck = false;
            p.mode = flying;
        } else {
            p.altitude = ground_level();
        }
    }

    // $10B92E: the hook catches one of the four wires (every $38 from the
    // carrier + $46) when the plane rolls fast enough without lift.
    void wires() {
        if (p.speed < 600 || p.lift) return;
        const int hook = p.facing == -1 ? word(p.x + 0x18) : word(p.x - 0x18);
        p.wire_x = word(w.carrier_x + 0x46);
        for (int i = 0; i < 4; ++i, p.wire_x = word(p.wire_x + 0x38)) {
            if (word(p.wire_x - 8) <= hook && hook <= word(p.wire_x + 8)) {
                p.mode = caught;
                p.signal = 4;
                w.caught_wire_x = p.wire_x;
                return;
            }
        }
    }

    // $10BCCE: the deck officer's signal.
    void officer() {
        if (p.fresh_on_deck) {
            p.signal = word(p.x - p.deck_low) < 0x136 && p.speed < 400 ? 1 : 0;
            return;
        }
        if (!on_elevator_spot()) {
            const int range = p.speed / 4;
            const int reach = div16(range * range * 2, 100);
            int distance = std::abs(word(p.x - w.carrier_x));
            distance -= 0x10;
            if (p.x < w.carrier_x) {
                if (distance < reach && p.facing == 1 && p.speed > 0) p.signal = 4;
            } else if (distance < reach && p.facing == -1 && p.speed > 0) {
                p.signal = 4;
            }
        } else {
            p.signal = 5;
        }
    }

    // $10BFF4: the stick in the air.
    void steer() {
        p.pulling_up = false;
        const int pushed = (cmd & command::left) ? -1 : 1;
        p.climb_kick = 0;
        const int quarter = div16(t.pitch_rate, 4);
        const auto level_off = [&] {
            if (p.turn == 0) {
                if (p.climb > 0) {
                    p.climb = std::max(p.climb - t.pitch_rate, 0);
                    if (p.climb < 0x1F5) p.climb_kick = -p.climb;
                }
            } else {
                p.climb = std::max(p.climb - quarter, -0x8CA);
                if (p.climb > 0 && p.climb < 0x1F5) p.climb_kick = -p.climb;
            }
        };
        if ((cmd & 0xF) == 0) {
            if (p.turn == 0) {
                if (p.climb > 0) {
                    p.climb = std::max(p.climb - t.pitch_rate, 0);
                    if (p.climb < 0x1F5) p.climb_kick = -p.climb;
                }
            } else {
                unwind_turn(false);
                p.climb = std::max(p.climb - quarter, -0x8CA);
                if (p.climb > 0 && p.climb < 0x1F5) p.climb_kick = -p.climb;
            }
            p.throttle = std::max(p.throttle - 1, 4);
            if (p.speed > stall_speed) p.speed = std::max(p.speed - p.throttle, stall_speed);
            p.engine_loud = false;
            engine_sound(false);
        } else if ((cmd & (command::left | command::right)) == 0) {
            p.engine_loud = false;
            engine_sound(false);
            if ((cmd & command::down) == 0) {
                if (cmd & command::up) {
                    if (p.facing == -1) {
                        if (p.climb == 0 || p.climb < 600) p.climb = std::min(p.climb + t.pitch_rate, 600);
                        else p.climb = std::max(p.climb - t.pitch_rate, 600);
                        p.pulling_up = true;
                    } else {
                        p.climb -= t.pitch_rate;
                        if (p.climb < -600) p.climb -= div16(p.climb + 600, 2);
                    }
                }
            } else {
                p.engine_loud = true;
                engine_sound(true);
                p.climb = std::max(p.climb - t.pitch_rate * 2, climb_floor);
            }
            p.climb_kick = 0;
            unwind_turn(false);
        } else {
            if (p.facing == pushed) {
                p.engine_loud = true;
                engine_sound(true);
                unwind_turn(true);
                p.throttle = std::min(p.throttle + 1, 8);
                p.speed = std::min(p.speed + p.throttle, max_speed);
            } else {
                turn_step(false);
            }
            if ((cmd & (command::up | command::down)) == 0) {
                level_off();
            } else if ((cmd & command::down) == 0) {
                if (p.speed <= stall_speed) {
                    p.climb += p.facing < 1 ? div16(t.pitch_rate, 8) : quarter;
                } else {
                    p.climb += t.pitch_rate;
                }
                p.climb = std::min(p.climb, 3000);
            } else if (p.turn == 0) {
                p.climb = std::max(p.climb - t.pitch_rate, climb_floor);
            } else {
                p.climb = std::max(p.climb - div16(t.pitch_rate, 2), climb_floor);
            }
        }
    }

    // $10BDFA: the motion, in Fast Floating Point as the game computes it.
    void move() {
        const int target = (p.pulling_up && p.climb == 600) ? -800 : p.climb;
        p.smoothed_climb = word(p.smoothed_climb + div16(word(target - p.smoothed_climb), 4));
        int angle = word(p.smoothed_climb + p.climb_kick);
        if (angle < 0) angle = -angle;
        Ffp sine = t.sine[std::size_t(div16(angle, 100))];
        const Ffp cosine = t.sine[std::size_t(div16(9000 - angle, 100))];
        if (p.smoothed_climb < 0 || p.climb_kick < 0) sine = ffp::negate(sine);
        p.throttle = word(p.throttle - ffp::to_int(sine));
        if (p.facing > 0 && p.speed < stall_speed) p.throttle = word(p.throttle - div16(p.throttle, 10));
        const Ffp across = ffp::multiply(ffp::multiply(t.turn_speed[std::size_t(p.turn)], ffp::from_int(p.speed)), cosine);
        p.speed_x = word(ffp::to_int(ffp::divide(ffp::add(across, fifty), hundred)));
        p.x = word(p.x + word(p.speed_x * p.facing));
        p.speed_y = word(ffp::to_int(ffp::divide(ffp::multiply(ffp::from_int(p.speed), sine), hundred)));
        if (p.speed < stall_speed && p.mode == flying) {
            if ((cmd & command::up) == 0) p.climb = std::max(p.climb - div16(t.pitch_rate, 2), climb_floor);
            p.speed_y = word(p.speed_y - div16(stall_speed - p.speed, 100));
        }
        p.altitude = word(p.altitude + p.speed_y);
        if (p.altitude > sky_top) {
            p.altitude = sky_top;
            p.climb = -p.climb;
            p.speed_y = -div16(p.speed_y, 2);
        } else if (p.altitude < -4) {
            p.altitude = -4;
        }
    }

    // $10B8C4: the wheels touch what is below.
    bool touching() {
        const int bottom = word(p.altitude - clearance());
        return bottom < obstacle(column(p.x)) || bottom < 1;
    }

    // $10BA80: touching down. On the carrier's deck, facing left while
    // pulling up, the plane lands (mode 1); otherwise it bounces off the
    // deck. Anywhere else it crashes (mode 4, $10AFBA).
    void touch_down() {
        const int c = column(p.x);
        const bool deck = p.x >= p.deck_low && p.x <= p.deck_high && p.turn == 0 &&
                          word(clearance() + obstacle(c) - 4) <= p.altitude && carrier().afloat;
        if (clearance() >= 0x37 || p.mode != flying || !touching()) return;
        if (deck) {
            if (p.facing == -1 && p.pulling_up) {
                p.mode = on_deck;
                screech();
                p.altitude = word(clearance() + obstacle(c));
            } else {
                p.speed_y = -p.speed_y;
                p.climb = -p.climb;
                p.altitude = word(p.altitude + 6);
                screech();
            }
        } else {
            p.mode = falling;
            p.speed = word(p.speed_x * 100);
            // $10BB9E: a blast two columns ahead, unless in the water or low
            // on a ship's deck; what stands in the plane's column is wrecked
            // ($1046C6, as a rocket would).
            if (!over_water(c) && !(over_ship(c) && p.altitude < 0x14)) {
                blast(column(p.x + p.facing * 16), p.altitude, false);
                island_hit(p, w, t, c * 8, 0);
            }
            crash();
        }
    }

    // $10B45A: wheels down near the carrier, and on deck.
    void gear() {
        if (p.mode == flying) {
            const bool near = word(w.carrier_x - carrier_reach) < p.x && p.x < word(w.carrier_x + carrier_reach);
            p.gear = near && carrier().afloat ? 5 : 0;
        } else {
            p.gear = (p.mode == on_deck || p.mode == caught || p.mode == going_below) ? 5 : 0;
        }
    }

    // $10C378 / $10ABDE: the shapes shown, from the pitch (or the turn).
    void shapes(int forced_step = -1) {
        if (forced_step >= 0) {
            // $10ABDE as the crash calls it ($10B0B2): the crash drops the
            // name it returns, so only the attitude changes (and the shape
            // would be mirrored to the facing).
            if ((p.mode == flying || p.mode == falling) && p.turn != 0) {
                p.attitude = 0;
            } else if (p.mode == flying || p.mode == falling) {
                p.attitude = t.attitude[std::size_t(p.turn + forced_step)];
            }
            return;
        }
        int step = 0, wheel_index = 9;
        p.wheels.clear();
        if (p.mode != on_deck && p.mode != going_below && p.turn == 0) {
            step = div16(p.climb + 5000, 500);
            wheel_index = step;
        }
        const bool flight_shape = p.mode == flying || p.mode == caught || p.mode == falling || p.lift;
        if (flight_shape && p.turn == 0) {
            if (p.facing == -1) {
                p.wheels = t.wheel_left_names[std::size_t(wheel_index)];
                p.wheel_attitude = step + 10;
            } else {
                p.wheels = t.wheel_right_names[std::size_t(wheel_index)];
                p.wheel_attitude = step;
            }
        }
        if (p.mode == burning) return;
        const int index = p.turn + step;
        switch (p.mode) {
        case flying:
        case falling:
            if (p.turn != 0) {
                p.attitude = 0;
                p.shape = p.facing == -1 ? t.turn_left_names[std::size_t(index)] : t.turn_right_names[std::size_t(index)];
            } else {
                p.shape = t.flying_names[std::size_t(index)];
                p.attitude = t.attitude[std::size_t(index)];
            }
            break;
        case on_deck:
        case going_below:
            // With lift the plane already shows its flying shape (entry 9).
            p.shape = p.lift ? t.flying_names[9] : t.deck_names[std::size_t(index)];
            p.attitude = 4;
            break;
        default:
            p.shape = t.flying_names[std::size_t(index)];
            p.attitude = t.attitude[std::size_t(index)];
            break;
        }
    }

    // $10C660.
    void fly() {
        fire();
        enemy_timer(p, w, t, cmd); // $10BC02
        if (w.menu) {
            // $10C676: while the weapon menu is up the plane waits.
            p.throttle = 0;
            p.speed = 0;
            p.eighth_scale = p.altitude > 0xBA;
            return;
        }
        switch (p.mode) {
        case flying:
            if (p.oil < 0x60 || p.fuel < 0) {
                p.signal = 3;
                p.mode = falling;
                p.speed = word(p.speed_x * 100);
                crash();
            } else if (p.altitude < -6) {
                p.mode = sinking;
                p.sink_step = p.wreck_time = p.next_blast = 0;
                p.signal = 3;
            } else {
                steer();
                move();
                touch_down();
                p.signal = p.altitude < 0x51 ? 2 : 3;
                officer();
            }
            break;
        case on_deck:
            deck_throttle();
            ground_roll();
            deck_edge();
            wires();
            officer();
            break;
        case caught:
            engine_idle(); // ($10C882)
            p.altitude = ground_level();
            p.climb = 0;
            p.climb_kick = 0;
            p.speed -= 0x6E;
            if (p.speed < 0) {
                p.speed = 0;
                p.mode = on_deck;
            }
            ground_roll();
            break;
        case falling:
            w.sound.engine_target = 0x19; // ($10C74E: the engine whines down)
            w.sound.engine_base = 0x3C0;
            p.engine_loud = false;
            w.guns = false;
            p.climb_kick = 0;
            p.signal = 3;
            crash();
            break;
        case sinking:
            w.sound.engine_target = 0; // ($10C78C)
            if (++p.sink_step > 2) {
                p.sink_step = 0;
                p.altitude = word(p.altitude - 1);
            }
            p.climb = std::max(p.climb - 250, climb_floor);
            wreck_blasts();
            wreck_timer();
            break;
        case burning:
            p.shape = t.wreck_names[p.facing == -1 ? 1 : 0];
            p.attitude = 0;
            p.altitude = over_ship(column(p.x)) ? word(obstacle(column(p.x)) + clearance()) : clearance();
            // $10C870: it smokes from the nose.
            w.effects.start_smoke(word(p.x + p.facing * 8), word(p.altitude + 0xB), 6, w.beam);
            wreck_blasts();
            wreck_timer();
            break;
        default:
            p.altitude = ground_level();
            if (w.elevator == 0) p.mode = on_deck;
            break;
        }
        gear();
        shapes();
        p.eighth_scale = p.altitude > 0xBA;
    }

public:
    // $103684 / $10B7EC.
    void new_plane(int random_load) {
        w.input_delay = 0xF;
        // $103756: every object in the table is gone (+$20 cleared: weapons
        // and blasts), the enemy's torpedo too.
        for (auto& o : w.effects.objects) o.kind = FlyingObject::Kind::none;
        w.effects.enemy_torpedo.kind = FlyingObject::Kind::none;
        w.weapon = 1;
        p.signal = 3;
        w.crew.phase = 0; // ($1036A8)
        w.sound.engine_level = w.sound.engine_target = 0; // ($10B9BC: the engine stops)
        p.speed = 0;
        w.elevator = 1;
        w.lift = 0x20;
        w.menu = true;
        p.fresh_on_deck = true;
        p.engine_loud = false;
        if (w.weapon_load != 0xFF) w.weapon_load = t.weapon_loads[std::size_t(w.weapon)];
        w.gauges.set_load(w.weapon_load);
        w.guns = false;        // ($1036E8)
        w.gun_supply = 0x600;  // ($1036B0)
        p.sea_counter = -1;
        w.menu_repeat = 1;
        // $10B7EC: the record.
        p.deck_low = carrier().first * 4 + 0x10;
        p.deck_high = carrier().last * 4 - 0x10;
        p.altitude = 0;
        p.speed_x = 0;
        p.speed_y = 0;
        p.mode = on_deck;
        p.spare = random_load;
        p.oil = 0x80;
        p.fuel = 0xC0;
        p.climb = 0;
        p.gear = 5;
        p.lift = false;
        p.fuel_period = p.fuel_counter = 0x1C;
        p.enemy_timer = 0x546;
    }
};

void read_names(const amiga::LoadedImage& wings, std::uint32_t at, std::string* out, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        std::string name;
        for (std::uint32_t c = 0; c < 4; ++c) name += char(wings.u8(at + std::uint32_t(i * 4) + c));
        out[i] = name;
    }
}

template <std::size_t N>
void read_words(const amiga::LoadedImage& wings, std::uint32_t at, std::array<int, N>& out) {
    for (std::size_t i = 0; i < N; ++i) out[i] = wings.s16(at + std::uint32_t(i * 2));
}

} // namespace

FlightTables read_flight_tables(const amiga::LoadedImage& wings) {
    FlightTables t;
    read_words(wings, 0x115A00, t.wheel_height);
    read_words(wings, 0x115A16, t.gear_height);
    read_words(wings, 0x115A54, t.turn_height);
    for (std::size_t i = 0; i < t.turn_speed.size(); ++i) t.turn_speed[i] = wings.u32(0x115A60 + std::uint32_t(i * 4));
    for (std::size_t i = 0; i < t.sine.size(); ++i) t.sine[i] = wings.u32(0x115AC8 + std::uint32_t(i * 4));
    read_words(wings, 0x115A2C, t.attitude);
    read_names(wings, 0x115D34, t.flying_names.data(), t.flying_names.size());
    read_names(wings, 0x115C64, t.turn_left_names.data(), t.turn_left_names.size());
    read_names(wings, 0x115CCC, t.turn_right_names.data(), t.turn_right_names.size());
    read_names(wings, 0x115C34, t.deck_names.data(), t.deck_names.size());
    read_names(wings, 0x115DA6, t.wheel_left_names.data(), t.wheel_left_names.size());
    read_names(wings, 0x115DF6, t.wheel_right_names.data(), t.wheel_right_names.size());
    read_words(wings, 0x115D84, t.deck_front);
    read_words(wings, 0x115D92, t.deck_back);
    read_words(wings, 0x115716, t.obstacle_height);
    for (std::size_t i = 0; i < t.sea_motion.size(); ++i) t.sea_motion[i] = wings.u8(0x114B98 + std::uint32_t(i));
    t.pitch_rate = wings.s16(0x115E6A);
    for (std::size_t i = 0; i < t.weapon_loads.size(); ++i) t.weapon_loads[i] = wings.u8(0x114B4D + std::uint32_t(i));
    // $10C88C writes the wreck's name as two words: 'hc' 'r5', or 'hc' 'rf'
    // facing left (immediates in the code).
    t.wreck_names = {"hcr5", "hcrf"};
    t.islands = read_island_tables(wings);
    t.enemies = read_enemy_tables(wings);
    t.ships = read_ship_tables(wings);
    for (std::uint32_t at = 0x11AF52 - 0x63E2 - 32; at < 0x11AF52 - 0x63E2 + 288; ++at) t.gun_angles.push_back(wings.u8(at));
    const auto text = [&](std::uint32_t at) {
        std::string s;
        for (char c; (c = char(wings.u8(at))) != 0; ++at) s += c;
        return s;
    };
    t.messages.island_taken = text(0x11AF52 - 0x7604);
    t.messages.next_mission = text(0x11AF52 - 0x7660);
    t.messages.promoted = text(0x11AF52 - 0x75A4);
    for (std::size_t i = 0; i < t.sine_quarter.size(); ++i) t.sine_quarter[i] = wings.s16(0x11AF52 - 0x6692 + std::uint32_t(2 * i));
    for (std::size_t i = 0; i < t.slope.size(); ++i) t.slope[i] = wings.s16(0x11AF52 - 0x6892 + std::uint32_t(2 * i));
    for (std::size_t i = 0; i < t.arctangent.size(); ++i) t.arctangent[i] = wings.u8(0x11AF52 - 0x5810 + std::uint32_t(i));
    return t;
}

void set_off_near(PlayerFlight& p, FlightWorld& w, const FlightTables& t, int x, int reach) {
    kill_soldiers_near(p, w, t, x, reach);
    // $101AE2: the player's torpedoes in the table, then the enemy one.
    for (auto& o : w.effects.objects) {
        if (o.listed == FlyingObject::Kind::none || o.weapon != 2) continue;
        if (std::uint16_t(std::abs(word(o.shown_x - x))) > reach) continue;
        o.kind = FlyingObject::Kind::blast;
        o.frame = 1;
    }
    auto& enemy = w.effects.enemy_torpedo;
    if (enemy.listed != FlyingObject::Kind::none && std::uint16_t(std::abs(word(enemy.shown_x - x))) <= reach) {
        enemy.kind = FlyingObject::Kind::blast;
        enemy.frame = 1;
    }
}

void ships_sinking(PlayerFlight& p, FlightWorld& w, const FlightTables& t) {
    for (auto& ship : w.ships) {
        if (!ship.afloat || ship.ready != 0) continue;
        // $101CD8: a step down when the timer runs out; each step comes
        // sooner than the last.
        if (--ship.sink_timer > 0) continue;
        ++ship.sunk;
        ship.sink_timer = ship.sink_delay;
        if (ship.sink_delay != 0) --ship.sink_delay;
        if (ship.value == 0) {
            // The US carrier: a plane below deck comes up (the step test
            // before it, `cmpi #1,$14`, has its flags overwritten: it acts
            // at every step); a plane on deck at step $21 sinks with it; the
            // game ends 100 frames later.
            if (w.elevator == 1) {
                p.mode = 0xB;
                w.elevator = 2;
                w.menu = false;
                p.turn = 0;
            }
            if (ship.sunk == 0x21 && p.mode == 1) {
                p.mode = 6;
                p.altitude = 0;
                w.spare_planes = 0;
                w.menu = false;
                w.game_over_countdown = 0x64;
            }
            if (ship.sunk < 0x78) continue;
            ship.afloat = false; // (and -$5A3D is set: nothing rebuilt reads it)
        } else {
            // An enemy ship: its value and the message at step 10.
            if (ship.sunk == 0xA) ship_sunk(p, w, t, ship);
            if (ship.sunk < 0x78) continue;
            ship.ready = -1;
        }
        // The ship's columns leave the map.
        for (int c = ship.first; c <= ship.last && c / 2 < int(w.mission_map.words.size()); c += 2) {
            w.mission_map.words[std::size_t(c / 2)] = 0;
        }
    }
}

void flight_tick(PlayerFlight& p, FlightWorld& w, const FlightTables& t, int command) {
    Tick(p, w, t, command).run();
}

const data::Sample* sample_file(const data::Assets& assets, Sample sample) {
    // ($103368 loads them by these names.)
    static constexpr std::array<const char*, 9> names = {"",        "boom",          "splash",  "machinegun", "engine",
                                                         "grind.1", "metal.clang.1", "screech", "scream"};
    for (const auto& s : assets.samples) {
        if (s.name == names[std::size_t(sample)]) return &s;
    }
    return nullptr;
}

std::array<int, 9> sample_lengths(const data::Assets& assets) {
    std::array<int, 9> lengths{};
    for (std::size_t i = 1; i < lengths.size(); ++i) {
        if (const auto* s = sample_file(assets, Sample(i))) lengths[i] = int(s->data.size());
    }
    return lengths;
}

void set_up_sounds(FlightWorld& w, const std::array<int, 9>& lengths) {
    auto& s = w.sound;
    s.lengths = lengths;
    const auto len = [&](Sample sample) { return lengths[std::size_t(sample)]; };
    // $101F76: the guns and the engine (channel 0), the enemy planes' guns
    // and engines (1), explosions and splashes (2), the enemy ships' guns (3).
    s.channels[0].request = {SoundRequest{false, Sample::machinegun, len(Sample::machinegun), 0xC8, 0x40, 0xFFFF},
                             SoundRequest{false, Sample::engine, len(Sample::engine), 0x17C, 0x3E, 0xFFFF}};
    s.channels[1].request = {SoundRequest{false, Sample::machinegun, len(Sample::machinegun), 0xA0, 0x39, 0xFFFF},
                             SoundRequest{false, Sample::engine, len(Sample::engine), 0x14A, 0x40, 0xFFFF}};
    s.channels[2].request = {SoundRequest{false, Sample::boom, len(Sample::boom), 0x1F4, 0x40, 1},
                             SoundRequest{false, Sample::splash, len(Sample::splash), 0x15E, 0x40, 1}};
    s.channels[3].request[0] = {false, Sample::machinegun, len(Sample::machinegun), 0x140, 0x40, 0xFFFF};
}


void new_plane(PlayerFlight& p, FlightWorld& w, const FlightTables& t, int random_load) {
    Tick(p, w, t, 0).new_plane(random_load);
}

} // namespace wings::game
