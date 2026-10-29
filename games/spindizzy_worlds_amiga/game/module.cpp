#include "spindizzy_worlds_amiga/game/module.hpp"
#include "spindizzy_worlds_amiga/data/assets.hpp"
#include "spindizzy_worlds_amiga/game/front_end.hpp"
#include "spindizzy_worlds_amiga/game/play_screen.hpp"
#include "spindizzy_worlds_amiga/play/level_play.hpp"
#include "spindizzy_worlds_amiga/play/surfaces.hpp"
#include "spindizzy_worlds_amiga/sound/effects.hpp"
#include "spindizzy_worlds_amiga/sound/music.hpp"
#include "spindizzy_worlds_amiga/view/panel.hpp"
#include "spindizzy_worlds_amiga/view/screen.hpp"
#include "state/archive.hpp"
#include "ui/vhs.hpp"
#include <algorithm>
#include <array>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>
#include <optional>

namespace spindizzy::game {
namespace {

constexpr std::uint32_t main_menu = 0x18B44, main_menu_box = 0x18B3A;   // $18A84
constexpr std::uint32_t next_menu = 0x18CDC, next_menu_box = 0x18CD2;   // $18CB0: "Next world", "Save game"
constexpr std::uint32_t reserve_menu = 0x18C2E; // $18C84: "Use reserve fuel", "Retry this world", "Abandon this world"
constexpr std::uint32_t retry_menu = 0x18C4A;   // $18C22: the last two
constexpr int main_menu_timeout = 0x64 << 3;                             // $18A90: d7 × 8 passes
constexpr std::uint32_t save_magic = 0x535A5753, save_version = 2; // "SWZS"
constexpr std::uint32_t easy_worlds = 0x9F800000, hard_worlds = 0x007FFFFF; // $11E10: $24AE per cluster

// The game's screens in the original's main loop ($18950).
enum class Stage { attract, menu, clusters, map, play, statistics, next_menu, game_over, break_up, ending };

class Module final : public app::GameModule {
public:
    explicit Module(const std::filesystem::path& content)
        : assets_(data::load_assets(content)), surfaces_(play::read_surfaces(assets_.main)), effects_(assets_.effects), music_(assets_.music) {
        paula_.set_memory(effects_.memory());
        effects_.reset(paula_);
        music_paula_.set_memory(music_.memory());
        open_attract();
    }

    double step_seconds() const override { return 1.0 / 50; } // PAL frames

    // A quick save is the whole state as it is now: the screen on show and
    // its own state, the level as it stands (grid, tile map, objects,
    // explorer, camera, stars, timers), the world's progress, both sound
    // players and their channels. Version 1 (the game between worlds only)
    // still loads, onto the world map.
    std::vector<std::uint8_t> write_state() override {
        state::Writer ar;
        std::uint32_t magic = save_magic, version = save_version;
        ar(magic, version);
        persist(ar);
        return std::move(ar.bytes);
    }
    void read_state(std::span<const std::uint8_t> in) override {
        if (in.size() >= 4 && in[0] == 'S' && in[1] == 'D' && in[2] == 'W' && in[3] == 1) {
            read_between_worlds(in);
            return;
        }
        state::Reader ar(in);
        std::uint32_t magic{}, version{};
        ar(magic, version);
        if (magic != save_magic || version != save_version)
            throw std::runtime_error("Spindizzy Worlds: not a save of this version.");
        persist(ar);
        ar.finish();
    }

