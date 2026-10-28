#include "state/archive.hpp"
#include "wings_of_fury_amiga/game/module.hpp"
#include "amiga/paula.hpp"
#include "wings_of_fury_amiga/data/assets.hpp"
#include "wings_of_fury_amiga/data/preview.hpp"
#include "wings_of_fury_amiga/game/dashboard.hpp"
#include "wings_of_fury_amiga/game/effects.hpp"
#include "wings_of_fury_amiga/game/flight.hpp"
#include "wings_of_fury_amiga/game/player_view.hpp"
#include "wings_of_fury_amiga/game/scenery.hpp"
#include "wings_of_fury_amiga/game/ticker.hpp"
#include "wings_of_fury_amiga/game/briefing.hpp"
#include "wings_of_fury_amiga/game/frontend.hpp"
#include "wings_of_fury_amiga/game/music.hpp"
#include "wings_of_fury_amiga/game/saved_game.hpp"
#include "wings_of_fury_amiga/game/smooth_motion.hpp"
#include "platform/files.hpp"
#include "ui/vhs.hpp"
#include <optional>
#include <algorithm>
#include <random>
#include <string>

namespace wings::game {
namespace {

constexpr int vblanks_per_tick = 4;   // $101754 queues a command every fourth vertical blank
constexpr int long_press = 10;        // $10C9E0: a press this many blanks or longer is held fire
constexpr int carrier_column_index = 0x21;
constexpr int smoke_clip_line = 0x84;     // $100EF6: the deck line smoke is cut at while the elevator moves // `rcar`: the map scan places the carrier by it ($103032)

class WingsOfFury final : public app::GameModule {
public:
    explicit WingsOfFury(const std::filesystem::path& content)
        : assets_(data::load_assets(content)), tables_(read_flight_tables(assets_.program)),
          crew_tables_(read_crew_tables(assets_.program)), dash_tables_(read_dashboard_tables(assets_.program)),
          briefing_tables_(read_briefing_tables(assets_.program)),
          map_(&assets_.maps.front()), day_(data::find_palette(assets_, "wingspalette")),
          sea_(data::find_palette(assets_, "ocean.palette")), ticker_font_(read_ticker_font(assets_.ticker_font)),
          front_end_(assets_, ticker_font_, content / "highscore"), content_(content),
          music_(platform::read_file(content / "songplay"), platform::read_file(content / "wofsongs")) {
        // $1087BA: the ticker's copper list sets colour 1 for its first ten
        // lines from `wings` $1158E8 (the last stays below).
        for (std::size_t i = 0; i < ticker_colours_.size(); ++i) {
            ticker_colours_[i] = std::uint16_t(assets_.program.s16(0x1158E8 + std::uint32_t(i * 2)));
        }
        for (const auto& picture : assets_.pictures) {
            if (picture.name == "iff-dash") dashboard_ = &picture.picture;
        }
        front_end_.start_opening();
    }

    double step_seconds() const override { return 1.0 / 50.0; } // PAL vertical blank
    double pixel_aspect() const override { return 0.5; }         // lowres view doubled to the hires dashboard

