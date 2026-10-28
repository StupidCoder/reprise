#pragma once
#include "app/game_module.hpp"
#include "platform/image.hpp"
#include "wings_of_fury_amiga/data/assets.hpp"
#include "wings_of_fury_amiga/game/scenery.hpp"
#include "wings_of_fury_amiga/game/ticker.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// The screens around the missions, stepped once per vertical blank:
//   the opening ($108022): the story scrolling up ($107E80), then the
//     Broderbund, title and credit pictures, each faded in and out;
//   rank selection ($108262);
//   after the game: the name for the hall of fame ($109472) when the score
//     beats the tenth, and the table ($109856, `ACES`).
// Fire skips what the original lets it skip. (The keyboard is the host's,
// read through Joyport's C64 key matrix.)
namespace wings::game {

// $106FF6: one colour `step` sixteenths (0..15) of the way from `from` to
// `to`, gun by gun; each gun's share is masked to its bits, so a falling gun
// borrows from the one above, as in the original.
std::uint16_t fade_colour(int step, std::uint16_t from, std::uint16_t to);

struct ScoreEntry {
    long score{};
    int rank{};
    std::string name;
};

class FrontEnd {
public:
    enum class Screen : std::uint8_t { none, story, pictures, rank_select, name_entry, hall_of_fame, saved_games };
    // What a step leads to (the saved-game screen: back where it was opened,
    // a game loaded (loaded_game()), or Exit Game).
    enum class Result : std::uint8_t { running, play, done, back, loaded, exit_game };

    FrontEnd(const data::Assets&, const TickerFont&, std::filesystem::path score_file);

    void start_opening();
    void start_rank_select();
    // After the game: the name (if the score makes the table), then the table.
    void start_game_over(long score, int rank);
    // $108B96: the saved games (`wof.<name>` beside the high scores): Save
    // Game with the game's text, or Load Game.
    void start_saved_games(bool save, std::string game_text = {});
    const std::string& loaded_game() const { return loaded_game_; }
    // Back to the game (no front-end screen).
    void close() { screen_ = Screen::none; }
    // The music the screens ask for ($1023DC: a song of `wofsongs`;
    // $102470, -2: none), taken once; -1 when nothing new.
    int take_song_request() {
        const int r = song_request_;
        song_request_ = -1;
        return r;
    }

    Result step(const app::GameInput&);
    Screen screen() const { return screen_; }
    int chosen_rank() const { return selection_; }
    // Whether the eighth entry (RETURN FROM R&R, a saved game) was chosen.
    bool load_chosen() const { return load_chosen_; }
    // The picture, `height` lines tall (640 wide; lowres screens doubled).
    platform::Image picture(int height) const;

private:
    const data::Assets* assets_;   // (pointers: the front end is copied by quick save)
    const TickerFont* font_;
    std::filesystem::path score_file_;
    Screen screen_ = Screen::none;
    int frame_ = 0;          // frames in the current phase
    bool fire_was_ = false, keys_were_ = false;
    int song_request_ = -1;

    // ---- the story ----
    struct StoryLine {
        std::string text;
        bool justified{};
    };
    std::vector<StoryLine> story_;
    std::vector<std::vector<bool>> story_bits_; // each line's pixels, 640 x 12
    std::vector<int> story_y_;  // where each drawn line starts, in rows since the start
    int scroll_ = 0;         // rows scrolled since the start
    int drawn_ = 0;          // lines drawn (-$6)
    int row_counter_ = 0;    // -$8: rows since the ring buffer's wrap
    int ring_ = 0xD2;        // d5: rows to the ring buffer's wrap
    int left_ = 0;           // d4: steps left after the last line
    int fade_lines_ = 16;    // the copper's fade lines at the top and bottom
    bool ending_ = false;

    // ---- pictures and fades ----
    struct Shown {
        const amiga::IlbmPicture* picture{};
        std::vector<std::uint16_t> colours;  // what the screen shows now
    } shown_;
    std::vector<std::uint16_t> fade_from_, fade_to_;
    int fade_step_ = -1;      // 0..15 while fading
    int phase_ = 0;
    int wait_ = 0;
    void fade_to(const std::vector<std::uint16_t>& target);
    bool fading();            // steps a fade; false when none runs
    const amiga::IlbmPicture* picture_named(const std::string&) const;

    // ---- rank selection ----
    int selection_ = 0;       // -$4398
    int idle_ = 0;            // frames without a choice (1800: the game starts)
    int repeat_ = 0;          // $108228: frames the stick has been held
    bool load_chosen_ = false;
    IndexedCanvas rank_canvas_{320, 200};
    void draw_rank_screen();

    // ---- hall of fame ----
    std::vector<ScoreEntry> scores_;
    long new_score_ = 0;
    int new_rank_ = 0;
    std::string name_;
    int cursor_ = 0;
    std::array<bool, 64> keys_{};
    bool left_was_ = false, right_was_ = false;
    void read_scores();
    void write_scores() const;
    void enter_score();
    bool edit_name(const app::GameInput&);  // true when the entry ends
    // $106086 itself: edits `text` (at most `length`); 0 while editing, else
    // how it ended: -1 up, 1 down, 2 Return.
    int edit_line(const app::GameInput&, std::string& text, int& cursor, int length);

    // ---- saved games ($108B96) ----
    bool saving_ = false;
    std::string game_text_, loaded_game_;
    std::array<std::string, 6> slots_, slots_found_; // -$3380, -$32D2 (as found)
    std::array<bool, 6> edited_{};  // drawn by the line editor (a pixel further left)
    int found_ = 0;          // saved games found ($108A06)
    int slot_ = 0;           // -$44: 0..5 the slots, 6 Exit Game, 7 Cancel
    int slot_cursor_ = 0;
    bool up_was_ = false, down_was_ = false, return_was_ = false;
    std::string busy_;       // "Saving game..." / "Loading game..." while it fades
    Result after_fade_ = Result::back;
    std::filesystem::path saved_game_file(const std::string& name) const;
};

} // namespace wings::game
