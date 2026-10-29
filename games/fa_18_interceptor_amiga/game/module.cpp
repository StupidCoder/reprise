#include "fa_18_interceptor_amiga/game/module.hpp"
#include "fa_18_interceptor_amiga/data/assets.hpp"
#include "fa_18_interceptor_amiga/data/preview.hpp"
#include "fa_18_interceptor_amiga/data/world.hpp"
#include "fa_18_interceptor_amiga/game/aircraft.hpp"
#include "fa_18_interceptor_amiga/game/cockpit.hpp"
#include "fa_18_interceptor_amiga/game/front_end.hpp"
#include "fa_18_interceptor_amiga/game/pilot_record.hpp"
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
constexpr int ready_text = 89;        // "READY FOR TAKEOFF"
constexpr int menu_palette = 0;

// Keys of the C64-style matrix the host gives (GameInput::c64_keys).
namespace key {
constexpr int backspace = 0, return_key = 1, g = 26, b = 28, z = 12, minus = 43, period = 44, comma = 47,
              equals = 53, tab = 63, v = 31;
constexpr int digits[10] = {35, 56, 59, 8, 11, 16, 19, 24, 27, 32}; // 0 .. 9
// The characters of the matrix's keys, for the call sign (0: none).
constexpr char characters[64] = {
    0,   0,   0,   0,   0,   0,   0,   0,   '3', 'W', 'A', '4', 'Z', 'S', 'E', 0,
    '5', 'R', 'D', '6', 'C', 'F', 'T', 'X', '7', 'Y', 'G', '8', 'B', 'H', 'U', 'V',
    '9', 'I', 'J', '0', 'M', 'K', 'O', 'N', 0,   'P', 'L', '-', '.', '\'', 0,   ',',
    0,   0,   ';', 0,   0,   '=', 0,   '/', '1', 0,   0,   '2', 0,   0,   'Q', 0};
} // namespace key


// The game: the front end (front_end.hpp) and free flight as the original
// runs it from its screen procedures: "READY FOR TAKEOFF" typed while the
// aircraft waits ($102D58, $102E4E, $102EDA); flying ($102EFE); after a
// crash the flash and the explosion's noise, then the aircraft back at the
// start ($1038D8, $103980) and held a few frames ($1039C2); after the last
// of the five aircraft the end screen ($1039F0, $103A36), then the main
// menu. Esc (here Tab) goes back to the start map, Shift-Esc (Ctrl+Tab) to
// the main menu.
class FreeFlight final : public app::GameModule {
public:
    explicit FreeFlight(const std::filesystem::path& content)
        : content_(content), assets_(data::load_assets(content)), world_(data::read_world(assets_.program)),
          tables_(read_flight_tables(assets_.program)), scene_(assets_, world_), cockpit_(assets_),
          palette_(assets_.palettes.at(data::flight_palette)), messages_(assets_.program), driver_(assets_),
          sounds_(driver_), pilot_(read_pilot(content)),
          front_(assets_, scene_, sounds_, pilot_, [this] { write_pilot(content_, pilot_); }), text_(assets_) {
        front_.start_title();
    }

    // Text screens run a frame per vertical blank; the map and the flight
    // at the original's frame cap ($119876).
    double step_seconds() const override {
        const bool blanks = in_front_ ? front_.fast() : phase_ == Phase::ready && !ready_done_;
        return blanks ? 1.0 / 50 : 1.0 / ticks_per_second;
    }
    double pixel_aspect() const override { return 1.0; }