    void tick(const app::GameInput& input) override {
        moved_ = false;
        if (mission_over_) return;
        if (front_end_.screen() != FrontEnd::Screen::none) {
            // $1023DC / $102470: a new song (or none) waits for the playing
            // one to fade out; the screens wait with it.
            if (song_pending_ != -1) {
                if (music_.state() != 0) return;
                if (song_pending_ >= 0) music_.play(song_pending_);
                playing_song_ = song_pending_ >= 0 ? song_pending_ : -1;
                song_pending_ = -1;
            }
            switch (front_end_.step(input)) {
            case FrontEnd::Result::play: new_game(front_end_.chosen_rank()); return;
            case FrontEnd::Result::back:
                // (The saved games from the rank menu: back to it; from the
                // game: the game goes on.)
                if (!saves_in_game_) front_end_.start_rank_select();
                return;
            case FrontEnd::Result::loaded:
                if (!restore(front_end_.loaded_game()) && !saves_in_game_) front_end_.start_rank_select();
                return;
            case FrontEnd::Result::exit_game: exited_ = true; return;
            default: break;
            }
            if (const int song = front_end_.take_song_request(); song != -1) {
                if (music_.state() != 0) {
                    music_.fade(2);
                    song_pending_ = song;
                } else if (song >= 0) {
                    music_.play(song);
                    playing_song_ = song;
                }
            }
            return;
        }
        // $10CCF6: the keys, with Ctrl ($10CD20). (Ctrl-s, sound off, and
        // Ctrl-l, a saved game, are Joyport's quick save and load: the
        // pause menu's mute and RETURN FROM R&R do those; Ctrl-b, an
        // `illegal` trap, is left out.)
        const auto ctrl = [&](int key) { return input.ctrl_keys[std::size_t(key)] && !ctrl_was_[std::size_t(key)]; };
        const bool end_key = ctrl(17), save_key = ctrl(26), version_key = ctrl(31), scores_key = ctrl(20), flip_key = ctrl(21);
        ctrl_was_ = input.ctrl_keys;
        if (end_key && briefing_ > 0) {
            // ($108712: at the briefing Ctrl-r fades it out and the game
            // starts again at the rank selection, $100066.)
            if (!briefing_out_) {
                briefing_ = 1;
                briefing_out_ = true;
                briefing_fade_ = 0;
            }
            ranks_after_briefing_ = true;
        } else if (end_key && !world_.game_over) {
            // Ctrl-r ends the game (-$5A3E): the fade, then rank selection
            // without the hall of fame.
            world_.game_over = true;
            quit_ = true;
        }
        if (save_key && plane_.mode == 1 && !world_.game_over && blackout_ == 0 && briefing_ == 0) {
            // Ctrl-g on deck ($10CD74): the sound stops, Save Game.
            for (int c = 0; c < 4; ++c) paula_.flush(c);
            channels_ = {};
            saves_in_game_ = true;
            front_end_.start_saved_games(true, save_game_text(world_, plane_));
            return;
        }
        if (version_key) world_.say("Version 1.0"); // (Ctrl-v: "Version %d.%d", -$321C/-$321A)
        if (scores_key) {
            // Ctrl-c deletes the file `highscore` (DeleteFile): the table
            // starts again from the disk's.
            std::error_code ignored;
            std::filesystem::remove(content_ / "highscore", ignored);
        }
        if (flip_key) inverted_ = !inverted_; // (Ctrl-f: -$5B08)
        debug_keys(input);
        if (world_.game_over) {
            // $1001C6: the sound stops, the ticker is cleared, and the last
            // frame fades out ($1073E6); then the hall of fame ($109856).
            if (end_fade_ < 0) {
                for (int c = 0; c < 4; ++c) paula_.flush(c);
                ticker_ = Ticker{};
            }
            if (++end_fade_ >= 16) {
                end_fade_ = -1;
                if (leave_after_fade_) {
                    exited_ = true;
                    return;
                }
                if (quit_) front_end_.start_rank_select();
                else front_end_.start_game_over(world_.score, world_.rank);
                quit_ = false;
            }
            return;
        }
        if (briefing_ > 0) {
            // $1086FA: the briefing waits 240 frames or for fire.
            const bool pressed = input.fire && !fire_down_;
            fire_down_ = input.fire;
            // $107084 before, $1073B0 after: 16 frames of fading each way.
            if (briefing_fade_ >= 0) {
                if (++briefing_fade_ >= 16) {
                    briefing_fade_ = -1;
                    if (briefing_out_) briefing_ = 0;
                    if (briefing_out_ && ranks_after_briefing_) {
                        ranks_after_briefing_ = false;
                        front_end_.start_rank_select();
                    }
                }
                return;
            }
            if (--briefing_ <= 0 || pressed) {
                briefing_ = 1;
                briefing_out_ = true;
                briefing_fade_ = 0;
            }
            return;
        }
        // $10184E (each vertical blank): the message ticker. A message the
        // game posted starts when none is being read; one read to its end
        // frees the game to post the next.
        if (world_.message_new) {
            ticker_.start(world_.message);
            world_.message_new = false;
        }
        ticker_.step(ticker_font_);
        if (!ticker_.running()) world_.message.clear();
        if (blackout_ > 0) {
            // $1035D8: the view stays black for 20 waits while the next plane
            // is made ready.
            --blackout_;
            return;
        }
        sample_fire(input.fire);
        moved_ = true;
        if (++vblank_ % vblanks_per_tick != 0) return;
        // (Smooth motion draws between the state the last tick left and the
        // one this tick makes.)
        if (smooth_) {
            before_world_ = world_;
            before_plane_ = plane_;
            have_before_ = true;
        }
        int command = 0;
        // ($10520E: with Ctrl-f's switch up and down change places.)
        const bool up = inverted_ && input.up != input.down ? input.down : input.up;
        const bool down = inverted_ && input.up != input.down ? input.up : input.down;
        if (up) command |= command::up;
        if (down) command |= command::down;
        if (input.left) command |= command::left;
        if (input.right) command |= command::right;
        // ($1012C8: in the weapon menu, with the stick at rest, Return or
        // Enter chooses as fire does.)
        if (command == 0 && !tapped_ && !held_ && world_.menu && input.c64_keys[1]) command |= command::fire_held;
        if (tapped_) command |= command::fire_tapped;
        if (held_) command |= command::fire_held;
        tapped_ = held_ = false;
        // One draw per tick here (the original draws about once per tick):
        // the elevator steps on every tick.
        world_.draw_counter = (world_.draw_counter + 1) % 100; // ($1037A8: it counts to 99)
        world_.random_load = 6 + int(random_() % 4); // (the original reads the beam position)
        world_.vblank_clock = vblank_;
        // The frame start's copies ($100F88) the tick reads; the frame after
        // the last tick was drawn.
        world_.frame_camera = plane_.x;
        world_.frame_altitude = plane_.altitude;
        world_.frame_eighth = plane_.eighth_scale;
        world_.frame_drawn = true;
        frame_facing_ = plane_.facing;
        world_.effects.frame_start();
        for (auto& ship : world_.ships) ship.bob = ship.sunk; // ($10101A)
        for (auto& ship : world_.ships) ship.planes.shown_count = ship.planes.count; // ($10102C)
        for (auto& a : world_.enemies.airfields) { // ($101008)
            a.shown_parked = a.parked;
            a.shown_takeoff = a.takeoff_x;
        }
        apply_cheats();
        const int fuel_before = plane_.fuel;
        flight_tick(plane_, world_, tables_, command);
        // What drawing the frame steps: the effects, the tower flag, the
        // deck crew and the islands' flags.
        world_.effects.advance();
        // $10557C (as the frame is drawn): the victory ceremony's sparks start
        // again in the normal view.
        if (world_.celebration && !plane_.eighth_scale) world_.effects.start_sparks(world_.carrier_x, world_.beam);
        islands_frame(plane_, world_, tables_, specials_shown(*world_.map, backdrop_state()).wrecked_huts);
        // (The infinite fuel cheat: nothing lowers it, neither burning nor
        // hits; a new plane's full tank still comes.)
        if (cheats_.infinite_fuel && plane_.fuel < fuel_before) plane_.fuel = fuel_before;
        animate_specials();
        // $10030C: a hit flashes the background colour on every second frame.
        flash_ = 0;
        if (world_.islands.flash_frames != 0) {
            if (world_.islands.flash_frames & 1) flash_ = world_.islands.flash_colour;
            --world_.islands.flash_frames;
        }
        world_.gauges.step(dashboard_input(), world_.beam.next());
        // $100DA6 (drawn after the player): the enemy planes and wrecks; the
        // gun flashes step as they are drawn.
        {
            const auto backdrop = backdrop_state();
            EnemyView view;
            view.camera = backdrop.camera;
            view.ground_line = backdrop.eighth_scale ? 0x4B8 : backdrop.ground_line;
            view.eighth_scale = backdrop.eighth_scale;
            view.margin = margin_;
            step_gun_flashes(world_.enemies, tables_.enemies, assets_, view);
            enemy_sprites_ = enemy_sprites(world_.enemies, tables_.enemies, assets_, view);
        }
        // $10012A (the main loop): mission accomplished and the plane below
        // deck: on to the next mission (a promotion brings a spare plane).
        if (world_.mission_accomplished && world_.menu) {
            if (world_.celebration) ++world_.spare_planes;
            start_mission();
            return;
        }
        // $101110 (each frame): the countdown after the carrier went down.
        if (world_.game_over_countdown > 0 && --world_.game_over_countdown <= 0) world_.game_over = true;
        play_sounds();
        // $10063A: the muzzle flash steps while the guns fire, upright, in
        // the normal view.
        if (world_.guns && plane_.turn == 0 && !plane_.eighth_scale) flash_counter_ = (flash_counter_ + 1) & 3;
        if (world_.blackout > 0) {
            blackout_ = world_.blackout;
            world_.blackout = 0;
        }
        // The gear part follows its target a pixel a draw ($100580); the view
        // is drawn once per tick here.
        if (gear_offset_ != plane_.gear) gear_offset_ += gear_offset_ > plane_.gear ? -1 : 1;
    }

