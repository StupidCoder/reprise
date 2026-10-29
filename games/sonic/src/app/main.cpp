// The BlueStreak application: library, ROM import, level selector, sound test,
// the in-game pause menu with quick save/load and enhancement toggles, and the
// fixed-rate main loop that runs the game module.
//
// Command line (all optional):
//   --data-dir DIRECTORY   use DIRECTORY instead of the user's app-data folder
//   --import ROM           import a ROM dump (.gg, .gba) on startup

#include "app/frame_rate.hpp"
#include "app/game_module.hpp"
#include "app/level_select.hpp"
#include "app/quick_save.hpp"
#include "app/shell_ui.hpp"
#include "app/titles.hpp"
#include "platform/host.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <future>
#include <iostream>
#include <optional>

namespace {

namespace fs = std::filesystem;

struct Options {
    fs::path data_dir;
    fs::path import;
};

Options parse_arguments(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if ((arg == "--data-dir" || arg == "--import") && i + 1 < argc) {
            const auto value = platform::utf8_path(argv[++i]);
            (arg == "--data-dir" ? options.data_dir : options.import) = value;
        } else if (arg == "--reduced-motion") {
            shell::style::reduce_motion = true;
        } else {
            throw std::runtime_error("Usage: bluestreak [--data-dir DIRECTORY] "
                                     "[--import ROM] [--reduced-motion]");
        }
    }
    return options;
}

// User preferences, stored as "sonic-gg.settings": a format version followed
// by one 0/1 value per option. Older formats are migrated on load.
struct Settings {
    shell::Enhancements enhancements;
    bool muted{};
};

constexpr int settings_version = 5;

Settings load_settings(const fs::path& root) {
    Settings s;
    auto& e = s.enhancements;
    std::ifstream in(root / "sonic-gg.settings");
    int version = 0, view = 0, muted = 0, value = 0;
    if (in >> version >> view >> muted && version >= 1 && version <= settings_version) {
        // Versions 1 and 2 stored an older view preference; they start in the
        // original view.
        e.widescreen = version >= 3 && view != 0;
        s.muted = muted != 0;
        // Each later version appended one option.
        if (version >= 2 && in >> value) {
            e.shield_transparency = value != 0;
        }
        if (version >= 3 && in >> value) {
            e.water = value != 0;
        }
        if (version >= 4 && in >> value) {
            e.rings = value != 0;
        }
        if (version >= 5 && in >> value) {
            e.lcd_screen = value != 0;
        }
    }
    return s;
}

void save_settings(const fs::path& root, const Settings& s) {
    const auto temp = root / "sonic-gg.settings.tmp";
    std::ofstream out(temp);
    const auto& e = s.enhancements;
    out << settings_version << ' ' << e.widescreen << ' ' << s.muted << ' ' << e.shield_transparency
        << ' ' << e.water << ' ' << e.rings << ' ' << e.lcd_screen << '\n';
    out.close();
    if (!out) {
        throw std::runtime_error("Could not save settings");
    }
    // A rename cannot replace an existing file on Windows. Settings are only
    // preferences, so a plain remove-then-rename is good enough here.
    std::error_code ec;
    fs::remove(root / "sonic-gg.settings", ec);
    fs::rename(temp, root / "sonic-gg.settings");
}

// Items of the pause menu, in display order. The enhancements menu lists the
// running game's enhancements (Title::enhancements) followed by Back.
enum PauseItem { resume_item, save_item, load_item, sound_item, enhancements_item, quit_item };

constexpr int pause_items = 6;

// Title card timing around a launch: the card sweeps in over the shell, the
// game is created while it covers the screen, then it sweeps out before the
// first game update. Shell screens change with a diagonal wipe.
constexpr double card_in = 0.35, card_hold = 0.12, card_out = 0.42, wipe_time = 0.4;

class BlueStreakApp {
public:
    explicit BlueStreakApp(const Options& options);
    void run();

private:
    // --- Library and import ---
    void restore_shell_state();
    void save_shell_state();
    void focus_game(int index);
    void enter_generation(int generation);
    void start_import(const fs::path& file);
    void poll_import();
    void update_library(const platform::Controls& in);