    void tick(const app::GameInput& input) override {
        const auto pressed = [&](int k) { return input.c64_keys[std::size_t(k)] && !keys_was_[std::size_t(k)]; };
        if (in_front_) {
            FrontKeys k;
            for (int i = 0; i < 64; ++i) {
                if (!pressed(i)) continue;
                k.any = true;
                if (key::characters[i]) k.typed = key::characters[i];
            }
            if (input.fire && !fire_was_ && !input.c64_keys[key::z]) {
                k.any = true;
                k.typed = ' ';
            }
            for (int d = 1; d <= 9; ++d) if (pressed(key::digits[d])) k.digit = d;
            k.enter = pressed(key::return_key);
            k.backspace = pressed(key::backspace);
            k.escape = pressed(key::tab);
            k.shift_escape = input.ctrl_keys[key::tab] && !ctrl_tab_was_;
            k.shift_2 = input.ctrl_keys[key::digits[2]] && !ctrl_two_was_;
            k.any = k.any || k.shift_escape;
            remember_keys(input);
            if (const auto start = front_.tick(k)) begin(*start);
            return;
        }
        if (input.ctrl_keys[key::tab] && !ctrl_tab_was_) {
            // Shift-Esc ($103A4C): back to the main menu.
            remember_keys(input);
            leave_flight();
            front_.to_menu();
            return;
        }
        if (pressed(key::tab) && phase_ != Phase::end_wait && phase_ != Phase::end_text) {
            // Esc: free flight's start map again.
            remember_keys(input);
            leave_flight();
            sounds_.ambience(8);
            front_.to_start_map();
            return;
        }
        before_ = aircraft_;
        if (flash_left_ > 0) { // (the frame's end, held by the flash)
            --flash_left_;
            return;
        }
        if (++second_ticks_ >= ticks_per_second) {
            second_ticks_ = 0;
            sounds_.second();
            // $119954: a second of too much G darkens the pilot's sight one
            // step (at most 15), a second without one lighter (down to -1).
            if (overloaded_) blackout_ = std::min(blackout_ + 1, 15);
            else if (blackout_ >= 0) --blackout_;
            sounds_.set_blackout(blackout_ > 5);
        }
        --countdown_; // $13ADC6, every frame ($101748)
        run_procedure();
        messages_.clock();
        if (phase_ == Phase::end_wait || phase_ == Phase::end_text || phase_ == Phase::ready) {
            remember_keys(input);
            sounds_.update(aircraft_);
            return;
        }
        if (flying_) fly_tick(input);
        else remember_keys(input);
        sounds_.warn(messages_.update(aircraft_));
        sounds_.update(aircraft_);
    }