    void audio(std::span<float> stereo, int sample_rate) override {
        // (The host hands in the last frame's buffer: Paula adds to it.)
        std::fill(stereo.begin(), stereo.end(), 0.0f);
        paula_.render(stereo, sample_rate, [](int, int) {});
        music_.render(stereo, sample_rate);
    }

    std::string stopped() const override {
        if (exited_) return "Wings of Fury has ended (Exit Game).";
        if (mission_over_) {
            return "Mission " + std::to_string(world_.mission) + " (" + map_->name +
                   ") needs parts of the game not rebuilt yet: the port stops here.";
        }
        return "";
    }

    platform::Image picture() const override {
        if (!smooth_now()) return game_picture(world_, plane_);
        FlightWorld world = world_;
        PlayerFlight plane = plane_;
        place_between(world, plane, before_world_, before_plane_, motion_time());
        return game_picture(world, plane, true);
    }
    void set_frame_fraction(double fraction) override { frame_fraction_ = std::clamp(fraction, 0.0, 1.0); }
    int vhs_level() const override { return vhs_; }

    // Quick save and load: the game where it is (front-end screen, mission,
    // flight, effects, ticker); the sound effects start again from the next
    // tick, a front-end song from its start.

    template <class A> void persist(A &ar) {
        std::string map_name = map_->name;
        ar(map_name);
        if constexpr (A::reading) {
            map_ = nullptr;
            for (const auto &m : assets_.maps)
                if (m.name == map_name)
                    map_ = &m;
            if (!map_)
                A::fail();
        }
        ar(world_, plane_, blackout_, random_, vblank_, press_start_, fire_down_, tapped_, held_,
           gear_offset_, us_flag_step_, flash_, flash_counter_, channels_, enemy_sprites_, front_end_, music_,
           song_pending_, ticker_, mission_over_, end_fade_, quit_, ctrl_was_, saves_in_game_, exited_,
           inverted_, plain_was_, debug_step_, debug_invulnerable_, leave_after_fade_, ranks_after_briefing_,
           playing_song_, vhs_, cheats_, briefing_, briefing_fade_, briefing_out_, frame_facing_,
           enemy_flag_counter_, enemy_flags_drawn_);
        // A visible enemy sprite retains its exact imported shape across a
        // frame boundary; drawing again must not advance its animation.
        for (auto &sprite : enemy_sprites_) {
            std::string library, shape;
            if constexpr (!A::reading) {
                if (sprite.shape) {
                    for (const auto &lib : assets_.libraries)
                        for (const auto &item : lib.library.shapes)
                            if (&item == sprite.shape) {
                                library = lib.name;
                                shape = item.name;
                            }
                    if (library.empty())
                        A::fail();
                }
            }
            ar(library, shape);
            if constexpr (A::reading) {
                sprite.shape = nullptr;
                if (!library.empty()) {
                    for (const auto &lib : assets_.libraries)
                        if (lib.name == library)
                            sprite.shape = lib.library.find(shape);
                    if (!sprite.shape)
                        A::fail();
                }
            }
        }
        paula_.persist_files(ar, [&](auto &a, auto &w) {
            std::string name;
            if constexpr (!A::reading) {
                if (w.image)
                    A::fail();
                for (const auto &sample : assets_.samples)
                    if (sample.data.data() == w.samples.data() && sample.data.size() == w.samples.size())
                        name = sample.name;
                if (name.empty())
                    A::fail();
            }
            a(name);
            if constexpr (A::reading) {
                w.image = nullptr;
                w.samples = {};
                for (const auto &sample : assets_.samples)
                    if (sample.name == name)
                        w.samples = sample.data;
                if (w.samples.empty())
                    A::fail();
            }
        });
        if constexpr (A::reading)
            set_lighting();
    }
    std::vector<std::uint8_t> write_state() override {
        state::Writer ar;
        persist(ar);
        return std::move(ar.bytes);
    }
    void read_state(std::span<const std::uint8_t> bytes) override {
        state::Reader ar(bytes);
        persist(ar);
        ar.finish();
    }
    bool save() override {
        saved_.emplace(Snapshot{world_, plane_, map_, blackout_, random_, vblank_, press_start_, fire_down_, tapped_, held_,
                                gear_offset_, us_flag_step_, flash_, flash_counter_, enemy_sprites_, front_end_,
                                song_pending_, playing_song_, ticker_, mission_over_, end_fade_, quit_, ctrl_was_, briefing_,
                                briefing_fade_, briefing_out_, frame_facing_, enemy_flag_counter_, enemy_flags_drawn_});
        return true;
    }
    bool load() override {
        if (!saved_) return false;
        have_before_ = false;
        const auto& s = *saved_;
        world_ = s.world;
        world_.map = &world_.mission_map; // (the copy's own map, not the snapshot's)
        set_lighting();
        plane_ = s.plane;
        map_ = s.map;
        blackout_ = s.blackout;
        random_ = s.random;
        vblank_ = s.vblank;
        press_start_ = s.press_start;
        fire_down_ = s.fire_down;
        tapped_ = s.tapped;
        held_ = s.held;
        gear_offset_ = s.gear_offset;
        us_flag_step_ = s.us_flag_step;
        flash_ = s.flash;
        flash_counter_ = s.flash_counter;
        enemy_sprites_ = s.enemy_sprites;
        front_end_ = s.front_end;
        song_pending_ = s.song_pending;
        playing_song_ = s.playing_song;
        ticker_ = s.ticker;
        mission_over_ = s.mission_over;
        end_fade_ = s.end_fade;
        quit_ = s.quit;
        ctrl_was_ = s.ctrl_was;
        briefing_ = s.briefing;
        briefing_fade_ = s.briefing_fade;
        briefing_out_ = s.briefing_out;
        frame_facing_ = s.frame_facing;
        enemy_flag_counter_ = s.enemy_flag_counter;
        enemy_flags_drawn_ = s.enemy_flags_drawn;
        for (int c = 0; c < 4; ++c) paula_.flush(c);
        channels_ = {};
        music_.silence();
        if (playing_song_ >= 0 && front_end_.screen() != FrontEnd::Screen::none) music_.play(playing_song_);
        return true;
    }
    bool has_save() const override { return saved_.has_value(); }