    // --- Level selector and sound test ---
    void update_level_select(const platform::Controls& in);
    void open_music_player();
    void close_music_player();
    void update_music_player(const platform::Controls& in, double elapsed);

    // --- Playing ---
    void start_launch(int level, std::string subtitle);
    void update_launch(platform::Controls& in);
    void play(int level);
    void quick_save();
    void quick_load(bool resume);
    void update_game(const platform::Controls& in, double elapsed, bool loaded_this_frame);
    void update_pause_menu(const platform::Controls& in);
    void update_enhancements_menu(const platform::Controls& in);

    // The enhancement settings the running game offers; the rest are off.
    shell::Enhancements active_enhancements() const {
        return shell::offered(settings_.enhancements, game_title_->enhancements);
    }

    // --- Output ---
    void present();
    void present_game();
    void present_shell();
    void notify(std::string message);
    void toggle_mute();
    void store_settings();

    // The title focused in the library, and the one whose game is running.
    const shell::Title* title() const { return shell::title_for(view_.selection); }

    void refresh_selection();

    fs::path pack() const { return title()->pack(root_); }

    const shell::Title* game_title_{};
    std::optional<shell::LevelLayout> layout_;

    platform::Host host_;
    fs::path root_;
    Settings settings_;
    shell::LibraryView view_;
    fs::path shell_state_path_;
    bool quit_{};

    // Background import.
    std::atomic<unsigned> progress_{};
    std::future<std::string> importer_;

    // Active screens: a game, the sound test, or the level selector.
    std::unique_ptr<shell::GameModule> game_;
    std::unique_ptr<gg::MusicPlayer> music_;
    int music_selection_{}, music_focus_{};
    bool selecting_level_{};
    int level_selection_{};

    // Pause and enhancement menus.
    bool paused_{};
    int pause_selection_{};
    bool enhancements_menu_{};
    int enhancement_selection_{};
    double menu_since_{};
    bool saved_{}; // A quick save exists.
    std::string game_message_;
    double message_until_{};

    // Launch title card and shell transitions.
    struct Launch {
        bool active{}, started{};
        int level{-1};
        double at{};
        std::string subtitle;
    } launch_;

    double card_leave_at_{-10};
    int shell_key_{-1}, shell_depth_{}, transition_direction_{1};
    double transition_at_{};
    std::optional<platform::Image> last_shell_, transition_from_;

    // Fixed-rate timing: real time accumulates and the game runs whole updates.
    double previous_{}, accumulator_{};
    shell::FrameRate frame_rate_{0};

    void reset_clock() {
        accumulator_ = 0;
        previous_ = host_.seconds();
    }
};

BlueStreakApp::BlueStreakApp(const Options& options)
    : host_("BlueStreak"), root_(options.data_dir) {
    if (root_.empty()) {
        root_ = host_.data_directory();
    }
    fs::create_directories(root_);
    settings_ = load_settings(root_);
    host_.mute(settings_.muted);
    host_.pause_audio(true);

    for (int n = 0; n < int(shell::catalog.size()); ++n) {
        const auto* t = shell::title_for(n);
        if (!t || !t->installed(root_)) {
            continue;
        }
        try {
            t->validate(t->pack(root_));
            view_.installed[std::size_t(n)] = true;
        } catch (const std::exception&) {
            view_.message = "The installed assets need repair. Import your supported ROM again.";
        }
    }
    shell_state_path_ = root_ / "shell.state";
    restore_shell_state();
    refresh_selection();
    if (!options.import.empty()) {
        start_import(options.import);
    }
    previous_ = host_.seconds();
    frame_rate_ = shell::FrameRate(previous_);
}

// The library reopens where the player left it: generation, game and depth.
void BlueStreakApp::restore_shell_state() {
    std::ifstream in(shell_state_path_);
    int version = 0, zone = 0, generation = 0, selection = 0;
    const bool valid = in >> version >> zone >> generation >> selection && version == 1 &&
                       generation >= 0 && generation < int(shell::generations.size()) &&
                       shell::generations[std::size_t(generation)].available &&
                       selection >= shell::first_game(generation) &&
                       selection < shell::first_game(generation) + shell::game_count(generation);
    if (valid) {
        view_.zone = zone != 0;
        view_.generation = view_.generation_from = generation;
        view_.selection = selection;
        view_.picks[std::size_t(generation)] = selection;
    } else {
        view_.zone =
            std::any_of(view_.installed.begin(), view_.installed.end(), [](bool b) { return b; });
    }
}