    template <class A> void persist(A& ar) {
        ar(stage_, cluster_, worlds_left_, worlds_done_, slow_, keys_held_, space_held_, stick_, offers_reserve_, records_, statistics_, saved_,
           sound_wait_, effects_volume_, statistics_then_next_, world_start_fuel_, world_start_reserve_, turn_,
           progress_, effects_, music_, paula_, music_paula_);
        // The screens, made first where they hold the assets, then read.
        part(ar, attract_, [&] { attract_.emplace(assets_); });
        part(ar, menu_, [&] { menu_.emplace(assets_, view::Screen{}, main_menu, 0, 0); });
        part(ar, clusters_, [&] { clusters_.emplace(assets_, turn_); });
        part(ar, map_, [&] { map_.emplace(assets_, true, 0, turn_); });
        part(ar, ending_, [&] { ending_.emplace(assets_, view::Screen{}, true); });
        bool playing = play_ != nullptr;
        ar(playing);
        if constexpr (A::reading) {
            play_.reset();
            if (playing) play_ = std::make_unique<PlayScreen>(assets_, surfaces_, progress_, play::LevelPlay::Restore{});
        }
        if (playing) play_->persist(ar);
        if constexpr (A::reading) {
            const bool shown = (stage_ == Stage::attract && attract_) || ((stage_ == Stage::menu || stage_ == Stage::next_menu || stage_ == Stage::game_over) && menu_) ||
                               (stage_ == Stage::clusters && clusters_) || ((stage_ == Stage::map || stage_ == Stage::break_up) && map_) ||
                               ((stage_ == Stage::play || stage_ == Stage::game_over) && play_) || (stage_ == Stage::ending && ending_) ||
                               stage_ == Stage::statistics;
            if (!shown || int(stage_) < 0 || stage_ > Stage::ending) state::Reader::fail();
        }
    }
    template <class A, class T, class Make> static void part(A& ar, std::optional<T>& screen, Make make) {
        bool has = screen.has_value();
        ar(has);
        if constexpr (A::reading) {
            screen.reset();
            if (has) make();
        }
        if (has) screen->persist(ar);
    }

    // Version 1: what the original's sd_game.dat keeps (the cluster, the
    // worlds done and their records, the clues shown, slow mode) and the
    // menu's slot; it goes to the world map.
    void read_between_worlds(std::span<const std::uint8_t> in) {
        std::size_t at = 4;
        auto get = [&]() -> std::int32_t {
            if (at + 4 > in.size()) throw std::runtime_error("Spindizzy Worlds: the save is cut short.");
            std::int32_t v = 0;
            for (int i = 0; i < 4; ++i) v = std::int32_t(std::uint32_t(v) << 8 | in[at++]);
            return v;
        };
        auto game = [&]() {
            Saved g;
            g.cluster = get(); g.worlds_left = std::uint32_t(get()); g.worlds_done = get(); g.clues_left = std::uint32_t(get());
            g.fuel = get(); g.reserve = get();
            const int n = get();
            for (int i = 0; i < n; ++i) { WorldRecord r; r.world = get(); r.fuel = get(); r.reserve = get(); r.found = get(); r.taken = get(); g.records.push_back(r); }
            return g;
        };
        const Saved now = game();
        if (get()) saved_ = game();
        slow_ = get() != 0;
        restore(now);
        enter_map();
    }
    double pixel_aspect() const override { return 1.0; }