    std::vector<app::GameOption> options() const override {
        return {{"Widescreen", {"Off", "On"}, widescreen_ ? 1 : 0,
                 "Fills the window to its edges and shows more of the sea and sky left and right, up to "
                 "21:9. The game plays the same."},
                {"Smooth motion", {"Off", "On"}, smooth_ ? 1 : 0,
                 "Planes, weapons, soldiers and the landscape glide at the display's own rate instead "
                 "of stepping 12.5 times a second. The game plays the same."},
                {"VHS look", {"Off", "Light", "Medium", "Heavy", "Broken"}, vhs_,
                 "A worn video tape's colour bleed and noise."},
                {"Infinite lives", {"Off", "On"}, cheats_.infinite_lives ? 1 : 0,
                 "A lost plane uses no spare: the game never ends for want of planes.", "Cheats"},
                {"Infinite fuel", {"Off", "On"}, cheats_.infinite_fuel ? 1 : 0,
                 "The tank never empties: flying, flak and fighters' hits burn no fuel.", "Cheats"},
                {"Bullet proof", {"Off", "On"}, cheats_.bullet_proof ? 1 : 0,
                 "Enemy fighters' guns and anti-aircraft fire do no harm: the game's own debugging switch.",
                 "Cheats"},
                {"Infinite ammo", {"Off", "On"}, cheats_.infinite_ammo ? 1 : 0,
                 "Rockets, bombs and torpedoes never run out: the game's own debugging key.", "Cheats"},
                {"Enemy duds", {"Off", "On"}, cheats_.enemy_duds ? 1 : 0,
                 "The enemy bombers' torpedoes are duds and leave the carrier unharmed.", "Cheats"}};
    }
    void set_option(std::size_t index, int value) override {
        if (index == 0) {
            widescreen_ = value != 0;
            set_canvas(canvas_width_, canvas_height_);
        }
        if (index == 1) {
            smooth_ = value != 0;
            have_before_ = false;
        }
        if (index == 2) vhs_ = std::clamp(value, 0, ui::vhs_levels - 1);
        if (index == 3) cheats_.infinite_lives = value != 0;
        if (index == 4) cheats_.infinite_fuel = value != 0;
        if (index == 5) cheats_.bullet_proof = value != 0;
        if (index == 6) cheats_.infinite_ammo = value != 0;
        if (index == 7) cheats_.enemy_duds = value != 0;
    }
    // Widescreen: the view as wide as the window's shape allows, from the
    // original's 320 columns up to 21:9 (the dashboard, the ticker and the
    // other screens stay their own width, in the middle).
    void set_canvas(int width, int height) override {
        canvas_width_ = width;
        canvas_height_ = height;
        margin_ = 0;
        if (!widescreen_ || width <= 0 || height <= 0) return;
        const double tall = picture_height();
        const double wide = std::clamp(tall * width / height, double(view_width), tall * 21 / 9);
        margin_ = std::max(0, int(wide - view_width) / 2);
    }

private:
    // The picture of `world` and `plane`: the game's own, or (smooth motion)
    // copies with the moving things placed between two ticks.
    platform::Image game_picture(const FlightWorld& world, const PlayerFlight& plane, bool blended = false) const {
        if (front_end_.screen() != FrontEnd::Screen::none) return widened(front_end_.picture(picture_height()));
        if (briefing_ > 0) {
            // The briefing's hires screen, 147 lines from the top, black below.
            const auto screen = draw_briefing(assets_, ticker_font_, briefing_tables_, world.rank, world.mission,
                                              world.islands.islands_left, world.islands.ships_left);
            platform::Image image(640, picture_height());
            for (int y = 0; y < screen.height; ++y) {
                for (int x = 0; x < screen.width; ++x) {
                    const auto c = briefing_tables_.palette[screen.at(x, y) & 7];
                    const int step = briefing_fade_ < 0 ? 15 : briefing_fade_;
                    image.put(x, y, data::to_color(briefing_out_ ? fade_colour(step, c, 0) : fade_colour(step, 0, c)));
                }
            }
            return widened(image);
        }
        if (blackout_ > 0) {
            IndexedCanvas black(view_width, view_height, margin_);
            return compose(black, draw_dashboard(assets_, dash_tables_, world.gauges, dashboard_input()));
        }
        const auto backdrop = backdrop_state(world, plane);
        auto canvas = draw_sky_and_scenery(assets_, *world.map, backdrop);

        CarrierDeckPicture deck;
        deck.camera = backdrop.camera;
        deck.ground_line = backdrop.eighth_scale ? 0x4B8 : backdrop.ground_line;
        deck.vertical_scroll = backdrop.vertical_scroll;
        deck.carrier_x = world.carrier_x;
        deck.afloat = world.ships[4].afloat;
        deck.deck = world.ships[4].deck;
        deck.bob = world.ships[4].bob;
        deck.spare_planes = world.spare_planes;
        deck.elevator_state = world.elevator;
        deck.lift = world.lift;
        deck.eighth_scale = backdrop.eighth_scale;
        draw_carrier_deck(canvas, assets_, deck);

        PlayerPicture player;
        player.hidden = world.elevator == 1;
        player.clip_at_deck = world.elevator != 0;
        player.altitude = plane.altitude;
        player.ground_line = deck.ground_line;
        player.vertical_scroll = backdrop.vertical_scroll;
        player.carrier_deck = deck.deck;
        player.carrier_bob = deck.bob;
        player.eighth_scale = backdrop.eighth_scale;
        if (backdrop.eighth_scale) {
            // -$4086 is the turn step, -$4082 the vertical speed ($101274).
            player.plane = eighth_scale_plane(assets_, plane.turn, plane.speed_y, plane.facing < 0);
        } else {
            // Shapes from the pitch tables are mirrored in place for facing
            // left ($10ABDE); the turn shapes have one set per direction.
            // The turn shapes have one set per direction and the burning wreck
            // ($10C88C) one name per direction: neither is mirrored.
            const bool turning = plane.turn != 0 && (plane.mode == 0 || plane.mode == 4);
            player.plane = hellcat(plane.shape);
            player.plane_mirrored = !turning && plane.mode != 8 && plane.facing < 0;
            player.gear = plane.wheels.empty() ? nullptr : hellcat(plane.wheels);
            player.gear_shown = player.gear != nullptr && (plane.mode == 0 || plane.mode == 7 || plane.lift);
            player.gear_offset = gear_offset_;
            player.upset = plane.turn != 0;
            player.gun_flash = gun_flash_shape(assets_, world.guns, player.upset, flash_counter_, plane.attitude,
                                               frame_facing_ < 0);
        }
        if (plane.mode == 7 && !backdrop.eighth_scale) {
            // -$42C4: the caught wire's x, on the screen relative to the camera.
            player.caught = true;
            player.wire_screen_x = world.caught_wire_x - backdrop.camera + 0xA0;
            player.facing_left = plane.facing < 0;
        }
        draw_player(canvas, player);
        if (blended) {
            EnemyView view;
            view.camera = backdrop.camera;
            view.ground_line = backdrop.eighth_scale ? 0x4B8 : backdrop.ground_line;
            view.eighth_scale = backdrop.eighth_scale;
            view.margin = margin_;
            draw_enemies(canvas, enemy_sprites(world.enemies, tables_.enemies, assets_, view));
        } else {
            draw_enemies(canvas, enemy_sprites_);
        }
        // The dugouts' guns ($103D78, with the scenery objects), then the sea.
        EffectView effects;
        effects.camera = backdrop.camera;
        effects.ground_line = deck.ground_line;
        effects.eighth_scale = backdrop.eighth_scale;
        std::vector<WorldSprite> soldiers;
        for (const auto& sprite : world.islands.sprites) {
            if (sprite.gun) draw_world_sprite(canvas, assets_, effects, {sprite.entry, sprite.x, sprite.y});
            else soldiers.push_back({sprite.entry, sprite.x, sprite.y});
        }
        {
            // $103A18: the airfields' parked planes and the one taking off.
            EnemyView view;
            view.camera = backdrop.camera;
            view.ground_line = deck.ground_line;
            view.eighth_scale = backdrop.eighth_scale;
            view.margin = margin_;
            draw_enemies(canvas, airfield_sprites(world.enemies, tables_.enemies, assets_, view));
            // $10391E: the planes parked on the enemy ships.
            draw_enemies(canvas, ship_plane_sprites(world_, tables_.enemies, assets_, view, backdrop.vertical_scroll));
        }
        draw_sea_and_sand(canvas, assets_, *world.map, backdrop);

        if (world.elevator != 0) effects.smoke_clip_bottom = smoke_clip_line + backdrop.vertical_scroll + deck.bob;
        draw_effects(canvas, assets_, world.effects, effects, soldiers);
        if (world.celebration && !backdrop.eighth_scale) draw_sparks(canvas, assets_, world.effects, effects);
        if (world.menu) draw_weapon_menu(canvas, assets_, world.weapon);
        // $1010C2: the last plane lost, `gmov` (THE END) over the view.
        if (world.the_end) {
            for (const auto& l : assets_.libraries) {
                if (l.name != "world.shp") continue;
                if (const auto* s = l.library.find("gmov")) canvas.draw(*s, 0xA0 - s->offset_x, 0x51 - s->offset_y);
            }
        }
        return compose(canvas, draw_dashboard(assets_, dash_tables_, world.gauges, dashboard_input()));
    }

private:
    // The sound driver's commands to the hardware ($10EA28 start, $10EB4C
    // period and volume - a negative one is not set -, $10EAC0 stop).
    void play_sounds() {
        using Kind = SoundBoard::Command::Kind;
        for (const auto& c : world_.sound.commands) {
            auto& state = channels_[std::size_t(c.channel)];
            if (c.kind == Kind::start) {
                const auto* file = sample_file(assets_, c.request.sample);
                if (file == nullptr) continue;
                amiga::Paula::Write write;
                write.samples = file->data;
                write.length = std::uint32_t(c.request.length & ~1); // (whole words)
                write.period = std::uint16_t(c.request.period);
                write.volume = std::uint16_t(c.request.volume);
                write.cycles = c.request.repeat == 0xFFFF ? 0 : c.request.repeat;
                write.set_period_volume = true;
                state = {write.period, write.volume};
                paula_.flush(c.channel);
                paula_.write(c.channel, write);
            } else if (c.kind == Kind::retune) {
                if (std::int16_t(c.request.period) >= 0) state.period = std::uint16_t(c.request.period);
                if (std::int16_t(c.request.volume) >= 0) state.volume = std::uint16_t(c.request.volume);
                paula_.set_period_volume(c.channel, state.period, state.volume);
            } else {
                paula_.flush(c.channel);
            }
        }
        world_.sound.commands.clear();
    }

