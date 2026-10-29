#pragma once
#include "tower_of_babel_amiga/data/assets.hpp"
#include "tower_of_babel_amiga/design/tower_design.hpp"
#include "tower_of_babel_amiga/front/data_disk.hpp"
#include "tower_of_babel_amiga/front/flow.hpp"
#include "tower_of_babel_amiga/front/play.hpp"
#include "tower_of_babel_amiga/front/screen.hpp"
#include "state/archive.hpp"
#include <array>
#include <memory>
#include <functional>
#include <optional>
#include <string>
#include <vector>

// The game as the player meets it, from the title to the towers and back:
// the title and credits, the player's name, the main menu, the game types,
// the tower chooser and the tower's information, playing it with its pause
// menus, and the screens after it (the score, or why it was lost) with a
// demonstration running above them. Written as the original's routines
// are, waiting for vertical blanks (front/flow.hpp); the texts, menus'
// click areas, pictures and palettes are the program's.
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

        // One word for the journal of a quick save.
        std::uint64_t packed() const;
        static Input unpacked(std::uint64_t);
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

    // Quick saves, anywhere: the state at the last point the flow can be
    // taken up again from (the start, the main menu drawn, the designer
    // opened, each game frame of a tower) and the vertical blanks' inputs
    // since. A load takes the state up at that point and plays the inputs
    // again, so the game goes on exactly as it would have. (What the data
    // disk held is read again as it is now.) The sounds the replay makes
    // are dropped: the module keeps the effects' own state.
    void write_state(state::Writer&);
    void read_state(state::Reader&); // on a fresh front end, before its first frame

