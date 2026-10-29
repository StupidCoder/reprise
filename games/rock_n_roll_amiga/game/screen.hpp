#pragma once
#include "rock_n_roll_amiga/data/assets.hpp"
#include "rock_n_roll_amiga/game/ball.hpp"
#include "rock_n_roll_amiga/game/font.hpp"
#include "rock_n_roll_amiga/game/overview_map.hpp"
#include "rock_n_roll_amiga/game/level_map.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

// The play screen as the original's copper list composes it ($73040):
// 304 pixels wide, the name bar (16 lines, $2CEC0), the play view (112
// lines, the view buffer with fine scroll) and the panel (64 lines,
// $29CC0), with the side pillars as attached sprites: behind the name bar
// where it is not colour 0, in front of the view. Pictures are colour
// indices 0-31 in the line's palette (palette()).
namespace rocknroll::game {

// What the panel and the name bar show in numbers ($6F090's elements).
struct PanelNumbers {
    int level{1};                        // $719AE + 1, in the name bar ($6F308)
    int seconds{}, hundredths{};         // the time as drawn ($719CA/$719CE)
    data::ScoreTable::Time record{};     // the level's record ($6F1A6)
    int high_score{};                    // $719BE ($6EDCE)
    bool score_shown{};                  // drawn once a find changed it ($6EDEE)
    int score{};                         // $719BA
    int money{};                         // $719C2 ($6F2DA)
    std::array<int, 4> keys{};           // $719EC, $719F8, $719F0, $719F4 (as Pocket::keys)
    int speed_bought{};                  // high byte of $71A6A ($6EE58)
    int shown_energy{};                  // $6F458 ($6F466 → $6F548)
    bool kind_drawn{}, kind_running{};   // the extra bar, drawn since a ball was bought ($6F4F4)
    int kind_updates{};                  // $6F45C
    int balls_left{};                    // $699F8 ($69A10)
    int item{2};                         // $6B4F4
    int item_count{};                    // bombs, repair sets or rescues for items 6, 8, 10
};

// The eye's map in the view ($6B77A/$6B6FA), drawn over the cleared view
// once its text has shown.
struct OverviewView {
    OverviewColours colours;
    int cells_drawn{};  // row by row, as far as the drawing has got
    bool marker{};      // the ball's place ($6B6FA, after the drawing)
};

class Screen {
public:
    static constexpr int width = 304;
    static constexpr int bar_lines = 16, view_lines = 112, panel_lines = 64;
    static constexpr int height = bar_lines + view_lines + panel_lines;

    Screen(const data::Assets&, const data::World&);

    // The whole screen for the ball's state. `view_text` replaces the view
    // with black and the texts, as the prompts and the results do ($6B8E6,
    // $6AE16).
    void draw(const LevelMap&, const Ball&, const PanelNumbers&, std::string_view level_name,
              std::string_view bar_text, const std::vector<ScreenText>& view_text = {},
              const OverviewView* overview = nullptr);

    const std::vector<std::uint8_t>& pixels() const { return pixels_; }
    // The colours of a picture line: the copper list ($73040) shows the
    // view with the world's palette and the name bar and panel with the
    // program's own ($73344, copied at $6854E).
    const std::vector<amiga::Rgb4>& palette(int line) const {
        return line >= bar_lines && line < bar_lines + view_lines ? world_->tiles.palette : frame_palette_;
    }

private:
    std::uint8_t& at(int x, int y) { return pixels_[std::size_t(y) * width + std::size_t(x)]; }
    void draw_bar(std::string_view level_name, std::string_view bar_text);
    void draw_view(const LevelMap&, const Ball&);
    void draw_panel(const PanelNumbers&);
    // $6F33C: `count` digit values (glyph '0' + value) into colour bit 2,
    // seven lines each, 8 pixels apart; leading zeros are blanks (glyph
    // space), the last digit always shows.
    void draw_digits(int x, int y, std::span<const int> values);
    // One font glyph (index from '$27') into colour bit 2, `lines` high.
    void glyph(int x, int y, int index, int lines);
    Canvas canvas() { return {pixels_.data(), width, height}; }
    void draw_pillars();
    void draw_overview(const LevelMap&, const Ball&, const OverviewView&);

    const data::World* world_;
    std::vector<std::uint8_t> pixels_;
    std::vector<amiga::Rgb4> frame_palette_; // the bar's and panel's 32 colours
    std::vector<std::uint8_t> sprite_even_, sprite_odd_; // 143 lines x (word A, word B)
};

} // namespace rocknroll::game