    DashboardInput dashboard_input() const {
        DashboardInput in;
        in.mode = plane_.mode;
        in.oil = plane_.oil;
        in.fuel = plane_.fuel;
        in.engine = plane_.engine_loud;
        in.weapon = world_.weapon;
        in.load = world_.weapon_load;
        in.spare_planes = world_.spare_planes;
        in.score = world_.score;
        in.kills = world_.kills;
        in.camera = plane_.x;
        in.altitude = plane_.altitude;
        in.facing = plane_.facing;
        in.eighth_scale = plane_.eighth_scale;
        in.map = world_.map;
        in.night = world_.night;
        for (const auto& ship : world_.ships) {
            if (&ship == &world_.ships.back() || (ship.present && ship.afloat)) {
                in.ships.push_back({ship.first, ship.last, ship.overlay, ship.value});
            }
        }
        for (const auto& e : world_.enemies.planes) {
            if (e.state != 0) in.enemies.push_back({(std::int16_t(e.x) >> 3) * 2, e.picture, e.altitude, e.x, e.mode}); // ($100FEC)
        }
        return in;
    }

    BackdropState backdrop_state() const { return backdrop_state(world_, plane_); }
    BackdropState backdrop_state(const FlightWorld& world, const PlayerFlight& plane) const {
        BackdropState backdrop;
        backdrop.camera = plane.x;
        backdrop.margin = margin_;
        backdrop.eighth_scale = plane.eighth_scale;
        backdrop.ground_line = plane.eighth_scale ? 0x4B8 : ground_line_for(plane.altitude);
        backdrop.vertical_scroll = plane.sea;
        backdrop.wave_frame = wave_frame();
        // ($104A4E: the enemy ships on this map, then the US carrier.)
        for (std::size_t i = 0; i < 4; ++i) {
            const auto& s = world.ships[i];
            if (s.present) backdrop.ships.push_back({s.first, s.last, s.bob});
        }
        backdrop.ships.push_back({world.ships[4].first, world.ships[4].last, world.ships[4].bob});
        backdrop.us_flag_step = us_flag_step_;
        backdrop.crew_pose_a = world.crew.pose_a;
        backdrop.crew_pose_b = world.crew.pose_b;
        backdrop.enemy_flag_counter = enemy_flag_counter_;
        return backdrop;
    }