private:
    // --- waiting
    auto next_frame() { return runner_.next_frame(); }
    void step(const Input&); // one vertical blank, `x` in the game's own picture
    Flow wait_frames(int n);
    Flow wait_release();        // $11318: until no button is held
    Flow select(std::uint32_t menu, int& choice); // $112EA
    Flow game_frame_wait();     // $87EE: a game frame is 14 ticks of the CIA timer
    Flow fade(const std::array<data::Colour, 48>& target, int first = 0, int count = 48); // $1252C / $1255C
    Flow fade_out();            // $124B0: to black
    Flow fade_in_program(std::uint32_t palette); // a 48-colour palette in the program

    // --- screens
    Flow run();
    Flow resumed();             // after a quick load: back to the main menu, the designer or the tower
    Flow title();               // $7EF6
    Flow credits();             // $15A2E
    Flow name_entry();          // $8BD6
    // A line editor ($12096): its record (place, most characters, rightmost
    // x, click box) and text; `filter` rejects keys (the requester's '.' and space).
    struct LineEditor {
        int x{}, y{}, most{}, right{};
        int box_x0{}, box_y0{}, box_x1{}, box_y1{};
        std::string text;
        int cursor{};
        bool digits{}; // only 0-9 ($C(a5) set)
    };
    Flow edit_field(LineEditor&); // $12096 on a record: until Return or a click outside its box
    void draw_editor(const LineEditor&, bool cursor = true);
    bool edit_key(LineEditor&, int key, int end_x); // false: Return
    void edit_click(LineEditor&, int x);
    Flow edit_name();           // $12096 on the name record $E46

    // The data disk ($126C0): a window listing the files of one kind ten to
    // a page, a name to type or pick, the action and Change Disk.
    // `action(name, replace)` returns 0, an error ($80 + n is its message),
    // or -1 to have the requester ask before replacing the file.
    Flow requester(FileKind, std::uint32_t title, std::function<int(const std::string&, bool)> action);
    Flow disk_error(int code);  // $12CFE
    Flow replace_file(bool& yes); // $12D6C
    Flow save_load();           // $A6B6
    Flow high_scores();         // $A81E
    void update_scores();       // $AA8C: the player's score into the table
    void sort_scores();         // $AA24
    std::vector<std::uint8_t> position() const; // what Save Game keeps ($110E-$11D3)
    bool load_position(const std::vector<std::uint8_t>&);
    Flow main_menu();           // $8D84
    Flow play_game();           // $8EAE
    Flow standard_game();       // $91B4
    Flow tutorial_game();       // $8F20
    Flow custom_game();         // $8FE4
    Flow choose_tower(int game_type, int& choice); // $A2DA + $A542 + menu $38EC
    Flow information(const data::Tower&, int letter, bool& play); // $A396
    Flow play_tower(const data::Tower&, int game_type, int& reason, bool frozen = false); // $969A
    Flow pause_menu(Play&, int& leave);  // $9704: Setup, View, Info, Exit
    Flow setup_page();          // $9AC4
    Flow view_page();           // $9C68
    Flow info_page(const data::Tower&); // $9FB6
    Flow exit_page(int game_type, int& leave); // $A060
    Flow confirm(std::uint32_t title, bool& yes); // $110D2
    Flow won_screen(int letter, int tries);       // $92EC
    Flow end_screen(int game_type, int reason);   // $97AA
    Flow demo_sequence();       // $15780
    Flow interlude_ok();        // $948C: OK clicked while the demonstration runs

    // --- the tower designer (front/designer.cpp)
    Flow designer();            // $E386
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
    Flow choose_slot(int title_x0, int y0, int y1, std::uint32_t title, std::uint32_t areas, std::uint32_t question, int& slot);
    Flow tower_unlocked(const std::array<std::uint8_t, data::tower_record_size>&, int letter, bool& allowed); // $EBD2
    Flow designer_file(bool& exit);
    Flow designer_options();
    Flow designer_toolbox();
    Flow designer_extras();
    Flow effects_page();        // $ED56
    Flow sound_page();          // $EE0C
    Flow moon_page();           // $EEB2
    Flow behaviour_page();      // $F008
    Flow finishing_page();      // $F0B0
    Flow text_page(int y0, std::uint32_t title, LineEditor&); // Name Tower, Tower's Author ($F27E, $F2AE)
    Flow test_design();         // Play/View Tower ($F2DE)
    Flow colours_page();        // $F38E
    Flow scroll_page(bool rotate, bool& done); // $F978 / $FA9E
    Flow level_page(bool swap, bool& done); // $FBB0 / $FC76
    Flow designer_setup();      // $1027E
    Flow statistics_page();     // $10384
    Flow support_page();        // $104AE
    Flow path_page();           // $10614
    Flow mouse_page(bool style);// $1082E / $1087C
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
    Play* active_play_{};     // the tower or demonstration being played, if any
    bool play_paused_{};      // its pause menu is open
    int margin_{};
    mutable view::Canvas wide_{320, 200}; // the wide picture as the pause began
    mutable bool paused_wide_{};
    view::Canvas wide_picture(int margin, double alpha) const;
    view::Canvas paused_screen_{320, 200}; // the screen as the pause began: what the menus change shows over the wide view
    DataDisk disk_;
    Screen screen_;
    FlowRunner runner_;
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
    LineEditor file_name_{0xAC, 0x6E, 8, 0xFE, 0xAC, 0x6C, 0xFF, 0x77, {}, 0}; // $F7E
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
    // Quick saves: the points the flow is taken up from, the last one's
    // state and the inputs since.
    enum class Mark { start, menu, designer, tower_frame };
    void mark(Mark);
    template <class A> void persist(A&);  // everything but a tower being played
    Mark mark_{Mark::start};
    std::vector<std::uint8_t> checkpoint_;
    std::vector<std::pair<std::uint64_t, int>> journal_; // (input, vertical blanks)
    bool in_designer_{};
    int playing_type_{};                // the game type of the tower being played
    // On a load, what the flow goes back into (the main menu's loop, the
    // designer or a game's tower with its state).
    bool resume_menu_{}, resume_designer_{};
    std::optional<int> resume_tower_;
    std::vector<std::uint8_t> resume_play_;
    long shown_bonus_[3]{}; // $1222-$1226 while counting
    int survivors_{};      // spiders left at the end of the last tower
};

} // namespace babel::front
