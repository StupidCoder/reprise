#include "fa_18_interceptor_amiga/game/module.hpp"
#include "fa_18_interceptor_amiga/data/assets.hpp"
#include "fa_18_interceptor_amiga/data/preview.hpp"
#include "fa_18_interceptor_amiga/data/world.hpp"
#include "fa_18_interceptor_amiga/game/aircraft.hpp"
#include "fa_18_interceptor_amiga/game/cockpit.hpp"
#include "fa_18_interceptor_amiga/game/messages.hpp"
#include "fa_18_interceptor_amiga/game/scene.hpp"
#include "fa_18_interceptor_amiga/game/sound_driver.hpp"
#include "fa_18_interceptor_amiga/game/sounds.hpp"
#include "fa_18_interceptor_amiga/game/text_screen.hpp"
#include "ui/vhs.hpp"
#include <optional>
#include <algorithm>
#include <cmath>

namespace interceptor::game {
namespace {

constexpr int screen_width = 320, screen_height = 200;
constexpr int ticks_per_second = 15;
// The crash's flash ($1082EC): 40 times palette 1 (all $FB2) for one
// vertical blank, then the view's own for one: 80 blanks, 24 ticks.
constexpr int flash_blanks = 80;
constexpr int flash_ticks = flash_blanks * ticks_per_second / 50;
constexpr int aircraft_given = 5;     // free flight's aircraft ($10217A)
constexpr int end_text_fa18 = 99;     // "HEY ROOKIE, F/A-18'S DON'T GROW ON TREES"
constexpr int menu_palette = 0;

// Keys of the C64-style matrix the host gives (GameInput::c64_keys).
namespace key {
constexpr int backspace = 0, g = 26, b = 28, minus = 43, period = 44, comma = 47, equals = 53;
constexpr int digits[10] = {35, 56, 59, 8, 11, 16, 19, 24, 27, 32}; // 0 .. 9
} // namespace key

// A place from the free-flight start table ($137CF6, 16 bytes each: coarse
// cell x and z, slot, offsets x and z (x 1024), fine offsets x and z (x 16),
// heading in degrees), as $10D51E puts the aircraft there.
Aircraft start_on_runway(const amiga::LoadedImage& m, const FlightTables& t, int place) {
    const std::uint32_t at = 0x137CF6 + std::uint32_t(16 * place);
    const int slot = m.s16(at + 4);
    const std::uint32_t pair = 0x110732 + std::uint32_t(4 * slot);
    Aircraft a;
    a.x = m.s16(at) * 0x1000000 + (m.s16(at + 6) + m.s16(pair)) * 0x400 + m.s16(at + 10) * 0x10;
    a.z = m.s16(at + 2) * 0x1000000 + (m.s16(at + 8) + m.s16(pair + 2)) * 0x400 + m.s16(at + 12) * 0x10;
    a.y = 0x708;
    a.attitude = {0, std::int16_t(m.s16(at + 14) * 80), 0};
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

// Free flight as the original runs it from its screen procedures:
// flying ($102EFE); after a crash the flash and the explosion's noise,
// then the aircraft back at the start ($1038D8, $103980) and held a few
// frames ($1039C2); after the last of the five aircraft the end screen
// ($1039F0, $103A36), then (the menu not being rebuilt) free flight anew.
class FreeFlight final : public app::GameModule {
public:
    explicit FreeFlight(const std::filesystem::path& content)
        : assets_(data::load_assets(content)), world_(data::read_world(assets_.program)),
          tables_(read_flight_tables(assets_.program)), scene_(assets_, world_), cockpit_(assets_),
          palette_(assets_.palettes.at(data::flight_palette)), messages_(assets_.program), driver_(assets_),
          sounds_(driver_) {
        begin();
    }

    double step_seconds() const override { return 1.0 / ticks_per_second; } // the original's frame cap ($119876)
    double pixel_aspect() const override { return 1.0; }

    void tick(const app::GameInput& input) override {
        before_ = aircraft_;
        if (flash_left_ > 0) { // (the frame's end, held by the flash)
            --flash_left_;
            return;
        }
        if (++second_ticks_ >= ticks_per_second) {
            second_ticks_ = 0;
            sounds_.second();
        }
        --countdown_; // $13ADC6, every frame ($101748)
        run_procedure();
        messages_.clock();
        if (phase_ == Phase::end_wait || phase_ == Phase::end_text) {
            keys_was_ = input.c64_keys;
            sounds_.update(aircraft_);
            return;
        }
        if (flying_) fly_tick(input);
        else keys_was_ = input.c64_keys;
        sounds_.warn(messages_.update(aircraft_.stall_warning));
        sounds_.update(aircraft_);
    }

    platform::Image picture() const override {
        const int width = screen_width + 2 * margin_;
        IndexedImage frame(width, screen_height);
        platform::Image image(width, screen_height);
        if (flash_left_ > 0) {
            // Palette 1 on the odd blanks: every colour $FB2.
            const double ticks = flash_ticks - flash_left_ + (smooth_ ? frame_fraction_ : 0.0);
            const int blank = int(ticks * 50 / ticks_per_second);
            if (blank % 2 == 0) {
                ui_fill(image, assets_.palettes.at(1)[0]);
                return image;
            }
        }
        const data::Palette* palette = &palette_;
        if (phase_ == Phase::end_wait || phase_ == Phase::end_text) {
            // The cleared screen, then the end text in the menu palette.
            if (text_) {
                palette = &assets_.palettes.at(menu_palette);
                text_->draw(frame, margin_);
            }
        } else {
            double t = 1;
            if (smooth_ && flash_left_ == 0) t = frame_fraction_;
            scene_.draw(frame, width / 2 - 1, Cockpit::view_bottom, camera(before_, aircraft_, t));
            auto readings = read_cockpit(aircraft_);
            readings.message = messages_.shown();
            readings.message_colour = messages_.colour();
            cockpit_.draw(frame, margin_, readings);
        }
        for (int y = 0; y < screen_height; ++y) {
            for (int x = 0; x < width; ++x) {
                image.put(x, y, data::to_color((*palette)[frame.pixels[std::size_t(y * width + x)] & 31]));
            }
        }
        return image;
    }
    void set_frame_fraction(double fraction) override { frame_fraction_ = std::clamp(fraction, 0.0, 1.0); }
    int vhs_level() const override { return vhs_; }

    void audio(std::span<float> stereo, int sample_rate) override {
        // (The host hands in the last frame's buffer: Paula adds to it.)
        std::fill(stereo.begin(), stereo.end(), 0.0f);
        if (sound_) driver_.render(stereo, sample_rate);
        else {
            std::vector<float> scratch(stereo.size());
            driver_.render(scratch, sample_rate);
        }
    }

    std::vector<app::GameOption> options() const override {
        return {{"Starting place", {"Runway 1", "Runway 2", "Runway 3"}, place_,
                 "Where free flight starts: the three runways of the original's map (the carrier is not "
                 "rebuilt yet). Takes effect at the next start."},
                {"Widescreen", {"Off", "On"}, widescreen_ ? 1 : 0,
                 "Shows more of the world left and right of the original's view, at the same scale. The "
                 "game plays the same."},
                {"Smooth motion", {"Off", "On"}, smooth_ ? 1 : 0,
                 "The view moves at the display's own rate instead of the original's 15 steps a second. "
                 "The game plays the same."},
                {"VHS look", {"Off", "Light", "Medium", "Heavy", "Broken"}, vhs_,
                 "A worn video tape's colour bleed and noise."},
                {"Sound", {"Off", "On"}, sound_ ? 1 : 0, "The original's engine, wind, gear, tyre, crash "
                 "and warning sounds."}};
    }
    void set_option(std::size_t index, int value) override {
        if (index == 0) place_ = std::clamp(value, 0, 2);
        if (index == 1) {
            widescreen_ = value != 0;
            set_canvas(canvas_width_, canvas_height_);
        }
        if (index == 2) smooth_ = value != 0;
        if (index == 3) vhs_ = std::clamp(value, 0, ui::vhs_levels - 1);
        if (index == 4) sound_ = value != 0;
    }
    void set_canvas(int width, int height) override {
        canvas_width_ = width;
        canvas_height_ = height;
        margin_ = 0;
        if (!widescreen_ || width <= 0 || height <= 0) return;
        const double wide = std::clamp(double(screen_height) * width / height, double(screen_width),
                                       screen_height * 21.0 / 9.0);
        margin_ = std::max(0, int(wide - screen_width) / 2);
    }

private:
    enum class Phase {
        flying,       // $102EFE (also while held after a restart)
        crashed,      // $1038D8: the first frame after the flash
        restarted,    // $103980
        held,         // $1039C2: until the countdown runs out
        end_wait,     // $1039F0: the cleared screen
        end_text,     // $103A36: the end text typed out
    };

    static void ui_fill(platform::Image& image, data::Colour colour) {
        const auto c = data::to_color(colour);
        for (int y = 0; y < image.height; ++y)
            for (int x = 0; x < image.width; ++x) image.put(x, y, c);
    }

    // A new free flight: five aircraft, the first on the runway.
    void begin() {
        aircraft_left_ = aircraft_given;
        text_.reset();
        sounds_.set_active(true);
        restart();
        flying_ = true;
        phase_ = Phase::flying;
    }

    // $10D51E: the aircraft at the chosen start; the engine sound starts.
    void restart() {
        aircraft_ = start_on_runway(assets_.program, tables_, place_);
        // The wheels' surface, as the first contact would find it.
        aircraft_.surface = std::uint8_t(world_.on_runway(aircraft_.x / 256.0, aircraft_.z / 256.0) ? 4 : 0);
        before_ = aircraft_;
        sounds_.event(EngineSound::start);
        messages_.clear();
        sounds_.clear_warnings();
    }

    // The screen procedure of this frame.
    void run_procedure() {
        switch (phase_) {
        case Phase::flying:
            if (aircraft_.state & flag::crashed) {
                // $102EFE: the crash. The flight stops, every sound stops
                // but the explosion's noise, and at the frame's end the
                // screen flashes.
                aircraft_.state &= ~flag::crashed;
                flying_ = false;
                sounds_.stop_all();
                sounds_.noise(0x3F, 0x78);
                flash_left_ = flash_ticks;
                phase_ = Phase::crashed;
            }
            break;
        case Phase::crashed:
            // $1038D8 / $103902: the next aircraft, or the end.
            restart();
            if (--aircraft_left_ > 0) {
                phase_ = Phase::restarted;
            } else {
                countdown_ = 5;
                sounds_.set_active(false);
                phase_ = Phase::end_wait;
            }
            break;
        case Phase::restarted: // $103980: once more at the start, then held
            restart();
            countdown_ = 5;
            phase_ = Phase::held;
            break;
        case Phase::held:
            if (countdown_ < 0) {
                flying_ = true;
                sounds_.event(EngineSound::start);
                phase_ = Phase::flying;
            }
            break;
        case Phase::end_wait:
            if (countdown_ >= 0) break;
            // $1039F0: the menu palette and the end text, typed from this
            // frame on.
            text_.emplace(assets_, end_text_fa18);
            phase_ = Phase::end_text;
            [[fallthrough]];
        case Phase::end_text:
            if (text_->tick()) sounds_.click();
            if (text_->finished()) begin(); // (the original returns to its menu)
            break;
        }
    }

    void fly_tick(const app::GameInput& input) {
        const auto pressed = [&](int k) { return input.c64_keys[std::size_t(k)] && !keys_was_[std::size_t(k)]; };
        FlightControls c;
        c.nose_down = input.up;
        c.nose_up = input.down;
        c.left = input.left;
        c.right = input.right;
        c.rudder_left = input.c64_keys[key::comma];
        c.rudder_right = input.c64_keys[key::period];
        c.throttle_up = input.c64_keys[key::equals];
        c.throttle_down = input.c64_keys[key::minus];
        // F1 - F10 of the original set the throttle in tenths (F10: full
        // power, then the afterburner); here the digit keys 1 - 0.
        for (int d = 0; d < 10; ++d) {
            if (pressed(key::digits[d])) {
                const int setting = (d == 0 ? 10 : d) * 12;
                aircraft_.throttle_target = std::int8_t(setting > 119 ? setting + 1 : setting);
            }
        }
        // Key releases stop the throttle keys' steps ($10E356).
        if ((keys_was_[key::equals] && !input.c64_keys[key::equals]) ||
            (keys_was_[key::minus] && !input.c64_keys[key::minus])) {
            aircraft_.controls &= ~3;
            aircraft_.throttle_target = 0;
        }
        if (pressed(key::g)) {
            // $10E966: the gear handle, refused on the ground.
            if (aircraft_.motion & flag::on_ground) {
                sounds_.refuse();
            } else {
                aircraft_.gear ^= 0x80;
                sounds_.gear_moved();
            }
        }
        if (pressed(key::backspace)) aircraft_.state ^= flag::speed_brake;   // the original's \\ or Backspace
        if (pressed(key::b)) aircraft_.motion ^= 0x8000;                     // the original's A (wheel brake)
        keys_was_ = input.c64_keys;

        ground_warnings(aircraft_, messages_, sounds_);
        FlightRules rules;
        rules.world = &world_;
        const auto events = fly(aircraft_, c, rules, tables_);
        if (events.engine_sound != 0) sounds_.event(EngineSound(events.engine_sound));
        if (events.engine_restart_in != 0) sounds_.restart_after(events.engine_restart_in);
        if (events.tyres_volume != 0) sounds_.tyres(events.tyres_volume);
        if (events.stall_warning) sounds_.warn(warning::stall);
        if (events.gear_speed_warning) sounds_.warn_beep(4);
        // (Touching down on water: the original's handling is not traced;
        // here it ends like a crash.)
        if (events.in_water) aircraft_.state |= flag::crashed;
        ++ticks_;
    }

    // The cockpit camera: the eye 5 up and 20 ahead of the aircraft's
    // centre along its view matrix ($10F49E), looking along the body
    // matrix, rows scaled as the original's view ($10D308, $122D84).
    static Camera camera(const Aircraft& was, const Aircraft& now, double t) {
        Camera c;
        const auto mix = [t](double a, double b) { return a + (b - a) * t; };
        std::array<double, 9> body{};
        std::array<double, 9> view{};
        for (std::size_t i = 0; i < 9; ++i) {
            body[i] = mix(was.body[i], now.body[i]) / 16384.0;
            view[i] = mix(was.view[i], now.view[i]) / 16384.0;
        }
        const double eye[3] = {(20 * view[2] + 5 * view[1]), (20 * view[5] + 5 * view[4]), (20 * view[8] + 5 * view[7])};
        c.position = {mix(was.x, now.x) / 256.0 + eye[0], mix(was.y, now.y) / 256.0 + eye[1],
                      mix(was.z, now.z) / 256.0 + eye[2]};
        constexpr double rows[3] = {0xA8 / 256.0, 0xFC / 256.0, 0x80 / 256.0};
        for (std::size_t r = 0; r < 3; ++r)
            for (std::size_t k = 0; k < 3; ++k) c.matrix[3 * r + k] = body[3 * r + k] * rows[r];
        return c;
    }

    data::Assets assets_;
    data::World world_;
    FlightTables tables_;
    Scene scene_;
    Cockpit cockpit_;
    data::Palette palette_;
    Messages messages_;
    SoundDriver driver_;
    Sounds sounds_;
    Aircraft aircraft_, before_;
    std::array<bool, 64> keys_was_{};
    Phase phase_ = Phase::flying;
    bool flying_ = true;          // $13ABBC bit 6: the flight runs
    int aircraft_left_ = aircraft_given; // $13AB87
    int flash_left_ = 0;          // ticks of the crash's flash still to come
    int countdown_ = 0;           // $13ADC6
    int second_ticks_ = 0;        // ticks into the current real second
    std::optional<TextScreen> text_;
    int place_ = 0;
    long ticks_ = 0;
    bool smooth_ = true, widescreen_ = false, sound_ = true;
    int vhs_ = 0;
    int margin_ = 0, canvas_width_ = 0, canvas_height_ = 0;
    double frame_fraction_ = 1;
};

} // namespace

std::unique_ptr<app::GameModule> start(const std::filesystem::path& content) {
    return std::make_unique<FreeFlight>(content);
}

} // namespace interceptor::game
