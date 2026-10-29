#pragma once
#include "tower_of_babel_amiga/data/assets.hpp"
#include "tower_of_babel_amiga/design/tower_design.hpp"
#include "tower_of_babel_amiga/front/data_disk.hpp"
#include "tower_of_babel_amiga/front/play.hpp"
#include "tower_of_babel_amiga/front/screen.hpp"
#include "tower_of_babel_amiga/front/tasks.hpp"
#include "state/archive.hpp"
#include <array>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// The game as the player meets it, from the title to the towers and back:
// the title and credits, the player's name, the main menu, the game types,
// the tower chooser and the tower's information, playing it with its pause
// menus, and the screens after it (the score, or why it was lost) with a
// demonstration running above them. Each of the original's routines that
// waits for vertical blanks is a task (front/tasks.hpp), so all of it is
// values; the texts, menus' click areas, pictures and palettes are the
// program's.
namespace babel::front {

class FrontEnd {
public:
    // `saves`: the folder that stands for the player's data disk.
    FrontEnd(const data::Assets&, std::filesystem::path saves);

    // What the player does during one vertical blank (PAL, 50 a second).
    struct Input {
        bool pointer{};         // the mouse is over the picture
        int x{}, y{};           // in the screen's pixels
        bool left{}, right{};   // buttons held
        int key{};              // a key typed this frame (the original's codes: ASCII, 8 delete, 13 return, 27 escape), 0 none

        template <class A> void persist(A& ar) { ar(pointer, x, y, left, right, key); }
    };
    void vblank(const Input&);

    const Screen& screen() const { return screen_; }
    // The picture to show: the screen as the game drew it, or, while a tower
    // or demonstration runs, its view drawn again `margin` columns wider on
    // each side (widescreen) and between game frames (smooth motion;
    // `fraction`: of the vertical blank since the last). Menus and other
    // screens are centred.
    platform::Image picture(int margin, bool smooth, double fraction) const;
    // The pointer as the game sees it, for a picture `margin` columns wider.
    int margin() const { return margin_; }
    void set_margin(int m) { margin_ = m; }
    // A sampled piece to start this frame (0 the title music, 1 the jingle), or none.
    std::optional<int> take_piece() { return std::exchange(piece_, std::nullopt); }
    bool piece_playing() const { return piece_frames_ > 0; }
    // What the sound player is told, in order, since the last call: stop
    // ($15494), pause ($154C6), go on ($154F2), a game frame's sounds ($15342)
    // or a sound started at once ($15540).
    struct SoundEvent {
        enum class Kind { stop, pause, resume, frame, start } kind;
        Effects::Frame frame{};
        std::uint32_t record{};
        int volume_offset{};
    };
    std::vector<SoundEvent> take_sounds() { return std::exchange(sounds_, {}); }

    // Quick saves, anywhere: the tasks under way with what each keeps, the
    // towers being played, the screen and everything the game keeps. A
    // load goes on from there.
    void write_state(state::Writer&);
    void read_state(state::Reader&); // on a fresh front end, before its first frame

private:
    // --- the tasks: each run() goes on with its task until it waits, calls
    // another or ends (see front/tasks.hpp).
    enum class Go { wait, call, end };
    void run_tasks();
    // Calls tasks in turn (the first runs first); the caller goes on after the last.
    template <class T, class... More> Go call(T task, More... more) {
        if constexpr (sizeof...(More) > 0) call(std::move(more)...);
        tasks_.emplace_back(std::move(task));
        return Go::call;
    }
    // The task's answer: a called task's result, read when its caller goes on.
    Go end(int result) {
        result_ = result;
        return Go::end;
    }
    tasks::Fade fade(const std::array<data::Colour, 48>& target, int first = 0, int count = 48) const;
    tasks::Fade fade_out() const;                             // $124B0: to black
    tasks::Fade fade_in_program(std::uint32_t palette) const; // a 48-colour palette in the program
    tasks::Select select(std::uint32_t areas) const { return {areas, false, {}}; }

