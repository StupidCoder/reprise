#pragma once
#include "mindwalker_amiga/data/game_files.hpp"
#include "mindwalker_amiga/screen/canvas.hpp"
#include "mindwalker_amiga/screen/gadget_bar.hpp"
#include "mindwalker_amiga/world/random.hpp"
#include <array>
#include <functional>
#include <string>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

// The title stage (program T): the title picture, the world menu and the set-up
// of a new game. Rebuilt from T's main routine ($600040) and the routines it
// calls; every table, text, palette and picture is read from the programs and
// MindB.
namespace mindwalker::title {

// What a new game starts with (T $6002F2-$6003E8): the fields the stages hand
// on to each other.
struct NewGame {
    std::uint32_t seed{};      // the world's seed (+$10)
    int stage{};               // starting stage, 0-35 in steps of 7, from the slider (+$5C)
    std::uint32_t progress{};  // T's table $600BA6 for the stage (+$62)
    std::uint32_t score{};     // +$66
    std::uint32_t field_6a{};  // +$6A (use not read yet)
    int lives{3};              // +$6E (hypothesis: lives)
    std::uint32_t bonus{42000};// self-development bonus (+$308, +$30C)
};

// The Hall of Fame (MindHighs): 32 entries of a score and three initials.
struct HallOfFame {
    struct Entry {
        std::uint32_t score{};
        std::string initials;
    };
    std::array<Entry, 32> entries{};
    static HallOfFame from_file(std::span<const std::uint8_t> bytes); // 32 x (long score, 4 chars)
    std::vector<std::uint8_t> to_file() const;
};

// What T is handed and what it may do with the game in memory: the score
// and where the game was (shared +$66, +$2BA: 0 none, 2 the landscape, 3 the
// tunnel, 4 the arena, 5 the psyche screen), the Hall of Fame, and the
// saved worlds A-H (the module keeps them).
struct Session {
    std::uint32_t score{};
    int where{};
    HallOfFame fame;
    std::function<bool(int world)> world_saved;            // MindWorldA-H exists
    std::function<bool(int world)> save_world;             // $6026AE: the game in memory saved
    std::function<bool(int world, bool same_world)> restart; // $602248: Continue Game / New Game, Same World
    std::function<void(const HallOfFame&)> fame_changed;   // h11: MindHighs written
};

// The pointer and left button as the stage reads them each frame.
struct Pointer {
    int x{}, y{};
    bool button{};
};

class TitleStage {
public:
    enum class Phase { title, pause, menu, initials, fame, command, loading, quit };

    // `clock_seed` stands for the value T mixes from CurrentTime ($600142);
    // `first` is true for the first title of a session (shared +$2BA = 0).
    TitleStage(const data::GameFiles&, std::span<const std::uint8_t> title_picture, std::uint32_t clock_seed,
               bool first = true, Session session = {});

    // One frame of the original (one vertical blank: every loop of the stage
    // waits for one).
    void frame(const Pointer&);

    // The progress slider's HorizPot (Intuition moves the knob; T reads the
    // pot when a game starts). For harness runs that poke it.
    void set_slider(std::uint16_t pot);

    Phase phase() const { return phase_; }
    bool paused() const { return paused_; }
    const std::optional<NewGame>& new_game() const { return new_game_; }
    platform::Image picture() const;
    // The screen as shown: the canvas, or while paused only the window's
    // bar over pen 0.
    screen::Canvas screen() const;
    // For replays: a gadget message as Intuition would post it.
    void gadget(int id);