// The library state that depends on the focused game.
void BlueStreakApp::refresh_selection() {
    const auto* t = title();
    view_.ready = t && view_.installed[std::size_t(view_.selection)];
    view_.sound_test = t && t->music;
    saved_ = t && shell::has_quick_save(root_, t->save_name);
}

void BlueStreakApp::save_shell_state() {
    // Convenience only: failures are ignored.
    std::ofstream out(shell_state_path_);
    out << 1 << ' ' << view_.zone << ' ' << view_.generation << ' ' << view_.selection << '\n';
}

void BlueStreakApp::store_settings() {
    try {
        save_settings(root_, settings_);
    } catch (const std::exception& e) {
        view_.message = e.what();
    }
}

void BlueStreakApp::toggle_mute() {
    settings_.muted = !settings_.muted;
    host_.mute(settings_.muted);
    store_settings();
}

void BlueStreakApp::notify(std::string message) {
    game_message_ = std::move(message);
    message_until_ = host_.seconds() + 6;
}

void BlueStreakApp::run() {
    while (!quit_) {
        auto in = host_.poll();
        quit_ = in.quit;
        if (auto choice = host_.take_file_choice()) {
            view_.choosing = false;
            if (choice->file) {
                start_import(*choice->file);
            } else {
                view_.message = choice->error.empty() ? "Import cancelled. No files were changed."
                                                      : choice->error;
            }
        }
        poll_import();
        update_launch(in);

        const double now = host_.seconds();
        const double elapsed = std::min(now - previous_, 0.25);
        previous_ = now;

        // Ctrl/Cmd+S and Ctrl/Cmd+L shortcuts.
        bool loaded_this_frame = false;
        if (in.quick_save && game_) {
            quick_save();
            reset_clock();
        }
        const bool library_ready_for_load =
            view_.ready && view_.zone && !view_.importing && !view_.choosing;
        if (!music_ && in.quick_load && !launch_.active && (game_ || library_ready_for_load)) {
            quick_load(true);
            reset_clock();
            loaded_this_frame = true;
        }

        if (music_) {
            update_music_player(in, elapsed);
        } else if (!game_ && selecting_level_) {
            accumulator_ = 0;
            update_level_select(in);
        } else if (!game_) {
            accumulator_ = 0;
            update_library(in);
        } else if (launch_.active) {
            accumulator_ = 0;
        } else {
            update_game(in, elapsed, loaded_this_frame);
        }
        present();
        frame_rate_.presented(host_.seconds());
    }
    save_shell_state();
    if (importer_.valid()) {
        const auto error = importer_.get();
        if (!error.empty()) {
            std::cerr << error << '\n';
        }
    }
}

// ---------------------------------------------------------------------------
// Library and import
// ---------------------------------------------------------------------------

void BlueStreakApp::focus_game(int index) {
    if (index == view_.selection) {
        return;
    }
    view_.slide_from = view_.selection - shell::first_game(view_.generation);
    view_.selection = index;
    view_.picks[std::size_t(view_.generation)] = index;
    view_.moved_at = host_.seconds();
    view_.message.clear();
    refresh_selection();
}

void BlueStreakApp::enter_generation(int generation) {
    view_.generation_from = view_.generation;
    view_.generation = generation;
    view_.moved_at = host_.seconds();
    if (!shell::generations[std::size_t(generation)].available) {
        view_.zone = false;
        view_.message = "Game Boy Advance and DS games are planned for a later update.";
        return;
    }
    view_.zone = true;
    view_.selection = view_.picks[std::size_t(generation)];
    view_.slide_from = view_.selection - shell::first_game(generation);
    view_.message.clear();
    refresh_selection();
}

