#include "fa_18_interceptor_amiga/game/autopilot.hpp"

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

// $121D4C (the attitude step, after the input step, for others in roles 6
// and 10): the stick rolls the wings back toward level, let go within 3.5
// degrees; the next frame flies by it.
void Autopilot::level_out(Aircraft& a, std::int16_t roll_before) {
    if (a.player || (a.role != 6 && a.role != 10)) return;
    const int roll = roll_before;
    std::uint8_t side = 0;
    if (roll < 0x3841) side = roll < 0x119 ? 0 : roll_right;
    else side = roll > 0x6F67 ? 0 : roll_left;
    a.controls = std::uint8_t((a.controls & throttle_bits) | side);
    a.state |= 0x2000;
}

namespace {
} // namespace

Autopilot::Autopilot(const amiga::LoadedImage& program) : program_(program) {}

void Autopilot::fly(Aircraft& a, std::uint8_t& pilot, const PilotWorld& world) const {
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
    // $120A74: a maneuver is over: the trainer (+2 bit 0) waits a moment
    // (role 10) and lets go (role 0, $120B58).
    const auto finish = [&] {
        if (!(a.motion & as_player) || world.practice_ready) {
            // (Others go back to their flight: the missions' roles.)
            return;
        }
        a.role = 10;
        a.timer = world.practice ? 0x24 : 0x3C;
    };
    const auto let_go = [&] {
        a.role = role::none;
        a.controls &= throttle_bits;
    };

    // (The height checks of `$120BC6` are for others; the trainer, +2 bit 0,
    // skips them.) A handler that moves on runs the next role in the same
    // frame, as the original falls through.
    for (int guard = 0; guard < 6; ++guard) {
        const int pitch = pitch_of(a), roll = roll_of(a);
        switch (a.role) {
        case 0:
        case 1:
            // $120C66: the trainer has no target point (+$2C -1): nothing to
            // do, the pilot's bits go ($120B66).
            pilot = 0;
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
                if (a.role == 10) continue;
                return;
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
                if (a.role == 10) continue;
                return;
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
                    if (a.role == 10) continue;
                    return;
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
                if (a.role == 10) continue;
                return;
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
                if (a.role == 10) continue;
                return;
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
            // (Roles 3-6, 8, 9, 29, 32, 35-38: the missions' flight.)
            return;
        }
    }
}

} // namespace interceptor::game
