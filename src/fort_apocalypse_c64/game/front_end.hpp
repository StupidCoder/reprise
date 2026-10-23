#pragma once
#include "c64/text_frame.hpp"
#include "fort_apocalypse_c64/data/assets.hpp"
#include "fort_apocalypse_c64/game/controls.hpp"
#include "fort_apocalypse_c64/game/hud.hpp"
#include <cstdint>
#include <optional>

// The game's front end: the title screen and the options screen, from the
// moment the game has set itself up until a game (or the attract game)
// starts.
//
// Recovered from: title interrupt $8AA2, title drawing $89C7-$8A9D, game
// start $8B97, main loop $8BB1-$8C05, key handling $92E2-$9350, options
// drawing $9351-$9439, HUD numbers $A92F/$A9D3, the clear at $9AB3. See
// .analysis/fort_apocalypse_c64/front_end.md.
namespace fort_apocalypse::game {

// The three options the options screen sets. Each is one of three values,
// shown with the texts in the game file.
struct Settings {
    int gravity{0};     // $F4: WEAK, NORMAL, STRONG
    int pilot_skill{0}; // $F6: NOVICE, PRO, EXPERT
    int robo_pilots{2}; // $F8: THREE, FIVE, SEVEN

    int& value(int setting) {
        return setting == 0 ? gravity : setting == 1 ? pilot_skill : robo_pilots;
    }
    int value(int setting) const {
        return setting == 0 ? gravity : setting == 1 ? pilot_skill : robo_pilots;
    }
};

class FrontEnd {
public:
    enum class Outcome {
        none,
        new_game,    // fire or F7: the player starts a game
        attract_game // the title waited long enough: the game plays itself
    };

    explicit FrontEnd(const data::Assets& assets);

    // One frame: the work the original does after the picture (its
    // interrupt at raster line $F9 and the main loop pass that follows).
    Outcome tick(const Controls&);

    // The picture of the current frame.
    void draw(c64::TextFrame&) const;

    const Settings& settings() const { return settings_; }
    bool on_title() const { return screen_ == Screen::title; }
    bool paused() const { return paused_; }

private:
    enum class Screen { title, options };

    Outcome title_tick(const Controls&);
    Outcome options_tick(const Controls&);
    void read_space(const Controls&);
    void enter_title();
    void enter_options();
    void use_key(FunctionKey);

    void draw_title(c64::TextFrame&) const;
    void draw_options(c64::TextFrame&) const;

    const data::Assets& assets_;
    Screen screen_{Screen::title};
    std::uint8_t frame_{}; // $15, counted by every interrupt

    // Title.
    std::uint8_t title_timer_{}; // $29, +1 every 8 frames
    // The title redraw after the options keeps interrupts off past raster
    // $F9, so the next frame's interrupt is missed.
    bool miss_interrupt_{};
    // The two shared colours trade places every 4 frames on the title; the
    // options screen keeps them as they were when it opened.
    bool colours_swapped_{};

    // Options.
    int selected_{};              // $FA
    std::uint8_t idle_{};         // $42, counted down while no key changes
    // $5A: the key the main loop last acted on; empty until the first one.
    std::optional<FunctionKey> last_key_;
    // The HUD interrupt has run since the options opened: until then the
    // counters are not drawn and the title's scroll ($D011 = $1B) applies.
    bool hud_interrupt_ran_{};
    bool options_drawn_{};       // the main loop has drawn the option texts
    bool paused_{};              // the space bar paused the game (mode 8)

    // Keyboard column with the space bar ($19, $FF): an event when the
    // column changes to "space alone", used up by the pause.
    bool space_down_{}, space_event_{};

    Settings settings_;
    Counters counters_;
};

} // namespace fort_apocalypse::game