// Imports on a worker thread; poll_import() picks up the result.
void BlueStreakApp::start_import(const fs::path& file) {
    if (view_.importing) {
        return;
    }
    // The game the file is for (by size); otherwise the focused game's
    // importer explains what is supported.
    int target = title() ? view_.selection : 0;
    for (int n = 0; n < int(shell::catalog.size()); ++n) {
        if (const auto* t = shell::title_for(n); t && t->recognizes(file)) {
            target = n;
            break;
        }
    }
    view_.selection = target;
    view_.generation = view_.generation_from = shell::catalog[std::size_t(target)].generation;
    view_.picks[std::size_t(view_.generation)] = target;
    view_.zone = true;
    view_.slide_from = target - shell::first_game(view_.generation);
    view_.importing = true;
    view_.choosing = false;
    view_.message.clear();
    progress_ = 0;
    const auto* t = shell::title_for(target);
    importer_ = std::async(std::launch::async, [file, root = root_, t, this] {
        try {
            t->import(file, root,
                      [this](unsigned done, unsigned total) { progress_ = done * 100 / total; });
            return std::string{};
        } catch (const std::exception& e) {
            return std::string(e.what());
        }
    });
}

void BlueStreakApp::poll_import() {
    if (!view_.importing) {
        return;
    }
    view_.progress = progress_.load();
    if (importer_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return;
    }
    const auto error = importer_.get();
    view_.importing = false;
    if (error.empty()) {
        view_.installed[std::size_t(view_.selection)] = true;
        refresh_selection();
        view_.message = "Import complete. Select Play to begin.";
        view_.time = host_.seconds();
    } else {
        view_.message = error;
    }
}

// The library has two levels: a row of hardware generations, and inside one
// generation a carousel of its games.
void BlueStreakApp::update_library(const platform::Controls& in) {
    if (in.back && !view_.choosing && !view_.importing && !view_.zone) {
        quit_ = true;
    }
    if (in.dropped_file && !view_.importing && !view_.choosing) {
        start_import(*in.dropped_file);
    }
    if (view_.importing || view_.choosing) {
        return;
    }

    if (!view_.zone) {
        // Generation select: panels left to right.
        const int count = int(shell::generations.size());
        if (in.nav_left || in.nav_right) {
            const int g =
                std::clamp(view_.generation + int(in.nav_right) - int(in.nav_left), 0, count - 1);
            if (g != view_.generation) {
                view_.generation_from = view_.generation;
                view_.generation = g;
                view_.moved_at = host_.seconds();
                view_.message.clear();
            }
        }
        bool activate = in.accept || in.start_pressed;
        if (in.click) {
            const int hit = shell::library_hit(view_, in.mouse_x, in.mouse_y);
            if (hit >= shell::hit_generation && hit < shell::hit_generation + count) {
                view_.generation = hit - shell::hit_generation;
                activate = true;
            }
        }
        if (activate) {
            enter_generation(view_.generation);
        }
        return;
    }

    // Game carousel inside one generation.
    const int first = shell::first_game(view_.generation);
    const int last = first + shell::game_count(view_.generation) - 1;
    bool back_out = in.back;
    if (in.nav_left || in.nav_right) {
        focus_game(std::clamp(view_.selection + int(in.nav_right) - int(in.nav_left), first, last));
    }
    if (in.prev_group || in.next_group) {
        // The next available generation in that direction; locked ones are skipped.
        const int step = in.next_group ? 1 : -1;
        for (int g = view_.generation + step; g >= 0 && g < int(shell::generations.size());
             g += step) {
            if (shell::generations[std::size_t(g)].available) {
                enter_generation(g);
                break;
            }
        }
    }
    bool activate = in.accept || in.start_pressed;
    if (in.click) {
        const int hit = shell::library_hit(view_, in.mouse_x, in.mouse_y);
        if (hit == shell::hit_action) {
            activate = true;
        } else if (hit == shell::hit_back) {
            back_out = true;
        } else if (hit >= shell::hit_generation) {
            enter_generation(hit - shell::hit_generation);
        } else if (hit >= 0) {
            activate = hit == view_.selection;
            focus_game(hit);
        }
    }
    if (back_out) {
        view_.zone = false;
        view_.generation_from = view_.generation;
        view_.moved_at = host_.seconds();
        view_.message.clear();
        activate = false;
    }
    const bool music_requested =
        view_.zone && view_.ready && view_.sound_test &&
        (in.toggle_mute || (in.click && shell::library_music_hit(view_, in.mouse_x, in.mouse_y)));
    if (music_requested) {
        open_music_player();
        reset_clock();
        activate = false;
    }
    if (!activate || !view_.zone) {
        return;
    }
    const auto& entry = shell::catalog[std::size_t(view_.selection)];
    if (!entry.implemented) {
        view_.message = std::string(entry.title) +
                        (entry.subtitle[0] ? std::string(" ") + entry.subtitle : "") +
                        " is planned. ROM import will be enabled with its native module.";
    } else if (view_.ready && title()->levels.empty()) {
        // No level selector for this game: straight to the full game.
        view_.message.clear();
        start_launch(-1, "Full game");
    } else if (view_.ready) {
        selecting_level_ = true;
        level_selection_ = 0;
        layout_.emplace(*title());
        view_.message.clear();
    } else {
        view_.choosing = true;
        view_.message = title()->supported;
        host_.choose_rom();
    }
}

