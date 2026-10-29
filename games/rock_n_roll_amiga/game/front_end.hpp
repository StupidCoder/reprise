#pragma once
#include "platform/image.hpp"
#include "rock_n_roll_amiga/data/assets.hpp"
#include "rock_n_roll_amiga/game/font.hpp"
#include <optional>
#include <string>
#include <vector>

// The title screen and what is reached from it ($68EA0): the title picture
// (world 8), the scroll down to the panel below it with the hall of fame
// and the levels' time records ($69854, $69144), and the name entry with
// its letter wheel ($693D8 → $695F0) and the names that do more ($6941C).
// Shown through the copper list $73278: 320 x 200 from the bank at $19000
// plus a scroll, the display shortened at the bottom as it scrolls.
namespace rocknroll::game {

// The player's input for one frame of the front end.
struct FrontEndInput {
    int mouse_x{}, mouse_y{}; // mouse counts moved this frame (right and down positive)
    bool left{}, right{};     // the buttons, held
    char key{};               // a key pressed this frame as the keyboard handler leaves it ($70BD8), or 0
};

// What the front end hands to the game.
struct GameStart {
    std::string name;   // as the records take it ($71996: the first 11 characters and a space)
    int level_index{};  // 0, or the level a code gave
    bool from_code{};   // started from a level code ($68454): the level ends the game's spare balls
    bool magic_map{};   // the name MAGIC MAP: every map piece ($6ADBC, the overview map is not rebuilt)
};

class FrontEnd {
public:
    static constexpr int width = 320, height = 200;

    FrontEnd(const data::Assets&, const data::ScoreTable&);

    // The title as $68EA0 sets it up. A finished game's hall-of-fame place
    // (0: none) makes the title scroll to the hall of fame at once, with
    // "CONGRATULATIONS PLACE nn" or "SORRY, NO ENTRY".
    void open(std::optional<int> hall_of_fame_place = std::nullopt);
    // One frame; the game's start once a name is entered.
    std::optional<GameStart> tick(const FrontEndInput&);
    platform::Image picture() const;

private:
    enum class State { title, release_left, scroll_down, records, scroll_up, name_entry, code_entry };

    void scroll_step(int direction);
    void draw_records();
    void step_records(bool left);
    // One pass of $695F0's loop (two frames); true when the name ended.
    bool name_pass(const FrontEndInput&, bool left_pressed_now);
    void begin_name(bool code);
    std::optional<GameStart> name_finished();
    Canvas canvas() { return {pixels_.data(), width, rows_}; }
    void print_name();

    const data::Assets* assets_;
    const data::ScoreTable* scores_;
    const data::World* world_; // world 8: the title's picture and palette
    int rows_{};
    std::vector<std::uint8_t> pixels_; // the bank's rows 0-335 as the title shows and prints into them
    State state_{State::title};
    int scroll_{};             // $69850 / 200: the first bank row shown
    bool pending_{};           // $68E90 bit 1: a finished game's place to show
    bool pending_text_{};      // $68E90 bit 0: its text still to print
    int pending_place_{};
    // Records ($69144): the table shown and its stepping counters.
    int table_{};              // $719AE while browsing: 32 = hall of fame
    bool redraw_{};            // $6903E
    int hold_count_{}, hold_period_{20}; // $69041, $69040
    // Name entry ($695F0).
    std::string name_;         // $68416: 13 characters, '(' blank
    int cursor_{};             // $68456 − $68416
    std::uint8_t letter_{'A'}; // $6840C
    bool waiting_release_{};   // the left button accepted, waiting for its release ($697BC)
    int pass_frame_{};         // the loop waits two frames a pass
    int pass_mouse_x_{};       // counts gathered over the pass
    bool code_{};              // entering a level code (the name RAINBOW ARTS)
    bool grey_{};              // colour 0 $555 (code entry) or $333 (after it)
    std::uint16_t grey_colour_{};
};

} // namespace rocknroll::game