    // The port's additions, all presentation only: the game, its sound and
    // its saves are the same with any choice.
    std::vector<app::GameOption> options() const override {
        return {{"Widescreen", {"Off", "On"}, widescreen_ ? 1 : 0,
                 "Fills the window to its edges, up to 21:9: the levels show more of the world left and right, "
                 "the pillars stand at the new edges. The other screens stay in the middle. The game plays the same.",
                 {}, true},
                {"Smooth motion", {"Off", "On"}, smooth_ ? 1 : 0,
                 "The explorer, the moving objects and the view glide at the display's own rate instead of "
                 "stepping 50 times a second. The game plays the same.",
                 {}, true},
                app::screen_filter_option(vhs_),
                cheat("Infinite fuel", progress_.cheats.infinite_fuel,
                      "The fuel never goes down: not with time, falls, hard landings, aliens or sparks. Jewels still add "
                      "to it. Deadly timers still end the game."),
                cheat("No falling deaths", progress_.cheats.no_fall_deaths,
                      "Falling off the world puts the explorer back where it last stood firmly, at no cost, and a hard "
                      "landing does no harm. Deadly floors, ice and timers still kill."),
                cheat("Harmless aliens", progress_.cheats.harmless_creatures,
                      "Pac aliens, pyramids and boinks still move and still pop when touched, but take no fuel."),
                cheat("Stopped timers", progress_.cheats.stopped_timers,
                      "A deadly timer stands still. Timers that take the explorer somewhere (a bonus level's end) still "
                      "run, since they are the way out."),
                cheat("All worlds open", open_worlds_,
                      "The cluster's last world, the planet in its middle, can be entered before the others are done: "
                      "turn the cluster up or down until the map names the middle world, then press fire. "
                      "Finishing worlds counts as usual for the planets' break-up and the ending.")};
    }
    static app::GameOption cheat(std::string name, bool on, std::string description) {
        return {std::move(name), {"Off", "On"}, on ? 1 : 0, std::move(description), "Cheats"};
    }
    void set_option(std::size_t index, int value) override {
        if (index == 0) {
            widescreen_ = value != 0;
            set_canvas(canvas_width_, canvas_height_);
        }
        if (index == 1) smooth_ = value != 0;
        if (index == 2) vhs_ = std::clamp(value, 0, ui::vhs_levels - 1);
        auto& c = progress_.cheats;
        if (index == 3) c.infinite_fuel = value != 0;
        if (index == 4) c.no_fall_deaths = value != 0;
        if (index == 5) c.harmless_creatures = value != 0;
        if (index == 6) c.stopped_timers = value != 0;
        if (index == 7) open_worlds_ = value != 0;
    }
    int vhs_level() const override { return vhs_; }
    void set_frame_fraction(double fraction) override { fraction_ = std::clamp(fraction, 0.0, 1.0); }
    // Widescreen: as wide as the window's shape allows, from 320 pixels up
    // to 21:9 of the 200 rows.
    void set_canvas(int width, int height) override {
        canvas_width_ = width;
        canvas_height_ = height;
        margin_ = 0;
        if (widescreen_ && width > 0 && height > 0) {
            const double wide = std::clamp(200.0 * width / height, 320.0, 200.0 * 21 / 9);
            margin_ = std::max(0, int(wide - 320) / 2);
        }
    }

    // The effects player runs from the CIA-B timer at 49.92 Hz ($10D5E:
    // $3782 E-clock ticks) while a level plays; the samples come from Paula.
    void audio(std::span<float> stereo, int rate) override {
        std::fill(stereo.begin(), stereo.end(), 0.0f);
        const std::size_t frames = stereo.size() / 2;
        constexpr double tick_rate = 709379.0 / 0x3782;
        std::size_t done = 0;
        while (done < frames) {
            // $1116C hooks the effects for a level; they stay hooked over
            // the map after a world and the ending until a tune starts.
            const bool level = stage_ == Stage::play || stage_ == Stage::game_over || stage_ == Stage::break_up || stage_ == Stage::ending;
            if (sound_wait_ <= 0) {
                // The hook $2054: the effects player in levels, the music
                // player elsewhere ($1116C / $112A2), each on its own copy
                // of the channels (only one runs at a time).
                if (level) effects_.tick(paula_);
                else music_.tick(music_paula_);
                sound_wait_ += double(rate) / tick_rate;
            }
            const std::size_t chunk = std::min(frames - done, std::size_t(std::max(1.0, sound_wait_)));
            (level ? paula_ : music_paula_).render(stereo.subspan(done * 2, chunk * 2), rate);
            sound_wait_ -= double(chunk);
            done += chunk;
        }
    }

