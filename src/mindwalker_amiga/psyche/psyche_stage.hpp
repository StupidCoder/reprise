#pragma once
#include "mindwalker_amiga/data/assets.hpp"
#include "mindwalker_amiga/data/game_files.hpp"
#include "mindwalker_amiga/psyche/psyche_sound.hpp"
#include "mindwalker_amiga/screen/canvas.hpp"
#include "mindwalker_amiga/screen/gadget_bar.hpp"
#include "mindwalker_amiga/world/random.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

// The psyche screen (program P; main loop $500040), after an arena won. A
// kaleidoscope (four bouncing points, their four-sided shape mirrored four
// ways, drawn in turning colours, now and then one of P's 32 words written
// over it) fills a hidden 140 x 120 picture, cut into 42 tiles of 20 x 20
// on a 7 x 6 grid. As many tiles as the progress allows start in their
// places; seven wait in a row of slots, and the player carries them home
// with the mouse (each tile shows its own piece of the moving picture, the
// tiles redrawn four a frame). Under each slot a colour of a turning bar
// and, below, a button in a V: a click sends a line climbing to the bar,
// and if the slot's colour is red when it gets there the tile flies home
// by itself, for 1,000 of the self-development bonus. All home: a portrait
// follows and then the landscape. The last picture of the game (progress
// 42) instead stays whole on screen with the bonuses for lives and time,
// the finale playing, until the window is closed.
namespace mindwalker::psyche {

struct PsycheInput {
    int x{}, y{};    // the pointer on the screen
    bool button{};   // the left mouse button
};
// P reads the button ($500418) before each tile it draws and once a pass.
using PsycheReader = std::function<PsycheInput()>;

class PsycheStage {
public:
    struct Start {
        std::uint32_t world_seed{}; // shared +$10
        int stage{};                // +$5C (the arena's 7 added)
        int lives{3};               // +$6E
        std::uint32_t score{};      // +$66
        std::uint32_t bonus{42000}; // +$308: the self-development bonus
        std::uint32_t time{};       // +$62: playing time (seconds)
    };
    enum class Outcome { playing, done, quit };

    PsycheStage(const data::GameFiles&, const data::Assets&, const Start&);

    // One display frame; `gadget` a gadget clicked this frame (-2 the close
    // gadget; the P box, id $10, pauses and resumes).
    void frame(const PsycheReader&, int gadget = -1);
    platform::Image picture(bool sprites = true) const;
    void render_audio(std::span<float> stereo, int rate) { sound_.render(stereo, rate); }

    Outcome outcome() const { return outcome_; }
    bool over() const { return outcome_ != Outcome::playing && sequence_.empty(); }
    std::uint32_t score() const { return score_; }
    std::uint32_t bonus() const { return bonus_; }
    bool whole() const { return whole_; }  // $5021A0: the last picture done
    int gadget_at(int x, int y) const;
    // h32: the portrait that follows (MindF1-6 by the progress) and its colour.
    int portrait() const;
    std::uint16_t portrait_colour() const;

    // For replays: the state the recorder logs, by name.
    void describe(const std::function<void(const std::string&, long)>& report) const;

private:
    using Step = std::function<void()>;
    void layout(std::uint32_t world_seed); // h11 / $5022EA: the shuffle and the first places
    void draw_panel();                     // $5010AC
    void draw_slot(int slot, int pen);     // $501358
    void draw_cell(int column, int row, int pen); // $5013B0
    void draw_colour(int slot);            // $5012FC
    void draw_images();                    // $501042
    void draw_scores();                    // $506718
    void draw_bonus();                     // $506758
    void draw_number(std::uint32_t value, int x, int y); // $5067C0
    void draw_whole();                     // $506694
    void kaleidoscope();                   // $501424
    void write_word();                     // $500E3C
    void blit_tile(int tile);              // a ClipBlit of its piece
    void button(const PsycheReader&);      // $500418 and $505852
    void click(int x, int y);              // $505852
    void after_tile();                     // $505970
    void hints(int from);                  // $5059B2 (from a slot on: after a flight)
    void after_hints();                    // the rest of $505970
    void hint_home(int slot);              // $505E98 with its flight ($506332)
    void place_cursor();                   // $50627E
    void end_of_pass();                    // the rest of $50198E
    void rotate_bar();                     // $505C5E
    void become_whole();                   // h41
    void pause();
    void resume();
    void image(const data::Bitmap&, int plane_pick, int x, int y);
    int hint_height() const;

    const amiga::LoadedImage& p_;
    const data::Assets& assets_;
    int stage_{}, lives_{};
    std::uint32_t score_{}, bonus_{}, time_{};
    std::array<std::uint16_t, 32> colours_{};
    screen::Canvas screen_, back_;
    screen::GadgetBar bar_;
    data::Bitmap button_image_, big_image_;
    std::array<data::Bitmap, 2> cursor_;
    PsycheSound sound_;
    // The tiles: where each is (0xFF held or none; bit 7: in the grid, column
    // bits 0-2, row bits 3-5; else a slot), the slots, the grid [column][row].
    std::array<std::uint8_t, 42> place_{};           // h13 $502480
    std::array<std::uint8_t, 42> order_{};           // $5024AA: the shuffle
    std::array<std::uint8_t, 7> slot_{};             // $5024D4
    std::array<std::array<std::uint8_t, 8>, 8> grid_{}; // $5024DC
    int target_{};                                   // $50251C: tiles home to finish
    int bar_pace_{};                                 // $506118
    int held_{0xFF};                                 // h37
    std::array<int, 7> hint_{};                      // $5060F8: each slot's line height (0: none)
    int humming_{-1};                                // $502EA6: the slot whose line sounds
    int blits_{}, bar_wait_{};                       // $5021A2, $506114
    world::ShiftRegister shuffle_random_, shuffle_extra_; // h12 $5023FC, $502400
    world::ShiftRegister wobble_, steer_, flip_;     // h9 $501BE8, $501BEC, $501BF0
    std::array<int, 4> x_{}, y_{}, vx_{}, vy_{};     // h10 $502158, $502168, $502178, $502188
    int pen_{1}, kaleido_count_{0xF};                // $502198, $50219C
    bool whole_{};                                   // $5021A0
    std::uint32_t lives_bonus_{}, time_bonus_{};     // $506AB0, $506AB4
    int cursor_x_{-0x50}, cursor_y_{};               // $5065FC / $506600
    int mouse_x_{}, mouse_y_{};
    bool button_down_{}, paused_{};
    int next_tile_{}, pass_home_{};                  // the pass's progress, tiles counted home
    bool pass_started_{}, waited_{};
    int whole_wait_{};                               // frames to the next timer tick (the last picture's passes)
    int top_{10};                                    // $500CF0
    Outcome outcome_{Outcome::playing};
    std::vector<Step> sequence_;
};

} // namespace mindwalker::psyche