    Go run(tasks::WaitFrames&);
    Go run(tasks::WaitRelease&);
    Go run(tasks::GameFrameWait&);
    Go run(tasks::Select&);
    Go run(tasks::Fade&);
    Go run(tasks::Game&);
    Go run(tasks::Title&);
    Go run(tasks::Credits&);
    Go run(tasks::CreditsWait&);
    Go run(tasks::NameEntry&);
    Go run(tasks::EditField&);
    Go run(tasks::MainMenu&);
    Go run(tasks::PlayGame&);
    Go run(tasks::ChooseTower&);
    Go run(tasks::Information&);
    Go run(tasks::StandardGame&);
    Go run(tasks::TutorialGame&);
    Go run(tasks::CustomGame&);
    Go run(tasks::PlayTower&);
    Go run(tasks::PauseMenu&);
    Go run(tasks::SetupPage&);
    Go run(tasks::ViewPage&);
    Go run(tasks::InfoPage&);
    Go run(tasks::ExitPage&);
    Go run(tasks::Confirm&);
    Go run(tasks::WonScreen&);
    Go run(tasks::EndScreen&);
    Go run(tasks::InterludeOk&);
    Go run(tasks::DemoSequence&);
    Go run(tasks::SaveLoad&);
    Go run(tasks::HighScores&);
    Go run(tasks::Requester&);
    Go run(tasks::ReplaceFile&);
    Go run(tasks::DiskError&);
    // The designer (front/designer.cpp).
    Go run(tasks::Designer&);
    Go run(tasks::ChooseSlot&);
    Go run(tasks::TowerUnlocked&);
    Go run(tasks::DesignerFile&);
    Go run(tasks::DesignerOptions&);
    Go run(tasks::TextPage&);
    Go run(tasks::EffectsPage&);
    Go run(tasks::SoundPage&);
    Go run(tasks::MoonPage&);
    Go run(tasks::BehaviourPage&);
    Go run(tasks::FinishingPage&);
    Go run(tasks::TestDesign&);
    Go run(tasks::DesignerToolbox&);
    Go run(tasks::ScrollPage&);
    Go run(tasks::LevelPage&);
    Go run(tasks::ColoursPage&);
    Go run(tasks::DesignerExtras&);
    Go run(tasks::DesignerSetup&);
    Go run(tasks::StatisticsPage&);
    Go run(tasks::SupportPage&);
    Go run(tasks::PathPage&);
    Go run(tasks::MousePage&);

    // --- drawing and rules the tasks share
    void draw_editor(const LineEditor&, bool cursor = true);
    bool edit_key(LineEditor&, int key, int end_x); // false: Return
    void edit_click(LineEditor&, int x);
    // A requester's action ($126C0's routine) on the name given: 0, an
    // error ($80 + n is its message), or -1 to have the requester ask before
    // replacing the file.
    int disk_action(DiskAction, const std::string& name, bool replace);
    void draw_requester_page(const tasks::Requester&); // $1297A
    void read_directory(tasks::Requester&);            // $12A10 / $12AFA
    void update_scores();       // $AA8C: the player's score into the table
    void sort_scores();         // $AA24
    std::vector<std::uint8_t> position() const; // what Save Game keeps ($110E-$11D3)
    bool load_position(const std::vector<std::uint8_t>&);
    void draw_high_scores();
    void toggle_ok(bool& lit);  // $948C's highlight

    void designer_start();      // $E386 up to the loop: the group, the design's settings, the screen
    void designer_screen();     // $10BDE: palette, title line, texts, the object palette
    void designer_views();      // $E3A8: floor plans, view window, password, spiders, floor boxes, the chosen kind
    void draw_plans();          // $10E44
    void draw_view_window();    // $10EFA
    void draw_palette_icon(int slot); // $1108A
    void draw_palette_cursor(); // $10D0C
    void designer_message(int message); // $10D70
    void draw_password();       // $10CD2
    void draw_spiders();        // $10C60
    void draw_floor_boxes();    // $10C1A
    void designer_edit(bool left); // $1098E
    void letter_grid(int y);    // $E538
    void tower_name_window(int slot); // $EC84
    void draw_moon_sky();       // the Moon page's little sky
    void draw_finishing_status();
    void draw_colours_page();   // $F38E: the page drawn
    void draw_chosen_colour();
    void step_colours(int command); // Step/Swap Colours ($1247A, $12420)
    void snapshot();            // $108D0: the undo copy
    void unpack_design(const std::array<std::uint8_t, data::tower_record_size>&); // $13024
    std::array<std::uint8_t, data::tower_record_size> pack_design() const;      // $12E4A