    void tick(const app::GameInput& input) override {
        std::uint8_t joystick = 0;
        if (input.up) joystick |= play::stick::up;
        if (input.down) joystick |= play::stick::down;
        if (input.left) joystick |= play::stick::left;
        if (input.right) joystick |= play::stick::right;
        if (input.fire) joystick |= play::stick::fire;
        stick_.read(joystick); // the CIA-B interrupt's reader ($104C0)
        // Keys pressed this frame ($107A8 keeps the last until it is read).
        std::array<bool, 64> fresh{};
        bool key_pressed = input.space && !space_held_;
        for (std::size_t k = 0; k < 64; ++k) {
            fresh[k] = input.c64_keys[k] && !keys_held_[k];
            key_pressed = key_pressed || fresh[k];
        }
        keys_held_ = input.c64_keys;
        space_held_ = input.space;
        switch (stage_) {
        case Stage::attract:
            if (attract_->frame(joystick)) open_menu();
            break;
        case Stage::menu: {
            const int item = menu_->frame(stick_);
            slow_ = menu_->slow_mode;
            if (item == -1) { // timed out: the attract again
                open_attract();
            } else if (item == 1) { // Start new game ($18AB4)
                cluster_ = 0;
                worlds_done_ = 0;
                records_.clear();
                progress_.clues_left = 0xFFFFFFFF;
                enter_map();
            } else if (item == 0 || item == 6) { // Continue / Slow Mode
                enter_map();
            } else if (item == 4) { // Check performance ($18AF6)
                open_statistics(false);
            } else if (item == 3) { // Save game state ($16CBE): the game between worlds
                saved_ = current();
                open_menu();
            } else if (item == 2 && saved_) { // Load saved game ($16D28): then the statistics ($18AD0)
                restore(*saved_);
                open_statistics(false);
            } else if (item == 5) { // Wind back world ($18B10): the last world done undone, then the map
                if (worlds_done_ > 0) {
                    --worlds_done_;
                    if (!records_.empty()) records_.pop_back();
                }
                // $16D44: the worlds left again from the records kept.
                if (cluster_ != 0) {
                    worlds_left_ = cluster_ == 1 ? easy_worlds : hard_worlds;
                    for (const auto& r : records_) worlds_left_ &= ~(1u << (r.world & 31));
                }
                enter_map();
            }
            break; // Create Data disk ($10AB0): the shell's saves stand in for it
        }
        case Stage::clusters:
            if (const int chosen = clusters_->frame(joystick)) {
                cluster_ = chosen;
                worlds_left_ = chosen == 1 ? easy_worlds : hard_worlds;
                open_map();
            }
            break;
        case Stage::map: {
            map_->middle_open = open_worlds_;
            const int world = map_->frame(joystick);
            turn_ = map_->turn();
            if (world < 0) break;
            // $1177A: world $16 needs the Hard worlds 0-21 done, $1F the Easy 23-28.
            // (Not with the "All worlds open" cheat.)
            if (!open_worlds_ && ((world == 0x1F && (worlds_left_ & 0x1F800000)) || (world == 0x16 && (worlds_left_ & 0x003FFFFF)))) {
                map_->refuse();
                break;
            }
            progress_.start_world(assets_.main, world, worlds_done_ * 2);
            // $16D48: the fuel and reserve of the last world done (a new game: $80, 0).
            progress_.fuel = records_.empty() ? 0x80 << 16 : records_.back().fuel << 16;
            progress_.reserve = records_.empty() ? 0 : records_.back().reserve << 16;
            world_start_fuel_ = progress_.fuel;
            world_start_reserve_ = progress_.reserve;
            play_ = std::make_unique<PlayScreen>(assets_, surfaces_, progress_);
            stage_ = Stage::play;
            break;
        }
        case Stage::play: {
            PlayInput in;
            in.joystick = joystick;
            in.space = input.space;
            // Keys as the C64 matrix gives them: R (17) and T (22) turn the level.
            // F (21) slow mode, C (20) centre, P (41) pause.
            // M (36) the overview map; [ has no key in the matrix the host
            // gives: '@' (46), where a C64 matrix would put it.
            constexpr int key_r = 17, key_t = 22, key_f = 21, key_c = 20, key_p = 41, key_m = 36, key_bracket = 46;
            auto pressed = [&](int k) { return fresh[std::size_t(k)]; };
            if (pressed(key_r)) in.turn = 1;
            if (pressed(key_t)) in.turn = -1;
            if (pressed(key_f)) slow_ = !slow_; // $11440
            in.centre = pressed(key_c);
            in.pause = pressed(key_p);
            in.hold = pressed(key_bracket);
            in.overview = pressed(key_m);
            in.key = key_pressed;
            play_->frame(in, slow_, stick_);
            for (const auto& sound : progress_.sounds) { // the request block $36D2
                auto& volume = effects_volume_[std::size_t(sound.channel & 3)];
                if (sound.volume >= 0) volume = sound.volume;
                if (sound.effect < 0) effects_.set_volume(sound.channel, volume);
                else effects_.request(sound.channel, sound.effect, volume);
            }
            progress_.sounds.clear();
            if (play_->outcome() == PlayScreen::Outcome::world_done) {
                // $11838: the map with the world's planet breaking up; its
                // making ($16A86) asks for effect 0 on channel 0.
                map_.emplace(assets_, cluster_ == 1, worlds_left_, turn_, progress_.world);
                request({0, 0, 0x40});
                stage_ = Stage::break_up;
            } else if (play_->outcome() == PlayScreen::Outcome::game_over) {
                // $18C14: the reserve is offered with at least $20 of it and
                // the fuel empty.
                offers_reserve_ = (progress_.reserve >> 16) >= 0x20 && (progress_.fuel >> 16) == 0;
                view::Screen behind;
                play_->draw(behind);
                menu_.emplace(assets_, behind, offers_reserve_ ? reserve_menu : retry_menu, 0, 0);
                stick_.byte = 0; // $17E5A
                stage_ = Stage::game_over;
            }
            break;
        }
        case Stage::break_up: {
            const bool over = map_->break_frame(joystick, slow_);
            turn_ = map_->turn();
            if (!over) break;
            // $16B8C: the world is recorded done; fuel and reserve go on.
            worlds_left_ &= ~(1u << (progress_.world & 31));
            ++worlds_done_;
            records_.push_back({progress_.world, progress_.fuel >> 16, progress_.reserve >> 16,
                                progress_.jewels_found, progress_.jewels_taken});
            if (worlds_left_ == 0) { // $18A5E: every world done, the ending
                view::Screen shown;
                map_->draw(shown);
                ending_.emplace(assets_, shown, cluster_ == 1);
                take_sounds(ending_->sounds);
                stage_ = Stage::ending;
                break;
            }
            start_tune(2);         // $18A66: the map's tune again
            open_statistics(true); // $18C90: the statistics, fire, then "Next world"
            break;
        }
        case Stage::ending: {
            const bool over = ending_->frame(key_pressed || stick_.take_fire());
            take_sounds(ending_->sounds);
            if (!over) break;
            // $17D5E: the worlds done and the cluster forgotten (the clues
            // kept), tune 1, then the title ($18962), which keeps it.
            cluster_ = 0;
            worlds_done_ = 0;
            records_.clear();
            start_tune(1);
            open_attract();
            break;
        }
        case Stage::statistics: { // $109CC: until a key or the stick's fire
            if (key_pressed || stick_.take_fire()) {
                if (statistics_then_next_) {
                    menu_.emplace(assets_, statistics_, next_menu, next_menu_box, 0);
                    stick_.byte = 0;
                    stage_ = Stage::next_menu;
                } else {
                    open_menu();
                }
            }
            break;
        }
        case Stage::next_menu: {
            const int item = menu_->frame(stick_);
            if (item == 1) { // "Save game" ($16CBE), then the menu again
                saved_ = current();
                menu_.emplace(assets_, statistics_, next_menu, next_menu_box, 0);
                stick_.byte = 0;
            } else if (item == 0) {
                enter_map();
            }
            break;
        }
        case Stage::game_over: {
            int item = menu_->frame(stick_);
            if (item < 0) break;
            if (!offers_reserve_) ++item; // the same meanings as the long menu's
            if (item == 0) { // use the reserve ($18A28)
                play_->revive();
                stage_ = Stage::play;
            } else if (item == 1) { // retry the world ($18A3C → $16D44)
                const auto fuel = world_start_fuel_, reserve = world_start_reserve_;
                progress_.start_world(assets_.main, progress_.world, worlds_done_ * 2);
                progress_.fuel = fuel;
                progress_.reserve = reserve;
                play_ = std::make_unique<PlayScreen>(assets_, surfaces_, progress_);
                stage_ = Stage::play;
            } else { // abandon ($18956): tune 3, then the title, which keeps it
                start_tune(3);
                open_attract();
            }
            break;
        }
        }
    }

