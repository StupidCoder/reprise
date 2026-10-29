#include "fa_18_interceptor_amiga/game/sounds.hpp"
#include <algorithm>
#include <cstdlib>

namespace interceptor::game {
namespace {

using i16 = std::int16_t;

constexpr int engine_left = 0, engine_right = 1, effects = 2, beeps = 3; // channels
constexpr int glide_ticks = 0x20;

} // namespace

Sounds::Sounds(SoundDriver& driver)
    : driver_(driver), cockpit_engine_(driver.has(1)), outside_engine_(driver.has(3)), tyres_(driver.has(5)),
      wind_(driver.has(9)), gear_(driver.has(11)), noise_(driver.has(6)), thump_(driver.has(12)) {}

void Sounds::second() {
    if (gear_seconds_ != 0) {
        gear_seconds_ = std::uint8_t((gear_seconds_ & 0x7F) - 1);
        if (std::int8_t(gear_seconds_) < 0) gear_seconds_ = 0;
    }
    if (beep_lock_ >= 0) --beep_lock_;
}

void Sounds::gear_moved() {
    if (gear_) gear_seconds_ = 0x83;
    else refuse();
}

void Sounds::update(const Aircraft& a) {
    ++frame_; // ($101748, every frame)
    if (!active_) return;

    // The gear's sound: started by the key, stopped by the second counter.
    if (gear_seconds_ & 0x80) {
        gear(outside_ ? 5 : 10); // ($104B08)
        gear_seconds_ &= 0x7F;
    } else if (gear_seconds_ == 1) {
        stop_gear();
        gear_seconds_ = 0;
    }

    // One warning beep, the first raised in this order; while the beeps
    // are locked the warnings wait.
    if (warnings_ != 0 && beep_lock_ <= 0) {
        const auto w = warnings_;
        if (w & warning::spin) {
            beep(0xA0, 0x10, 8, 0xC8, 0x10, 5, 3, 4, 4);
        } else if ((a.gear & 0x0F) == 0) {
            if (w & warning::crash) beep(0x144, 0xA, 8, 0xC8, 0xA, 5, 3, 1, 4);
            else if (w & warning::bit_01) {
                beep(0x7C, 0x10, 8, 0xB4, 0x10, 5, 3, 3, 4);
                beep_lock_ = 3;
            } else if (w & warning::message) beep(0xE5, 3, 5, 0xE5, 3, 5, 1, 0, 1);
            else if (w & warning::bit_100) beep(0xC0, 4, 9, 0xC8, 0, 3, 1, 0, 1);
            else if (w & warning::bit_10) beep(0x1F4, 0xF, 5, 0x1F4, 0xF, 5, 3, 1, 5);
            else if (w & warning::bit_02) beep(0x1F4, 0xA, 5, 0x82, 0, 1, 3, 3, 5);
            else if (w & warning::stall) beep(0x190, 8, 5, 0x14A, 0, 1, 3, 2, 5);
            else if (w & warning::bit_800) beep(0x1F4, 0xA, 5, 0x82, 0, 1, 3, 1, 5);
            else if (w & warning::message_off) driver_.stop(beeps);
        }
        warnings_ = 0;
    }

    // The engine, every fourth frame or at once for an event.
    const auto e = event_;
    if (e != EngineSound::none || (frame_ & 3) == 0) {
        if ((a.type & 0xF0) == 0x30) {
            wind(wind_volume(a));
        } else {
            i16 throttle = i16(std::abs(int(a.throttle)));
            constexpr int full = 0x3D;
            int volume = engine_volume(a, throttle);
            if (volume == 0) {
                wind(wind_volume(a));
            } else {
                // Turning works the engine: the larger of the roll and
                // pitch rates raises the pitch (lowers it at full power).
                i16 roll = i16(std::abs(int(i16(a.roll_rate >> 4))));
                const i16 pitch = i16(std::abs(int(i16(a.pitch_rate >> 4))));
                if (roll < pitch) roll = i16(roll + i16(a.pitch_rate >> 4));
                if (throttle >= 120) {
                    throttle = i16(throttle - roll);
                } else {
                    throttle = i16(throttle + roll);
                    if (throttle > 120) throttle = 120;
                }
                const bool burning = a.motion & flag::afterburner;
                if (outside_) {
                    // $104F14: from outside only the textegn pair, its
                    // pitch a little lower, its volume a quarter less.
                    volume -= volume >> 2;
                    constexpr int loud = 0x3F;
                    const int period = i16(0x320 - 2 * throttle - (throttle >> 1) - (throttle >> 2));
                    switch (e) {
                    case EngineSound::start: engine_outside(period, volume); break;
                    case EngineSound::afterburner_on: engine_outside(period + 0x20, loud); break;
                    case EngineSound::afterburner_off: engine_outside(period + 0x20, volume); break;
                    case EngineSound::catapult:
                        if (thump_) thump(loud);
                        else engine_outside(period + 0x16, loud);
                        break;
                    case EngineSound::boom: engine_outside(period + 0x16, std::min(loud * 2, 0x3F)); break;
                    default: glide(period, volume, glide_ticks); break;
                    }
                    event_ = EngineSound::none;
                    if (restart_in_ > 0 && --restart_in_ == 0 && !burning) event_ = EngineSound::start;
                    return;
                }
                if (volume < 0x28) volume = 0x28;
                const int period = i16(0x310 - 2 * throttle - (throttle >> 2));
                switch (e) {
                case EngineSound::start:
                    if (!burning) engine_cockpit(period, volume);
                    else engine_outside(period, volume >> 2);
                    break;
                case EngineSound::afterburner_on: engine_outside(period + 0x23, full >> 1); break;
                case EngineSound::afterburner_off: engine_cockpit(period + 0x20, volume); break;
                case EngineSound::catapult:
                    if (thump_) {
                        thump(full);
                        restart_in_ = 0;
                    } else {
                        engine_outside(period + 0x16, full);
                    }
                    break;
                case EngineSound::boom: engine_outside(period + 0x16, std::min(full * 2, 0x28)); break;
                default:
                    if (restart_in_ != 0) engine_glide(period, cockpit_engine_ ? 10 : 0x28, glide_ticks);
                    else if (!burning || !cockpit_engine_) engine_glide(period, volume, glide_ticks);
                    else engine_glide(period, (volume >> 2) + (volume >> 3), glide_ticks);
                    break;
                }
            }
        }
        event_ = EngineSound::none;
    }
    if (restart_in_ > 0 && --restart_in_ == 0 && !(a.motion & flag::afterburner)) event_ = EngineSound::start;
}

// $10530E: the engine's volume from the throttle (0: the wind instead).
int Sounds::engine_volume(const Aircraft& a, int throttle) const {
    if (throttle == 0) return (a.motion & flag::on_ground) && a.airspeed != 0 ? 12 : 0;
    if (restart_in_ != 0) return 0x3F;
    int v = throttle > 8 ? 0x3F - (i16(0x78 - throttle) >> 2) : 0x3C - (i16(0x78 - throttle) >> 1);
    if (!outside_) return blackout_ ? v >> 1 : v; // ($1054A8)
    // $105354: from outside, quieter by the camera's place (ahead of the
    // aircraft loudest) and distance, a quarter less without the
    // afterburner.
    const auto most = [](int x) { return (x >> 1) + (x >> 3); }; // $1054E6
    const int side = view_position_ > 4 ? std::max(0, 8 - view_position_) : view_position_;
    if (side <= 1) v = most(v);
    else if (side == 2) v -= v >> 2;
    else if (side == 3) v -= v >> 3;
    if (view_distance_ == 1) v -= v >> 3;
    else if (view_distance_ == 2) v -= v >> 2;
    else if (view_distance_ == 3) v = most(v);
    else if (view_distance_ == 4) v >>= 1;
    if (!(a.motion & flag::afterburner)) v -= v >> 2;
    return v > 0 ? v : 1;
}

// $105502: the wind's volume from the airspeed.
int Sounds::wind_volume(const Aircraft& a) const {
    if ((a.type & 0xF0) == 0x30) return 0x10;
    int v = std::abs(int(a.airspeed)) >> 9;
    if (v > 0x3F) v = 0x3F;
    v >>= 1; // (from the cockpit)
    return v == 0 ? 1 : v;
}

void Sounds::engine_cockpit(int period, int volume) {
    if (!cockpit_engine_) {
        engine_outside(period, volume >> 2);
        return;
    }
    auto& left = driver_.slot(0);
    auto& right = driver_.slot(1);
    left.volume_step = right.volume_step = 0;
    left.period_step = right.period_step = 0;
    left.period = period << 16;
    right.period = (period + 2) << 16;
    driver_.play(0, engine_left, volume);
    driver_.play(1, engine_right, volume);
}

void Sounds::engine_outside(int period, int volume) {
    if (!outside_engine_) return;
    auto& left = driver_.slot(2);
    auto& right = driver_.slot(3);
    left.volume_step = right.volume_step = 0;
    left.period_step = right.period_step = 0;
    left.period = period << 16;
    right.period = (period + 2) << 16;
    driver_.play(2, engine_left, volume);
    driver_.play(3, engine_right, volume);
}

void Sounds::engine_glide(int period, int volume, int ticks) {
    glide(period, cockpit_engine_ ? volume : volume >> 2, ticks);
}

// $109EFA: both engine channels slide to the period and volume over
// `ticks` ticks (the step from the left one's values).
void Sounds::glide(int period, int volume, int ticks) {
    record({0x109EFA, period, volume, ticks});
    if (!outside_engine_) return;
    const int l = driver_.playing(engine_left), r = driver_.playing(engine_right);
    if (r < 0 || l < 0) return;
    auto& left = driver_.slot(l);
    auto& right = driver_.slot(r);
    const std::int32_t period_step = ((period << 16) - left.period) / ticks;
    right.period_step = left.period_step = period_step;
    right.period_ticks = left.period_ticks = ticks;
    const std::int32_t volume_step = ((volume << 16) - left.volume) / ticks;
    right.volume_step = left.volume_step = volume_step;
    right.volume_ticks = left.volume_ticks = ticks;
}

void Sounds::wind(int volume) {
    if (!wind_) {
        driver_.stop(engine_left);
        driver_.stop(engine_right);
        return;
    }
    driver_.slot(8).period = (driver_.random_bits(6) + 0x168) << 16;
    driver_.slot(9).period = (driver_.random_bits(6) + 0x168) << 16;
    driver_.play(8, engine_left, volume);
    driver_.play(9, engine_right, volume);
}

void Sounds::gear(int volume) {
    record({0x10A1E6, volume});
    if (!gear_) return;
    driver_.stop(effects);
    driver_.slot(11).wait = 0;
    driver_.start_script(11, SoundDriver::gear_script);
    driver_.play(11, effects, volume);
}

void Sounds::stop_gear() {
    record({0x10A24C});
    driver_.stop(effects);
}

void Sounds::stop_all() {
    for (int c = 0; c < 4; ++c) driver_.stop(c);
}

// The crash's noise: `volume` falling to 0 over `ticks` ticks at a low
// random pitch. Without the noise slot the engine's boom stands in.
void Sounds::noise(int volume, int ticks) {
    record({0x10A0DC, volume, ticks});
    if (!noise_) {
        event_ = EngineSound::boom;
        restart_in_ = 2;
        return;
    }
    driver_.stop(effects);
    driver_.script_value(SoundDriver::noise_script, 0) = volume << 16;
    driver_.script_value(SoundDriver::noise_script, 1) = -(volume << 16) / ticks;
    driver_.script_value(SoundDriver::noise_script, 2) = ticks;
    driver_.start_script(6, SoundDriver::noise_script);
    driver_.slot(6).period = (driver_.random_bits(11) * 4 + 0x231E) << 16;
    driver_.play(6, effects, 0);
}

// The tyres at a touch-down: once, the pitch falling.
void Sounds::tyres(int volume) {
    record({0x10A18C, volume});
    if (!tyres_) return;
    auto& s = driver_.slot(5);
    s.period = 0x136 << 16;
    s.period_step = -2 << 16;
    s.repeat = 1;
    driver_.play(5, effects, volume);
}

// $10A258: the catapult's thump, with the slot's own script.
void Sounds::thump(int volume) {
    record({0x10A258, volume});
    if (!thump_) return;
    driver_.stop(effects);
    driver_.script_value(SoundDriver::thump_script, 3) = volume << 16;
    driver_.script_value(SoundDriver::thump_script, 4) = -((volume - 1) << 16) / 60;
    driver_.start_script(12, SoundDriver::thump_script);
    driver_.slot(12).period = (driver_.random_bits(3) + 0x8C) << 16;
    driver_.play(12, effects, 0);
}

// The beeps: `groups` times (`repeats` times tone 1 then tone 2, each
// for its ticks), then a pause; tone volumes 0 are rests.
void Sounds::beep(int period1, int volume1, int ticks1, int period2, int volume2, int ticks2, int repeats,
                  int groups, int pause) {
    record({0x10A042, period1, volume1, ticks1, period2, volume2, ticks2, repeats, groups, pause});
    if (!driver_.has(4)) return;
    driver_.stop(beeps);
    driver_.script_value(SoundDriver::beep_script, 0) = groups;
    driver_.script_value(SoundDriver::beep_script, 1) = repeats;
    driver_.script_value(SoundDriver::beep_script, 2) = period1 << 16;
    driver_.script_value(SoundDriver::beep_script, 3) = volume1 << 16;
    driver_.script_value(SoundDriver::beep_script, 4) = ticks1;
    driver_.script_value(SoundDriver::beep_script, 5) = period2 << 16;
    driver_.script_value(SoundDriver::beep_script, 6) = volume2 << 16;
    driver_.script_value(SoundDriver::beep_script, 7) = ticks2;
    driver_.script_value(SoundDriver::beep_script, 9) = pause;
    driver_.start_script(4, SoundDriver::beep_script);
    driver_.play(4, beeps, 0);
}

void Sounds::click() {
    if (beep_lock_ > 0) return;
    beep(0x12C, 2, 1, 0x12C, 2, 1, 1, 1, 1);
}

void Sounds::warn_beep(int groups) {
    beep(0x12C, 4, 1, 0x12C, 4, 1, 1, groups, groups);
}

void Sounds::menu_key() {
    if (driver_.tune_playing()) beep(0x12C, 2, 1, 0x12C, 2, 1, 1, 2, 2);
    else refuse();
}

void Sounds::start_tune() {
    if (!driver_.has_tune() || driver_.tune_playing()) return;
    stop_all();
    driver_.play(13, 0, 0x3F);
    driver_.play(14, 1, 0x3F);
    driver_.set_tune_playing(true);
}

void Sounds::stop_tune() {
    driver_.set_tune_playing(false);
    driver_.stop(0);
    driver_.stop(1);
    driver_.set_master_volume(0x3F);
}

void Sounds::ambience(int volume) {
    if (wind_) wind(volume);
    else engine_outside(0x300, volume);
}

void Sounds::refuse() {
    if (beep_lock_ > 0) return;
    beep(0x12C, 4, 1, 0x12C, 4, 1, 1, 2, 2);
}

} // namespace interceptor::game