    platform::Image picture() const override {
        const int width = screen_width + 2 * margin_;
        IndexedImage frame(width, screen_height);
        platform::Image image(width, screen_height);
        if (in_front_) {
            front_.draw(frame, margin_);
            const auto& colours = front_.palette();
            for (int y = 0; y < screen_height; ++y)
                for (int x = 0; x < width; ++x)
                    image.put(x, y, data::to_color(colours[frame.pixels[std::size_t(y * width + x)] & 31]));
            return image;
        }
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
            palette = &assets_.palettes.at(menu_palette);
            text_.draw(frame, margin_);
        } else {
            double t = 1;
            if (smooth_ && flash_left_ == 0) t = frame_fraction_;
            auto readings = read_cockpit(aircraft_);
            if (outside_) {
                // The outside view ($13AA75): the scene down to row 167
                // ($13AC74) with the aircraft in it, and its numbers below.
                Vehicle self;
                // The shape types: 4 the F/A-18, 3 the light single-fin
                // jet for the F-16 (hypothesis, from the shapes' looks).
                self.type = aircraft_type_ == 0x10 ? 3 : 4;
                const auto mix = [t](double a, double b) { return a + (b - a) * t; };
                self.position = {mix(before_.x, aircraft_.x) / 256.0, mix(before_.y, aircraft_.y) / 256.0,
                                 mix(before_.z, aircraft_.z) / 256.0};
                self.body = aircraft_.body;
                const int bottom = above_ ? 0xB3 : outside_bottom; // ($13AC74)
                const auto view = outside_camera(t);
                scene_.draw(frame, width / 2 - 1, bottom, view, {self});
                if (above_) grid(frame, view, width / 2 - 1, bottom);
                cockpit_.draw_outside(frame, margin_, readings, bottom + 1);
                return to_image(frame, image, *palette);
            }
            // $10E7B8: with a look direction the cockpit slides sideways by
            // `$13AC76` x 16 pixels (table `$10E828`); from 50 on there is
            // none, the view reaches row $A7 or $B3 and the aircraft itself
            // shows (its tail and wings).
            static constexpr int look_shift[12] = {0, -7, -14, -21, -50, 50, 50, 50, 50, 21, 14, 7};
            const int shift = look_shift[look_];
            const bool cockpit = std::abs(shift) < 21;
            const int view_rows = cockpit ? Cockpit::view_bottom : (look_ >= 5 && look_ <= 7 ? 0xB3 : 0xA7);
            std::vector<Vehicle> self;
            if (std::abs(shift) >= 50) {
                Vehicle v;
                v.shadow = false; // (only from outside, $113984)
                v.type = aircraft_type_ == 0x10 ? 3 : 4;
                const auto mix = [t](double a, double b) { return a + (b - a) * t; };
                v.position = {mix(before_.x, aircraft_.x) / 256.0, mix(before_.y, aircraft_.y) / 256.0,
                              mix(before_.z, aircraft_.z) / 256.0};
                v.body = aircraft_.body;
                self.push_back(v);
            }
            scene_.draw(frame, width / 2 - 1, cockpit ? Cockpit::view_bottom : view_rows,
                        camera(before_, aircraft_, t, look_), self);
            readings.hud = phase_ != Phase::ready; // ($13AA91, set as the flight starts)
            readings.message = messages_.shown();
            readings.message_colour = messages_.colour();
            if (!cockpit) {
                for (int y = view_rows + 1; y < frame.height; ++y)
                    for (int x = 0; x < frame.width; ++x) frame.put(x, y, 3);
                return to_image(frame, image, *palette);
            }
            if (shift != 0) {
                // The cockpit drawn on its own and laid over the view, moved.
                IndexedImage panel(width, screen_height);
                std::fill(panel.pixels.begin(), panel.pixels.end(), std::uint8_t(0xFF));
                cockpit_.draw(panel, margin_, readings);
                // (What it uncovers below the glare shield's lower edge is
                // colour 3, as on the original's screen.)
                for (int y = 140; y < screen_height; ++y)
                    for (int x = 0; x < width; ++x) frame.put(x, y, 3);
                for (int y = 0; y < screen_height; ++y)
                    for (int x = 0; x < width; ++x) {
                        const int from = x - shift * 16;
                        if (from < 0 || from >= width) continue;
                        const auto c = panel.pixels[std::size_t(y * width + from)];
                        if (c != 0xFF) frame.put(x, y, c);
                    }
            } else {
                cockpit_.draw(frame, margin_, readings);
            }
            if (phase_ == Phase::ready) text_.draw(frame, margin_);
            // The panel is a view port of its own with `pix/inst5`'s colour
            // map, whose colours 16-31 repeat 0-15 (the flight palette's
            // 16-31 are their darker copies).
            to_image(frame, image, *palette);
            for (int y = Cockpit::view_bottom + 1; y < frame.height; ++y)
                for (int x = 0; x < frame.width; ++x)
                    image.put(x, y, data::to_color((*palette)[frame.pixels[std::size_t(y * frame.width + x)] & 15]));
            return image;
        }
        return to_image(frame, image, *palette);
    }

    static platform::Image to_image(const IndexedImage& frame, platform::Image& image, const data::Palette& palette) {
        for (int y = 0; y < frame.height; ++y)
            for (int x = 0; x < frame.width; ++x)
                image.put(x, y, data::to_color(palette[frame.pixels[std::size_t(y * frame.width + x)] & 31]));
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
        return {{"Widescreen", {"Off", "On"}, widescreen_ ? 1 : 0,
                 "Shows more of the world left and right of the original's view, at the same scale. The "
                 "game plays the same."},
                {"Smooth motion", {"Off", "On"}, smooth_ ? 1 : 0,
                 "The view moves at the display's own rate instead of the original's 15 steps a second. "
                 "The game plays the same."},
                app::screen_filter_option(vhs_),
                {"Sound", {"Off", "On"}, sound_ ? 1 : 0, "The original's engine, wind, gear, tyre, crash "
                 "and warning sounds."}};
    }
    void set_option(std::size_t index, int value) override {
        if (index == 0) {
            widescreen_ = value != 0;
            set_canvas(canvas_width_, canvas_height_);
        }
        if (index == 1) smooth_ = value != 0;
        if (index == 2) vhs_ = std::clamp(value, 0, ui::vhs_levels - 1);
        if (index == 3) sound_ = value != 0;
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
        ready,        // $102E4E / $102EDA: "READY FOR TAKEOFF", then a beep
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

    // A new free flight from the front end: five aircraft, the first at the
    // chosen place, held until "READY FOR TAKEOFF" is typed.
    void begin(const FlightStart& start) {
        in_front_ = false;
        place_ = start.place;
        aircraft_type_ = start.aircraft;
        aircraft_left_ = aircraft_given;
        text_.clear();
        text_.queue(ready_text);
        sounds_.set_active(true);
        restart();
        flying_ = false;
        ready_done_ = false;
        phase_ = Phase::ready;
    }

    // Free flight ends (Esc, Shift-Esc, the end text): every sound stops.
    void leave_flight() {
        flash_left_ = 0;
        sounds_.stop_all();
        sounds_.set_active(false);
        in_front_ = true;
    }

    void remember_keys(const app::GameInput& input) {
        keys_was_ = input.c64_keys;
        fire_was_ = input.fire;
        ctrl_tab_was_ = input.ctrl_keys[key::tab];
        ctrl_two_was_ = input.ctrl_keys[key::digits[2]];
    }

    // $10D51E: the aircraft at the chosen start; the engine sound starts.
    void restart() {
        aircraft_ = start_at_place(assets_.program, tables_, place_);
        // The wheels' surface, as the first contact would find it.
        aircraft_.surface = std::uint8_t(world_.on_runway(aircraft_.x / 256.0, aircraft_.z / 256.0) ? 4 : 0);
        before_ = aircraft_;
        sounds_.event(EngineSound::start);
        messages_.clear();
        sounds_.clear_warnings();
        blackout_ = 0; // ($103922)
        overloaded_ = false;
        sounds_.set_blackout(false);
    }

    // The screen procedure of this frame.
    void run_procedure() {
        switch (phase_) {
        case Phase::ready:
            // $102E4E: the text typed, then three frames and a beep.
            // (Typed a frame per vertical blank over the still view.)
            if (!ready_done_) {
                if (text_.tick()) sounds_.click();
                if (text_.done()) {
                    ready_done_ = true;
                    countdown_ = 3;
                }
            } else if (countdown_ < 0) {
                sounds_.beep(0x12C, 2, 1, 0x12C, 2, 1, 1, 2, 2); // $127D7C
                text_.clear();
                flying_ = true;
                phase_ = Phase::flying;
            }
            break;
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
            text_.clear();
            text_.queue(end_text_fa18);
            phase_ = Phase::end_text;
            [[fallthrough]];
        case Phase::end_text:
            if (text_.tick()) sounds_.click();
            if (text_.done()) { // $103A36 → $101A70: the main menu
                leave_flight();
                front_.to_menu();
            }
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
        view_keys(input);
        if (pressed(key::backspace)) aircraft_.state ^= flag::speed_brake;   // the original's \\ or Backspace
        if (pressed(key::b)) aircraft_.motion ^= 0x8000;                     // the original's A (wheel brake)
        remember_keys(input);

        ground_warnings(aircraft_, messages_, sounds_);
        FlightRules rules;
        rules.world = &world_;
        const auto events = fly(aircraft_, c, rules, tables_);
        if (events.engine_sound != 0) sounds_.event(EngineSound(events.engine_sound));
        if (events.engine_restart_in != 0) sounds_.restart_after(events.engine_restart_in);
        if (events.tyres_volume != 0) sounds_.tyres(events.tyres_volume);
        if (events.stall_warning) sounds_.warn(warning::stall);
        if (events.gear_speed_warning) sounds_.warn_beep(4);
        // $117128: 7.8 G or more beeps once until it is less again.
        const int load = std::abs((aircraft_.pitch_rate + (aircraft_.pitch_rate >> 3)) >> 3);
        if (load >= 0x4E) {
            if (!overloaded_) sounds_.warn_beep(8);
            overloaded_ = true;
        } else {
            overloaded_ = false;
        }
        // (Touching down on water: the original's handling is not traced;
        // here it ends like a crash.)
        if (events.in_water) aircraft_.state |= flag::crashed;
        chase_camera();
        ++ticks_;
    }

    // The view keys ($10E4D4-$10E720; here the numeric keypad's 0 for the
    // keypad's 5, its 4 and 6 to step around the aircraft, 8 for the view
    // from above, 2 from behind, Ctrl with = and - for the distance).
    void view_keys(const app::GameInput& input) {
        const auto edge = [](bool now, bool& was) {
            const bool pressed = now && !was;
            was = now;
            return pressed;
        };
        if (edge(input.c64_keys[key::v], v_was_)) {
            // $10ECE0 (the original's M): the view from straight above the
            // aircraft at the height `$13AF32` (kept between uses, $13A954),
            // or back to the outside camera.
            above_ = !above_;
            outside_ = true;
            chase_set_ = false;
        }
        if (edge(input.fire2, pad_was_[0])) {
            above_ = false;
            // $10E3B8 / $1041E8: the outside view starts in front of the
            // aircraft ($13ABA2 = 4) at the nearest distance ($13AA75 = -1).
            outside_ = !outside_;
            look_ = 0; // ($1042AE)
            if (outside_) {
                position_ = 4;
                distance_ = -1;
            }
            chase_set_ = false;
        }
        const bool left = edge(input.left2, pad_was_[1]), right = edge(input.right2, pad_was_[2]);
        const bool up = edge(input.up2, pad_was_[3]), down = edge(input.down2, pad_was_[4]);
        bool nearer = edge(input.ctrl_keys[key::equals], pad_was_[5]);
        bool farther = edge(input.ctrl_keys[key::minus], pad_was_[6]);
        sounds_.set_view(outside_, position_, distance_);
        if (!outside_) {
            // In the cockpit ($10E684, $10E6B4, $10E668, $10E64C): keypad 4
            // and 6 step the look direction round, 8 looks ahead, 2 back.
            if (left) look_ = (look_ + 11) % 12;
            if (right) look_ = (look_ + 1) % 12;
            if (up) look_ = 0;
            if (down) look_ = 6;
            return;
        }
        if (left) position_ = position_ >= 8 ? 1 : (position_ + 1) % 8;
        if (right) position_ = position_ >= 8 ? 7 : (position_ + 7) % 8;
        if (up) position_ = 8;
        if (down) position_ = 0;
        if (above_) {
            // $10E582 / $10E608: the height in steps of $2000000 (32-bit).
            if (nearer) above_height_ = std::max(above_height_ - 0x2000000 / 256.0, 0x1000000 / 256.0);
            if (farther) above_height_ = std::min(above_height_ + 0x2000000 / 256.0, 0x8000000 / 256.0);
            nearer = farther = false;
        }
        // $10E5AE / $10E634: the distances -1, 1, 2, 3, 4.
        if (nearer) distance_ = distance_ <= 1 ? -1 : distance_ - 1;
        if (farther) distance_ = distance_ < 0 ? 1 : std::min(4, distance_ + 1);
        sounds_.set_view(outside_, position_, distance_);
        // ($10E52E, $10E5BA: a view key asks for the engine's update now,
        // even when the view stays the same.)
        if (left || right || up || down || nearer || farther) sounds_.event(EngineSound::update);
    }

    // $11D8F2 / $11DD2C: the outside camera's place: the offset of its
    // position ($11D770 for the F/A-18, words x, y, z, 4 world units each)
    // scaled by the distance (1: 1.5 times, then doubling), turned by the
    // aircraft's view matrix for the views from behind and ahead and by its
    // heading alone for the others, at least 7 units above the ground; the
    // camera goes half the way there each frame.
    std::array<double, 3> chase_target() const {
        const auto& m = assets_.program;
        const std::uint32_t at = 0x11D770 + std::uint32_t(6 * position_);
        double off[3] = {double(m.s16(at)), double(m.s16(at + 2)), double(m.s16(at + 4))};
        const double scale = distance_ < 0 ? 1 : distance_ == 1 ? 1.5 : double(1 << (distance_ - 1));
        for (auto& o : off) o *= 4 * scale;
        Matrix turn = aircraft_.view;
        if (position_ != 0 && position_ != 4) turn = view_matrix(tables_, {0, aircraft_.attitude.heading, 0});
        std::array<double, 3> p{aircraft_.x / 256.0, aircraft_.y / 256.0, aircraft_.z / 256.0};
        for (int r = 0; r < 3; ++r)
            for (int k = 0; k < 3; ++k) p[std::size_t(r)] += turn[std::size_t(3 * r + k)] * off[k] / 16384.0;
        p[1] = std::max(p[1], 7.0);
        return p;
    }

    void chase_camera() {
        if (!outside_) return;
        chase_was_ = chase_;
        const auto target = chase_target();
        if (!chase_set_) {
            chase_ = chase_was_ = target;
            chase_set_ = true;
            return;
        }
        for (std::size_t i = 0; i < 3; ++i) chase_[i] += (target[i] - chase_[i]) / 2;
    }

    // $11FD40: the view from above carries a grid of half degrees: 24
    // lines along z (x from the 16-bit coordinate $F680 in steps of $1C0,
    // from z $23E0 to $EF60) and 16 along x (z from $FEB0 in steps of $230,
    // x $1440 to $EA40), colour 8, every other one labelled (115 on, 34
    // on) with the panel's number drawer at its clipped end (the bottom
    // end of the first set, the left of the second).
    void grid(IndexedImage& frame, const Camera& view, int centre, int bottom) const {
        const auto project = [&](double x, double z) {
            const double dx = x - view.position[0], dy = -view.position[1], dz = z - view.position[2];
            const auto& m = view.matrix;
            const double xc = m[0] * dx + m[1] * dy + m[2] * dz, yc = m[3] * dx + m[4] * dy + m[5] * dz,
                         zc = m[6] * dx + m[7] * dy + m[8] * dz;
            return std::array<double, 2>{centre - 160 * xc / zc, 90 - 90 * yc / zc};
        };
        // A line clipped to the view (Liang-Barsky); its ends, if any.
        const auto clipped = [&](std::array<double, 2> a, std::array<double, 2> b)
            -> std::optional<std::array<std::array<double, 2>, 2>> {
            double t0 = 0, t1 = 1;
            const double dx = b[0] - a[0], dy = b[1] - a[1];
            const double p[4] = {-dx, dx, -dy, dy};
            const double q[4] = {a[0], frame.width - 1 - a[0], a[1], bottom - a[1]};
            for (int i = 0; i < 4; ++i) {
                if (p[i] == 0) {
                    if (q[i] < 0) return std::nullopt;
                    continue;
                }
                const double r = q[i] / p[i];
                if (p[i] < 0) t0 = std::max(t0, r);
                else t1 = std::min(t1, r);
                if (t0 > t1) return std::nullopt;
            }
            return std::array<std::array<double, 2>, 2>{{{a[0] + t0 * dx, a[1] + t0 * dy}, {a[0] + t1 * dx, a[1] + t1 * dy}}};
        };
        const auto draw = [&](std::array<std::array<double, 2>, 2> e) {
            const int steps = int(std::max(std::abs(e[1][0] - e[0][0]), std::abs(e[1][1] - e[0][1]))) + 1;
            for (int i = 0; i <= steps; ++i)
                frame.put(int(e[0][0] + (e[1][0] - e[0][0]) * i / steps), int(e[0][1] + (e[1][1] - e[0][1]) * i / steps), 8);
        };
        const auto label = [&](int value, int x, int y) {
            const auto t = std::to_string(value);
            int left = (x & ~3) + 8; // ($127640: 4-pixel steps, 8 on)
            for (char c : t) {
                const auto* glyph = assets_.small_font.glyph(c);
                if (glyph)
                    for (int r = 0; r < assets_.small_font.rows && r < int(glyph->size()); ++r)
                        for (int b = 0; b < 4; ++b)
                            if ((*glyph)[std::size_t(r)] & (0x80 >> b)) frame.put(left + b, y + r, 2);
                left += 4;
            }
        };
        const auto world = [](int word) { return double(std::int16_t(word)) * 256; };
        int value = 0x73;
        for (int count = 24, i = 0; count > 0; --count, ++i) {
            const double x = world(0xF680 + 0x1C0 * i);
            const auto e = clipped(project(x, world(0x23E0)), project(x, world(0xEF60)));
            if (e) {
                draw(*e);
                const int lx = std::min(int((*e)[1][0]) - 12, 0x12C), ly = int((*e)[1][1]) - 5;
                if ((count & 1) && ly >= 8 && lx >= 8) label(value, lx, ly);
            }
            if (count & 1) ++value;
        }
        value = 0x22;
        for (int count = 16, i = 0; count > 0; --count, ++i) {
            const double z = world(0xFEB0 + 0x230 * i);
            const auto e = clipped(project(world(0x1440), z), project(world(0xEA40), z));
            if (e) {
                draw(*e);
                const int lx = std::max(int((*e)[0][0]), 0), ly = std::clamp(int((*e)[0][1]) - 1, 4, 0xAF);
                if (count & 1) label(value, lx, ly);
            }
            if (count & 1) ++value;
        }
    }

    // The outside camera looks at the aircraft, level; from above it looks
    // straight down like the start map.
    Camera outside_camera(double t) const {
        Camera c;
        if (above_) {
            c.position = {(before_.x + (aircraft_.x - before_.x) * t) / 256.0, above_height_,
                          (before_.z + (aircraft_.z - before_.z) * t) / 256.0};
            c.matrix = {0xA8 / 256.0, 0, 0, 0, 0, 0xFC / 256.0, 0, -0x80 / 256.0, 0};
            return c;
        }
        for (std::size_t i = 0; i < 3; ++i) c.position[i] = chase_was_[i] + (chase_[i] - chase_was_[i]) * t;
        const double ax = (before_.x + (aircraft_.x - before_.x) * t) / 256.0;
        const double ay = (before_.y + (aircraft_.y - before_.y) * t) / 256.0;
        const double az = (before_.z + (aircraft_.z - before_.z) * t) / 256.0;
        double f[3] = {ax - c.position[0], ay - c.position[1], az - c.position[2]};
        const double n = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
        if (n < 1e-6) return camera(before_, aircraft_, t);
        for (auto& v : f) v /= n;
        double x[3] = {f[2], 0, -f[0]}; // up x forward
        const double xn = std::sqrt(x[0] * x[0] + x[2] * x[2]);
        if (xn < 1e-6) { x[0] = 1; x[2] = 0; } else { x[0] /= xn; x[2] /= xn; }
        const double y[3] = {f[1] * x[2] - f[2] * x[1], f[2] * x[0] - f[0] * x[2], f[0] * x[1] - f[1] * x[0]};
        constexpr double rows[3] = {0xA8 / 256.0, 0xFC / 256.0, 0x80 / 256.0};
        for (int k = 0; k < 3; ++k) {
            c.matrix[std::size_t(k)] = x[k] * rows[0];
            c.matrix[std::size_t(3 + k)] = y[k] * rows[1];
            c.matrix[std::size_t(6 + k)] = f[k] * rows[2];
        }
        return c;
    }

    // The cockpit camera: the eye 5 up and 20 ahead of the aircraft's
    // centre along its view matrix ($10F49E), looking along the body
    // matrix, rows scaled as the original's view ($10D308, $122D84).
    static Camera camera(const Aircraft& was, const Aircraft& now, double t, int look = 0) {
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
        // The look direction ($122340): the view turned about the
        // aircraft's own up axis by the heading of `$12249A` (80 a degree,
        // clockwise: 1 is 28.2 degrees right of the nose ... 11 as far left).
        static constexpr int look_heading[12] = {0, 2256, 4800, 7200, 9600, 12000, 14400, 16800, 19200, 21600, 24000, 26544};
        if (look > 0) {
            const double a = look_heading[look] / 80.0 * 3.14159265358979 / 180, cs = std::cos(a), sn = std::sin(a);
            for (std::size_t k = 0; k < 3; ++k) {
                const double left = body[k], ahead = body[6 + k];
                body[k] = cs * left + sn * ahead;
                body[6 + k] = cs * ahead - sn * left;
            }
        }
        constexpr double rows[3] = {0xA8 / 256.0, 0xFC / 256.0, 0x80 / 256.0};
        for (std::size_t r = 0; r < 3; ++r)
            for (std::size_t k = 0; k < 3; ++k) c.matrix[3 * r + k] = body[3 * r + k] * rows[r];
        return c;
    }

    std::filesystem::path content_;
    data::Assets assets_;
    data::World world_;
    FlightTables tables_;
    Scene scene_;
    Cockpit cockpit_;
    data::Palette palette_;
    Messages messages_;
    SoundDriver driver_;
    Sounds sounds_;
    PilotRecord pilot_;
    FrontEnd front_;
    TextRunner text_;             // "READY FOR TAKEOFF", the end text
    bool in_front_ = true;
    bool ready_done_ = false;     // the takeoff text has run ($13AAD0 bit 7)
    Aircraft aircraft_, before_;
    std::array<bool, 64> keys_was_{};
    bool fire_was_ = false, ctrl_tab_was_ = false, ctrl_two_was_ = false;
    int aircraft_type_ = 0x11;    // $13AB39 (the F-16, $10, flies as the F/A-18 for now)
    // The outside view ($13AA75 distance 1-4, 0 the cockpit; $13ABA2 the
    // camera's position around the aircraft) and its chasing camera.
    bool outside_ = false;
    int distance_ = -1, position_ = 4;
    std::array<double, 3> chase_{}, chase_was_{};
    bool chase_set_ = false;
    std::array<bool, 7> pad_was_{};
    bool above_ = false, v_was_ = false;      // $13AA9D: the view from above
    int look_ = 0;                            // $13AA97: the cockpit's look direction
    double above_height_ = 0x3000000 / 256.0; // $13AF32 / $13A954
    static constexpr int outside_bottom = 0xA7;
    Phase phase_ = Phase::flying;
    bool flying_ = true;          // $13ABBC bit 6: the flight runs
    int aircraft_left_ = aircraft_given; // $13AB87
    int flash_left_ = 0;          // ticks of the crash's flash still to come
    int countdown_ = 0;           // $13ADC6
    bool overloaded_ = false;     // $13AB78: 7.8 G or more
    int blackout_ = 0;            // $13AB79: seconds of overload, -1 .. 15
    int second_ticks_ = 0;        // ticks into the current real second
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
