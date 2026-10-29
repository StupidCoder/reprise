#include "fa_18_interceptor_amiga/game/module.hpp"
#include "fa_18_interceptor_amiga/data/assets.hpp"
#include "fa_18_interceptor_amiga/data/preview.hpp"
#include "fa_18_interceptor_amiga/data/world.hpp"
#include "fa_18_interceptor_amiga/game/aircraft.hpp"
#include "fa_18_interceptor_amiga/game/autopilot.hpp"
#include "fa_18_interceptor_amiga/game/carrier.hpp"
#include "fa_18_interceptor_amiga/game/collisions.hpp"
#include "fa_18_interceptor_amiga/game/cockpit.hpp"
#include "fa_18_interceptor_amiga/game/front_end.hpp"
#include "fa_18_interceptor_amiga/game/pilot_record.hpp"
#include "fa_18_interceptor_amiga/game/radar.hpp"
#include "fa_18_interceptor_amiga/game/replay.hpp"
#include "fa_18_interceptor_amiga/game/messages.hpp"
#include "fa_18_interceptor_amiga/game/object_step.hpp"
#include "fa_18_interceptor_amiga/game/scene.hpp"
#include "fa_18_interceptor_amiga/game/sound_driver.hpp"
#include "fa_18_interceptor_amiga/game/sounds.hpp"
#include "fa_18_interceptor_amiga/game/text_screen.hpp"
#include "fa_18_interceptor_amiga/game/weapons.hpp"
#include "fa_18_interceptor_amiga/game/world_objects.hpp"
#include "ui/vhs.hpp"
#include <optional>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>

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
constexpr int end_text_f16 = 98;      // the same for the F-16 ($103A06)
constexpr int ready_text = 89;        // "READY FOR TAKEOFF"
constexpr int launch_text = 90;       // "READY TO LAUNCH" (on the carrier, $102E10)
constexpr int maneuvers_text = 9;     // "BASIC FLIGHT MANEUVERS - PART 1" ($102488)
constexpr int practice_text = 10;     // "... PART 2" ($102496)
constexpr int lead_lost_text = 72;    // "THE LEAD AIRCRAFT HAS BEEN DESTROYED ..." ($1032AE)
constexpr int qualification_text = 73; // the qualification's briefing ($101CDA)
constexpr int landed_text = 74;       // "LANDING SUCCESSFUL ... YOU ARE NOW QUALIFIED" ($1032D6)
constexpr int menu_hint_text = 83;    // "(SHIFT ESC) TO RETURN TO MAIN MENU"
constexpr int bogey_text = 88;        // "YOUR VECTOR XXX FOR BOGEY" (mission 3, the demo's)
constexpr int carrier_place = 3;      // place 4 ($137CF6's $800E: record 14)
constexpr int menu_palette = 0;

// Keys of the C64-style matrix the host gives (GameInput::c64_keys).
namespace key {
constexpr int backspace = 0, return_key = 1, g = 26, b = 28, z = 12, minus = 43, period = 44, comma = 47,
              equals = 53, tab = 63, v = 31, c = 20, f = 21, r = 17, t = 22, j = 34;
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
          sounds_(driver_), carrier_(assets_.program), pilot_(read_pilot(content)),
          front_(assets_, scene_, sounds_, pilot_, [this] { write_pilot(content_, pilot_); }), text_(assets_) {
        front_.set_carrier({carrier_.position[0], carrier_.position[2]});
        front_.start_title();
    }