    // The specials step as they are drawn ($103B1C): the tower flag on odd
    // frames ($103D10), the deck crew once a frame ($103BCE, flag -$3BA6),
    // each island flag column once ($103B52, counted from the frame before).
    void animate_specials() {
        enemy_flag_counter_ = ((enemy_flag_counter_ - enemy_flags_drawn_) % 3 + 3) % 3;
        const auto shown = specials_shown(*world_.map, backdrop_state());
        if (world_.draw_counter & 1) {
            for (int i = 0; i < shown.us_flag; ++i) us_flag_step_ = us_flag_step_ == 0 ? 3 : us_flag_step_ - 1;
        }
        if (shown.deck_crew > 0) world_.crew.step(crew_tables_, plane_.signal);
        enemy_flags_drawn_ = shown.enemy_flag;
    }

    const data::Shape* hellcat(const std::string& name) const {
        for (const auto& library : assets_.libraries) {
            if (library.name == "hellcat.shp") return library.library.find(name);
        }
        return nullptr;
    }

    // The cheats, as the options stand, on the world before each tick (a new
    // mission or a quick load starts from a fresh world). Bullet proof is the
    // switch -$408C, infinite ammo the load $FF: both what the original's
    // debug mode keys ($10D184, $10D08A) set; infinite lives and the duds
    // are not the original's. (Infinite fuel is kept after the tick.)
    void apply_cheats() {
        world_.enemies.invulnerable = cheats_.bullet_proof || debug_invulnerable_;
        world_.infinite_lives = cheats_.infinite_lives;
        world_.enemy_duds = cheats_.enemy_duds;
        const bool infinite = world_.weapon_load == 0xFF;
        if (cheats_.infinite_ammo != infinite) {
            world_.weapon_load = cheats_.infinite_ammo ? 0xFF : tables_.weapon_loads[std::size_t(world_.weapon)];
            world_.gauges.set_load(world_.weapon_load); // ($10EDBC)
        }
    }

    // $10C9E0: fire pressed and released within 10 blanks is a tap; held
    // longer, every command while it is held carries the long press.
    void sample_fire(bool fire) {
        if (fire) {
            if (!fire_down_) press_start_ = vblank_;
            if (vblank_ - press_start_ >= long_press) held_ = true;
        } else if (fire_down_ && vblank_ - press_start_ < long_press) {
            tapped_ = true;
        }
        fire_down_ = fire;
    }

    int wave_frame() const {
        // -$5C70 steps 11 -> 0 every second draw ($10105E); one draw per tick here.
        return 11 - int((vblank_ / vblanks_per_tick / 2) % 12);
    }

    // The tallest screen: the hall of fame's two viewports ($4C + $91).
    int picture_height() const { return std::max(view_height + 37 + 1 + Ticker::rows, 0x4C + 0x91); }

    // $103562 (a new game at the chosen rank, $108262): score, spare planes
    // and the carrier's hits set, mission 1 of the rank.
    void new_game(int rank) {
        if (front_end_.load_chosen()) {
            // RETURN FROM R&R: Load Game ($108B96 with 0).
            saves_in_game_ = false;
            front_end_.start_saved_games(false);
            return;
        }
        front_end_.close();
        world_ = FlightWorld{};
        world_.rank = rank;
        world_.mission = 1;
        world_.score = 0;
        world_.spare_planes = 3;
        world_.kills = 0;
        mission_over_ = false;
        start_mission();
    }

    // $10CE96..: without Ctrl, typing `colin` steps -$50E6 to 5 (a wrong
    // letter of it starts again): the debug mode, whose keys follow. (Space,
    // free memory in the ticker, and Help and F10 are left out: the host
    // has no such keys for the game, and the port no Amiga memory.)
    void debug_keys(const app::GameInput& input) {
        const auto key = [&](int index) { return input.c64_keys[std::size_t(index)] && !plain_was_[std::size_t(index)]; };
        std::array<bool, 64> pressed{};
        for (int i = 0; i < 64; ++i) pressed[std::size_t(i)] = key(i);
        plain_was_ = input.c64_keys;
        const bool on = debug_step_ == 5;
        if (pressed[38]) debug_step_ = debug_step_ == 1 ? 2 : 0;                  // o
        else if (pressed[42]) debug_step_ = debug_step_ == 2 ? 3 : 0;             // l
        else if (pressed[39]) debug_step_ = debug_step_ == 4 ? 5 : 0;             // n
        else if (pressed[33]) {                                                    // i
            if (on) world_.pitch_adjust += 0x32;
            else if (debug_step_ == 3) debug_step_ = 4;
            else if (debug_step_ != 0) debug_step_ = 0;
        } else if (pressed[37] && on) world_.pitch_adjust -= 0x32;                  // k
        else if (pressed[21] && on) plane_.fuel = 0x80;                            // f: fuel
        else if (pressed[41] && on) ++world_.spare_planes;                         // p: a plane
        else if (pressed[62] && on) {                                              // q: the game ends, the program too
            world_.game_over = true;
            quit_ = true;
            leave_after_fade_ = true;
        } else if (pressed[36] && on) {                                            // m: no load used ($FF), or back
            world_.weapon_load = world_.weapon_load == 0xFF ? tables_.weapon_loads[std::size_t(world_.weapon)] : 0xFF;
            if (world_.weapon_load != 0xFF) world_.gauges.set_load(world_.weapon_load); // ($10EDBC)
        } else if (pressed[17] && on) {                                            // r: $103756
            for (auto& o : world_.effects.objects) o.kind = FlyingObject::Kind::none;
            world_.effects.enemy_torpedo.kind = FlyingObject::Kind::none;
        } else if (pressed[20]) {                                                  // c
            if (debug_step_ == 0) debug_step_ = 1;
            else if (on) world_.weapon = (world_.weapon + 1) % 3;                   // the next weapon
        } else if (pressed[27] && on) world_.gravity += 0x1000;                    // 8, 2, 4, 6: the weapons' fall
        else if (pressed[59] && on) world_.gravity -= 0x1000;
        else if (pressed[11] && on) world_.gravity -= 0x100;
        else if (pressed[19] && on) world_.gravity += 0x100;
        else if (pressed[18] && on) {                                              // d: oil, flying, bullet proof
            plane_.oil = 0x80;
            plane_.mode = 0;
            world_.enemies.invulnerable = !world_.enemies.invulnerable;
            debug_invulnerable_ = world_.enemies.invulnerable;
        }
    }

