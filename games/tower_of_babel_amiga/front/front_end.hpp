#pragma once
#include "tower_of_babel_amiga/data/assets.hpp"
#include "tower_of_babel_amiga/front/flow.hpp"
#include "tower_of_babel_amiga/front/play.hpp"
#include "tower_of_babel_amiga/front/screen.hpp"
#include <array>
#include <memory>
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
    explicit FrontEnd(const data::Assets&);

    // What the player does during one vertical blank (PAL, 50 a second).
    struct Input {
        bool pointer{};         // the mouse is over the picture
        int x{}, y{};           // in the screen's pixels
        bool left{}, right{};   // buttons held
        int key{};              // a key typed this frame (the original's codes: ASCII, 8 delete, 13 return, 27 escape), 0 none
    };
    void vblank(const Input&);

    const Screen& screen() const { return screen_; }
    // A sampled piece to start this frame (0 the title music, 1 the jingle), or none.
    std::optional<int> take_piece() { return std::exchange(piece_, std::nullopt); }
    bool piece_playing() const { return piece_frames_ > 0; }
    // Effect records started since the last call, in order.
    std::vector<std::uint32_t> take_sounds() { return std::exchange(sounds_, {}); }

private:
    // --- waiting
    auto next_frame() { return runner_.next_frame(); }
    Flow wait_frames(int n);
    Flow wait_release();        // $11318: until no button is held
    Flow select(std::uint32_t menu, int& choice); // $112EA
    Flow game_frame_wait();     // $87EE: a game frame is 14 ticks of the CIA timer
    Flow fade(const std::array<data::Colour, 48>& target, int first = 0, int count = 48); // $1252C / $1255C
    Flow fade_out();            // $124B0: to black
    Flow fade_in_program(std::uint32_t palette); // a 48-colour palette in the program

    // --- screens
    Flow run();
    Flow title();               // $7EF6
    Flow credits();             // $15A2E
    Flow name_entry();          // $8BD6
    Flow edit_name();           // $12096
    Flow main_menu();           // $8D84
    Flow play_game();           // $8EAE
    Flow standard_game();       // $91B4
    Flow tutorial_game();       // $8F20
    Flow custom_game();         // $8FE4
    Flow choose_tower(int game_type, int& choice); // $A2DA + $A542 + menu $38EC
    Flow information(const data::Tower&, int letter, bool& play); // $A396
    Flow play_tower(const data::Tower&, int game_type, int& reason); // $969A
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
    Screen screen_;
    FlowRunner runner_;
    Input in_{}, previous_{};
    bool clicked_{};        // the left button went down since the last game frame
    int clicked_x_{}, clicked_y_{};
    double ticks_{};        // CIA-A timer B ticks since the last game frame
    std::array<data::Colour, 48> current_{};
    std::optional<int> piece_;
    int piece_frames_{};
    std::vector<std::uint32_t> sounds_;
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
    std::unique_ptr<Play> interlude_;
    long shown_bonus_[3]{}; // $1222-$1226 while counting
    int survivors_{};      // spiders left at the end of the last tower
};

} // namespace babel::front
