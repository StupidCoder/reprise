#include "fa_18_interceptor_amiga/game/aircraft.hpp"
#include <algorithm>
#include <cstdlib>

namespace interceptor::game {
namespace {

using i16 = std::int16_t;
using i32 = std::int32_t;

constexpr std::uint8_t control_up = 0x10, control_down = 0x20, control_left = 0x08, control_right = 0x04;
constexpr std::uint8_t control_rudder_left = 0x80, control_rudder_right = 0x40; // keys , and .
constexpr std::uint8_t throttle_more = 1, throttle_less = 2;

i16 signed_lookup(const std::vector<i16>& table, int input) {
    return input < 0 ? i16(-table[std::size_t(-input)]) : table[std::size_t(input)];
}

bool gear_down(const Aircraft& a) { return (a.gear & 0x70) == 0; }

// ---------------------------------------------------------------- input

// The control byte and the throttle target ($10DFD2, first half).
void read_controls(Aircraft& a, const FlightControls& c) {
    if (c.recorded) {
        a.controls = *c.recorded;
        return;
    }
    std::uint8_t bits = a.controls & 3;
    if (c.throttle_up) {
        bits = throttle_more;
        a.throttle_target = 0;
    } else if (c.throttle_down) {
        bits = throttle_less;
        a.throttle_target = 0;
    }
    a.controls = std::uint8_t((c.nose_down ? control_up : 0) | (c.nose_up ? control_down : 0) |
                              (c.left ? control_left : 0) | (c.right ? control_right : 0) |
                              (c.rudder_left ? control_rudder_left : 0) |
                              (c.rudder_right ? control_rudder_right : 0) | bits);
    if (a.throttle_target == 0) return;
    // Head for the F-key setting: step up or down, and stop when there.
    const int target = std::max<int>(a.throttle_target, 0);
    const int steps = a.throttle_steps & 15;
    const auto set = [&](std::uint8_t b) { a.controls = std::uint8_t((a.controls & ~3) | b); };
    if (target == a.throttle) {
        if (a.throttle < 120 || (a.motion & flag::afterburner) || !(a.motion & flag::full_power)) {
            a.throttle_target = 0;
            set(0);
        }
    } else if (target < a.throttle) {
        set(std::int8_t(target - a.throttle) < -8 || steps < 2 ? throttle_less : 0);
    } else {
        set(std::int8_t(target - a.throttle) < 9 && steps > 1 ? 0 : throttle_more);
    }
}

// The stick and rudder ramp up while held and drop to zero when let go
// ($10DFD2, second half).
void ramp_inputs(Aircraft& a) {
    const auto ramp = [](std::int8_t now, bool held, bool positive, int step, int limit) -> std::int8_t {
        if (!held) return 0;
        if (positive) return std::int8_t(std::min(now < 0 ? step : now + step, limit));
        return std::int8_t(std::max(now > 0 ? -step : now - step, -limit));
    };
    const std::uint8_t c = a.controls;
    a.pitch_input = ramp(a.pitch_input, c & 0x30, (c & 0x30) == control_up, 1, 20);
    a.rudder_input = ramp(a.rudder_input, c & 0xC0, (c & 0xC0) == control_rudder_right, 1, 20);
    a.roll_input = ramp(a.roll_input, c & 0x0C, (c & 0x0C) == control_right, 3, 60);
}

// ---------------------------------------------------------------- speed

// Drag from the attitude and the thinner air higher up ($106A32).
i16 attitude_drag(const Aircraft& a, const FlightRules& rules) {
    i16 pitch = a.attitude.pitch;
    if (pitch >= 0x3840) pitch = i16(pitch - full_turn);
    pitch = i16(pitch >> 1);
    i16 roll = a.attitude.roll;
    if (roll >= 0x3840) roll = i16(full_turn - roll);
    if (roll >= 0x1C20) roll = i16(0x3840 - roll);
    roll = i16(roll >> 3);
    const i16 turning = i16(std::abs(int(a.pitch_rate)) * 2 + roll - pitch);
    const i32 thin = (rules.ceiling - a.y) >> ((a.motion & flag::afterburner) ? 11 : 13);
    return i16(turning + thin);
}

void drag_toward(Aircraft& a, i16 value) { a.ground_drag = i16(a.ground_drag - (i16(a.ground_drag - value) >> 3)); }

void fade(i16& value, int dead_band, int shift) {
    if (value > dead_band || value < -dead_band) value = i16(value - (value >> shift));
    else value = 0;
}

// The speed change from the climb angle and the sink at this airspeed ($11AC14).
void climb_and_sink(Aircraft& a, const FlightRules& rules, const FlightTables& t) {
    if (!rules.stall_model) {
        a.sink = 0;
        a.climb_speed = 0;
        return;
    }
    const int by_speed = std::min(std::abs(int(a.speed)) >> 7, 30);
    const int tenths = a.attitude.pitch >> 3;
    i16 factor = tenths < 0x709 ? i16(0x100 - (t.sine(900 - tenths) >> 6))
                                : i16(-(0x100 - (t.sine(tenths - 2700) >> 6)));
    i16 wanted = i16((i32(t.climb_by_speed[std::size_t(by_speed)]) * factor) >> 3);
    i16 now = a.climb_speed;
    if (a.motion & flag::on_ground) {
        now = 0;
    } else {
        if (!(a.gear & 0x80)) wanted = i16(wanted - (wanted >> 2));
        if (wanted < now) {
            if (i16(now - wanted) < 0x100) {
                if (now > 0) --now;
                else if (now < 0) now = 0;
            } else {
                now = i16(now - (i16(now - wanted) >> 8));
            }
        } else {
            now = i16(now - (i16(now - wanted) >> 5));
        }
    }
    a.climb_speed = now;
    a.sink = t.sink_by_speed[std::size_t(std::min(std::abs(int(a.airspeed)) >> 7, 30))];
}

// Throttle steps, fuel and the engine speed ($105ED4).
FlightEvents run_engine(Aircraft& a, const FlightRules& rules, const FlightTables& t) {
    FlightEvents events;
    const bool ground = a.motion & flag::on_ground;
    if (a.fuel == 0) {
        a.throttle = 0;
        a.motion &= ~flag::afterburner;
    } else if ((a.controls & 3) == 0) {
        a.throttle_steps = std::uint8_t((a.throttle_steps & 0xF0) + 1);
    } else {
        int steps = (a.throttle_steps & 15) + 1;
        if (steps < 9) a.throttle_steps = std::uint8_t((a.throttle_steps & 0xF0) | steps);
        else steps = 8;
        const int step = steps >> 1;
        if (step != 0 && a.throttle == 0) events.engine_sound = -2; // the engine starts
        if ((a.controls & 3) == throttle_more) {
            a.motion &= ~flag::reverse_armed;
            const int now = a.throttle + step;
            a.throttle = std::int8_t(now);
            if (now >= 120) {
                if (!(a.motion & flag::full_power)) {
                    a.motion |= flag::full_power;
                    a.controls &= ~3;
                    a.throttle_target = 0;
                } else if (!(a.motion & flag::afterburner)) {
                    a.motion |= flag::afterburner;
                    events.engine_sound = -3;
                    events.engine_restart_in = 6;
                }
                a.throttle = 120;
            }
        } else if ((a.controls & 3) == throttle_less) {
            const bool burning = a.motion & flag::afterburner;
            a.motion &= ~flag::full_power;
            if (burning) {
                a.motion &= ~flag::afterburner;
                a.throttle = std::int8_t(a.throttle - 1);
                events.engine_sound = -4;
            } else if (a.throttle < 0) {
                a.throttle = std::int8_t(std::max(a.throttle - step, -0x20));
            } else {
                a.throttle = std::int8_t(a.throttle - 4);
                if (a.throttle < 0) {
                    if (a.surface & 8) {
                        a.throttle = 0;
                    } else if (!(a.motion & flag::reverse_armed)) {
                        a.controls &= ~3;
                        a.throttle = 0;
                        a.motion |= flag::reverse_armed;
                    }
                }
            }
        }
    }

    const i16 speed = a.speed;
    i16 back = i16(-speed); // the original works with the negated speed
    const i16 thrust = i16(a.throttle * 0x100);
    const i16 drag = speed < 0x800 ? i16(0) : attitude_drag(a, rules);
    const auto add_ground_drag = [&](i16 value) {
        drag_toward(a, value);
        if (value != 0) back = i16(back + a.ground_drag);
    };
    if (rules.flying) {
        if ((thrust == 0 && !(a.motion & flag::afterburner)) || a.fuel == 0) {
            // Gliding or rolling with the engine idle.
            if (!ground) {
                if (back < 1) {
                    if (speed > 0x1D4C) back = i16(back + 0x52);
                    if (speed < 0x5DC) {
                        if (speed > 0x1E) back = i16(back + 0x1E);
                    } else {
                        back = i16(back + 0x3C);
                    }
                } else if (back < 0x91) {
                    back = back < 0x3D ? i16(0) : i16(back - 0x3C);
                } else {
                    back = i16(back - 0x52);
                }
            } else {
                drag_toward(a, 0);
                if (back < 1) back = speed < 0x10 ? i16(0) : i16(back + 0xF);
                else back = back < 0x10 ? i16(0) : i16(back - 0xF);
            }
        } else if (!(a.motion & flag::afterburner)) {
            a.fuel -= std::abs(int(thrust)) >> 9;
            if (thrust < 0) {
                if (!ground) {
                    if (back < 0) {
                        back = i16(back + 0x57);
                    } else {
                        const i16 limit = i16(-thrust - (i16(-thrust) >> 2));
                        if (limit < back) back = i16(back - 0x2A);
                        else if (back < limit - 0x2A) back = i16(back + 0x2A);
                    }
                } else {
                    i16 change = 0;
                    if (back < 0) {
                        change = 0x3C;
                    } else {
                        i16 limit = i16((i16(-thrust) >> 4) - (i16(-thrust) >> 6));
                        if (!(a.surface & 4)) limit = i16(limit >> 1);
                        if (limit < back) change = -0x16;
                        else if (back < limit - 0x16) change = 0x16;
                    }
                    add_ground_drag(change);
                }
            } else if (!ground) {
                const i16 part = i16((thrust >> 1) - (thrust >> 3));
                i16 target = i16(part - (part >> 2));
                if (a.handling & 1) target = i16(target >> 2);
                if (target > 0x1FEF) target = 0x1FEF;
                target = i16(target - drag);
                if (a.state & flag::speed_brake) target = i16(target - (target >> 2));
                if (gear_down(a)) target = i16(target - (target >> 2));
                if (speed < target) {
                    if (back < 1) back = i16(back - (target - speed < 0x150 ? 5 : 0x2A));
                    else back = i16(back - 0x78);
                    if (target < -back) back = i16(-target);
                } else if (target + 0x3C < speed) {
                    if (speed < 0x1D4D) back = i16(back + (speed - target < 0x1E0 ? 7 : 0x3C));
                    else back = i16(back + 0x52);
                }
            } else {
                i16 target = i16((thrust >> 3) - (thrust >> 5));
                if (!(a.surface & 8) && a.throttle < 12) target = 0;
                if (!(a.surface & 0x44)) target = i16(target >> 1);
                else if (a.handling & 1) target = 0;
                i16 change = 0;
                if (speed < target) {
                    if (back < 1) change = i16(-((i16(target - speed) >> 8) + ((a.surface & 8) ? 0x42 : 0x16)));
                    else change = -0x78;
                } else if (target + 0xF < speed) {
                    change = 0xF;
                }
                add_ground_drag(change);
            }
        } else {
            // Afterburner.
            a.fuel -= 0x600;
            i16 target = i16(15000 - drag);
            if (target > 18000) target = 18000;
            if (gear_down(a)) target = i16(target - (target >> 2));
            if (a.state & flag::speed_brake) target = i16(target - (target >> 2));
            if (speed < target) {
                if (back < 1) {
                    if (!ground) back = i16(back + (speed < 0x1D4D ? -0x36 : -0x2D));
                    else add_ground_drag(-0x25);
                } else {
                    back = i16(back - 0x78);
                }
            } else if (target + 0x3C < speed) {
                back = i16(back + 0x3C);
            }
        }
    }
    events.overspeed = -back >= 0x20D0;
    // Passing the speed of sound: the boom, once until slower again.
    if (!events.overspeed) {
        a.supersonic = false;
    } else if (!a.supersonic) {
        a.supersonic = true;
        events.engine_sound = -6;
        events.engine_restart_in = 2;
    }

    // Brakes: the wheel brakes, the speed brake on the ground, the gear
    // handle up on the ground (a belly slide), the ground handling.
    i16 limit = 0;
    if ((a.motion & flag::wheel_brakes) == flag::wheel_brakes) {
        fade(a.speed, 7, 3);
    } else if (ground && (a.state & flag::speed_brake) && a.speed >= 0x41) {
        fade(a.speed, 0xF, 4);
    } else if (ground && (a.gear & 0x80)) {
        fade(a.speed, 0x1F, 5);
    } else if (a.handling & 1) {
        fade(a.speed, 3, 2);
    } else if (!(a.surface & 8)) {
        a.speed = i16(-back);
    } else {
        limit = 0x6C;
    }
    if (limit != 0) {
        if (!(a.surface & 8)) a.speed = i16(-back);
        else if (a.throttle < limit && -limit < a.throttle) fade(a.speed, 7, 3);
        else a.speed = i16(-back);
    }
    climb_and_sink(a, rules, t);
    a.airspeed = i16(a.speed + a.climb_speed);
    return events;
}

// ---------------------------------------------------------------- rates

i16 fast_rate_boost(const Aircraft& a, i16 rate) {
    const i16 extra = i16(a.speed >> 7);
    if (rate < 0) return rate > -0x500 ? rate : i16(rate - extra);
    return rate < 0x500 ? rate : i16(rate + extra);
}

void decay(i16& rate, int shift) {
    if (rate < -0xF) rate = i16(rate + (i16(-rate) >> shift));
    else if (rate < 0x10) rate = i16(rate < 0 ? rate + 1 : rate - 1);
    else rate = i16(rate - (rate >> shift));
}

void pitch_toward(Aircraft& a, i16 wanted) {
    if (!(a.motion & flag::on_ground) && a.ground_drag != 0 && wanted <= 0) return;
    if (a.handling & 4) wanted = i16(wanted >> 1);
    a.pitch_rate = fast_rate_boost(a, i16(a.pitch_rate - (i16(a.pitch_rate - wanted) >> 2)));
}

void yaw_toward(Aircraft& a, i16 wanted) {
    wanted = i16((wanted >> 3) + (wanted >> 1));
    if (a.handling & 4) wanted = i16(wanted >> 1);
    a.yaw_rate = fast_rate_boost(a, i16(a.yaw_rate - (i16(a.yaw_rate - wanted) >> 2)));
}

void roll_toward(Aircraft& a, i16 wanted) {
    wanted = i16((wanted >> 3) + (wanted >> 1));
    if (a.handling & 4) wanted = i16(wanted >> 1);
    const int shift = a.type == 0x14 ? 2 : 1;
    a.roll_rate = fast_rate_boost(a, i16(a.roll_rate - (i16(a.roll_rate - wanted) >> shift)));
}

// On the ground the nose settles onto the nose wheel ($105BDE) ...
void settle_nose(Aircraft& a) {
    const i16 pitch = a.attitude.pitch;
    if (pitch <= 0) return;
    if (pitch < 400) {
        a.motion |= flag::nose_wheel_down;
        a.pitch_rate = 0;
    } else {
        const i16 wanted = pitch < 0x3840 ? i16(-0x40) : i16(0x40);
        a.pitch_rate = i16(a.pitch_rate - (i16(a.pitch_rate - wanted) >> (a.speed > 0x6BF ? 1 : 0)));
    }
}

// ... and the wings level ($105CAA).
void level_wings(Aircraft& a) {
    if (a.attitude.roll == 0) a.roll_rate = 0;
    else a.roll_rate = a.attitude.roll < 0x3841 ? i16(-0x20) : i16(0x20);
}

// The stick and rudder become rates ($10557C).
void respond(Aircraft& a, const FlightRules& rules, const FlightTables& t) {
    a.motion &= ~flag::nose_wheel_down;
    if (!rules.flying) return;
    // ($10557C: others respond by `$132834`, unless flown as the player's,
    // +2 bit 0.)
    const auto& table = a.player || (a.motion & 0x0100) ? t.player_response : t.other_response;
    const int pitch_in = a.pitch_input;
    int rudder_in = a.rudder_input, roll_in = a.roll_input;
    if (a.surface & 8) rudder_in = roll_in = 0;
    const bool ground = a.motion & flag::on_ground;

    if (pitch_in != 0) {
        const i16 wanted = signed_lookup(table, pitch_in);
        if (!ground) {
            pitch_toward(a, wanted);
        } else if (!(a.handling & 1) && a.speed > 0x300 &&
                   (a.attitude.pitch == 0 || a.attitude.pitch > 0x6D60) && pitch_in < 0) {
            pitch_toward(a, i16(-table[std::size_t(-pitch_in >> 1)]));
        } else {
            settle_nose(a);
            level_wings(a);
        }
    } else if (ground) {
        settle_nose(a);
    } else if (a.pitch_rate != 0 && a.ground_drag == 0) {
        decay(a.pitch_rate, a.player ? 2 : 1); // ($105B7A: record 0 by 2, others by 1)
    }

    if (rudder_in != 0) {
        if (!(a.surface & 2) && a.airspeed != 0) yaw_toward(a, signed_lookup(table, rudder_in));
        else decay(a.yaw_rate, 1);
    } else if (!(roll_in != 0 && ground) && a.yaw_rate != 0) {
        decay(a.yaw_rate, 1);
    }

    if (roll_in != 0) {
        if (ground) roll_in = std::clamp(roll_in, -10, 10);
        const i16 wanted = signed_lookup(table, roll_in);
        if (!ground) {
            roll_toward(a, i16(-wanted));
        } else if (!(a.surface & 2) && a.airspeed != 0) {
            // On the ground the stick steers; the wings only bank when fast.
            yaw_toward(a, wanted);
            const i16 roll = a.attitude.roll;
            if ((roll < 0x50 || roll > 0x7030) && a.airspeed > 0x360) roll_toward(a, i16(-wanted));
            else if (roll < 0x3840 ? roll_in < 0 : roll_in > 0) a.roll_rate = 0;
            else roll_toward(a, i16(-wanted));
        } else {
            decay(a.yaw_rate, 1);
            decay(a.roll_rate, 1);
        }
    } else {
        if (ground) level_wings(a);
        if (a.roll_rate != 0) decay(a.roll_rate, 1);
    }
}

// A banked aircraft turns and drops its nose ($122526). Returns the extra
// pitch and yaw rates for this tick.
std::array<i16, 2> bank_turn(Aircraft& a, const FlightTables& t) {
    if (a.state & 0x8000) return {0, 0};
    const int tenths = a.attitude.roll >> 3;
    const auto right_side = [&] {
        if (a.bank_side != 0) a.bank_turn = i16(-a.bank_turn);
        a.bank_side = 0;
    };
    const auto left_side = [&] {
        if (a.bank_side == 0) a.bank_turn = i16(-a.bank_turn);
        a.bank_side = -1;
    };
    SineCosine sc;
    if (tenths < 900) {
        right_side();
        sc = sine_cosine(t, tenths);
    } else if (tenths < 1800) {
        left_side();
        sc = sine_cosine(t, 1800 - tenths);
        sc.cosine = i16(-sc.cosine);
    } else if (tenths < 2700) {
        left_side();
        sc = sine_cosine(t, 3600 - tenths);
        sc.cosine = i16(-sc.cosine);
    } else {
        right_side();
        sc = sine_cosine(t, tenths - 1800);
    }
    a.bank_sine = sc.sine;
    a.bank_cosine = sc.cosine;
    i16 pitch_part = sc.sine, yaw_part = sc.cosine;

    // The remembered bank follows the sine an eighth at a time.
    i16 size = i16(std::abs(int(sc.sine)));
    if (sc.cosine < 0) size = i16(-size);
    const i16 diff = i16(a.bank_turn - size);
    i16 step = 0;
    if (diff > 0) step = diff <= 7 ? i16(1) : i16(diff >> 3);
    else if (diff < 0) step = diff >= -7 ? i16(-1) : i16(diff >> 3);
    a.bank_turn = i16(a.bank_turn - step);
    i16 amount = i16(std::abs(int(a.bank_turn)));

    const bool same_side = a.bank_turn < 0 ? yaw_part < 0 : yaw_part > 0;
    const bool level = a.bank_turn < 0 ? yaw_part < 0x100 : yaw_part > -0x100;
    if (same_side) {
        a.bank_side_was = a.bank_side;
    } else if (!level && (a.bank_side ^ a.bank_side_was) >= 0) {
        pitch_part = i16(-pitch_part);
        yaw_part = i16(-yaw_part);
    }
    if (amount > a.bank_turn_limit) amount = a.bank_turn_limit;
    amount = i16(amount - (amount >> 2));
    const auto scale = [&](i16 v) { return i16((i32(i16((-(i32(v) << 7)) >> 13)) * amount) >> 13); };
    pitch_part = scale(pitch_part);
    yaw_part = scale(yaw_part);
    i16 pitch = a.attitude.pitch;
    if (pitch >= 0x3840) pitch = i16(full_turn - pitch);
    if (pitch > 0xE10) return {0, 0};
    const i16 f = t.bank_turn_by_pitch[std::size_t(pitch >> 8)];
    return {i16((i32(pitch_part) * f) >> 8), i16((i32(yaw_part) * f) >> 8)};
}

// Moves an angle a step toward the nearest of 0 and 180° (or toward 0 only).
i16 ease(i16 angle, i16 step_limit) {
    i16 d;
    bool up;
    if (angle > 0x3840) {
        if (angle < 0x5460) { d = i16(angle - 0x3840); up = false; }
        else { d = i16(full_turn - angle); up = true; }
    } else if (angle > 0x1C20) {
        d = i16(0x3840 - angle);
        up = true;
    } else {
        d = angle;
        up = false;
    }
    if (d > 1) d = std::min<i16>(i16(d >> 1), step_limit);
    if (up) {
        angle = i16(angle + d);
        return angle >= full_turn ? i16(0) : angle;
    }
    angle = i16(angle - d);
    return angle < 0 ? i16(0) : angle;
}

void rotate_step(Aircraft& a, i16 pitch_rate, i16 yaw_rate, i16 roll_rate, const FlightRules& rules, const FlightTables& t);

// One tick of attitude: rates, bank turn, rotation, stall ($121BE0, player).
void turn(Aircraft& a, const FlightRules& rules, const FlightTables& t) {
    respond(a, rules, t);
    i16 pitch_rate = a.pitch_rate, yaw_rate = a.yaw_rate;
    const i16 roll_rate = a.roll_rate;
    if (rules.bank_turns) {
        const auto extra = bank_turn(a, t);
        a.bank_pitch = extra[0];
        pitch_rate = i16(pitch_rate + extra[0]);
        yaw_rate = i16(yaw_rate + extra[1]);
    }
    rotate_step(a, pitch_rate, yaw_rate, roll_rate, rules, t);
}

// The rotation and what follows it in the attitude step ($1226B8 on).
void rotate_step(Aircraft& a, i16 pitch_rate, i16 yaw_rate, i16 roll_rate, const FlightRules& rules, const FlightTables& t) {
    Angles next = rotate(t, a.body, pitch_rate, yaw_rate, roll_rate);
    const Angles was = a.attitude;

    if (a.motion & flag::auto_level) {
        a.motion &= ~flag::auto_level;
        if (was.roll != 0x3840) next.roll = ease(was.roll, 0x50);
    }
    const auto toward_level = [](i16 pitch, i16 step) {
        if (pitch > 0x3840) {
            pitch = i16(pitch + step);
            return pitch >= full_turn ? i16(0) : pitch;
        }
        pitch = i16(pitch - step);
        return pitch <= 0 ? i16(0) : pitch;
    };
    if (a.role == 0xA) {
        next.pitch = toward_level(was.pitch, 0x50);
    } else if (a.motion & flag::nose_wheel_down) {
        next.pitch = toward_level(was.pitch, 0xA0);
    } else {
        bool falling = false;
        bool check_vertical = true;
        if (rules.stall_model) {
            if (a.player && (a.handling & 2)) { // ($121FAC: the player's spin only)
                falling = true;
            } else if (!(a.motion & flag::on_ground) && (a.type & 0xF0) == 0x10) {
                // The stall speed rises with the climb angle and the bank.
                i16 p = i16(was.pitch >> 1);
                if (p < 0x1C20) p = i16(std::min<i16>(p, 0xE10) + 0x3840);
                i16 stall = i16(0x4650 - p);
                stall = i16((std::uint32_t(std::uint16_t(stall)) * 0x6C) >> 8);
                const std::uint16_t bank = std::uint16_t(i16(a.bank_sine >> 8));
                stall = i16(stall + i16((std::uint32_t(bank) * std::uint16_t(stall)) >> 6));
                if (stall < a.airspeed) {
                    a.ground_drag = 0;
                    a.handling &= ~4;
                } else {
                    a.handling |= 4;
                    falling = true;
                    if ((a.pitch_rate | a.yaw_rate | a.roll_rate) != 0 && i16(stall >> 1) < a.airspeed) {
                        falling = false;
                    }
                }
            } else {
                a.handling &= ~4;
            }
        }
        if (falling) {
            // Stalled: the nose drops faster and faster.
            a.pitch_rate = 0;
            a.ground_drag = i16(a.ground_drag + (i16(0x50 - a.ground_drag) >> 2));
            i16 p = i16(was.pitch + a.ground_drag);
            if (p >= full_turn) p = 0;
            else if (p < 0x3840 && p > 0x12C0) p = i16(p - 0x50);
            next.pitch = p;
            check_vertical = false;
        }
        if (check_vertical && was.pitch > 0x1770 && was.pitch < 0x5910) {
            // Near the vertical, heading and roll can flip by half a turn;
            // the original then skips the tick and nudges a rate instead.
            bool flipped = false;
            int dh = std::abs(next.heading - was.heading);
            if (dh > 0x4B0 && dh < 0x6BD0) {
                flipped = true;
                dh = std::abs(dh - 0x3840);
            }
            int dr = std::abs(next.roll - was.roll);
            bool skip;
            if (dr > 0x4B0 && dr < 0x6BD0) skip = std::abs(std::abs(dr - 0x3840) - dh) >= 0x320;
            else skip = flipped;
            if (skip) {
                const i16 nudge = t.player_response[6];
                if (!(a.motion & 0x0004)) {
                    a.motion |= 0x0004;
                    if (a.pitch_input != 0) a.pitch_rate = i16(a.pitch_rate + (a.pitch_rate >= 0 ? nudge : -nudge));
                } else {
                    a.yaw_rate = i16(a.yaw_rate + (a.yaw_rate >= 0 ? nudge : -nudge));
                }
                return;
            }
        }
    }
    a.motion &= ~0x0004;
    a.attitude = next;
    a.body = body_matrix(t, next);
    a.view = view_matrix(t, next);
}

// ---------------------------------------------------------------- motion

i16 smooth(i16 target, i16 previous) {
    i16 sum = i16(previous + target);
    const int size = std::abs(int(sum));
    if (size >= 5) sum = i16(sum >> 2);
    else if (size > 2) sum = i16(sum >> 1);
    return i16(sum - previous);
}

// Velocity and height ($106B0E): the velocity turns toward the direction
// of flight a quarter at a time, the sink pulls down, and the ground holds
// the aircraft up at its gear (or belly) height.
void move(Aircraft& a, const FlightRules& rules, const FlightTables& t, FlightEvents& events) {
    const auto target = scaled_vector(t, i16(-a.airspeed), a.view[2], a.view[5], a.view[8]);
    i16 vx = target[0], vz = target[2];
    if (!(a.motion & flag::on_ground)) {
        vx = smooth(target[0], i16(a.vx >> 2));
        vz = smooth(target[2], i16(a.vz >> 2));
    }
    const i16 vy = i16(smooth(target[1], i16(a.vy >> 2)) + a.sink);
    a.vx = -(i32(vx) << 2);
    a.vy = -(i32(vy) << 2);
    a.vz = -(i32(vz) << 2);
    const i32 climb = i32(vy) << 2;

    i32 ground;
    const bool deck = a.surface & 0xC0;
    const i32 deck_base = deck ? i32(a.deck_height) * 0x100 : 0;
    if (!gear_down(a)) ground = std::max(deck_base + 0x2EE, i32(0x2EE));
    else if (a.type == 0x14) ground = std::max(deck_base + 0xA8C, i32(0xA8C));
    else ground = std::max(deck_base + 0x708, i32(0x708));
    if (!deck && (a.type & 0xF0) == 0) ground = -0x200;

    const i32 next = a.y - climb;
    a.y = next;
    if (ground < next) {
        if (!(a.motion & flag::on_ground)) {
            if (a.y > rules.ceiling) a.y = rules.ceiling;
        } else {
            a.motion &= ~flag::on_ground;
            a.motion &= ~flag::touched_down_fast;
            a.surface &= ~(2 | 8);
            events.took_off = true;
        }
        return;
    }
    a.handling &= ~4;
    if (a.type & 0xF0) a.y = ground;
    // What the wheels touch, on contact and every few ticks rolling.
    if (!deck && rules.world && (!(a.motion & flag::on_ground) || (a.timer & 7) == 0)) {
        const double wx = a.x / 256.0, wz = a.z / 256.0;
        a.surface = std::uint8_t((a.surface & ~6) | (rules.world->over_water(wx, wz) ? 2 : 0) |
                                 (rules.world->on_runway(wx, wz) ? 4 : 0));
        if ((a.surface & 2) && gear_down(a)) {
            a.gear |= 0x80;
            a.handling |= 1;
        }
    }
    events.in_water = a.surface & 2;
    if (!(a.state & flag::crashed)) {
        if (!(a.motion & flag::on_ground)) {
            a.motion |= flag::on_ground;
            a.ground_drag = 0;
            events.touched_down = true;
            events.touchdown_rate = vy;
            if (vy < 0x240) {
                if (a.airspeed != 0) {
                    a.speed = a.airspeed;
                    events.engine_sound = -2;
                    events.tyres_volume = vy < 0x181 ? 0x1E : 0x28;
                }
                if (((a.surface & 2) || !gear_down(a)) && a.airspeed >= 0x3C0) a.motion |= flag::touched_down_fast;
            } else {
                a.state |= flag::crashed;
                events.crashed = true;
            }
        }
    }
    a.vy = 0;
    if (a.airspeed < 0x3C0) a.motion &= ~flag::touched_down_fast;
}

// The gear's travel (high nibble of `gear`, $11A77C) and the other
// animation in the low nibble.
void move_gear(Aircraft& a) {
    std::uint8_t low = a.gear & 0x0F, high = a.gear & 0xF0;
    if (low != 0) {
        if (low > 6) {
            --low;
            if (low <= 6) low = 1;
        } else if (++low > 6) {
            if (a.motion & 0x0020) {
                low = 0;
                a.motion &= ~0x0020;
            } else {
                low = 6;
            }
        }
        a.gear = std::uint8_t(high | low);
    }
    if (high & 0x80) {
        high &= 0x7F;
        if (high < 0x60) a.gear = std::uint8_t((high + 0x10) | 0x80);
    } else if (high > 0) {
        a.gear = std::uint8_t(high - 0x10);
    }
}

} // namespace

FlightEvents fly(Aircraft& a, const FlightControls& controls, const FlightRules& rules, const FlightTables& t) {
    // $1183E2 (the frame's player checks, before the flight): faster than
    // $1200 with the gear down warns once.
    bool gear_speed_warning = false;
    if (a.speed >= 0x1200 && gear_down(a)) {
        if (!a.gear_speed_warned) gear_speed_warning = a.gear_speed_warned = true;
    } else {
        a.gear_speed_warned = false;
    }
    read_controls(a, controls);
    if (a.player) a.timer = std::int16_t(a.timer - 2); // (others: the object step, $1179A4)
    if ((a.gear & 0x0F) == 0) ramp_inputs(a);
    FlightEvents events;
    if (!(a.handling & 2) && (a.state & flag::moves)) events = run_engine(a, rules, t);
    events.gear_speed_warning = gear_speed_warning;
    // The fuel warnings ($1055F8, while the flight runs): exhausted (the
    // afterburner goes out), critical below 1,800 lb, low below 3,000 lb.
    if (rules.flying) {
        if (a.fuel <= 0) {
            a.fuel = 0;
            if (a.fuel_warning != fuel::exhausted) a.motion &= ~flag::afterburner;
            a.fuel_warning = fuel::exhausted;
        } else if (a.fuel < 0x70800) {
            a.fuel_warning = fuel::critical;
        } else if (a.fuel < 0xBB800) {
            a.fuel_warning = fuel::low;
        } else {
            a.fuel_warning = 0;
        }
    }
    // The stall warning ($10557C's end, before this tick's stall model):
    // on while stalled in the air with fuel, its beep when it comes on.
    if (!(a.state & 0x8000)) {
        if (!rules.flying || a.fuel == 0 || !(a.handling & 4) || (a.motion & flag::on_ground)) {
            a.stall_warning = false;
        } else if (!a.stall_warning) {
            a.stall_warning = true;
            events.stall_warning = true;
        }
        turn(a, rules, t);
    }
    if (rules.flying) {
        move(a, rules, t, events);
    } else {
        a.vx = a.vy = a.vz = 0;
    }
    if (a.state & flag::moves) {
        constexpr i32 edge = 0x1FFFFFFF;
        a.x = std::clamp(a.x + a.vx, 0, edge);
        a.z = std::clamp(a.z + a.vz, 0, edge);
    }
    move_gear(a);
    return events;
}

void turn_object(Aircraft& a, std::array<std::int16_t, 3> rates, const FlightRules& rules, const FlightTables& t) {
    // $121E74: each rate halfway to the new one, twice over.
    const auto toward = [](i16 old, i16 r) { return i16(i16(r + i16(i16(old + r) >> 1)) >> 1); };
    a.pitch_rate = toward(a.pitch_rate, rates[0]);
    a.yaw_rate = toward(a.yaw_rate, rates[1]);
    a.roll_rate = toward(a.roll_rate, rates[2]);
    rotate_step(a, a.pitch_rate, a.yaw_rate, a.roll_rate, rules, t);
}

void coast(Aircraft& a, const FlightRules& rules, const FlightTables& t) {
    FlightEvents events;
    move(a, rules, t, events);
    constexpr i32 edge = 0x1FFFFFFF;
    a.x = std::clamp(a.x + a.vx, 0, edge);
    a.z = std::clamp(a.z + a.vz, 0, edge);
}

std::array<std::int32_t, 2> place_position(const amiga::LoadedImage& m, int place) {
    const std::uint32_t at = 0x137CF6 + std::uint32_t(16 * place);
    const std::uint32_t pair = 0x110732 + std::uint32_t(4 * m.s16(at + 4));
    return {m.s16(at) * 0x1000000 + (m.s16(at + 6) + m.s16(pair)) * 0x400 + m.s16(at + 10) * 0x10,
            m.s16(at + 2) * 0x1000000 + (m.s16(at + 8) + m.s16(pair + 2)) * 0x400 + m.s16(at + 12) * 0x10};
}

Aircraft start_at_place(const amiga::LoadedImage& m, const FlightTables& t, int place) {
    const auto [x, z] = place_position(m, place);
    Aircraft a;
    a.x = x;
    a.z = z;
    a.y = 0x708;
    a.attitude = {0, std::int16_t(m.s16(0x137CF6 + std::uint32_t(16 * place) + 14) * 80), 0};
    a.body = body_matrix(t, a.attitude);
    a.view = view_matrix(t, a.attitude);
    a.state = 0x11C8;   // $10D8D8: $148 | $1080, then $10D51E's mask
    a.motion = flag::on_ground;
    a.throttle = 9;     // $13B49F
    a.throttle_steps = 1;
    a.fuel = 6400000;   // 25,000 lb
    a.bank_turn_limit = 0x1400;
    a.type = 0x11;      // the F/A-18
    return a;
}

} // namespace interceptor::game