    // State as the original holds it, for the harness's comparisons.
    std::uint32_t mixed_seed() const { return seed_.state; }
    std::uint32_t menu_seed() const { return new_game_seed_.state; }
    const world::Mixer& pillar_mixer() const { return pillar_random_; }
    const std::array<int, 8>& pillar_offsets() const { return pillar_offset_; }
    int twinkle_colour() const { return twinkle_colour_; }
    const std::vector<screen::Rgb4>& colours() const { return colours_; }
    // The colours when the title last read the button (after its twinkle).
    const std::vector<screen::Rgb4>& colours_at_read() const { return read_colours_; }
    const screen::Canvas& canvas() const { return canvas_; }
    // S's one-plane screen, where T draws the loading picture (shared +8).
    const screen::Canvas& loading_canvas() const { return loading_; }
    // The loading picture for a progress (+$5C).
    screen::Canvas loading_picture(int stage) const;
    int frames_in_phase() const { return phase_frames_; }
    // A game restarted from the command screen (the module set it up).
    bool restarted() const { return restarted_; }
    // For replays: whether the next frame reads the button (the original's
    // waits and the Hall of Fame read none).
    bool reading() const {
        if (paused_ || shrink_frames_ > 0 || resume_frames_ > 0) return false;
        return phase_ == Phase::title || phase_ == Phase::menu || (phase_ == Phase::initials && wait_ == 0) ||
               (phase_ == Phase::command && draw_items_ >= 15);
    }

private:
    void draw_title();
    void twinkle();
    void enter_menu();
    void draw_menu_background();
    void draw_menu();
    void draw_pillar(int pillar);
    void animate_pillar(int pillar);
    void start_game();
    void draw_loading_screen();
    void drag_slider(const Pointer&);
    void gadget_clicked(int id);              // h5 / $600CB4
    int gadget_at_pointer(const Pointer&) const;
    void back_to_menu();                      // $6004F0
    // The pause box ($600DF4): the window shrunk to its bar ($600E20), back
    // to its size and the screen redrawn ($600EE6).
    bool pause_clicked(const Pointer&, bool press);
    void pause();
    void resume();
    void redraw_after_pause();
    // The Hall of Fame (h17, $6035D4, $603714-$603AFC).
    void check_high_score();                  // h17
    void sort_fame();                         // $6035D4
    void draw_initials_screen();              // $604526 / $6042B0
    void draw_initials();                     // $604246
    void initials_click(int x, int y);        // $60413A
    void show_fame();                         // $603714
    void animate_fame();                      // $603814
    int print_number(std::uint32_t value, int x, int y, bool pad); // $603AFC
    void print_itext(std::uint32_t at, int x, int y, const std::string* text = nullptr);
    // The command screen (h8, $6016B6-$6027DE).
    void open_command();                      // $601AB6
    void draw_command_background();           // $601AE2
    void draw_item(int item);                 // $6019A2
    void select_command(int command);         // $6017E8
    void message(int item);                   // $602754
    void command_click(int x, int y);         // $6016B6
    void world_clicked(int world);            // $601868

    const amiga::LoadedImage& t_;
    std::span<const std::uint8_t> picture_file_;
    std::uint32_t clock_seed_{};
    bool first_{};
    screen::Canvas canvas_;
    screen::Canvas loading_;
    screen::GadgetBar bar_;
    std::vector<screen::Rgb4> colours_; // the HAM screen's 16 base colours
    std::vector<screen::Rgb4> title_colours_, twinkle_work_, read_colours_;
    Phase phase_{Phase::title};
    int phase_frames_{};

    world::ShiftRegister seed_;          // $600AF8
    world::ShiftRegister new_game_seed_; // $600BD6 (preset slot 4)
    world::Mixer pillar_random_;         // $600C18
    std::array<int, 8> pillar_offset_{}; // $600BF8
    world::ShiftRegister twinkle_pick_, twinkle_colour_pick_; // $603494, $603498
    int twinkle_colour_{-1}, twinkle_last_{}, twinkle_level_{}, twinkle_delay_{};
    bool button_was_down_{};             // $600BDA
    bool slider_held_{};                 // Intuition's drag of the slider's knob
    int slider_grab_{};                  // pointer x - knob x when grabbed
    int choice_{};                       // $600BDE
    std::optional<NewGame> new_game_;
    Session session_;
    // The Hall of Fame as sorted ($603F0C), each entry's rank ($60404C).
    std::array<HallOfFame::Entry, 32> ranked_{};
    std::array<int, 32> rank_{};
    int new_entry_{}, typed_{}, wait_{}, fame_count_{}, fame_step_{};
    std::vector<std::uint8_t> small_image_; // the Hall of Fame's creatures' data (S's +$2D0 frames)
    std::string initials_ = "   ";              // $6048FA
    bool forget_{};                              // $604910
    // The command screen's items (texts change: the '>' and the world's letter).
    std::array<std::string, 28> item_text_;
    std::array<int, 28> item_back_{};
    int command_{1}, confirm_world_{}, draw_items_{};
    bool confirming_{}, restarted_{};
    // $601626: paused; the frames of the shrink (15) and of the way back (30).
    bool paused_{};
    int shrink_frames_{}, resume_frames_{};
};

} // namespace mindwalker::title