    void header();              // $A0DA: the name and score bar
    void menu_boxes(int rows);  // $11AF6
    void objective(const data::Tower&); // $A12A
    void draw_towers(int game_type, int completed, int available); // $A542
    void start_interlude(int index); // $158AA
    void draw_score_numbers();  // $961E
    const data::Tower& tower_in(int game_type, int index) const;
    std::uint8_t& progress(int game_type, int index);
    int completed_mask(int game_type, int& count) const; // $8E6C

    const data::Assets& assets_;
    std::deque<Task> tasks_;  // the routines under way, the one going on last (a deque: its tasks stay in place)
    int result_{};            // the last ended task's answer
    std::string edited_;      // the last line edited
    // The tower or demonstration being played, if any.
    const Play* active_play() const;
    Play* active_play() { return const_cast<Play*>(std::as_const(*this).active_play()); }
    bool play_paused_{};      // its pause menu is open
    int margin_{};
    mutable view::Canvas wide_{320, 200}; // the wide picture as the pause began
    mutable bool paused_wide_{};
    view::Canvas wide_picture(int margin, double alpha) const;
    view::Canvas paused_screen_{320, 200}; // the screen as the pause began: what the menus change shows over the wide view
    DataDisk disk_;
    Screen screen_;
    Input in_{}, previous_{};
    bool clicked_{};        // the left button went down since the last game frame
    int clicked_x_{}, clicked_y_{};
    double ticks_{};        // CIA-A timer B ticks since the last game frame
    std::array<data::Colour, 48> current_{};
    std::optional<int> piece_;
    int piece_frames_{};
    std::vector<SoundEvent> sounds_;
    void sound(SoundEvent::Kind k, std::uint32_t record = 0) { sounds_.push_back({k, {}, record, 0}); }
    void sound_frame(const Play&);
    std::uint16_t random_{0x1234}; // $890

    // The player and the game ($111E, $113C, $1140-$11CA, $FD6).
    std::string name_;
    long score_{};
    int group_{};
    bool all_done_{};      // $1142 = 0: every standard tower solved
    std::array<std::uint8_t, 117> standard_progress_{};
    std::array<std::uint8_t, 9> custom_progress_{}, tutorial_progress_{};
    int selected_{-1};     // $1220
    int completed_count_{}; // $11DC
    int interludes_{};     // $FD6
    Settings settings_;
    int screen_x_{1}, screen_y_{}; // $11CC, $11CE: kept for saved positions
    struct Score {
        std::string name;
        long score{};

        template <class A> void persist(A& ar) { ar(name, score); }
    };
    std::vector<Score> scores_;    // $122E: ten, sorted (room for 21 while merging)
    LineEditor file_name_{0xAC, 0x6E, 8, 0xFE, 0xAC, 0x6C, 0xFF, 0x77, {}, 0, false}; // $F7E
    std::vector<data::Tower> custom_towers_; // the custom group ($60952)
    bool fast_fades_{};    // $500: fades are instant (the designer)
    std::optional<data::Tower> last_tower_; // the tower the game unpacked last ($13278)
    // The designer's state.
    design::TowerDesign design_;
    std::string time_text_;            // $F58: the time limit as typed
    std::vector<std::array<std::uint8_t, data::tower_record_size>> designer_group_; // $5F512
    std::array<std::uint8_t, data::tower_record_size> undo_{};
    bool undo_ready_{};                // $E14 = 0
    int floor_{};                      // $DA8
    int slot_{};                       // $DAA: the palette slot
    std::array<std::int8_t, 24> palette_{}; // $DB8: the kind each slot shows
    int hover_x_{8}, hover_y_{8};      // $DAC, $DAE: the view window's cell under the pointer
    bool depth_cue_{true}, enemy_movement_{true}, auto_repeat_{true}, confirm_replace_{true}, bright_{}; // $E08-$E12
    bool painting_{};                  // $E0E bit 7: hold to paint floor
    int colour_{};                     // $DB4
    bool whole_tower_{true};           // $DB6 = 0
    int mouse_scaling_{1}, mouse_style_{}; // $11D0, $11D2 (no effect with the host's pointer)
    std::array<data::Colour, 18> previous_colours_{}; // $2616
    std::unique_ptr<Play> interlude_;
    int interlude_index_{};            // its demonstration
    std::array<std::uint8_t, data::tower_record_size> loaded_tower_{}; // Load Tower's record
    template <class A> void persist(A&);
    template <class A> void persist_task(A&, Task&);
    long shown_bonus_[3]{}; // $1222-$1226 while counting
    int survivors_{};      // spiders left at the end of the last tower
};

} // namespace babel::front