// ---------------------------------------------------------------------------
// Level selector and sound test
// ---------------------------------------------------------------------------

void BlueStreakApp::update_level_select(const platform::Controls& in) {
    if (in.back) {
        selecting_level_ = false;
        view_.message.clear();
        return;
    }
    if (in.nav_left || in.nav_right || in.nav_up || in.nav_down) {
        level_selection_ = layout_->move(level_selection_, int(in.nav_right) - int(in.nav_left),
                                         int(in.nav_down) - int(in.nav_up));
        view_.message.clear();
    }
    bool activate = in.accept || in.start_pressed;
    if (in.click) {
        const int n = layout_->hit(in.mouse_x, in.mouse_y);
        if (n >= 0) {
            level_selection_ = n;
            activate = true;
        }
    }
    if (!activate) {
        return;
    }
    if (level_selection_ == layout_->back) {
        selecting_level_ = false;
    } else if (level_selection_ == 0) {
        start_launch(-1, "Full game");
    } else {
        const auto& level = title()->levels[std::size_t(level_selection_ - 1)];
        start_launch(level.id, level.world + " " + level.label);
    }
}

void BlueStreakApp::open_music_player() {
    try {
        music_ = title()->music(pack());
        music_selection_ = 0;
        music_focus_ = 0;
        selecting_level_ = false;
        save_shell_state();
        host_.clear_audio();
        host_.pause_audio(true);
        view_.message.clear();
    } catch (const std::exception& e) {
        music_.reset();
        view_.message = std::string("Could not open music player. ") + e.what();
    }
}

void BlueStreakApp::close_music_player() {
    music_.reset();
    host_.pause_audio(true);
    host_.clear_audio();
}

// The sound test: a track list plus a row of transport buttons. music_focus_ 0
// is the list; 1..6 are the buttons (previous, play/pause, next, stop, back,
// mute), which also have keyboard shortcuts.
void BlueStreakApp::update_music_player(const platform::Controls& in, double elapsed) {
    if (in.back) {
        close_music_player();
        accumulator_ = 0;
        return;
    }
    const int tracks = int(music_->tracks().size());

    enum Action { previous = 100, play_pause, next, stop, back, mute };

    bool audio_changed = false;
    if (in.toggle_view) {
        music_focus_ = (music_focus_ + 1) % 7;
    }
    if (in.nav_up || in.nav_down) {
        music_focus_ = 0;
        music_selection_ = (music_selection_ + tracks + int(in.nav_down) - int(in.nav_up)) % tracks;
    }
    if (in.nav_left || in.nav_right) {
        const int direction = in.nav_right ? 1 : -1;
        if (music_focus_) {
            music_focus_ = 1 + (music_focus_ - 1 + 6 + direction) % 6;
        } else {
            music_->skip(direction);
            music_selection_ = int(music_->track());
            audio_changed = true;
        }
    }
    int action = -1;
    if ((in.accept && !in.space_pressed) || in.start_pressed) {
        action = music_focus_ ? 99 + music_focus_ : music_selection_;
    }
    if (in.click) {
        action = shell::music_hit(*music_, music_selection_, in.mouse_x, in.mouse_y);
    }
    if (in.space_pressed) {
        action = play_pause;
    }
    if (in.stop_playback) {
        action = stop;
    }
    if (in.reset) {
        music_->play(music_->track());
        audio_changed = true;
    }
    if (in.toggle_mute) {
        action = mute;
    }

    if (action >= 0 && action < tracks) {
        music_selection_ = action;
        music_focus_ = 0;
        music_->play(unsigned(action));
        audio_changed = true;
    } else if (action == previous || action == next) {
        music_->skip(action == previous ? -1 : 1);
        music_selection_ = int(music_->track());
        audio_changed = true;
    } else if (action == play_pause) {
        music_->toggle();
        audio_changed = true;
    } else if (action == stop) {
        music_->stop();
        audio_changed = true;
    } else if (action == back) {
        close_music_player();
    } else if (action == mute) {
        toggle_mute();
    }

    if (!music_) {
        accumulator_ = 0;
        return;
    }
    if (audio_changed) {
        host_.pause_audio(!music_->playing());
        host_.clear_audio();
        reset_clock();
    }
    if (!music_->playing()) {
        accumulator_ = 0;
        return;
    }
    accumulator_ += elapsed;
    while (accumulator_ >= gg::MusicPlayer::step_seconds() && music_->playing()) {
        host_.audio(music_->tick());
        accumulator_ -= gg::MusicPlayer::step_seconds();
    }
}

