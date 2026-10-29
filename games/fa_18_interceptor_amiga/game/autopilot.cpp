#include "fa_18_interceptor_amiga/game/autopilot.hpp"
#include "fa_18_interceptor_amiga/game/attitude.hpp"
#include <cstdlib>

namespace interceptor::game {

namespace {

using i16 = std::int16_t;

// The control byte's bits ($12126E, $121362).
constexpr std::uint8_t throttle_bits = 0x03, roll_right = 0x04, roll_left = 0x08, pitch_down = 0x10,
                       pitch_up = 0x20;
// The record's +$20 bits the maneuvers use as their progress.
constexpr std::uint8_t passed = 0x08, turned = 0x10;
constexpr std::uint8_t spinning = 0x02;
// +$64: the pilot's bits; bit 4 keeps the control byte for the flight
// step (`$1213FC`).
constexpr std::uint8_t steering = 0x10;
// The motion word's bits: +2 bit 0 (flown as the player's, the
// trainer), +3 bit 4 (level the wings).
constexpr std::uint16_t as_player = 0x0100, level_wings = 0x0010;

// The record's angles as the handlers compare them (+$66, +$6A).
int pitch_of(const Aircraft& a) { return a.attitude.pitch; }
int roll_of(const Aircraft& a) { return a.attitude.roll; }

// $12126E: roll toward the rate's side - left above 0, right below - with
// the throttle's bits kept (the stick's pitch and the rudder let go).
void roll_toward(Aircraft& a, int rate) {
    const std::uint8_t side = rate > 0 ? roll_left : rate < 0 ? roll_right : 0;
    a.controls = std::uint8_t((a.controls & throttle_bits) | side);
}

// $121362 / $12135E: pull or push while the pitch rate (+$56) is short
// of `rate`; $12135E keeps the roll's bits.
void pitch_toward(Aircraft& a, int rate, bool keep_roll = false) {
    std::uint8_t bits = 0;
    if (rate >= 0) {
        if (rate > a.pitch_rate) bits = pitch_down;
    } else if (rate < a.pitch_rate) {
        bits = pitch_up;
    }
    const std::uint8_t kept = keep_roll ? std::uint8_t(a.controls & 0xCF) : std::uint8_t(a.controls & throttle_bits);
    a.controls = std::uint8_t(kept | bits);
}

} // namespace

// $121C6E (the attitude step for others, after the autopilot): without the
// pilot's bit 4 the stick is let go; in roles 6 and 10 - or, by the pilot's
// bits, once on course in roles 0, 1 (which become role 6 for 30 frames)
// and 8 - the stick rolls the wings back toward level, let go within 3.5
// degrees.
void Autopilot::level_out(Aircraft& a, std::uint8_t pilot, std::int16_t roll_before) {
    if (a.player) return;
    const bool trainer = a.motion & as_player;
    if (!trainer && !(pilot & steering)) {
        a.controls &= 0xC3;
        return;
    }
    if (a.role != 6 && a.role != 10) {
        if (trainer) return;
        bool go = false;
        if (pilot & 0x08) {
            if (!(pilot & 0x04)) {
                a.state &= ~0x2000;
                if (!(pilot & 0x40)) return;
            }
            go = pilot & 0x20;
        } else {
            if (!(pilot & 0x02)) {
                a.state &= ~0x2000;
                if (!(pilot & 0x20)) return;
            }
            go = pilot & 0x40;
        }
        if (!go || (a.type & 0xF0) != 0x10) return; // (others than aircraft: not built)
        if (a.role == 0 || a.role == 1) {
            a.role = 6;
            a.timer = 0x1E;
        } else if (a.role != 8) {
            return;
        }
    }
    const int roll = roll_before;
    std::uint8_t side = 0;
    if (roll < 0x3841) side = roll < 0x119 ? 0 : roll_right;
    else side = roll > 0x6F67 ? 0 : roll_left;
    a.controls = std::uint8_t((a.controls & throttle_bits) | side);
    a.state |= 0x2000;
    if ((pilot & 0x06) == 0x06) a.motion |= level_wings;
}

namespace {

using i32 = std::int32_t;
constexpr std::uint8_t rudder_right = 0x80, rudder_left = 0x40;
// +$64's bits while heading for a point ($120C66): 0 the side the point
// is on, 3 the axis (set: up / down), 5 and 6 the point ahead, 7 close
// sideways.
constexpr std::uint8_t pilot_side = 0x01, pilot_vertical = 0x08, pilot_ahead_side = 0x20, pilot_ahead_up = 0x40,
                       pilot_close = 0x80;
constexpr std::uint32_t other_rates = 0x121406, type_14_pitch_rates = 0x121448, slow_roll_rates = 0x12145E;

// The dot of (x, y, z) with a column of the view matrix.
i32 dot(const Aircraft& a, int column, i32 x, i32 y, i32 z) {
    return i32(a.view[std::size_t(column)]) * x + i32(a.view[std::size_t(3 + column)]) * y +
           i32(a.view[std::size_t(6 + column)]) * z;
}

// $12103A: the rudder towards the rate, against the yaw rate.
void rudder_toward(Aircraft& a, int rate) {
    std::uint8_t bits = 0;
    if (rate > 0 && rate > a.yaw_rate) bits = rudder_right;
    else if (rate < 0 && rate < a.yaw_rate) bits = rudder_left;
    a.controls = std::uint8_t((a.controls & throttle_bits) | bits);
}

// For others than aircraft ($121078, $1211D8, $12134E, $1213A8): the rate
// goes straight into the record's turn words (+$50 pitch, +$52 yaw, +$54
// roll), the other two cleared.
void set_turn(Aircraft& a, int which, int rate) {
    a.bank_side = std::int16_t(which == 0 ? rate : 0);
    a.bank_side_was = std::int16_t(which == 1 ? rate : 0);
    a.bank_turn = std::int16_t(which == 2 ? rate : 0);
}

} // namespace

// $121C6E for a record other than an aircraft (a missile): on course by
// the pilot's bits (as `level_out`'s) it rolls back toward level at $3C0 a
// tick and turns no other way ($121D24); else it turns by its turn words
// (+$50 pitch, +$52 yaw, +$54 roll, `$121E12`) over `divisor` (0: whole).
std::array<std::int16_t, 3> Autopilot::object_turn(Aircraft& a, std::uint8_t pilot, int divisor) {
    bool on_course = false;
    if (!(a.motion & as_player) && !(pilot & steering)) {
        a.controls &= 0xC3;
    } else if (a.role == 6 || a.role == 10) {
        a.state |= 0x2000; // (the levelling's control bits do not move it)
    } else if (!(a.motion & as_player)) {
        if (pilot & 0x08) {
            if (!(pilot & 0x04)) a.state &= ~0x2000;
            on_course = ((pilot & 0x04) || (pilot & 0x40)) && (pilot & 0x20);
        } else {
            if (!(pilot & 0x02)) a.state &= ~0x2000;
            on_course = ((pilot & 0x02) || (pilot & 0x20)) && (pilot & 0x40);
        }
    }
    if (on_course) {
        const int roll = roll_of(a);
        std::int16_t r = 0;
        if (roll <= 0x3840) r = roll > 0x118 ? std::int16_t(-0x3C0) : std::int16_t(0);
        else r = roll < 0x6F68 ? std::int16_t(0x3C0) : std::int16_t(0);
        return {0, 0, r};
    }
    std::array<std::int16_t, 3> r{a.bank_side, a.bank_side_was, a.bank_turn};
    if (divisor > 0) {
        for (auto& v : r) v = std::int16_t(v / divisor);
    }
    return r;
}

Autopilot::Autopilot(const amiga::LoadedImage& program, const FlightTables& tables) : program_(program), tables_(tables) {}

bool Autopilot::steer_to(Aircraft& a, std::uint8_t& pilot, Course& course, const FlightTables& tables) const {
    const auto& t = course.target;
    if (t.cell_x < 0) return false;
    const std::uint8_t family = a.type & 0xF0;
    // (Missiles give up when their +$26 count runs out.)
    if (family == 0) {
        a.ground_drag = std::int16_t(a.ground_drag - 1);
        if (a.ground_drag >= 10) return false;
    }
    // The way to the point in fine units (a cell is $4000), the height
    // kept above a floor and below a ceiling that grow with the speed.
    i32 dx = (i32(std::int16_t(t.cell_x - std::int16_t(a.x >> 22))) << 14);
    i32 dz = (i32(std::int16_t(t.cell_z - std::int16_t(a.z >> 22))) << 14);
    i32 fine_x = t.fine_x, fine_z = t.fine_z;
    i32 height = t.height;
    if (!(a.handling & spinning) && family != 0 && a.type != 0x14 && a.type != 0x15) {
        const i32 margin = i32(a.speed) << (a.role == 8 ? 6 : 7);
        if (a.vy < 0) {
            if (margin >= a.y && margin >= t.height) height = margin;
        } else {
            const i32 ceiling = 0x800000 - margin;
            if (ceiling <= a.y && ceiling < t.height) height = ceiling;
        }
    }
    if (!(course.target_record & 0x80)) {
        // (A plain record number: the point's fine parts scaled by it.)
        const int k = course.target_record;
        fine_x = (fine_x << k) & 0x3FFF;
        height <<= k;
        fine_z = (fine_z << k) & 0x3FFF;
    }
    i32 x = fine_x - i32((a.x & 0x3FFFFF) >> 8) + dx;
    i32 z = fine_z - i32((a.z & 0x3FFFFF) >> 8) + dz;
    i32 y = height - (a.y >> 8);
    i32 longest = std::max(std::abs(x), std::max(std::abs(y), std::abs(z)));
    while (longest >= 0x4800) {
        x >>= 2;
        y >>= 2;
        z >>= 2;
        longest >>= 2;
    }
    const auto d = scaled_vector(tables, 0xC0, std::int16_t(x), std::int16_t(y), std::int16_t(z));
    x = d[0];
    y = d[1];
    z = d[2];
    // The thresholds: aligned within the first three (by side), close
    // sideways within the last two.
    struct Limits {
        i32 right_side, right, ahead_wide, left_side, left, ahead_left, close, near;
    } lim{};
    if (a.role == 8) lim = {0xC000, 0x10000, 0x18000, -0xC000, -0x10000, -0x18000, 0x30000, 0x80000};
    else if (family != 0x10) lim = {0x10000, 0x10000, 0x20000, -0x10000, -0x10000, -0x20000, 0x20000, 0x20000};
    else lim = {0x30000, 0x40000, 0x50000, -0x30000, -0x40000, -0x50000, 0x80000, 0x80000};
    pilot &= 0x79;
    // The axis the point lies further along: sideways (column 0) or up
    // and down (column 1).
    const i32 across = dot(a, 0, x, y, z), up = dot(a, 1, x, y, z);
    i32 off = 0;
    if (std::abs(across) <= std::abs(up)) {
        pilot |= pilot_vertical;
        off = up;
    } else {
        pilot &= ~pilot_vertical;
        off = across;
    }
    bool aligned = false;
    if (off < 0) {
        pilot &= ~pilot_side;
        aligned = off >= ((pilot & pilot_vertical) ? lim.left_side : lim.left);
    } else {
        pilot |= pilot_side;
        aligned = off <= ((pilot & pilot_vertical) ? lim.right_side : lim.right);
    }
    const bool flies = a.role == 0 || a.role == 1 || (a.role == 8 && !(a.motion & flag::on_ground));
    const auto vertical = [&](i32 v, bool on_course) { // $12128E / $1212D2
        int index = 0;
        if (on_course) {
            // (From on course with the point behind: the signed value's
            // high word - below 0 reads before the table, as the original.)
            index = std::min(v >> 16, 10);
        } else {
            const i32 size = std::abs(v);
            if (size >= lim.close || dot(a, 2, x, y, z) < 0) pilot &= ~pilot_ahead_up;
            else pilot |= pilot_ahead_up;
            index = std::min(size >> 16, 10);
        }
        pilot &= ~0x04;
        int rate = family != 0x10 ? table(other_rates, index)
                   : a.type == 0x14 ? table(type_14_pitch_rates, index)
                                    : pitch_rate(index);
        if (v >= 0) rate = -rate;
        if (!flies) return;
        pitch_toward(a, rate);
        if (family != 0x10) set_turn(a, 0, rate);
    };
    if (aligned) {
        // $12139A: on course - the stick as it is.
        if (family != 0x10) set_turn(a, 0, 0);
        if (!(pilot & pilot_vertical)) {
            pilot |= 0x02 | pilot_ahead_side;
        } else if (dot(a, 2, x, y, z) < 0) {
            vertical(off, true);
            return true;
        } else {
            pilot |= 0x04 | pilot_ahead_up;
        }
        pilot ^= 0x01;
        pilot |= steering;
        return true;
    }
    if (pilot & pilot_vertical) {
        vertical(off, false);
        return true;
    }
    // Sideways ($120FBA).
    const i32 size = std::abs(off);
    if (size < lim.near) pilot |= pilot_close;
    if (size >= lim.close || dot(a, 2, x, y, z) < 0) pilot &= ~pilot_ahead_side;
    else pilot |= pilot_ahead_side;
    pilot &= ~0x02;
    const int index = std::min(size >> 16, 10);
    if (family != 0x10) {
        int rate = table(other_rates, index);
        if (off >= 0) rate = -rate;
        rudder_toward(a, rate);
        set_turn(a, 1, rate);
        return true;
    }
    int rate = (a.type == 0x14 || a.role == 8) ? table(slow_roll_rates, index) : roll_rate(index);
    if (a.speed >= 0x1800) rate >>= 1;
    i32 away = off;
    if (off < 0) {
        rate = -rate;
        away = -off;
    }
    const int roll = roll_of(a), pitch = pitch_of(a);
    // Steeply banked or pitched: the rudder alone.
    if ((roll >= 0x1770 && roll <= 0x5910) || (pitch <= 0x6A40 && pitch > 0x640)) {
        rudder_toward(a, rate);
        return true;
    }
    // The bank limit grows with how far off the point is (behind: more).
    if (dot(a, 2, x, y, z) < 0) away = 0x600000 - away;
    int limit = away >> 8;
    if (limit > 0x3000) limit = 0x3000;
    else if (limit > 0x20) limit = std::max(limit, 0x1800);
    else limit = 0x1000;
    if (course.behaviour == 3 || course.behaviour == 4) {
        // Not too far over: the roll held near level.
        const bool ahead = (pilot & 0x60) == 0x60;
        if (rate < 0) {
            if (ahead && roll >= 0x3840 && roll < 0x6D60) rate = 0xF0;
            else if (roll > 0x3840 && roll <= 0x68B0) rate = roll > 0x6720 ? 0 : 0xF0;
        } else {
            if (ahead && roll <= 0x3840 && roll > 0x320) rate = -0xF0;
            else if (roll < 0x3840 && roll >= 0x7D0) rate = roll < 0x960 ? 0 : -0xF0;
        }
    }
    if (!flies) return true;
    // $121202: the rudder and the stick's roll.
    std::uint8_t bits = 0;
    if ((pilot & 0x60) == 0x60) {
        if (rate > 0 && rate > a.yaw_rate) bits = rudder_right;
        else if (rate < 0 && rate < a.yaw_rate) bits = rudder_left;
        if (bits) {
            if (roll <= 0x3840) {
                if (roll > 0x50) bits |= roll_right;
            } else if (roll <= 0x7030) {
                bits |= roll_left;
            }
        }
    } else if ((pilot & pilot_close) && ((roll <= 0x3840 && roll >= 0x190) || (roll > 0x3840 && roll <= 0x6EF0))) {
        bits = roll <= 0x3840 ? roll_right : roll_left;
    } else if (rate != 0) {
        bits = rate < 0 ? roll_right : roll_left;
    }
    a.controls = std::uint8_t((a.controls & throttle_bits) | bits);
    if (family != 0x10) set_turn(a, 2, rate);
    if (a.role == 8) limit >>= 1;
    a.bank_turn_limit = std::int16_t(limit);
    return true;
}

void Autopilot::fly(Aircraft& a, std::uint8_t& pilot, const PilotWorld& world, Course* course) const {
    if ((a.type & 0xF0) == 0x30) { // (parachutes and the like)
        pilot = 0;
        return;
    }
    a.bank_turn_limit = 0x1400;
    if (!(a.state & 0x0080)) {
        pilot = 0;
        return;
    }
    const auto steer = [&] { pilot |= steering; };  // $1213FC
    const auto wait = [&](std::uint8_t next) {      // the wings levelled while the timer runs
        if (a.timer > 0) {
            a.motion |= level_wings;
            steer();
            return true;
        }
        a.role = next;
        return false;
    };
    Course none;
    Course& c = course ? *course : none;
    // $120A74: a role is over. The trainer (+2 bit 0) waits a moment (role
    // 10) and lets go (role 0, $120B58) unless the practice waits for its
    // trigger; others wait in role 6, chasing a record longer at lower
    // levels.
    const auto finish = [&] {
        if (a.motion & as_player) {
            if (!world.practice_ready) {
                a.role = 10;
                a.timer = world.practice ? 0x24 : 0x3C;
                return;
            }
            a.timer = 10;
        } else if (!(c.target_record & 0x80) || c.target_record == 0xFF ||
                   (world.level > 2 && (pilot & 0x60) != 0x60)) {
            a.timer = 10;
        } else {
            a.timer = world.level <= 0 ? 0x4B : world.level == 1 ? 0x25 : world.level == 2 ? 0x12 : 4;
        }
        a.role = 6;
    };
    const auto let_go = [&] {
        a.role = role::none;
        a.controls &= throttle_bits;
    };
    // The height band an aircraft keeps (`$120BC6`, `$120B06`): below the
    // speed x 64 while sinking, or above $800000 less it while climbing,
    // it turns back to heading for its point.
    const auto out_of_band = [&] {
        const std::int32_t low = std::int32_t(a.speed) << 6;
        if (a.vy < 0) return !(low < a.y);
        return 0x800000 - low < a.y;
    };
    const auto head_for_point = [&](std::uint8_t r) { // $120C5A / $120C4C
        a.role = r;
        a.timer = 0x1E;
    };

    if (a.role != 0 && a.role != 8 && a.role != 1 && !(a.handling & spinning) && !(a.motion & as_player) &&
        out_of_band()) {
        head_for_point(1);
    } else if (a.role != 8 && a.role != 0 && world.mode == 5 && (a.state & 0x0008) && a.timer <= 0) {
        // ($120BEA: in mission 5 a touchable record whose timer ran out
        // follows its path, role 8, from the path entry +$3A.)
        const std::uint32_t at = 0x11DDC0 + std::uint32_t(program_.s16(0x11DDC0 + 2u * c.path));
        if (program_.s16(at) >= 0) {
            a.role = 8;
            c.target = {program_.s16(at), program_.s16(at + 2), program_.s16(at + 4), program_.s16(at + 6),
                        program_.s16(at + 8)};
        }
    }

    // A handler that moves on runs the next role in the same frame, as the
    // original falls through.
    for (int guard = 0; guard < 6; ++guard) {
        const int pitch = pitch_of(a), roll = roll_of(a);
        switch (a.role) {
        case 0:
        case 1:
        case 3:
        case 4:
        case 8: {
            // $120C66: heading for the point.
            if (a.handling & spinning) {
                // Spinning with the nose well down: wait (role 6).
                if (pitch <= 0x3840 && pitch >= 0xAF0) {
                    a.role = 6;
                    continue;
                }
            } else if (a.role == 8) {
                if (a.motion & as_player) {
                    if (!world.practice_ready) return;
                    if (a.timer < 0) {
                        a.timer = 0x0B;
                        a.role = 6;
                        continue;
                    }
                }
                // Upside down and not steep: wait a while.
                const bool steep = pitch >= 0xE10 && pitch < 0x6270;
                if (!steep && roll >= 0x1C20 && roll < 0x5460) {
                    a.timer = 0x25;
                    a.role = 6;
                    continue;
                }
            } else if (!(a.motion & as_player) && a.timer < 0) {
                finish();
                continue;
            }
            if (!steer_to(a, pilot, c, tables_)) pilot = 0;
            return;
        }
        case 5: // $1209E8: a banked turn (14 degrees) for the timer's time
        case 9: { // $1209F2: (8 degrees)
            const int bank = a.role == 5 ? 0x460 : 0x280, band = a.role == 5 ? 0x1E0 : 0x140;
            if (a.timer < 0) {
                let_go();
                return;
            }
            const int target = roll >= 0x3840 ? full_turn - bank : bank;
            int rate = roll_rate(2);
            if (std::abs(roll - target) >= band) {
                if (roll >= target) rate = -rate;
                roll_toward(a, rate);
            } else {
                a.controls &= throttle_bits;
            }
            steer();
            return;
        }
        case 6: // $120AEC: waiting
            if (a.handling & spinning) return;
            if (!(a.motion & as_player)) {
                if (a.timer < 0 || out_of_band()) {
                    // ($120B1C: sinking too low also posts message $3C,
                    // `$10F20E`; not traced.)
                    head_for_point(1);
                    continue;
                }
                return;
            }
            if (world.practice_ready) return;
            if (a.timer < 0) {
                head_for_point(8);
                continue;
            }
            return;
        case 2: { // $12096A: roll to the nearest knife edge (90° / 270°)
            const int target = roll < 0x3840 ? 0x1C20 : 0x5460;
            if (std::abs(roll - target) >= 0x4B0) {
                roll_toward(a, roll >= target ? -roll_rate(8) : roll_rate(8));
            } else {
                a.role = 7;
                a.timer = 0x20;
                a.controls &= throttle_bits;
            }
            steer();
            return;
        }
        case 7: // $1209C2: pull for the timer's time
            if (a.timer < 0) {
                finish();
                continue;
            }
            pitch_toward(a, -pitch_rate(4));
            steer();
            return;
        case 10: // $120A68: a moment, then let go
            if (a.timer < 0) let_go();
            return;
        // The maneuvers' first roles: clear the progress bits, then wings
        // level for 15 frames.
        case 11: case 14: case 23: case 26:
            a.handling &= ~(passed | turned);
            a.role = std::uint8_t(a.role + 1);
            a.timer = 15;
            continue;
        case 17: // $12062C: too low for the split-S
            if (a.y <= 0x100000) {
                finish();
                continue;
            }
            a.handling &= ~(passed | turned);
            a.role = 18;
            a.timer = 15;
            continue;
        case 20: // $1206B8
            a.handling &= ~passed;
            a.role = 21;
            a.timer = 15;
            continue;
        case 35: // $1204A4: throttle to 96, at airspeed $840 on
            if (a.throttle < 0x60) a.controls |= 0x01;
            else a.controls &= 0xFC;
            if (a.airspeed >= 0x840) {
                a.role = 36;
                a.timer = 0x3C;
            }
            steer();
            return;
        case 36: // $1204D4: a while, then pull up past 25 degrees nose up
            if (a.timer >= 0) {
                steer();
                return;
            }
            a.handling &= ~turned;
            a.role = 37;
            continue;
        case 37: { // $1204E8
            const int pitch = pitch_of(a);
            bool more = false;
            if (!(a.handling & turned)) {
                if (pitch < 0x3840) more = true;
                else a.handling |= turned;
            }
            // (Nose-up pitches count down from $7080: below $68B0 is more
            // than 25 degrees up.)
            if (!more && pitch >= 0x68B0) more = true;
            if (more) {
                pitch_toward(a, -0x28);
                steer();
                return;
            }
            pitch_toward(a, 0);
            a.controls = 0;
            a.role = 38;
            continue;
        }
        case 38: // $120526: until height $240000
            if (a.y > 0x240000) {
                finish();
                continue;
            }
            steer();
            return;
        case 39: // $120948
            a.role = 40;
            a.timer = 15;
            continue;
        case 12: if (wait(13)) return; continue;
        case 15: if (wait(16)) return; continue;
        case 18: if (wait(19)) return; continue;
        case 21: if (wait(22)) return; continue;
        case 24: if (wait(25)) return; continue;
        case 27: if (wait(28)) return; continue;
        case 30: if (wait(31)) return; continue;
        case 33: if (wait(34)) return; continue;
        case 40: if (wait(2)) return; continue;
        case 13: { // $120564: a full roll, let go near level
            int rate = roll_rate(8);
            bool more = false;
            if (!(a.handling & turned)) {
                if (!(a.handling & passed)) {
                    if (roll >= 0x3840) more = true;
                    else a.handling |= passed;
                }
                if (!more) {
                    if (roll < 0x3840) more = true;
                    else a.handling |= turned;
                }
            }
            if (!more) {
                if (roll < 0x320) {
                    finish();
                    continue;
                }
                if (roll >= 0x6E00) rate = 0;
            }
            roll_toward(a, rate);
            steer();
            return;
        }
        case 16: { // $1205E2: pull round the loop
            bool more = false;
            if (!(a.handling & turned)) {
                if (!(a.handling & passed)) {
                    if (pitch <= 0x3840) more = true;
                    else a.handling |= passed;
                }
                if (!more) {
                    if (pitch > 0x3840) more = true;
                    else a.handling |= turned;
                }
            }
            if (!more && pitch > 0x3840) {
                finish();
                continue;
            }
            pitch_toward(a, -pitch_rate(6));
            steer();
            return;
        }
        case 19:   // $120666: roll inverted, then the loop's pull (split-S)
        case 28: { // $1207D4: roll inverted and hold (inverted flight)
            int rate = roll_rate(8);
            if (!(a.handling & passed)) {
                if (roll >= 0x3840) {
                    roll_toward(a, rate);
                    steer();
                    return;
                }
                a.handling |= passed;
            }
            if (roll == 0x3840) {
                if (a.role == 19) {
                    a.role = 15;
                    a.timer = 2;
                } else {
                    a.handling &= ~turned; // ($12081A)
                    a.role = 30;
                    a.timer = 10;
                }
                continue;
            }
            if (roll >= 0x35C0) {
                a.motion |= level_wings;
                rate = 0;
            }
            roll_toward(a, rate);
            steer();
            return;
        }
        case 22: // $1206E0: up past the vertical, then roll level (half loop)
            if (!(a.handling & passed)) {
                if (pitch <= 0x3840) {
                    pitch_toward(a, -pitch_rate(6));
                    steer();
                    return;
                }
                a.handling |= passed;
            }
            if (pitch > 0x3840) {
                pitch_toward(a, -pitch_rate(6));
                steer();
                return;
            }
            a.role = 13;
            continue;
        case 25: { // $12074C: roll and pull together (barrel roll)
            bool more = false;
            if (!(a.handling & turned)) {
                if (!(a.handling & passed)) {
                    if (roll >= 0x3840) more = true;
                    else a.handling |= passed;
                }
                if (!more) {
                    if (roll < 0x3840) more = true;
                    else a.handling |= turned;
                }
            }
            if (!more && roll < 0x3840) {
                finish();
                continue;
            }
            roll_toward(a, roll_rate(7));
            pitch_toward(a, -pitch_rate(7), true);
            steer();
            return;
        }
        case 31:   // $120842: inverted, the nose up to the horizon...
        case 34: { // $1208D4: ...and down again
            const bool up = a.role == 31;
            const bool inverted = roll <= 0x5460 && roll >= 0x1C20;
            bool more = false;
            if (!(a.handling & turned)) {
                const bool before = up == inverted ? pitch > 0x3840 : pitch < 0x3840;
                if (before) more = true;
                else a.handling |= turned;
            }
            if (!more) {
                if (up) more = inverted ? pitch < 0x960 : pitch >= 0x6720;
                else more = inverted ? pitch > 0x6720 : pitch <= 0x960;
            }
            if (more) {
                pitch_toward(a, up ? -pitch_rate(4) : pitch_rate(4));
                steer();
                return;
            }
            pitch_toward(a, 0);
            if (up) {
                // $1208AC: then down, 25 frames on.
                a.handling &= ~turned;
                a.role = 33;
                a.timer = 0x19;
                continue;
            }
            // $120938: and on to the half loop's end.
            a.handling |= passed;
            a.role = 22;
            continue;
        }
        default:
            // (Roles 3-5, 8, 9, 29, 32: the missions' flight.)
            return;
        }
    }
}

} // namespace interceptor::game