    // $11250: a tune started (from its beginning); the music player takes
    // the interrupt hook again ($112A2).
    void start_tune(int tune) { music_.start(tune, music_paula_); }
    void request(const EffectRequest& r) {
        effects_volume_[std::size_t(r.channel & 3)] = r.volume;
        effects_.request(r.channel, r.effect, r.volume);
    }
    void take_sounds(std::vector<EffectRequest>& sounds) {
        for (const auto& r : sounds) request(r);
        sounds.clear();
    }

    platform::Image picture() const override {
        if (margin_ > 0 || smooth_) {
            const double fraction = smooth_ ? fraction_ : 1.0;
            if (stage_ == Stage::play) return play_->draw_wide(margin_, fraction).image();
            if (stage_ == Stage::game_over && play_ && margin_ > 0) {
                // The menu over the level: its box over the wide picture.
                auto wide = play_->draw_wide(margin_, 1.0);
                view::Screen shown;
                menu_->draw(shown);
                const auto& behind = menu_->behind();
                for (int y = 0; y < shown.height; ++y)
                    for (int x = 0; x < shown.width; ++x)
                        if (shown.at(x, y) != behind.at(x, y)) wide.put(margin_ + x, y, shown.at(x, y));
                return wide.image();
            }
        }
        view::Screen screen;
        switch (stage_) {
        case Stage::attract: attract_->draw(screen); break;
        case Stage::menu: case Stage::next_menu: case Stage::game_over: menu_->draw(screen); break;
        case Stage::clusters: clusters_->draw(screen); break;
        case Stage::map: case Stage::break_up: map_->draw(screen); break;
        case Stage::ending: ending_->draw(screen); break;
        case Stage::play: play_->draw(screen); break;
        case Stage::statistics: screen = statistics_; break;
        }
        if (margin_ == 0) return screen.image();
        view::Screen wide(320 + 2 * margin_, 200); // the other screens in the middle
        wide.palette = screen.palette;
        wide.paste(screen, margin_, 0);
        return wide.image();
    }

private:
    // $17694 → $1771C: the title starts tune 0 only when no tune plays
    // (the module's byte $6F9, $439A1), so a tune started before (3 after
    // "Abandon", 1 after the ending) goes on over the attract and menu.
    void open_attract() {
        attract_.emplace(assets_);
        if (!music_.playing()) start_tune(0);
        stage_ = Stage::attract;
    }
    void open_menu() {
        view::Screen behind;
        attract_->draw(behind);
        menu_.emplace(assets_, behind, main_menu, main_menu_box, main_menu_timeout);
        stick_.byte = 0; // $17E5A
        menu_->slow_mode = slow_;
        stage_ = Stage::menu;
    }
    struct Saved {
        int cluster{};
        std::uint32_t worlds_left{};
        int worlds_done{};
        std::uint32_t clues_left{0xFFFFFFFF};
        std::int32_t fuel{0x80 << 16}, reserve{};
        std::vector<WorldRecord> records;
        template <class A> void persist(A& ar) { ar(cluster, worlds_left, worlds_done, clues_left, fuel, reserve, records); }
    };
    Saved current() const {
        // Between worlds: the fuel and reserve the next world starts with.
        Saved g{cluster_, worlds_left_, worlds_done_, progress_.clues_left, 0x80 << 16, 0, records_};
        if (!records_.empty()) { g.fuel = records_.back().fuel << 16; g.reserve = records_.back().reserve << 16; }
        return g;
    }
    void restore(const Saved& g) {
        cluster_ = g.cluster;
        worlds_left_ = g.worlds_left;
        worlds_done_ = g.worlds_done;
        records_ = g.records;
        const auto cheats = progress_.cheats;
        progress_ = play::Progress{};
        progress_.cheats = cheats;
        progress_.clues_left = g.clues_left;
        progress_.fuel = g.fuel;
        progress_.reserve = g.reserve;
        play_.reset();
    }