// ---------------------------------------------------------------------------
// Playing
// ---------------------------------------------------------------------------

void BlueStreakApp::start_launch(int level, std::string subtitle) {
    launch_ = {true, false, level, host_.seconds(), std::move(subtitle)};
}

// While the launch card runs it owns the input (only closing the window passes
// through). The game is created once the card covers the screen.
void BlueStreakApp::update_launch(platform::Controls& in) {
    if (!launch_.active) {
        return;
    }
    const bool close = in.quit;
    in = {};
    in.quit = close;
    const double t = host_.seconds() - launch_.at;
    if (!launch_.started && t >= card_in) {
        play(launch_.level);
        launch_.started = true;
        launch_.at = host_.seconds();
        if (!game_) {
            launch_.active = false;
        }
    } else if (launch_.started && t >= card_hold + card_out) {
        launch_.active = false;
        reset_clock();
    }
}

// Starts the full game (level -1) or a level selector start point.
void BlueStreakApp::play(int level) {
    try {
        game_title_ = title();
        game_ = level < 0 ? game_title_->make(pack(), {}) : game_title_->make_level(pack(), level);
        selecting_level_ = false;
        game_message_.clear();
        paused_ = false;
        save_shell_state();
        host_.pause_audio(false);
        view_.message.clear();
    } catch (const std::exception& e) {
        game_.reset();
        view_.ready = false;
        view_.message = std::string("Could not load this pack. Import the ROM again. ") + e.what();
    }
}

void BlueStreakApp::quick_save() {
    if (!game_) {
        view_.message = "Start a game before making a quick save.";
        return;
    }
    try {
        shell::write_quick_save(root_, game_title_->save_name, game_->save_state());
        saved_ = true;
        notify("Quick saved. " + std::string(platform::shortcut_modifier) +
               "+L restores this moment.");
    } catch (const std::exception& e) {
        notify(std::string("Save failed: ") + e.what());
    }
}

// Loads the quick save. The restored module is built separately, so a failed
// load never disturbs the running game. From the pause menu the game stays
// paused (resume = false); the Ctrl/Cmd+L shortcut resumes at once.
void BlueStreakApp::quick_load(bool resume) {
    try {
        const auto* t = game_ ? game_title_ : title();
        auto state = shell::read_quick_save(root_, t->save_name);
        auto restored = t->make(t->pack(root_), state);
        game_ = std::move(restored);
        game_title_ = t;
        selecting_level_ = false;
        paused_ = !resume;
        menu_since_ = host_.seconds();
        pause_selection_ = 0;
        enhancements_menu_ = false;
        host_.pause_audio(paused_);
        host_.clear_audio();
        saved_ = true;
        notify(resume ? "Quick loaded." : "Quick loaded. Resume when ready.");
    } catch (const std::exception& e) {
        if (game_) {
            notify(std::string("Load failed: ") + e.what());
        } else {
            view_.message = std::string("Load failed: ") + e.what();
        }
    }
}

