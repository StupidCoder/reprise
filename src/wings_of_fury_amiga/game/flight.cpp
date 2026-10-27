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
    }

private:
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
    }

    // $101460: the elevator moves a pixel per drawn frame; at the top it is
    // up (state 0), at the bottom a new plane is made ready (state 1).
    void elevator() {
        if (w.draw_counter == w.elevator_seen) return;
        w.elevator_seen = w.draw_counter;
        if (w.elevator == 2) {
            if (--w.lift == 0) {
                w.elevator = 0;
                p.engine_loud = false; // ($10B9CC: engine sound)
            }
        } else if (w.elevator == 3) {
            if (++w.lift == 0x20) new_plane(w.random_load);
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
            w.spare_planes = 0;
            return;
        }
        new_plane(w.random_load);
    }

    // $10AED8: for its first 75 ticks the wreck keeps blowing up (sinking:
    // splashing at the waterline) every 0..12 ticks, up to a column either
    // side.
    void wreck_blasts() {
        const bool sinking_now = p.mode == sinking;
        const int height = sinking_now ? 0 : word(p.altitude - clearance());
        if (p.wreck_time >= 0x4B || p.wreck_time < p.next_blast) return;
        p.next_blast = p.wreck_time + (w.random.next() & 0xC);
        w.effects.start_blast(column(p.x + (w.random.next() & 0xF) - 8), height, sinking_now);
    }

    // $10AF7C: the wreck waits 150 ticks, or a fire press after 30.
    void wreck_timer() {
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
                w.effects.start_blast(c, p.altitude - 1, false);
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
                    w.effects.start_blast(c, p.altitude, false);
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
            // ($10B304: on land and ships the original also starts a blast
            // here, but passes the plane's x and altitude where the column's
            // map address belongs: it lands at a fixed spot that depends on
            // where the map sits in memory (x 2040 in the reference machine)
            // and is left out. On land it then wrecks what stands in the
            // column and the soldiers near it, $1046C6 and $101A84: islands
            // are not rebuilt yet.)
            if (surface == water) splash();
        } else {
            p.climb = std::max(p.climb - t.pitch_rate, -0xC1C);
            falling_on = true;
        }
        if (falling_on) {
            p.speed_y = std::max(p.speed_y - 1, -10);
            p.altitude = word(p.altitude + p.speed_y);
            if (word(p.altitude - clearance()) <= 0) {
                p.altitude = clearance();
                w.effects.start_blast(c, p.altitude - 1, false);
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
    // tap fires the guns and a long press drops/launches the load (not
    // rebuilt yet: they only start objects).
    void fire() {
        if ((cmd & (command::fire_held | command::fire_tapped)) == 0) return;
        if (p.mode == on_deck && w.elevator == 0 && carrier().ready != 0 && on_elevator_spot() && p.speed == 0) {
            p.signal = 3;
            w.elevator = 3;
            p.mode = going_below;
            p.altitude = ground_level();
        }
    }

    // $10C4E8: rolling on deck. Pushing the way the plane faces opens the
    // throttle (the step grows to 8); pushing the other way brakes and, at a
    // standstill, turns the plane round (six steps, then facing flips).
    void deck_throttle() {
        if ((cmd & (command::left | command::right)) == 0) {
            p.engine_loud = false;
            p.throttle = 0;
            p.speed -= 8;
        } else {
            const int pushed = (cmd & command::left) ? -1 : 1;
            if (p.facing == pushed) {
                p.engine_loud = true;
                if (p.turn == 0) {
                    p.throttle = std::min(p.throttle + 1, 8);
                } else {
                    --p.turn;
                }
                p.speed += p.throttle;
            } else {
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
        } else if ((cmd & (command::left | command::right)) == 0) {
            p.engine_loud = false;
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
                p.climb = std::max(p.climb - t.pitch_rate * 2, climb_floor);
            }
            p.climb_kick = 0;
            unwind_turn(false);
        } else {
            if (p.facing == pushed) {
                p.engine_loud = true;
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
                p.altitude = word(clearance() + obstacle(c));
            } else {
                p.speed_y = -p.speed_y;
                p.climb = -p.climb;
                p.altitude = word(p.altitude + 6);
            }
        } else {
            p.mode = falling;
            p.speed = word(p.speed_x * 100);
            // $10BB9E: a blast two columns ahead, unless in the water or low
            // on a ship's deck. (On an island it also wrecks what stands
            // there, $1046C6: not rebuilt yet.)
            if (!over_water(c) && !(over_ship(c) && p.altitude < 0x14)) {
                w.effects.start_blast(column(p.x + p.facing * 16), p.altitude, false);
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
        // ($10BC02, the timer that sends enemy planes, is not rebuilt.)
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
            p.engine_loud = false;
            p.climb_kick = 0;
            p.signal = 3;
            crash();
            break;
        case sinking:
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
        w.weapon = 1;
        p.signal = 3;
        w.crew.phase = 0; // ($1036A8)
        p.speed = 0;
        w.elevator = 1;
        w.lift = 0x20;
        w.menu = true;
        p.fresh_on_deck = true;
        p.engine_loud = false;
        if (w.weapon_load != 0xFF) w.weapon_load = t.weapon_loads[std::size_t(w.weapon)];
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
    return t;
}

void flight_tick(PlayerFlight& p, FlightWorld& w, const FlightTables& t, int command) {
    Tick(p, w, t, command).run();
}

void new_plane(PlayerFlight& p, FlightWorld& w, const FlightTables& t, int random_load) {
    Tick(p, w, t, 0).new_plane(random_load);
}

} // namespace wings::game
