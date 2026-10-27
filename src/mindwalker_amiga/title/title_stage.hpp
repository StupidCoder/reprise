#pragma once
#include "mindwalker_amiga/data/game_files.hpp"
#include "mindwalker_amiga/screen/canvas.hpp"
#include "mindwalker_amiga/screen/gadget_bar.hpp"
#include "mindwalker_amiga/world/random.hpp"
#include <array>
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

// The pointer and left button as the stage reads them each frame.
struct Pointer {
    int x{}, y{};
    bool button{};
};

class TitleStage {
public:
    enum class Phase { title, pause, menu, loading, quit };

    // `clock_seed` stands for the value T mixes from CurrentTime ($600142);
    // `first` is true for the first title of a session (shared +$2BA = 0).
    TitleStage(const data::GameFiles&, std::span<const std::uint8_t> title_picture, std::uint32_t clock_seed,
               bool first = true);

    // One frame of the original (one vertical blank: every loop of the stage
    // waits for one).
    void frame(const Pointer&);

    // The progress slider's HorizPot (Intuition moves the knob; T reads the
    // pot when a game starts). For harness runs that poke it.
    void set_slider(std::uint16_t pot);

    Phase phase() const { return phase_; }
    const std::optional<NewGame>& new_game() const { return new_game_; }
    platform::Image picture() const;

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
};

} // namespace mindwalker::title