void BlueStreakApp::update_game(const platform::Controls& in, double elapsed,
                                bool loaded_this_frame) {
    // The pause menu opens with the menu key, or Start during gameplay (Start
    // on the title and map screens still belongs to the game).
    bool opened = loaded_this_frame;
    if (!loaded_this_frame && !paused_ && (in.menu || (in.start_pressed && game_->gameplay()))) {
        paused_ = true;
        opened = true;
        menu_since_ = host_.seconds();
        pause_selection_ = 0;
        enhancements_menu_ = false;
        host_.pause_audio(true);
        accumulator_ = 0;
    }
    if (paused_) {
        // The menus ignore input on the update that opened them.
        if (!opened) {
            if (enhancements_menu_) {
                update_enhancements_menu(in);
            } else {
                update_pause_menu(in);
            }
        }
        accumulator_ = 0;
        return;
    }
    if (loaded_this_frame) {
        return;
    }
    if (in.toggle_mute) {
        toggle_mute();
    }
    if (in.reset) {
        game_->restart();
        host_.clear_audio();
        accumulator_ = 0;
    }
    // Run as many fixed game updates as real time allows. The view size is set
    // first because the widescreen view changes object activation.
    accumulator_ += elapsed;
    const auto canvas = host_.canvas_size();
    const auto enhancements = active_enhancements();
    game_->set_view(enhancements.widescreen, canvas[0], canvas[1]);
    while (accumulator_ >= game_->step_seconds()) {
        host_.audio(game_->tick(
            {in.left, in.right, in.button1, in.down, in.up, in.start, in.button2}, enhancements));
        accumulator_ -= game_->step_seconds();
    }
}

void BlueStreakApp::update_pause_menu(const platform::Controls& in) {
    pause_selection_ =
        (pause_selection_ + pause_items + int(in.nav_down) - int(in.nav_up)) % pause_items;
    bool activate = in.accept || in.start_pressed;
    if (in.click) {
        const int n = shell::pause_hit(in.mouse_x, in.mouse_y);
        if (n >= 0) {
            pause_selection_ = n;
            activate = true;
        }
    }
    if (in.back || in.menu || (activate && pause_selection_ == resume_item)) {
        paused_ = false;
        host_.pause_audio(false);
        return;
    }
    if (!activate) {
        return;
    }
    switch (pause_selection_) {
    case save_item:
        quick_save();
        previous_ = host_.seconds();
        break;
    case load_item:
        quick_load(false);
        previous_ = host_.seconds();
        break;
    case sound_item:
        toggle_mute();
        break;
    case enhancements_item:
        enhancements_menu_ = true;
        menu_since_ = host_.seconds();
        enhancement_selection_ = 0;
        break;
    case quit_item:
        game_.reset();
        paused_ = false;
        host_.pause_audio(true);
        view_.message = "Run ended. Select Play to start again.";
        card_leave_at_ = host_.seconds();
        break;
    }
}

void BlueStreakApp::update_enhancements_menu(const platform::Controls& in) {
    const auto& offered = game_title_->enhancements;
    const int back = int(offered.size()), items = back + 1;
    enhancement_selection_ =
        (enhancement_selection_ + items + int(in.nav_down) - int(in.nav_up)) % items;
    bool activate = in.accept || in.start_pressed;
    if (in.click) {
        const int n = shell::enhancements_hit(in.mouse_x, in.mouse_y, back);
        if (n >= 0) {
            enhancement_selection_ = n;
            activate = true;
        }
    }
    if (in.back || in.menu || (activate && enhancement_selection_ == back)) {
        enhancements_menu_ = false;
        menu_since_ = host_.seconds();
        return;
    }
    if (!activate) {
        return;
    }
    bool& on = shell::setting(settings_.enhancements, offered[std::size_t(enhancement_selection_)]);
    on = !on;
    store_settings();
}

// ---------------------------------------------------------------------------
// Output
// ---------------------------------------------------------------------------