    // Text screens run a frame per vertical blank; the map and the flight
    // at the original's frame cap ($119876).
    double step_seconds() const override {
        const bool blanks =
            in_front_ ? front_.fast() : (phase_ == Phase::ready && !ready_done_) || phase_ == Phase::fade_in || phase_ == Phase::fade_out ||
            (phase_ == Phase::briefing && !text_.done()) || phase_ == Phase::landed || phase_ == Phase::lead_lost;
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
            // F1-F6: the missions, the C64's way (F2, F4, F6 with Shift);
            // Ctrl+1 - Ctrl+6 too.
            const bool f[3] = {input.f1 && !f_was_[0], input.f3 && !f_was_[1], input.f5 && !f_was_[2]};
            for (int i = 0; i < 3; ++i) if (f[i]) k.mission = 3 + 2 * i + (input.shift ? 1 : 0);
            for (int d = 1; d <= 6; ++d) {
                if (input.ctrl_keys[key::digits[d]] && !ctrl_digits_was_[std::size_t(d)]) k.mission = 2 + d;
            }
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
        if (pressed(key::tab) && phase_ != Phase::end_wait && phase_ != Phase::end_text && phase_ != Phase::fade_out) {
            // Esc ($101854): the flight stops, the screen fades out and the
            // selection starts again ($101A96): free flight's start map,
            // the qualification's catapult ("RE-POSITION ON CATAPULT").
            remember_keys(input);
            start_again();
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
        if (phase_ == Phase::end_wait || phase_ == Phase::end_text || phase_ == Phase::ready ||
            phase_ == Phase::descent || phase_ == Phase::to_ready || phase_ == Phase::fade_in ||
            phase_ == Phase::fade_out || phase_ == Phase::briefing || phase_ == Phase::landed ||
            phase_ == Phase::lead_lost || phase_ == Phase::mission_text) {
            if (phase_ == Phase::briefing) {
                // ($101D06: any key ends the briefing; `$13AA93`.)
                bool any = input.fire && !fire_was_;
                for (int i = 0; i < 64; ++i) any = any || pressed(i);
                if (any) briefing_key_ = true;
            }
            remember_keys(input);
            sounds_.update(aircraft_);
            return;
        }
        if (flying_ && mode_ == FlightMode::maneuvers) {
            // ($10E8EA: Return in mode 2 asks for the next maneuver.)
            if (pressed(key::return_key)) skip_maneuver_ = true;
            view_keys(input);
            remember_keys(input);
            trainer_tick();
        } else if (flying_) {
            fly_tick(input);
        } else {
            remember_keys(input);
        }
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
        data::Palette fading = palette_;
        const data::Palette* palette = &palette_;
        if (fade_ < 15) {
            // The fade's step ($1093F2): colours 0-15 from the table of
            // darker copies before `$11A04C`.
            const std::uint32_t at = 0x11A04C - std::uint32_t((15 - fade_) * 32);
            for (std::uint32_t k = 0; k < 16; ++k) fading[k] = assets_.program.u16(at + 2 * k);
            palette = &fading;
        }
        if (phase_ == Phase::end_wait || phase_ == Phase::end_text) {
            // The cleared screen, then the end text in the menu palette.
            palette = &assets_.palettes.at(menu_palette);
            text_.draw(frame, margin_);
        } else {
            double t = 1;
            if (smooth_ && flash_left_ == 0) t = frame_fraction_;
            auto readings = read_cockpit(aircraft_);
            readings.zoom = zoom_ == 0x80 ? 1 : zoom_ == 0x40 ? 2 : 4; // ($1266C8)
            for (const auto& b : radar_.blips()) readings.contacts.push_back({b.x, b.y, b.colour, b.wide});
            if (outside_) {
                // The outside view ($13AA75): the scene down to row 167
                // ($13AC74) with the aircraft in it, and its numbers below.
                Vehicle self;
                // The shape types ($10D58C): 4 the F/A-18, 3 the F-16.
                self.type = aircraft_type_ == 0x10 ? 3 : 4;
                const auto mix = [t](double a, double b) { return a + (b - a) * t; };
                self.position = {mix(before_.x, aircraft_.x) / 256.0, mix(before_.y, aircraft_.y) / 256.0,
                                 mix(before_.z, aircraft_.z) / 256.0};
                self.body = aircraft_.body;
                const int bottom = above_ ? 0xB3 : outside_bottom; // ($13AC74)
                const auto view = outside_camera(t);
                std::vector<Vehicle> vehicles{self, carrier_vehicle()};
                std::vector<Spark> sparks;
                add_weapons(vehicles, sparks, t);
                add_lead(vehicles, t);
                add_objects(vehicles, t);
                scene_.draw(frame, width / 2 - 1, bottom, view, vehicles, sparks);
                if (above_) grid(frame, view, width / 2 - 1, bottom);
                // (No numbers while the camera comes down from the map: $126AB2
                // needs `$13AAC9`.)
                cockpit_.draw_outside(frame, margin_, readings, bottom + 1, phase_ != Phase::descent);
                return to_image(frame, image, *palette);
            }
            // $10E7B8: with a look direction the cockpit slides sideways by
            // `$13AC76` x 16 pixels (table `$10E828`); from 50 on there is
            // none, the view reaches row $A7 or $B3 and the aircraft itself
            // shows (its tail and wings).
            static constexpr int look_shift[14] = {0, -7, -14, -21, -50, 50, 50, 50, 50, 21, 14, 7, 50, 50};
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
            self.push_back(carrier_vehicle());
            std::vector<Spark> sparks;
            add_weapons(self, sparks, t);
            add_lead(self, t);
            add_objects(self, t);
            scene_.draw(frame, width / 2 - 1, cockpit ? Cockpit::view_bottom : view_rows,
                        camera(before_, aircraft_, t, look_, zoom_), self, sparks);
            readings.hud = phase_ != Phase::ready && phase_ != Phase::to_ready; // ($13AA91, set as the flight starts)
            {
                const auto& st = weapons_.stores();
                readings.weapon = int(st.selected);
                readings.weapon_count = weapons_.count();
                readings.sidewinders = st.sidewinders;
                readings.amraams = st.amraams;
                // (Only at zoom 1, $13AD32 $80; and here only looking ahead.)
                if (st.selected == Weapon::gun && zoom_ == 0x80 && look_ == 0) readings.pipper = pipper_;
            }
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
            if (phase_ == Phase::ready || phase_ == Phase::briefing || phase_ == Phase::landed ||
                phase_ == Phase::lead_lost)
                text_.draw(frame, margin_);
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

    // $1288CE: the gun's lead point, ahead and above the aircraft along its
    // view matrix ((0, $600, $4000) / 64), kept for two frames; where the
    // older one now shows is where the circle goes next frame.
    void lead_point() {
        pipper_ = pipper_next_;
        Camera c = camera(aircraft_, aircraft_, 1);
        c.position = {aircraft_.x / 256.0, aircraft_.y / 256.0, aircraft_.z / 256.0};
        const auto& old = lead_[1];
        pipper_next_ = Scene::project(c, {old[0] / 256.0, old[1] / 256.0, old[2] / 256.0}, 159);
        const auto& v = aircraft_.view;
        lead_[1] = lead_[0];
        lead_[0] = {aircraft_.x + ((v[1] * 0x600 + v[2] * 0x4000) >> 6),
                    aircraft_.y + ((v[4] * 0x600 + v[5] * 0x4000) >> 6),
                    aircraft_.z + ((v[7] * 0x600 + v[8] * 0x4000) >> 6)};
    }

    // The missiles and projectiles in flight, drawn back along their last
    // move by the part of the tick not yet shown.
    void add_weapons(std::vector<Vehicle>& vehicles, std::vector<Spark>& sparks, double t) const {
        const double back = smooth_ ? 1 - t : 0;
        for (const auto& slot : weapons_.missiles()) {
            if (!slot) continue;
            const auto& b = slot->body;
            Vehicle v;
            v.type = slot->type;
            v.position = {(b.x - b.vx * back) / 256.0, (b.y - b.vy * back) / 256.0, (b.z - b.vz * back) / 256.0};
            v.body = b.body;
            vehicles.push_back(v);
        }
        for (const auto& slot : weapons_.shots()) {
            if (!slot) continue;
            const auto& s = *slot;
            const double k = (s.moved && !s.hit) ? back : 0;
            Spark p;
            p.position = {(s.x - s.vx * k) / 256.0, (s.y - s.vy * k) / 256.0, (s.z - s.vz * k) / 256.0};
            p.burst = s.hit || s.kind == Shot::Kind::chaff;
            sparks.push_back(p);
        }
    }

    // The practice's lead, drawn with the F-16's shape.
    void add_lead(std::vector<Vehicle>& vehicles, double t) const {
        if (mode_ != FlightMode::practice || !lead_active_) return;
        Vehicle v;
        v.type = 3;
        const double back = smooth_ ? 1 - t : 0;
        v.position = {(leader_.x - leader_.vx * back) / 256.0, (leader_.y - leader_.vy * back) / 256.0,
                      (leader_.z - leader_.vz * back) / 256.0};
        v.body = leader_.body;
        vehicles.push_back(v);
    }

    // `$13AB96` as the object lists know it: free flight 1, the
    // qualification 9, the demo $7F. (The maneuvers' trainer and the
    // practice's lead are their own records here; their world is free
    // flight's.)
    int list_mode() const {
        switch (mode_) {
        case FlightMode::demo: return 0x7F;
        case FlightMode::qualification: return 9;
        case FlightMode::mission: return mission_;
        default: return 1;
        }
    }

    // $11CF06: the records the flight starts with; the ship among them is
    // the carrier.
    void spawn_objects() {
        ObjectChoice choice;
        choice.mode = list_mode();
        choice.variant_random = int(random_() & 0xFFFF);
        choice.level = level_;
        if (mode_ == FlightMode::mission) choice.stage = pilot_.stage(mission_);
        const auto spawned = lists_.spawn(choice, tables_);
        objects_ = spawned.objects;
        list_start_ = spawned.first_entry;
        for (const auto& o : objects_) {
            if (o.craft.type == 0x20) carrier_.place(o.craft, tables_);
        }
    }

    // The object step for the lists' records, in record order (`$116C4E`
    // ...): the others' part (`$11797E`), the autopilot, the flight step.
    // (Their touches, weapons and crashes are not built yet.)
    void objects_tick() {
        Records records{&aircraft_, player_distance_, &objects_};
        StepWorld world;
        world.locked = radar_.locked();
        world.mode = std::uint8_t(mission_mode());
        world.level = level_;
        world.list = list_start_;
        for (auto& o : objects_) {
            auto& a = o.craft;
            if (!o.active() || (a.type & 0xF0) != 0x10) continue;
            object_step_.before_flight(o, records, world);
            PilotWorld pilot_world;
            pilot_world.mode = world.mode;
            pilot_world.level = world.level;
            autopilot_.fly(a, o.pilot, pilot_world, &o.course);
            FlightControls c;
            c.recorded = a.controls;
            const auto roll_before = a.attitude.roll;
            FlightRules rules;
            rules.world = &world_;
            if (a.state & 0x1000) fly(a, c, rules, tables_);
            Autopilot::level_out(a, o.pilot, roll_before);
        }
    }

    // The radar after the scene ($10FF26) and in the panel ($125E22) when
    // the cockpit shows; a change of view or look draws the panel anew
    // ($10F024).
    void radar_tick() {
        static constexpr int look_shift[14] = {0, -7, -14, -21, -50, 50, 50, 50, 50, 21, 14, 7, 50, 50};
        const int look = outside_ ? 50 : look_shift[look_];
        if (outside_ != radar_outside_ || look != radar_look_) radar_.redraw_panel();
        radar_outside_ = outside_;
        radar_look_ = look;
        Records records{&aircraft_, player_distance_, &objects_};
        const auto frame = std::uint16_t(ticks_);
        radar_.after_scene(records, frame, outside_, tables_);
        if (!outside_) {
            radar_.panel(records, frame, look, radar_range_, std::uint8_t(mission_mode()));
            radar_.end_panel(frame);
        }
    }

    // `$13AB96` while flying: the demo flies as mission 3.
    int mission_mode() const { return mode_ == FlightMode::demo ? 3 : list_mode(); }

    // $102DB8 / $1195D4: before a mission's flight, "YOUR VECTOR XXX FOR
    // BOGEY" with the heading from the player to a point 500 units ahead of
    // the list's first aircraft in the air (not type $15), in degrees
    // rounded to tens (360 is 000); without one, no line.
    void queue_bogey_vector() {
        for (const auto& o : objects_) {
            if (o.craft.type == 0x15 || (o.craft.type & 0xF0) != 0x10 || !o.active()) continue;
            const auto& v = o.craft.view;
            const std::int64_t ahead[3] = {(std::int64_t(v[2]) * 0x1F4) >> 4, (std::int64_t(v[5]) * 0x1F4) >> 4,
                                           (std::int64_t(v[8]) * 0x1F4) >> 4};
            const auto dx = std::int32_t((o.craft.x + ahead[0] - aircraft_.x) >> 8);
            const auto dz = std::int32_t((o.craft.z + ahead[2] - aircraft_.z) >> 8);
            int degrees = direction_of(tables_, dx, 0, dz).heading / 80;
            degrees = (degrees + 5) / 10 * 10;
            if (degrees >= 360) degrees = 0;
            const auto found = std::find_if(assets_.texts.begin(), assets_.texts.end(),
                                            [](const data::TextBlock& b) { return b.number == bogey_text; });
            if (found == assets_.texts.end()) return;
            auto block = *found;
            char digits[4];
            std::snprintf(digits, sizeof digits, "%03d", degrees);
            for (auto& line : block.lines) {
                const auto at = line.text.find("XXX");
                if (at != std::string::npos) line.text.replace(at, 3, digits);
            }
            text_.queue(block);
            return;
        }
    }

    // The other records (not the carrier, which is drawn by itself).
    void add_objects(std::vector<Vehicle>& vehicles, double t) const {
        const double back = smooth_ ? 1 - t : 0;
        for (const auto& o : objects_) {
            if (!o.active() || (o.craft.type & 0xF0) == 0x20) continue; // (the carrier: carrier_vehicle)
            const auto& a = o.craft;
            Vehicle v;
            v.type = o.shape_type;
            v.position = {(a.x - a.vx * back) / 256.0, (a.y - a.vy * back) / 256.0, (a.z - a.vz * back) / 256.0};
            v.body = a.body;
            vehicles.push_back(v);
        }
    }

    Vehicle carrier_vehicle() const {
        Vehicle v;
        v.type = carrier_.shape_type;
        v.position = {carrier_.position[0] / 256.0, carrier_.position[1] / 256.0, carrier_.position[2] / 256.0};
        v.body = carrier_.body;
        return v;
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
        fade_in,      // $101B54 / $101B9C: the demo's screen fades in (`$13AB90`)
        fade_out,     // $101A96: the demo's end, the screen fading out before it starts again
        briefing,     // $101CC0 / $101D06: the qualification's text, until typed or a key
        landed,       // $1031F4 / $103AA8: the qualifying landing's texts, then a new launch
        lead_lost,    // $103A4C: the practice's lead lost: block 72, then the main menu
        mission_text, // $1031F4: a mission's texts over the paused flight
        descent,      // $102D58 with the outside camera's flight down (`$13AB9E` = 6)
        to_ready,     // $102D58: five frames in the cockpit
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
        mode_ = start.mode;
        if (mode_ == FlightMode::demo) {
            begin_demo();
            return;
        }
        if (mode_ == FlightMode::qualification) {
            begin_qualification(start.again);
            return;
        }
        if (mode_ == FlightMode::maneuvers) {
            begin_maneuvers(start.again);
            return;
        }
        if (mode_ == FlightMode::practice) {
            begin_practice();
            return;
        }
        if (mode_ == FlightMode::mission) {
            begin_mission(start.mission, start.again);
            return;
        }
        fade_ = 15;
        place_ = start.place;
        aircraft_type_ = start.aircraft;
        aircraft_left_ = aircraft_given;
        text_.clear();
        text_.queue(place_ == carrier_place ? launch_text : ready_text);
        sounds_.set_active(true);
        restart();
        start_descent();
    }

    // The outside camera comes down from the map to the aircraft first
    // ($102C6E: aimed 23,040 units behind it, $10D44A).
    void start_descent() {
        flying_ = false;
        ready_done_ = false;
        descent_ = Descent{};
        descent_.target = plane_offset(0, 0, -0x1680);
        outside_ = true;
        above_ = false;
        position_ = 0;
        chase_ = chase_was_ = {descent_.camera[0] / 256.0, descent_.camera[1] / 256.0, descent_.camera[2] / 256.0};
        chase_set_ = true;
        cam_angles_ = cam_angles_was_ = {0x1C20, 0}; // straight down, as the map ($10D2B2)
        phase_ = Phase::descent;
    }

    // A mission ($1020CC, $102534 ...): three aircraft; the screen fades
    // in, then (chosen from the menu) the briefing: block 91 and the
    // mission's first (its stage 0) or later text, the security check
    // (`$1027C8`: passed here, the disk being cracked), block 93 and the
    // orders (`$1028DA`: missions started + 1); then the camera's descent,
    // the bogey's vector and "READY ...". Mission 3 starts on runway 1,
    // the others on the carrier.
    void begin_mission(int mission, bool again) {
        mission_ = mission;
        brief_ = !again;
        place_ = mission == 3 ? 0 : carrier_place;
        aircraft_type_ = 0x11;
        aircraft_left_ = 3; // ($1020BC)
        level_ = std::int8_t(std::min<int>(pilot_.level, 3));
        mission_result_ = 0;
        airborne_ = false;
        text_.clear();
        sounds_.set_active(true);
        restart();
        flying_ = false;
        ready_done_ = false;
        outside_ = above_ = false;
        look_ = 0;
        zoom_ = 0x80;
        fade_ = 0;
        fade_ticks_ = 0;
        briefing_key_ = false;
        briefing_part_ = 0;
        phase_ = Phase::fade_in;
    }

    // `$133FFE` + `$1340DA`: the mission's five text blocks (first
    // briefing, later briefing, the security check, the orders, success).
    int mission_text(int index) const {
        const auto& m = assets_.program;
        const std::uint32_t at = 0x133FFE + std::uint32_t(m.s16(0x1340DA + 2u * std::uint32_t(mission_ - 3)));
        return m.s16(at + 2u * std::uint32_t(index));
    }

    // After the briefing: the descent, the vector and the "ready" line.
    void mission_descent() {
        text_.clear();
        queue_bogey_vector();
        text_.queue(place_ == carrier_place ? launch_text : ready_text);
        start_descent();
    }

    // $118650 ... (each frame of a mission's flight): the objective (mission
    // 3: the radar's confirmation, `$13AB5F` < 0), then the landing
    // (`$118BA2`: on the ground at a standstill on a runway, or the deck
    // when it started on the carrier, after having flown - +$21 bit 0,
    // hypothesis: set at take-off). Each pauses the flight for its texts
    // (`$1031F4`): the success text and "RETURN TO BASE / CARRIER"; then
    // "YOUR LOG NOW REFLECTS THIS COMPLETED MISSION" and the Shift-Esc hint,
    // with the mission completed in the log (`$1034A0`).
    void mission_tick() {
        if (mode_ != FlightMode::mission) return;
        if (mission_result_ == 0) {
            const bool done = mission_ == 3 && radar_.confirmation() < 0;
            if (!done) return;
            mission_result_ = 1;
            text_.clear();
            text_.queue(mission_text(4));
            text_.queue(mission_ == 3 ? 85 : 86);
            pause_for_texts();
            return;
        }
        if (mission_result_ == 1) {
            const bool on_place = place_ == carrier_place ? (aircraft_.surface & 0xC0) != 0 : (aircraft_.surface & 0x04) != 0;
            if (!airborne_ || !(aircraft_.motion & flag::on_ground) || !on_place || aircraft_.airspeed != 0) return;
            mission_result_ = 4;
            complete_mission();
            text_.clear();
            text_.queue(84);
            text_.queue(menu_hint_text);
            pause_for_texts();
        }
    }

    // $1034A0: the log takes the mission: the last mission and its stage,
    // completed + 1, the level and the mission's stage + 1 (at most 3);
    // written ($10858A).
    void complete_mission() {
        pilot_.last_mission = std::uint8_t(mission_);
        pilot_.last_stage = pilot_.stage(mission_);
        ++pilot_.missions_completed;
        level_ = std::int8_t(std::min(level_ + 1, 3));
        pilot_.level = std::uint16_t(level_);
        pilot_.set_stage(mission_, std::uint8_t(std::min(pilot_.stage(mission_) + 1, 3)));
        write_pilot(content_, pilot_);
    }

    void pause_for_texts() {
        flying_ = false;
        phase_ = Phase::mission_text;
    }

    // The demo ($10215E, $101BF4): mission 3's start on the carrier's
    // catapult with three aircraft, the screen fading in, the bogey's
    // vector and "READY TO LAUNCH" typed, then the recording flies it.
    void begin_demo() {
        place_ = carrier_place;
        aircraft_type_ = 0x11;
        aircraft_left_ = 3; // ($1020BC)
        sounds_.set_active(true);
        restart();
        text_.clear();
        queue_bogey_vector();
        text_.queue(launch_text);
        flying_ = false;
        ready_done_ = false;
        outside_ = above_ = false;
        look_ = 0;
        zoom_ = 0x80;
        fade_ = 0;
        fade_ticks_ = 0;
        replay_.start();
        replay_game_ = replay_view_ = 0;
        replay_ended_ = false;
        phase_ = Phase::fade_in;
    }

    // The maneuvers' demo ($102190): the trainer (record 4, the object list
    // of mode 2) flies them, seen from outside (position 3, distance 1); the
    // screen fades in, briefing 9 (also after Esc: there is no check of
    // `$13AB1B` here), then the flight with no "ready" text.
    void begin_maneuvers(bool again) {
        (void)again;
        place_ = 0;
        aircraft_type_ = 0x10; // (the trainer's shape: the F-16's)
        aircraft_left_ = 3;
        text_.clear();
        sounds_.set_active(true);
        restart();
        spawn_trainer();
        before_ = aircraft_;
        flying_ = false;
        ready_done_ = true;
        outside_ = true;
        above_ = false;
        position_ = 3;
        distance_ = 1;
        chase_set_ = false;
        look_ = 0;
        zoom_ = 0x80;
        maneuver_ = 0;
        maneuver_started_ = false;
        skip_maneuver_ = false;
        maneuvers_running_ = false;
        brief_ = true;
        fade_ = 0;
        fade_ticks_ = 0;
        briefing_key_ = false;
        phase_ = Phase::fade_in;
    }

    // $11D318 for mode 2's second entry: the F-16 in record 4 on path $2E,
    // at its height with the gear up, speed $2000, level; the training's
    // flag (+2 bit 0) set ($1021DA).
    void spawn_trainer() {
        const auto& m = assets_.program;
        const std::uint32_t path = 0x11DDC0 + std::uint32_t(m.s16(0x11DDC0 + 0x2E));
        Aircraft a = start_at_place(m, tables_, 0);
        a.x = (m.s16(path) << 22) + (m.s16(path + 4) << 8);
        a.z = (m.s16(path + 2) << 22) + (m.s16(path + 6) << 8);
        a.y = m.s16(path + 8) << 8;
        a.attitude = {0, 0, 0};
        a.body = body_matrix(tables_, a.attitude);
        a.view = view_matrix(tables_, a.attitude);
        a.type = 0x10;
        a.player = false;
        a.state = 0x10C8 | 0x0140;
        a.motion = 0x0100;
        a.surface = 0;
        a.gear = 0xE0;
        a.speed = a.airspeed = 0x2000;
        a.vx = a.vy = a.vz = 0;
        a.throttle = 0x60;
        a.role = 0;
        a.timer = 0;
        aircraft_ = a;
        trainer_pilot_ = 0x10;
    }

    // $1172AA for mode 2: the maneuvers one after the other ($11756A), the
    // trainer's role set to each and waited for; Return skips to the next
    // one; after the seventh the trainer starts again from its place.
    void training_step() {
        if (!maneuvers_running_) return; // ($13AA83, from the flight's start)
        const auto& m = assets_.program;
        if (skip_maneuver_) {
            skip_maneuver_ = false;
            spawn_trainer();
            next_maneuver();
        } else if (maneuver_ == 0) {
            // ($117354: the first frame: the engine's start, then on.)
            aircraft_.throttle = 0x60;
            sounds_.event(EngineSound::start);
            next_maneuver();
        }
        for (int guard = 0; guard < 2; ++guard) {
            if (!maneuver_started_) {
                aircraft_.role = std::uint8_t(m.u32(0x11756A + std::uint32_t(8 * maneuver_) + 4) & 0xFF);
                maneuver_started_ = true;
                aircraft_.motion &= ~0x0800;
                return;
            }
            if (aircraft_.role != role::none) return;
            // Done: the next one ($11739C, $1173EA).
            aircraft_.controls &= 0xC3;
            if (maneuver_ >= 7) spawn_trainer();
            next_maneuver();
        }
    }

    // $117478: the next maneuver (1-7 round), the camera's place for it,
    // its name in the message line ("TOO LOW FOR SPLIT-S" when so).
    void next_maneuver() {
        const auto& m = assets_.program;
        maneuver_ = maneuver_ >= 7 ? 1 : maneuver_ + 1;
        maneuver_started_ = false;
        aircraft_.controls = 0;
        const std::uint32_t view = m.u32(0x11756A + std::uint32_t(8 * maneuver_));
        position_ = int(view & 0xFF);
        if (outside_) {
            const int d = int((view >> 8) & 0xFF);
            distance_ = d > 0 ? d : -1;
        }
        sounds_.set_view(outside_, position_, distance_);
        if (maneuver_ == 4 && aircraft_.y <= 0x100000) messages_.post(0x35);
        else messages_.post(m.u16(0x117522 + std::uint32_t(8 * maneuver_) + 2));
    }

    // The trainer's frame in the object step: the training, its timer
    // ($1179A4), the autopilot, the flight step, the attitude step's
    // levelling.
    void trainer_tick() {
        training_step();
        aircraft_.timer = std::int16_t(aircraft_.timer - 1);
        PilotWorld world;
        world.mode = 2;
        world.practice = maneuvers_running_;
        autopilot_.fly(aircraft_, trainer_pilot_, world);
        FlightControls c;
        c.recorded = aircraft_.controls;
        const auto roll_before = aircraft_.attitude.roll;
        FlightRules rules;
        rules.world = &world_;
        const auto events = fly(aircraft_, c, rules, tables_);
        Autopilot::level_out(aircraft_, trainer_pilot_, roll_before);
        if (events.engine_sound != 0) sounds_.event(EngineSound(events.engine_sound));
        chase_camera();
        ++ticks_;
    }

    // $101854 / $103B24: the flight stops, the screen fades out (`$13AB91`
    // 0), then the selection starts again (`$101A96`).
    void start_again() {
        flying_ = false;
        fade_ticks_ = 0;
        phase_ = Phase::fade_out;
    }

    // The qualification ($102252): on the carrier's catapult with three
    // aircraft, the screen fading in, then - chosen from the menu, after
    // the security check (`$1027C8`, `$13AB1B` clear) - the briefing
    // (`$101CC0`), then "READY TO LAUNCH". Started again (Esc, a landing)
    // the check and the briefing are left out.
    void begin_qualification(bool again) {
        brief_ = !again;
        place_ = carrier_place;
        aircraft_type_ = 0x11;
        aircraft_left_ = 3; // ($1020BC)
        text_.clear();
        sounds_.set_active(true);
        restart();
        flying_ = false;
        ready_done_ = false;
        outside_ = above_ = false;
        look_ = 0;
        zoom_ = 0x80;
        fade_ = 0;
        fade_ticks_ = 0;
        briefing_key_ = false;
        phase_ = Phase::fade_in;
    }

    // Free flight ends (Esc, Shift-Esc, the end text): every sound stops.
    void leave_flight() {
        flash_left_ = 0;
        sounds_.stop_all();
        sounds_.set_active(false);
        in_front_ = true;
    }

    void remember_keys(const app::GameInput& input) {
        f_was_[0] = input.f1;
        f_was_[1] = input.f3;
        f_was_[2] = input.f5;
        for (int d = 0; d < 10; ++d) ctrl_digits_was_[std::size_t(d)] = input.ctrl_keys[key::digits[d]];
        keys_was_ = input.c64_keys;
        fire_was_ = input.fire;
        ctrl_tab_was_ = input.ctrl_keys[key::tab];
        ctrl_two_was_ = input.ctrl_keys[key::digits[2]];
    }

    // $10D51E: the aircraft at the chosen start; the engine sound starts.
    void restart() {
        radar_.reset();
        player_distance_ = 0;
        spawn_objects();
        if (place_ == carrier_place) {
            aircraft_ = carrier_.start_on_deck(assets_.program, tables_);
        } else {
            aircraft_ = start_at_place(assets_.program, tables_, place_);
            aircraft_.type = std::uint8_t(aircraft_type_); // ($10D5A8: $13AB39 into +$62)
            // The wheels' surface, as the first contact would find it.
            aircraft_.surface = std::uint8_t(world_.on_runway(aircraft_.x / 256.0, aircraft_.z / 256.0) ? 4 : 0);
        }
        before_ = aircraft_;
        weapons_.reset();
        lead_ = {};
        pipper_.reset();
        pipper_next_.reset();
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
        case Phase::fade_in:
            // The interrupt's fade: a step every third blank up to 15
            // (`$1093F2`), then the frames before the text ($101BD0).
            if (++fade_ticks_ % 3 == 0 && fade_ < 15) ++fade_;
            if (fade_ == 15) {
                if (mode_ == FlightMode::maneuvers || mode_ == FlightMode::practice) {
                    sounds_.set_active(false);
                    text_.clear();
                    text_.queue(mode_ == FlightMode::practice ? practice_text : maneuvers_text);
                    phase_ = Phase::briefing;
                    break;
                }
                if (mode_ == FlightMode::qualification && brief_) {
                    // ($101CD4: the sound update stops while it is typed.)
                    sounds_.set_active(false);
                    text_.clear();
                    text_.queue(qualification_text);
                    phase_ = Phase::briefing;
                    break;
                }
                if (mode_ == FlightMode::mission) {
                    if (!brief_) {
                        mission_descent();
                        break;
                    }
                    sounds_.set_active(false);
                    text_.clear();
                    text_.queue(91);
                    text_.queue(mission_text(pilot_.stage(mission_) == 0 ? 0 : 1));
                    briefing_part_ = 1;
                    briefing_key_ = false;
                    phase_ = Phase::briefing;
                    break;
                }
                if (mode_ == FlightMode::qualification) text_.queue(launch_text);
                countdown_ = 5;
                phase_ = Phase::to_ready;
            }
            break;
        case Phase::briefing:
            if (!text_.done() && text_.tick()) sounds_.click();
            if ((text_.done() || briefing_key_) && mode_ == FlightMode::maneuvers) {
                // $1024C4 / $102DB8 / $102E4E: no text, the flight starts
                // ($13AA83 set), three frames, the beep ($102EDA).
                sounds_.set_active(true);
                text_.clear();
                maneuvers_running_ = true;
                countdown_ = 3;
                phase_ = Phase::ready;
                break;
            }
            if ((text_.done() || briefing_key_) && mode_ == FlightMode::practice) {
                // $1024F8: the camera's descent from the aircraft itself
                // (`$11DC7E`, state 3), then "READY FOR TAKEOFF".
                sounds_.set_active(true);
                text_.clear();
                text_.queue(ready_text);
                descent_ = Descent{};
                descent_.state = 3;
                descent_.camera = {aircraft_.x, aircraft_.y, aircraft_.z};
                descent_.target = descent_.camera;
                outside_ = true;
                position_ = 0;
                chase_ = chase_was_ = {aircraft_.x / 256.0, aircraft_.y / 256.0, aircraft_.z / 256.0};
                chase_set_ = true;
                phase_ = Phase::descent;
                break;
            }
            if ((text_.done() || briefing_key_) && mode_ == FlightMode::mission) {
                briefing_key_ = false;
                if (briefing_part_ == 1) {
                    text_.clear();
                    text_.queue(93);
                    text_.queue(mission_text(3));
                    ++pilot_.missions_started; // (written with the log)
                    briefing_part_ = 2;
                    break;
                }
                sounds_.set_active(true);
                mission_descent();
                break;
            }
            if (text_.done() || briefing_key_) {
                sounds_.set_active(true); // ($101D18)
                text_.clear();
                text_.queue(launch_text);
                countdown_ = 5;
                phase_ = Phase::to_ready;
            }
            break;
        case Phase::lead_lost:
            if (!text_.done()) {
                if (text_.tick()) sounds_.click();
                if (text_.done()) countdown_ = 0x64 / 6; // (as after the landing)
            } else if (countdown_ < 0) {
                leave_flight();
                front_.to_menu();
            }
            break;
        case Phase::mission_text:
            // ($102EFE: the flight goes on once they are typed; the wait
            // after them is the landing's - hypothesis.)
            if (!text_.done()) {
                if (text_.tick()) sounds_.click();
                if (text_.done()) countdown_ = 0x64 / 6;
            } else if (countdown_ < 0) {
                text_.clear();
                flying_ = true;
                phase_ = Phase::flying;
            }
            break;
        case Phase::landed:
            // The texts typed over the stopped view ($103AA8), 100 passes
            // of the main loop ($13ADC6 = $64), then the selection again
            // ($103B24). With `$13AA85` clear a pass neither draws nor
            // waits for the frame cap (`$101140`): its length is the
            // machine's. Port decision: the A500's, about 6 passes a
            // blank (the demo's recording: 18 a fade step of 3 blanks),
            // so 17 blanks.
            if (!text_.done()) {
                if (text_.tick()) sounds_.click();
                if (text_.done()) countdown_ = 0x64 / 6;
            } else if (countdown_ < 0) {
                start_again();
            }
            break;
        case Phase::fade_out:
            // ($1093F2: a step down every second blank.) Then $101A96 /
            // $101AE2: the demo starts itself again; anything else goes back
            // to its chosen line in the menu ($101E04, `$13AB1B` set).
            if (++fade_ticks_ % 2 == 0 && fade_ > 0) --fade_;
            if (fade_ == 0) {
                if (mode_ == FlightMode::demo) {
                    begin_demo();
                } else {
                    leave_flight();
                    // ($101AF4: after the maneuvers' demo comes their practice.)
                    if (mode_ == FlightMode::mission) front_.again_mission(mission_);
                    else front_.again(mode_ == FlightMode::qualification ? 5 :
                                 (mode_ == FlightMode::maneuvers || mode_ == FlightMode::practice) ? 4 : 2);
                }
            }
            break;
        case Phase::descent:
            descend();
            chase_was_ = chase_;
            chase_ = {descent_.camera[0] / 256.0, descent_.camera[1] / 256.0, descent_.camera[2] / 256.0};
            aim_camera(0x230);
            if (descent_.done) {
                // $11DD1C: the view switches to the cockpit ($1041E8), the
                // text follows five frames on ($102D58).
                outside_ = false;
                chase_set_ = false;
                countdown_ = 5;
                phase_ = Phase::to_ready;
            }
            break;
        case Phase::to_ready:
            if (countdown_ < 0) phase_ = Phase::ready;
            break;
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
                ticks_ = 0; // ($102EC0: the frame counter `$13ABCA` from 0)
                flying_ = true;
                phase_ = Phase::flying;
            }
            break;
        case Phase::flying:
            if (mode_ == FlightMode::practice && !lead_active_ && !(aircraft_.state & flag::crashed)) {
                // $118AEC / $1032AA: the lead lost: block 72 over the view,
                // 100 passes, the main menu ($103A4C, $101A70).
                flying_ = false;
                text_.clear();
                text_.queue(lead_lost_text);
                phase_ = Phase::lead_lost;
                break;
            }
            if (replay_ended_) {
                // $10EF08 / $101854: the recording's end pauses the flight
                // and starts the selection again.
                replay_ended_ = false;
                start_again();
                break;
            }
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
            text_.queue(aircraft_type_ == 0x10 ? end_text_f16 : end_text_fa18);
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
        if (mode_ == FlightMode::demo) {
            // The demo's recording ($10DFEE): last frame's key events go to
            // the handlers as this frame starts ($101544), then this
            // frame's control byte flies the aircraft.
            for (int k = 0; k < replay_key::game_count; ++k)
                if (replay_game_ & (1u << k)) game_key(replay_key::Game(k));
            for (int k = 0; k < replay_key::view_count; ++k)
                if (replay_view_ & (1u << k)) view_key(replay_key::View(k));
            const auto f = replay_.next();
            c.recorded = f.control;
            replay_game_ = f.game;
            replay_view_ = f.view;
            replay_ended_ = f.ended;
            remember_keys(input);
        } else {
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
            if (pressed(key::g)) game_key(replay_key::gear);
            view_keys(input);
            if (pressed(key::backspace)) game_key(replay_key::speed_brake); // the original's \ or Backspace
            if (pressed(key::b)) game_key(replay_key::hook);                // the original's A
            // The weapons: Return the next one ($10E90E), space fires
            // ($10F0AA, its release $10F100), F a flare, C chaff; R the
            // radar's range, T the next target, J the ECM.
            if (mode_ == FlightMode::practice) {
                // $10E8D8: Return re-positions beside the lead while it waits
                // for the trigger, else "NOT YET RDY TO RE-POSITION"; $10F0B6:
                // the fire key is the trigger, the gun is not armed.
                if (pressed(key::return_key)) {
                    if (practice_wait_ < 0) {
                        sounds_.refuse();
                        reposition_ = true;
                    } else {
                        messages_.post(0x401F);
                    }
                }
                if (input.fire && !fire_was_ && practice_wait_ < 0) trigger_ = true;
            } else {
                if (pressed(key::return_key)) game_key(replay_key::weapon);
                if (input.fire != fire_was_) game_key(input.fire ? replay_key::fire : replay_key::fire_up);
            }
            if (pressed(key::f)) game_key(replay_key::flare);
            if (pressed(key::c)) game_key(replay_key::chaff);
            if (pressed(key::r)) game_key(replay_key::radar_range);
            if (pressed(key::t)) game_key(replay_key::target);
            if (pressed(key::j)) game_key(replay_key::ecm);
            remember_keys(input);
        }

        ground_warnings(aircraft_, messages_, sounds_);
        FlightRules rules;
        rules.world = &world_;
        if (mode_ == FlightMode::practice) reposition_step();
        // $1183E2: the distance to the locked record, before the move.
        {
            const Aircraft* locked = nullptr;
            for (const auto& o : objects_) {
                if (radar_.locked() > 0 && o.record == radar_.locked()) locked = &o.craft;
            }
            object_step_.measure_player(aircraft_, player_distance_, locked);
        }
        const std::array<std::int32_t, 3> before{aircraft_.x, aircraft_.y, aircraft_.z};
        const Aircraft unmoved = aircraft_;
        const auto events = fly(aircraft_, c, rules, tables_);
        // $11B6A2 (after the move, by the place before it): the carrier's
        // deck, wires and hull, then the other objects.
        if (carrier_.touch(aircraft_, unmoved)) aircraft_.state |= flag::crashed;
        player_touches(before);
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
        // $118AA8 (the qualification): standing on the deck held by a wire
        // with the hook down qualifies the pilot ($1032C2: config +0 set
        // and written).
        if (mode_ == FlightMode::qualification && (aircraft_.motion & 0xC080) == 0xC080 && aircraft_.airspeed == 0 &&
            !(aircraft_.state & flag::crashed)) {
            pilot_.qualified = true;
            write_pilot(content_, pilot_);
            flying_ = false;
            text_.clear();
            text_.queue(landed_text);
            text_.queue(menu_hint_text);
            phase_ = Phase::landed;
        }
        // The object step's missile records, then (with the drawing) the
        // projectiles and the gun's lead point.
        weapons_.move_missiles(aircraft_, rules, sounds_);
        weapons_.move_shots(aircraft_, outside_, std::uint16_t(ticks_), sounds_);
        lead_point();
        if (mode_ == FlightMode::practice) lead_tick();
        objects_tick();
        radar_tick();
        if (events.took_off) airborne_ = true;
        mission_tick();

        chase_camera();
        ++ticks_;
    }

    // The bodies that touch: the player's aircraft (group 0) and the
    // practice's lead (record 4, group 4).
    std::vector<Body> bodies() {
        std::vector<Body> b{{&aircraft_, 0, true}};
        if (mode_ == FlightMode::practice) b.push_back({&leader_, 4, lead_active_});
        return b;
    }

    // $11A7EA for the player: the touch after the move and what it does
    // ($11A88E): out of control (state bit 15) and, faster than $3C0, by
    // the frame counter's bit 0 "TURBINE FAILURE" and a spin - unless
    // flares or chaff are out - or "MINOR HYDRAULIC / STRUCTURE DAMAGE"
    // (bit 1). A spin on the ground blows the aircraft up ($11AA80).
    void player_touches(const std::array<std::int32_t, 3>& before) {
        auto list = bodies();
        if (list.size() > 1 && !(aircraft_.state & 0x0400)) {
            bool near = false;
            if (touch_bodies(list, 0, before, near) == touch::object) {
                aircraft_.state |= 0x8000;
                if (aircraft_.airspeed > 0x3C0) {
                    if ((ticks_ & 1) == 0) {
                        if (!weapons_.decoys_out()) {
                            aircraft_.handling |= 0x02;
                            messages_.post(0x9014);
                        }
                    } else {
                        messages_.post((ticks_ & 2) ? 0xD00C : 0xD00B);
                    }
                }
                return;
            }
            if (near) aircraft_.motion |= 0x0002; // ($11AAE0)
            else aircraft_.motion &= ~0x0002;
        }
        if ((aircraft_.handling & 0x02) && (aircraft_.motion & flag::on_ground) && !(aircraft_.state & 0x0400))
            aircraft_.state |= flag::crashed;
    }

    // The practice ($7D, $102202): free flight's start at runway 3 with one
    // aircraft, briefing 10 first; the lead - a copy of the player's
    // record with the F-16's shape, 72 units ahead (`$11D0AA`) - starts
    // once the player moves, climbs (roles 35-38), and flies each
    // maneuver when triggered.
    void begin_practice() {
        place_ = 2;
        aircraft_type_ = 0x11;
        aircraft_left_ = 1; // ($102202)
        text_.clear();
        sounds_.set_active(true);
        restart();
        spawn_lead();
        flying_ = false;
        ready_done_ = false;
        outside_ = above_ = false;
        look_ = 0;
        zoom_ = 0x80;
        maneuver_ = 0;
        maneuver_started_ = false;
        practice_wait_ = 0;
        trigger_ = reposition_ = false;
        brief_ = true;
        fade_ = 0;
        fade_ticks_ = 0;
        briefing_key_ = false;
        phase_ = Phase::fade_in;
    }

    void spawn_lead() {
        leader_ = aircraft_;
        const auto p = plane_offset(0, 0, 0x48);
        leader_.x = std::int32_t(p[0]);
        leader_.y = std::int32_t(p[1]);
        leader_.z = std::int32_t(p[2]);
        leader_.type = 0x10;
        leader_.player = false;
        leader_.role = 0;
        leader_.timer = 0;
        lead_pilot_ = 0;
        lead_active_ = true;
    }

    // Record 4's frame in the practice: the exploding and crashed states
    // (`$11A398`, `$1169D0`), the training step ($1172AA for $7D), the
    // timer, the autopilot, the flight step and its touch ($11A996).
    void lead_tick() {
        if (!lead_active_) return;
        auto& g = leader_;
        if (g.state & 0x0400) {
            if (g.timer <= 0) {
                g.state &= ~0x0400;
                if ((g.motion & flag::on_ground) || (g.state & 0x8000)) {
                    lead_active_ = false; // (removed)
                    return;
                }
            }
            g.state &= ~0x2000;
        }
        practice_step();
        g.timer = std::int16_t(g.timer - 1);
        if (g.state & 0x0200) { // ($1169D0: crashed becomes exploding)
            g.timer = 15;
            g.state = std::uint16_t((g.state & ~0x0200) | 0x0400);
        }
        if ((g.handling & 0x02) && (g.state & 0x1000)) {
            // $1179C0: spinning, the speed goes toward $1200 by $90 (and the
            // autopilot heads for the spot it is over: the missions' path
            // following, not built).
            g.speed = std::int16_t(g.speed > 0x1200 ? std::max(g.speed - 0x90, 0x1200) : std::min(g.speed + 0x90, 0x1200));
            g.airspeed = g.speed;
            g.motion &= 0xDFF7;
        }
        PilotWorld world;
        world.mode = 0x7D;
        world.practice_ready = practice_wait_ < 0;
        autopilot_.fly(g, lead_pilot_, world);
        if (!(g.state & 0x1000)) return;
        FlightControls c;
        c.recorded = g.controls;
        const auto roll_before = g.attitude.roll;
        const std::array<std::int32_t, 3> before{g.x, g.y, g.z};
        FlightRules rules;
        rules.world = &world_;
        fly(g, c, rules, tables_);
        Autopilot::level_out(g, lead_pilot_, roll_before);
        if (g.state & 0x0400) {
            // ($11A7DA: no touches while it blows up; it stops after this
            // move - seen in `work/ref/prac.jsonl`, frame 271.)
            g.state &= ~0x1000;
            return;
        }
        auto list = bodies();
        bool near = false;
        if (touch_bodies(list, 1, before, near) == touch::object) {
            // $11A9DE / $11AA04: it spins, and is taken off ($11AA70).
            g.handling |= 0x02;
            g.motion |= 0x1000;
            g.role = 1;
            lead_active_ = false;
            return;
        }
        if ((g.handling & 0x02) && (g.motion & flag::on_ground)) {
            g.state &= ~0x1000;
            g.state |= 0x0400;
            g.timer = 15;
        }
    }

    // $1172AA / $11739C for $7D: the lead starts with the player's first
    // move; after each maneuver (and its start, the climb) it waits for
    // the trigger with "TRIGGER FOR: ..." for the next one (`$13AA8A`).
    void practice_step() {
        const auto& m = assets_.program;
        auto& g = leader_;
        if (!(g.motion & 0x0100)) {
            g.role = 0;
            if (aircraft_.airspeed == 0) return;
            g.motion |= 0x0100;
            maneuver_started_ = false;
        }
        for (int guard = 0; guard < 3; ++guard) {
            if (!maneuver_started_) {
                g.role = std::uint8_t(m.u32(0x11756A + std::uint32_t(8 * maneuver_) + 4) & 0xFF);
                maneuver_started_ = true;
                g.motion &= ~0x0800;
                return;
            }
            if (g.role != role::none) return;
            g.controls &= 0xC3;
            if (trigger_) {
                // ($1173EA) the trigger: the next maneuver, flown now.
                trigger_ = false;
                practice_wait_ = 0;
                if (maneuver_ >= 7) {
                    spawn_lead();
                    g.motion |= 0x0100;
                    g.throttle = 0x60;
                }
                practice_next();
                continue;
            }
            if (practice_wait_ < 0) return;
            practice_wait_ = -1;
            const int next = maneuver_ >= 7 ? 1 : maneuver_ + 1;
            messages_.post(m.u16(0x117522 + std::uint32_t(8 * next)));
            return;
        }
    }

    // $117478 in the practice: the next maneuver and its name (the camera
    // follows the player, so it stays).
    void practice_next() {
        const auto& m = assets_.program;
        maneuver_ = maneuver_ >= 7 ? 1 : maneuver_ + 1;
        maneuver_started_ = false;
        leader_.controls = 0;
        if (maneuver_ == 4 && leader_.y <= 0x100000) messages_.post(0x35);
        else messages_.post(m.u16(0x117522 + std::uint32_t(8 * maneuver_) + 2));
    }

    // $117170 (the player's step, after Return in the practice): the next
    // maneuver's prompt, and the player's aircraft put beside the lead (its
    // record copied, the stores kept, 9, 1, -97 from it).
    void reposition_step() {
        if (!reposition_) return;
        reposition_ = false;
        const auto& m = assets_.program;
        if (practice_wait_ != -1) maneuver_ = maneuver_ >= 7 ? 1 : maneuver_ + 1;
        const int next = maneuver_ >= 7 ? 1 : maneuver_ + 1;
        messages_.post(m.u16(0x117522 + std::uint32_t(8 * next)));
        practice_wait_ = -2;
        Aircraft a = leader_;
        a.type = 0x11;
        a.player = true;
        a.motion &= ~0x0100;
        a.role = 0;
        a.controls = 0;
        const auto& v = leader_.view;
        a.x = leader_.x + ((v[0] * 9 + v[1] * 1 + v[2] * -0x61) >> 4);
        a.y = leader_.y + ((v[3] * 9 + v[4] * 1 + v[5] * -0x61) >> 4);
        a.z = leader_.z + ((v[6] * 9 + v[7] * 1 + v[8] * -0x61) >> 4);
        aircraft_ = a;
        before_ = aircraft_;
    }

    // The game's key handlers ($10DAC8), from the keys or the demo's
    // recording.
    void game_key(replay_key::Game k) {
        switch (k) {
        case replay_key::gear:
            // $10E966: the gear handle, refused on the ground.
            if (aircraft_.motion & flag::on_ground) {
                sounds_.refuse();
            } else {
                aircraft_.gear ^= 0x80;
                sounds_.gear_moved();
            }
            break;
        case replay_key::hook:
            // $10E36A (the F/A-18 only): the arresting hook (+2 bit 15; with
            // the wires' bit 14 the full brake), its message for a while.
            // (Hook up's message: hypothesis.)
            if (aircraft_type_ != 0x11) break;
            sounds_.refuse();
            aircraft_.motion ^= 0x8000;
            messages_.post((aircraft_.motion & 0x8000) ? 0x4023 : 0x4022);
            break;
        case replay_key::fire: weapons_.trigger(true); break;
        case replay_key::fire_up: weapons_.trigger(false); break;
        case replay_key::weapon: weapons_.select(sounds_); break;
        case replay_key::speed_brake: aircraft_.state ^= flag::speed_brake; break;
        case replay_key::flare: weapons_.flare(messages_); break;
        case replay_key::chaff: weapons_.chaff(messages_); break;
        case replay_key::radar_range:
            // $10DF1C: 40, 10, 2 miles round (+$63's low nibble D, B, 9).
            sounds_.refuse();
            radar_range_ = radar_range_ == 0x9 ? 0xD : radar_range_ == 0xB ? 0x9 : 0xB;
            break;
        case replay_key::target: // $10DEF8: the radar takes the next target at its next pass
            sounds_.refuse();
            radar_.request_target();
            break;
        case replay_key::ecm:                             // $10EF46
            sounds_.refuse();
            ecm_ = !ecm_;
            break;
        default: break; // Shift-E (the ejection): not built
        }
    }

    // The view keys' handlers by their entry points ($10E3C0-$10E720), as
    // the demo's recording presses them: outside, keypad 8 9 6 3 2 1 4 7 5
    // and . set the camera's positions 0-9; in the cockpit they set the
    // look direction (8 and 9 ahead, 6 and 4 a step round, 2 back, 7 up,
    // 1 down).
    void view_key(replay_key::View k) {
        using namespace replay_key;
        static constexpr int positions[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
        int position = -1, look = -1;
        switch (k) {
        case above:
            above_ = !above_;
            outside_ = true;
            chase_set_ = false;
            return;
        case outside: {
            outside_ = !outside_;
            above_ = false;
            look_ = 0;
            if (outside_) {
                position_ = 4;
                distance_ = -1;
            }
            chase_set_ = false;
            sounds_.set_view(outside_, position_, distance_);
            return;
        }
        case pad_8: position = positions[0]; look = 0; break;
        case pad_9: position = positions[1]; look = 0; break;
        case pad_6: position = positions[2]; look = look_ + 1 > 11 ? 0 : look_ + 1; break;
        case pad_3: position = positions[3]; break;
        case pad_2: position = positions[4]; look = 6; break;
        case pad_1: position = positions[5]; look = 13; break;
        case pad_4: {
            position = positions[6];
            const int l = look_ - 1;
            look = l < 0 ? 11 : (l == 0 || l >= 11) ? 0 : l;
            break;
        }
        case pad_7: position = positions[7]; look = 12; break;
        case pad_5: position = 8; break;
        case pad_point: position = 9; break;
        case nearer:
        case farther:
            if (!outside_) return;
            if (above_) {
                above_height_ = k == nearer ? std::max(above_height_ - 0x2000000 / 256.0, 0x1000000 / 256.0)
                                            : std::min(above_height_ + 0x2000000 / 256.0, 0x8000000 / 256.0);
                return;
            }
            if (k == nearer) distance_ = distance_ <= 1 ? -1 : distance_ - 1;
            else distance_ = distance_ < 0 ? 1 : std::min(4, distance_ + 1);
            sounds_.set_view(outside_, position_, distance_);
            sounds_.event(EngineSound::update);
            return;
        default: return; // the tower view (Shift-Enter) and keypad releases
        }
        if (outside_ || k == pad_3 || k == pad_5 || k == pad_point) {
            if (position >= 0) position_ = position;
            sounds_.set_view(outside_, position_, distance_);
            sounds_.event(EngineSound::update);
            aim_now_ = true;
        } else if (look >= 0) {
            look_ = look;
        }
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
            // `[` / `]` ($10E836, $10E856; here Ctrl with - / =): the zoom
            // `$13AD32` halves (in, kept at $20 or more here: the readout
            // knows 1.0, 2.0 and 4.0) or doubles back up to $80.
            if (nearer && zoom_ > 0x20) zoom_ >>= 1;
            if (farther && zoom_ < 0x80) zoom_ <<= 1;
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
        if (left || right || up || down || nearer || farther) {
            sounds_.event(EngineSound::update);
            aim_now_ = true; // ($12222C: a new view position turns the camera at once)
        }
    }

    // $11D8F2 / $11DD2C: the outside camera's place: the offset of its
    // position ($11D770 for the F/A-18, $11D734 for the F-16; words x, y, z,
    // 4 world units each)
    // scaled by the distance (1: 1.5 times, then doubling), turned by the
    // aircraft's view matrix for the views from behind and ahead and by its
    // heading alone for the others, at least 7 units above the ground; the
    // camera goes half the way there each frame.
    std::array<double, 3> chase_target() const {
        const auto& m = assets_.program;
        const std::uint32_t at = (aircraft_type_ == 0x11 ? 0x11D770 : 0x11D734) + std::uint32_t(6 * position_);
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

    // $10454A: the outside camera's angles turn a quarter of the way to the
    // aircraft each frame, at most `rate` (80ths of a degree); a negative
    // rate (a new view position, or the first frame) turns them at once.
    void aim_camera(int rate) {
        const double dx = aircraft_.x / 256.0 - chase_[0], dy = aircraft_.y / 256.0 - chase_[1],
                     dz = aircraft_.z / 256.0 - chase_[2];
        const auto angle = [](double r) {
            int a = int(std::lround(r * 180 / 3.14159265358979 * 80)) % 0x7080;
            return a < 0 ? a + 0x7080 : a;
        };
        const int want[2] = {angle(std::atan2(-dy, std::sqrt(dx * dx + dz * dz))), angle(std::atan2(-dx, dz))};
        cam_angles_was_ = cam_angles_;
        for (int i = 0; i < 2; ++i) {
            int& a = cam_angles_[std::size_t(i)];
            if (rate < 0) {
                a = want[i];
                continue;
            }
            int d = want[i] - a;
            if (d < 0) {
                if (d < -0x3840) a += std::min((d + 0x7080) >> 2, rate);
                else a -= std::min((-d) >> 2, rate);
            } else if (d <= 0x3840) {
                a += std::min(d >> 2, rate);
            } else {
                a -= std::min((0x7080 - d) >> 2, rate);
            }
            a = ((a % 0x7080) + 0x7080) % 0x7080;
        }
        if (rate < 0) cam_angles_was_ = cam_angles_;
    }

    void chase_camera() {
        if (!outside_) return;
        chase_was_ = chase_;
        const auto target = chase_target();
        if (!chase_set_) {
            chase_ = chase_was_ = target;
            chase_set_ = true;
            aim_camera(-1);
            return;
        }
        chase_ = target; // ($11D9AC: the camera takes its place at once)
        aim_camera(aim_now_ ? -1 : 0x7D0);
        aim_now_ = false;
    }

    // The outside camera's flight down from the start map to the aircraft
    // ($11D9B8 with `$13AB9E` = 6): states `$13AAA6` 0-6 (table `$11D710`),
    // in 32-bit units. Each frame the target's distance (the largest of
    // |dx|, |dy|, |dz|) picks the speed exponent or the next state; the
    // velocity is the direction scaled to 512 and shifted by the exponent,
    // half-way blended with the last one ($11DD2C); the camera adds it.
    struct Descent {
        int state = 0;
        std::array<std::int64_t, 3> camera{0x10800000, 0x3000000, 0x10C00000}; // the map's camera ($10D3D8)
        std::array<std::int64_t, 3> target{}, velocity{};
        int countdown = 0; // $13AB95
        bool done = false;
    };

    std::array<std::int64_t, 3> plane_offset(int ox, int oy, int oz) const {
        // $10D486: the aircraft's place plus the offset turned by its view
        // matrix, the products over 16.
        const auto& m = aircraft_.view;
        const std::int64_t o[3] = {ox, oy, oz};
        std::array<std::int64_t, 3> p{aircraft_.x, aircraft_.y, aircraft_.z};
        for (int r = 0; r < 3; ++r)
            p[std::size_t(r)] += (m[std::size_t(3 * r)] * o[0] + m[std::size_t(3 * r + 1)] * o[1] + m[std::size_t(3 * r + 2)] * o[2]) >> 4;
        return p;
    }

    // $11DBEE: a point on the way, from the camera and the aircraft.
    std::array<std::int64_t, 3> way_point(const std::array<std::int64_t, 3>& c) const {
        const std::array<std::int64_t, 3> p{aircraft_.x, aircraft_.y, aircraft_.z};
        std::array<std::int64_t, 3> m, n, o, q, r;
        for (int i = 0; i < 3; ++i) m[std::size_t(i)] = (c[std::size_t(i)] + p[std::size_t(i)]) >> 1;
        n = {(m[0] + c[0]) >> 1, m[1] >> 1, (m[2] + c[2]) >> 1};
        for (int i = 0; i < 3; ++i) o[std::size_t(i)] = (n[std::size_t(i)] + m[std::size_t(i)]) >> 1;
        for (int i = 0; i < 3; ++i) q[std::size_t(i)] = (o[std::size_t(i)] + m[std::size_t(i)]) >> 1;
        for (int i = 0; i < 3; ++i) r[std::size_t(i)] = (q[std::size_t(i)] + o[std::size_t(i)]) >> 1;
        return r;
    }

    void descend() {
        auto& d = descent_;
        const auto distance = [&] {
            std::int64_t m = 0;
            for (int i = 0; i < 3; ++i) m = std::max(m, std::abs(d.target[std::size_t(i)] - d.camera[std::size_t(i)]));
            return m;
        };
        if (d.state == 6) { // $11DD14
            if (--d.countdown <= 0) d.done = true;
            return;
        }
        const std::int64_t far = distance();
        int exponent = 0;
        const bool left_side = position_ == 1 || position_ == 2; // ($11DC96: the side of the near offsets)
        switch (d.state) {
        case 0:
            exponent = 0xD;
            if (far > 0x1C00000) break;
            exponent = 0xB;
            if (far > 0xD00000) break;
            d.state = 1;
            d.target = way_point(d.camera);
            break;
        case 1:
            exponent = 0xB;
            if (far > 0x200000) break;
            exponent = 0xA;
            if (far > 0x60000) break;
            d.state = 2;
            d.target = way_point(d.camera);
            break;
        case 2:
            exponent = 9;
            if (far > 0x60000) { d.target = way_point(d.camera); break; }
            exponent = 7;
            if (far > 0x30000) { d.target = way_point(d.camera); break; }
            d.state = 3;
            d.target = {aircraft_.x, aircraft_.y, aircraft_.z};
            break;
        case 3:
            d.target = {aircraft_.x, aircraft_.y, aircraft_.z};
            exponent = 7;
            if (far > 0x60000) break;
            exponent = 5;
            if (far > 0xD000) break;
            exponent = 3;
            if (far > 0x8000) break;
            d.state = 4;
            d.target = left_side ? plane_offset(6, 4, 3) : plane_offset(-6, 4, -9);
            break;
        case 4:
            exponent = 2;
            if (far > 0x2800) break;
            d.state = 5;
            d.target = plane_offset(left_side ? 6 : -6, 1, 6);
            break;
        default:
            exponent = 1;
            break;
        }
        // $11DCEA: close enough: a few frames still, then the cockpit.
        if (far <= 0x240) {
            d.state = 6;
            d.countdown = 4;
            return;
        }
        std::array<std::int64_t, 3> delta;
        for (int i = 0; i < 3; ++i) delta[std::size_t(i)] = d.target[std::size_t(i)] - d.camera[std::size_t(i)];
        std::int64_t largest = far;
        while (largest >= 0x4800) {
            for (auto& v : delta) v >>= 2;
            largest >>= 2;
        }
        const double length = std::sqrt(double(delta[0] * delta[0] + delta[1] * delta[1] + delta[2] * delta[2]));
        std::array<std::int64_t, 3> wanted{};
        if (length > 0)
            for (int i = 0; i < 3; ++i)
                wanted[std::size_t(i)] = std::int64_t(double(delta[std::size_t(i)]) * 512 / length) << exponent;
        const bool still = d.velocity[0] == 0 && d.velocity[1] == 0 && d.velocity[2] == 0;
        for (int i = 0; i < 3; ++i) {
            auto& v = d.velocity[std::size_t(i)];
            v = still ? wanted[std::size_t(i)] : v + ((wanted[std::size_t(i)] - v) >> 1);
            d.camera[std::size_t(i)] += v;
        }
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
        // Its own angles ($13AD84 pitch, looking down from 0 to $1C20, and
        // $13AD86 heading, clockwise), between the last two frames.
        const auto between = [t](int a, int b) {
            int d = b - a;
            if (d > 0x3840) d -= 0x7080;
            if (d < -0x3840) d += 0x7080;
            return (a + d * t) / 80.0 * 3.14159265358979 / 180;
        };
        const double p = between(cam_angles_was_[0], cam_angles_[0]), h = between(cam_angles_was_[1], cam_angles_[1]);
        const double f[3] = {-std::sin(h) * std::cos(p), -std::sin(p), std::cos(h) * std::cos(p)};
        const double x[3] = {std::cos(h), 0, std::sin(h)};
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
    static Camera camera(const Aircraft& was, const Aircraft& now, double t, int look = 0, int zoom = 0x80) {
        Camera c;
        const auto mix = [t](double a, double b) { return a + (b - a) * t; };
        std::array<double, 9> body{};
        std::array<double, 9> view{};
        for (std::size_t i = 0; i < 9; ++i) {
            body[i] = mix(was.body[i], now.body[i]) / 16384.0;
            view[i] = mix(was.view[i], now.view[i]) / 16384.0;
        }
        // The pilot's eye ($10F49E): (0, 5, 20) in the F/A-18, (0, 4, 18) in the F-16.
        const double up = now.type == 0x11 ? 5 : 4, ahead = now.type == 0x11 ? 20 : 18;
        const double eye[3] = {(ahead * view[2] + up * view[1]), (ahead * view[5] + up * view[4]),
                               (ahead * view[8] + up * view[7])};
        c.position = {mix(was.x, now.x) / 256.0 + eye[0], mix(was.y, now.y) / 256.0 + eye[1],
                      mix(was.z, now.z) / 256.0 + eye[2]};
        // The look direction ($122340): the view turned about the
        // aircraft's own up axis by the heading of `$12249A` (80 a degree,
        // clockwise: 1 is 28.2 degrees right of the nose ... 11 as far left).
        static constexpr int look_heading[12] = {0, 2256, 4800, 7200, 9600, 12000, 14400, 16800, 19200, 21600, 24000, 26544};
        if (look >= 12) {
            // 12 up, 13 down ($1223B6 / $122396): the view pitched by 90
            // degrees from the aircraft's, 83 while it flies within 7 degrees
            // of level.
            const int pitch = now.attitude.pitch & 0xFFFF;
            const bool level = pitch < 0x230 || pitch >= 0x6E50;
            const int turn = look == 12 ? (level ? 0x5690 : 0x5460) : (level ? 0x19F0 : 0x1C20);
            const double a = turn / 80.0 * 3.14159265358979 / 180, cs = std::cos(a), sn = std::sin(a);
            for (std::size_t k = 0; k < 3; ++k) {
                const double up = body[3 + k], ahead = body[6 + k];
                body[6 + k] = cs * ahead - sn * up;
                body[3 + k] = cs * up + sn * ahead;
            }
        } else if (look > 0) {
            const double a = look_heading[look] / 80.0 * 3.14159265358979 / 180, cs = std::cos(a), sn = std::sin(a);
            for (std::size_t k = 0; k < 3; ++k) {
                const double left = body[k], ahead = body[6 + k];
                body[k] = cs * left + sn * ahead;
                body[6 + k] = cs * ahead - sn * left;
            }
        }
        const double rows[3] = {0xA8 / 256.0, 0xFC / 256.0, zoom / 256.0};
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
    Carrier carrier_;
    Weapons weapons_{assets_.program, tables_};
    FlightMode mode_ = FlightMode::free_flight;
    Replay replay_{assets_.demo_controls, assets_.demo_keys};
    int fade_ = 15, fade_ticks_ = 0;    // $13AB90: the screen's fade step (15: full)
    std::uint16_t replay_game_ = 0, replay_view_ = 0; // `$13AC86`, `$13AC88`: last frame's key events
    bool replay_ended_ = false;
    Autopilot autopilot_{assets_.program, tables_};
    ObjectLists lists_{assets_.program};
    std::vector<WorldObject> objects_;  // the object lists' records (the carrier among them)
    ObjectStep object_step_{assets_.program, tables_, lists_};
    std::uint32_t list_start_ = 0;      // `$13AA2A`
    std::int8_t level_ = 0;             // `$13AB97` (the demo: 0)
    Radar radar_;
    std::int16_t player_distance_ = 0;  // the player's +$4A (`$1183E2`)
    bool radar_outside_ = false;
    int radar_look_ = 0;
    std::mt19937 random_{std::random_device{}()}; // (`$13ADE8`'s draws)
    std::uint8_t trainer_pilot_ = 0;    // the trainer's +$64
    int maneuver_ = 0;                  // $13AA89
    bool maneuver_started_ = false;     // $14508A
    bool skip_maneuver_ = false;        // record 0's +2 bit 3 (Return)
    bool maneuvers_running_ = false;    // $13AA83
    Aircraft leader_;                   // the practice's lead (record 4)
    std::uint8_t lead_pilot_ = 0;       // its +$64
    bool lead_active_ = false;
    std::int8_t practice_wait_ = 0;     // $13AA8A: negative while the trigger is awaited
    bool trigger_ = false;              // record 4's +2 bit 3 (the fire key)
    bool reposition_ = false;           // the player's +2 bit 3 (Return)
    bool brief_ = false;                // the qualification's briefing comes (`$13AB1B` clear)
    int mission_ = 0;                   // `$13AB96` of a mission (3-8)
    int mission_result_ = 0;            // `$13AA88`'s course: 0, 1 the objective, 4 landed after it
    int briefing_part_ = 0;             // a mission's briefing: 1 the first text, 2 the orders
    bool airborne_ = false;             // +$21 bit 0 (hypothesis: flown since the start)
    std::array<bool, 3> f_was_{};       // F1, F3, F5 held last frame
    std::array<bool, 10> ctrl_digits_was_{};
    bool briefing_key_ = false;         // a key during the briefing
    int radar_range_ = 0xD;             // +$63 low nibble: 9, B, D (2, 10, 40 miles; key R)
    bool ecm_ = false;                  // $13ABA5 (key J)
    // The gun's lead point ($1288CE): the newest two ($13AA06, $13AA12) and
    // where the circle shows ($13AC2E: worked out a frame before it shows).
    std::array<std::array<std::int32_t, 3>, 2> lead_{};
    std::optional<std::array<int, 2>> pipper_, pipper_next_;
    PilotRecord pilot_;
    FrontEnd front_;
    TextRunner text_;             // "READY FOR TAKEOFF", the end text
    bool in_front_ = true;
    bool ready_done_ = false;     // the takeoff text has run ($13AAD0 bit 7)
    Aircraft aircraft_, before_;
    std::array<bool, 64> keys_was_{};
    bool fire_was_ = false, ctrl_tab_was_ = false, ctrl_two_was_ = false;
    int aircraft_type_ = 0x11;    // $13AB39: $11 the F/A-18, $10 the F-16
    // The outside view ($13AA75 distance 1-4, 0 the cockpit; $13ABA2 the
    // camera's position around the aircraft) and its chasing camera.
    bool outside_ = false;
    int distance_ = -1, position_ = 4;
    std::array<double, 3> chase_{}, chase_was_{};
    bool chase_set_ = false;
    std::array<bool, 7> pad_was_{};
    bool above_ = false, v_was_ = false;      // $13AA9D: the view from above
    int look_ = 0;                            // $13AA97: the cockpit's look direction
    Descent descent_;
    std::array<int, 2> cam_angles_{}, cam_angles_was_{}; // $13AD84, $13AD86
    bool aim_now_ = false;
    int zoom_ = 0x80;                         // $13AD32
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