    // A saved game loaded ($105E1A): the game where it was saved, on deck.
    bool restore(const std::string& text) {
        auto saved = load_game_text(text, assets_.maps);
        if (!saved) return false;
        front_end_.close();
        world_ = std::move(saved->world);
        world_.map = &world_.mission_map;
        plane_ = saved->plane;
        for (const auto& m : assets_.maps) {
            if (m.name == world_.mission_map.name) map_ = &m;
        }
        set_lighting();
        set_up_sounds(world_, sample_lengths(assets_));
        ticker_ = Ticker{};
        for (int c = 0; c < 4; ++c) paula_.flush(c);
        channels_ = {};
        music_.silence();
        playing_song_ = -1;
        flash_ = 0;
        enemy_sprites_.clear();
        mission_over_ = false;
        briefing_ = 0;
        blackout_ = 0;
        end_fade_ = -1;
        quit_ = false;
        world_.gauges.set_load(world_.weapon_load);
        world_.gauges.set_spares(world_.spare_planes);
        return true;
    }

    // Day or night: the view's and the sea's palettes and the dashboard.
    void set_lighting() {
        day_ = data::find_palette(assets_, world_.night ? "night.p" : "wingspalette");
        sea_ = data::find_palette(assets_, world_.night ? "nightocean.p" : "ocean.palette");
        for (const auto& picture : assets_.pictures) {
            if (picture.name == (world_.night ? "nightdash" : "iff-dash")) dashboard_ = &picture.picture;
        }
    }

    // Every map (a.map - o.map): islands, the US carrier, torpedo bombers,
    // airfields and the four kinds of enemy ship.
    static bool map_supported(const std::string& name) {
        static constexpr const char* rebuilt[] = {"a.map", "b.map", "c.map", "d.map", "e.map", "f.map", "g.map", "h.map",
                                                  "i.map", "j.map", "k.map", "l.map", "m.map", "n.map", "o.map"};
        return std::find(std::begin(rebuilt), std::end(rebuilt), name) != std::end(rebuilt);
    }

    // $102ADC and $1035A8 for the mission (rank -$5C40, mission -$5C3E): the
    // map is the sum of the missions of the ranks below plus the mission
    // ($102AFC, table -$5AB6, names -$5AF2); the islands and the carrier
    // from its scan, the enemy planes and wrecks cleared; the score, spare
    // planes and kills stay. Then the briefing and the first plane.
    void start_mission() {
        int index = world_.mission - 1;
        for (int r = 0; r < world_.rank && r < 7; ++r) index += tables_.islands.missions_per_rank[std::size_t(r)];
        map_ = &assets_.maps[std::size_t(std::clamp(index, 0, int(assets_.maps.size()) - 1))];
        FlightWorld next;
        next.score = world_.score;
        next.spare_planes = world_.spare_planes;
        next.kills = world_.kills;
        next.rank = world_.rank;
        next.mission = world_.mission;
        next.beam = world_.beam;
        next.random = world_.random;
        world_ = std::move(next);
        plane_ = PlayerFlight{};
        ticker_ = Ticker{};
        // ($101234 / $101F4E: the sound stops.)
        for (int c = 0; c < 4; ++c) paula_.flush(c);
        channels_ = {};
        flash_ = 0;
        enemy_sprites_.clear();
        set_up_carrier();
        if (!map_supported(map_->name)) {
            mission_over_ = true;
            return;
        }
        first_plane();
        briefing_ = BriefingScreen::frames;
        briefing_fade_ = 0;
        briefing_out_ = false;
    }

    // The map scan ($102DE8 -> $103032): the US carrier spans the byte
    // offsets (rcar column x 2) - $A0 .. + $20, afloat, deck height $21.
    void set_up_carrier() {
        world_.set_map(*map_);
        world_.islands = find_islands(*map_);
        {
            const std::size_t at = std::size_t(world_.rank * 4 + world_.mission);
            const int row = at < tables_.islands.bonus_row.size() ? tables_.islands.bonus_row[at] : 0;
            find_airfields(world_.enemies, *map_, tables_.enemies, row);
            find_ships(world_, *map_, tables_, row);
            // $1011FC: from row 7 on a mission may be flown at night (the
            // fourth of four beam draws, bit 15): its palettes and dashboard
            // ($10653C, $108892).
            world_.night = false;
            if (row > 6) {
                int r = 0;
                for (int i = 0; i < 4; ++i) r = world_.beam.next();
                world_.night = (r & 0x8000) != 0;
            }
            set_lighting();
        }
        set_up_sounds(world_, sample_lengths(assets_));
        world_.carrier_x = map_->carrier_x();
        auto& carrier = world_.ships[4];
        for (std::size_t c = 0; c < map_->words.size(); ++c) {
            const auto column = map_->column(c);
            if (column.draw && column.index == carrier_column_index) {
                carrier.first = int(c) * 2 - 0xA0;
                carrier.last = carrier.first + 0xC0;
                break;
            }
        }
        carrier.afloat = true;
        carrier.ready = 4;
        carrier.deck = 0x21;
        carrier.sink_timer = 0x14;
    }

    // The first plane: below deck with the weapon menu ($103562 -> $1035D8
    // -> $103684), facing left at the carrier.
    void first_plane() {
        plane_.x = world_.carrier_x;
        plane_.facing = -1;
        plane_.turn = 0;
        plane_.sea_period = assets_.program.s16(0x11533C);
        plane_.sea_step = assets_.program.s16(0x1146BE);
        plane_.turn_delay = 2;
        plane_.attitude = 4;
        new_plane(plane_, world_, tables_, 6 + int(random_() % 4));
        world_.gauges.set_spares(world_.spare_planes); // ($10EDAA: the mission begins)
        gear_offset_ = plane_.gear;
    }

    // The view (lowres, doubled) above the dashboard (hires).
    bool smooth_now() const {
        return smooth_ && have_before_ && front_end_.screen() == FrontEnd::Screen::none && briefing_ == 0 &&
               blackout_ == 0 && !world_.game_over;
    }
    // Where the picture shown lies between the last two ticks (0 .. 1): the
    // vertical blanks since the last tick and the time into the next.
    double motion_time() const {
        const double blanks = vblank_ % vblanks_per_tick + (moved_ ? frame_fraction_ : 0.0);
        return std::min(1.0, blanks / vblanks_per_tick);
    }