void BlueStreakApp::present() {
    if (game_ && !music_) {
        present_game();
    } else {
        present_shell();
    }
}

// The game frame is presented at its own small resolution and scaled by the
// host; menus, messages, the launch card and the frame-rate badge are separate
// overlay layers.
void BlueStreakApp::present_game() {
    const auto canvas = host_.canvas_size();
    const auto enhancements = active_enhancements();
    const auto frame = game_->render(enhancements, canvas[0], canvas[1]);
    std::optional<platform::Image> overlay;
    const auto stop = game_->stopped();
    const auto message = !stop.empty()                      ? "Stopped: " + stop
                         : host_.seconds() < message_until_ ? game_message_
                                                            : "";
    shell_key_ = -1;
    if (launch_.active && launch_.started) {
        // The card is sized to the window's aspect so that it covers it fully.
        const int scale = std::max(1, (canvas[0] + 959) / 960);
        const double leave = (host_.seconds() - launch_.at - card_hold) / card_out;
        overlay = shell::title_card(std::max(1, canvas[0] / scale), std::max(1, canvas[1] / scale),
                                    game_title_->name, launch_.subtitle, 0, 1, leave);
    } else if (paused_ || !message.empty()) {
        const std::string game_subtitle =
            std::string(game_title_->name) + " / " + game_title_->platform;
        overlay = shell::game_overlay({.status = game_->status(),
                                       .message = message,
                                       .paused = paused_,
                                       .muted = settings_.muted,
                                       .saved = saved_,
                                       .enhancements_open = enhancements_menu_,
                                       .selection = pause_selection_,
                                       .enhancement_selection = enhancement_selection_,
                                       .since = host_.seconds() - menu_since_,
                                       .game = game_subtitle,
                                       .offered = game_title_->enhancements,
                                       .enhancements = enhancements});
    }
    const auto badge = shell::frame_rate_image(frame_rate_.fps(), canvas[1]);
    const bool fill = enhancements.widescreen && (frame.width != 160 || frame.height != 144);
    host_.present_game(frame, fill, paused_, overlay ? &*overlay : nullptr, &badge,
                       enhancements.lcd_screen);
}

void BlueStreakApp::present_shell() {
    const double t = host_.seconds();
    view_.time = t;
    auto image = music_ ? shell::music_image(*music_, title()->name, music_selection_, music_focus_,
                                             settings_.muted, t)
                 : selecting_level_ ? shell::level_select_image(*layout_, title()->name,
                                                                level_selection_, view_.message, t)
                                    : shell::library_image(view_);

    // A diagonal wipe whenever the shell changes screens: deeper screens wipe
    // in from the left, going back wipes from the right.
    const int key = music_ ? 3 : selecting_level_ ? 2 : view_.zone ? 10 + view_.generation : 0;
    const int depth = key == 0 ? 0 : key >= 10 ? 1 : 2;
    if (key != shell_key_) {
        if (shell_key_ != -1 && last_shell_) {
            transition_from_ = std::move(last_shell_);
            transition_at_ = t;
            transition_direction_ = depth != shell_depth_ ? (depth > shell_depth_ ? 1 : -1)
                                                          : (key > shell_key_ ? 1 : -1);
        }
        shell_key_ = key;
        shell_depth_ = depth;
    }
    last_shell_ = image;
    if (transition_from_) {
        const double progress = (t - transition_at_) / wipe_time;
        if (progress >= 1) {
            transition_from_.reset();
        } else {
            shell::wipe(image, *transition_from_, progress, transition_direction_);
        }
    }

    // Title cards: sweeping in before a launch, or out after leaving a game.
    if (launch_.active && !launch_.started) {
        shell::composite(image, shell::title_card(960, 640, title()->name, launch_.subtitle, 0,
                                                  (t - launch_.at) / card_in, 0));
    } else if (t - card_leave_at_ < card_out) {
        shell::composite(image, shell::title_card(960, 640, "BlueStreak", "Back to the library", 0,
                                                  1, (t - card_leave_at_) / card_out));
    }
    host_.present(image, "BlueStreak");
}

} // namespace

int main(int argc, char** argv) {
    try {
        BlueStreakApp app(parse_arguments(argc, argv));
        app.run();
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    return 0;
}