    void open_statistics(bool then_next) {
        statistics_ = view::Screen();
        view::set_level_palette(statistics_, assets_.graphics); // $11066: palette 0
        draw_statistics(statistics_, assets_, records_);
        statistics_then_next_ = then_next;
        stage_ = Stage::statistics;
    }
    // $116F0: the map's tune from its start ($1170A), then the cluster
    // choice when none is chosen yet, then the map.
    void enter_map() {
        start_tune(2);
        open_map();
    }
    void open_map() {
        if (cluster_ == 0) { // $11710: no cluster chosen yet
            clusters_.emplace(assets_, turn_);
            stage_ = Stage::clusters;
            return;
        }
        map_.emplace(assets_, cluster_ == 1, worlds_left_, turn_);
        stage_ = Stage::map;
    }

    data::Assets assets_;
    play::SurfaceTable surfaces_;
    play::Progress progress_;
    Stage stage_{Stage::attract};
    std::optional<Attract> attract_;
    std::optional<Menu> menu_;
    std::optional<ClusterChoice> clusters_;
    std::optional<WorldMap> map_;
    std::optional<Ending> ending_;
    view::ClusterView::Turn turn_;  // the map's angles, kept between visits
    std::unique_ptr<PlayScreen> play_;
    int cluster_{};                 // $36B4: 0 none yet, 1 Easy, 2 Hard
    std::uint32_t worlds_left_{};   // $24AE
    int worlds_done_{};             // $36B0 / 2
    bool slow_{true};               // $24A6: the menu's "Slow Mode? Yes"
    std::array<bool, 64> keys_held_{};
    bool space_held_{};
    play::StickByte stick_; // $10A63 / $10A64
    bool offers_reserve_{};
    std::vector<WorldRecord> records_; // $36B6 …
    view::Screen statistics_;
    std::optional<Saved> saved_; // the menu's "Save game state" slot
    sound::EffectsPlayer effects_;
    sound::MusicPlayer music_;
    amiga::PaulaChannels paula_, music_paula_;
    double sound_wait_{};
    std::array<int, 4> effects_volume_{0x40, 0x40, 0x40, 0x40};
    bool statistics_then_next_{};
    std::int32_t world_start_fuel_{}, world_start_reserve_{}; // what $16D44 restores for a retry
    // The port's presentation options (not saved with the game).
    bool widescreen_{}, smooth_{};
    bool open_worlds_{}; // the "All worlds open" cheat
    int vhs_{};
    int canvas_width_{}, canvas_height_{}, margin_{};
    double fraction_{1.0};
};

} // namespace

std::unique_ptr<app::GameModule> start(const std::filesystem::path& content) {
    return std::make_unique<Module>(content);
}

} // namespace spindizzy::game