    // A hires screen of the original's width in the middle of a widescreen
    // picture, black at the sides.
    platform::Image widened(const platform::Image& screen) const {
        if (margin_ == 0) return screen;
        platform::Image image(screen.width + 4 * margin_, screen.height);
        for (int y = 0; y < screen.height; ++y) {
            for (int x = 0; x < screen.width; ++x) {
                image.put(2 * margin_ + x, y, screen.pixels[std::size_t(y * screen.width + x)]);
            }
        }
        return image;
    }

    platform::Image compose(const IndexedCanvas& canvas, const IndexedCanvas& dash) const {
        const int dash_height = dashboard_ ? dash.height : 0;
        const int side = 2 * margin_; // (hires pixels)
        platform::Image image(640 + 2 * side, picture_height());
        // (After the game the colours fade to black, $1073E6.)
        const auto col = [&](std::uint16_t c) { return data::to_color(end_fade_ >= 0 ? fade_colour(end_fade_, c, 0) : c); };
        for (int y = 0; y < canvas.height; ++y) {
            const auto& palette = y < 151 ? day_ : sea_;
            for (int x = 0; x < canvas.width; ++x) {
                const auto index = canvas.at(x, y);
                auto colour = col(index < palette.size() ? palette[index] : 0);
                // (The flash sets the upper palette's colour 0 in the copper
                // list; hypothesis: rows above the sea's palette split.)
                if (index == 0 && flash_ != 0 && y < 151) colour = col(std::uint16_t(flash_));
                image.put(2 * x, y, colour);
                image.put(2 * x + 1, y, colour);
            }
        }
        if (dashboard_) {
            for (int y = 0; y < dash.height; ++y) {
                for (int x = 0; x < dash.width; ++x) {
                    const auto index = dash.at(x, y);
                    image.put(side + x, canvas.height + y, col(dashboard_->palette[index % dashboard_->palette.size()]));
                }
            }
        }
        // The ticker, a line below the dashboard: 640 of its 672 pixels.
        const int ticker_top = canvas.height + dash_height + 1;
        for (int y = 0; y < Ticker::rows; ++y) {
            const auto colour = col(ticker_colours_[std::min<std::size_t>(std::size_t(y), ticker_colours_.size() - 1)]);
            for (int x = 0; x < 640; ++x) {
                if (ticker_.pixel(x, y)) image.put(side + x, ticker_top + y, colour);
            }
        }
        return image;
    }

    data::Assets assets_;
    FlightTables tables_;
    CrewTables crew_tables_;
    DashboardTables dash_tables_;
    BriefingTables briefing_tables_;
    const data::MissionMap* map_;
    data::Palette day_, sea_;
    const amiga::IlbmPicture* dashboard_{};
    FlightWorld world_;
    PlayerFlight plane_;
    int blackout_ = 0;
    std::minstd_rand random_{std::random_device{}()};
    long vblank_ = 0;
    long press_start_ = 0;
    bool fire_down_ = false, tapped_ = false, held_ = false;
    int gear_offset_ = 5;
    int us_flag_step_ = 0;
    int flash_ = 0;           // this frame's background colour 0 from a hit (0: none)
    int flash_counter_ = 0;   // -$5C76
    amiga::Paula paula_;
    struct ChannelState {
        std::uint16_t period{}, volume{};
        template <class A> void persist(A &ar) { ar(period, volume); }
    };
    std::array<ChannelState, 4> channels_{};
    std::vector<EnemySprite> enemy_sprites_;
    TickerFont ticker_font_;
    FrontEnd front_end_;
    std::filesystem::path content_; // (the saved games and high scores live here)
    MusicPlayer music_;
    int song_pending_ = -1;   // a song waiting for the last one's fade (-2: none)
    Ticker ticker_;
    std::array<std::uint16_t, 10> ticker_colours_{};
    bool mission_over_ = false;
    int end_fade_ = -1;       // $1073E6 after the game: the step of the fade to black
    bool quit_ = false;
    std::array<bool, 64> ctrl_was_{};
    bool saves_in_game_ = false; // the saved games opened from the game (not the rank menu)
    bool exited_ = false;        // Exit Game
    bool inverted_ = false;      // Ctrl-f: up and down swapped (-$5B08)
    std::array<bool, 64> plain_was_{};
    int debug_step_ = 0;            // -$50E6: `colin` typed so far (5: the debug mode)
    bool debug_invulnerable_ = false; // the debug key d's switch (-$408C)
    bool leave_after_fade_ = false;  // q: the program ends after the game ($10020E)
    bool ranks_after_briefing_ = false; // Ctrl-r at a briefing
    int playing_song_ = -1;   // the song the front end plays (for quick load)
    int vhs_ = 0;
    bool smooth_ = false;
    // Smooth motion: the state the last tick started from, the shell's time
    // since its last call to tick (a fraction of a vertical blank), and
    // whether that call ran the game on (not while it waits, paused).
    FlightWorld before_world_;
    PlayerFlight before_plane_;
    bool have_before_ = false;
    double frame_fraction_ = 0;
    bool moved_ = false;
    bool widescreen_ = false;
    int margin_ = 0; // widescreen columns on each side of the view
    int canvas_width_ = 0, canvas_height_ = 0;
    struct Cheats {
        bool infinite_lives{}, infinite_fuel{}, bullet_proof{}, infinite_ammo{}, enemy_duds{};
        template <class A> void persist(A &ar) {
            ar(infinite_lives, infinite_fuel, bullet_proof, infinite_ammo, enemy_duds);
        }
    } cheats_;
    struct Snapshot {
        FlightWorld world;
        PlayerFlight plane;
        const data::MissionMap* map;
        int blackout;
        std::minstd_rand random;
        long vblank, press_start;
        bool fire_down, tapped, held;
        int gear_offset, us_flag_step, flash, flash_counter;
        std::vector<EnemySprite> enemy_sprites;
        FrontEnd front_end;
        int song_pending, playing_song;
        Ticker ticker;
        bool mission_over;
        int end_fade;
        bool quit;
        std::array<bool, 64> ctrl_was;
        int briefing, briefing_fade;
        bool briefing_out;
        int frame_facing, enemy_flag_counter, enemy_flags_drawn;
    };
    std::optional<Snapshot> saved_;
    int briefing_ = 0;        // vertical blanks the briefing still shows
    int briefing_fade_ = -1;  // its fade's step (-1: none)
    bool briefing_out_ = false;
    int frame_facing_ = -1;   // -$4084: the facing at the frame start
    int enemy_flag_counter_ = 0;
    int enemy_flags_drawn_ = 0;
};

} // namespace

std::unique_ptr<app::GameModule> start(const std::filesystem::path& content) {
    return std::make_unique<WingsOfFury>(content);
}

} // namespace wings::game
