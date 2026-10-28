#pragma once
#include "c64/text_frame.hpp"
#include "platform/image.hpp"
#include <array>
#include <cstdint>
#include <utility>
#include <vector>

// Wizball's picture as the C64 shows it: the whole PAL frame with the
// vertical border opened (the game keeps it open at raster $F7, $8BBB), so
// sprites show from the top of the frame to the bottom. The game builds a
// Display each frame from its state: which settings apply from which raster
// line (the original changes them in its raster interrupts), the character
// rows, and each hardware sprite's settings over the frame (the original
// re-uses sprites further down the screen). The renderer follows the VIC-II
// where the game relies on it: character rows start on the "bad lines" of
// vertical scroll 3, a sprite starts on the line after its y matches and
// then runs for 21 lines whatever happens to its y register.
namespace wizball::game {

using Glyph = c64::Glyph;
using Charset = std::array<Glyph, 256>;
using SpriteShape = std::array<std::uint8_t, 63>;

// What applies to one raster line.
struct LineSettings {
    std::uint8_t border{}, background{}, shared1{}, shared2{};
    std::uint8_t sprite_shared1{}, sprite_shared2{}; // $D025, $D026
    bool multicolour{};
    bool columns38{};          // CSEL cleared: the window is 304 pixels wide
    std::uint8_t xscroll{};
    const Charset* charset{};  // null: no character graphics (blank)
    int screen{};              // which character matrix a row starting here fetches
    bool wide{};               // widescreen: the window runs on into the side columns

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) {
        ar(border, background, shared1, shared2, sprite_shared1, sprite_shared2, multicolour, columns38,
           xscroll, screen, wide);
    }
};

// One hardware sprite's registers.
struct SpriteSettings {
    bool enabled{};
    int x{}, y{};              // VIC coordinates (x 24 / y 51: top left of the window)
    SpriteShape shape{};
    std::uint8_t colour{};
    bool multicolour{}, expand_x{}, expand_y{}, behind{};

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) {
        ar(enabled, x, y, shape, colour, multicolour, expand_x, expand_y, behind);
    }
};

class Display {
public:
    static constexpr int first_line = 16, line_count = 284, width = 384;
    static constexpr int raster_lines = 312;
    static constexpr int text_top = 51; // first line of character row 0 (vertical scroll 3)

    struct Matrix {
        std::array<std::uint8_t, 1000> codes{};

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) { ar(codes); }
    };

    Display();

    // Settings from `line` to the end of the frame (later calls override).
    void set_lines_from(int line, const LineSettings& settings);
    LineSettings& line(int raster) { return lines_[std::size_t(raster)]; }
    const LineSettings& line(int raster) const { return lines_[std::size_t(raster)]; }

    std::vector<Matrix> screens{1};
    std::array<std::uint8_t, 1000> colours{}; // colour RAM (low nibble)
    // The play screen opens the top and bottom borders ($8BBB); the front
    // end's screens keep them: outside lines 51-250 only the border shows.
    bool vertical_border_open = true;

    // Widescreen (an enhancement): `extension` more character columns each
    // side; the picture grows by 16 pixels per column. Lines marked wide
    // show the side cells set here (columns -extension..-1 and 40..39+
    // extension); the others show their border there.
    int extension = 0;
    void set_side(int row, int column, std::uint8_t code, std::uint8_t colour);

    // Carnage mode (not the original game): a multicolour cell drawn from its
    // own pixel pairs instead of its character: per pair (4 x 8) 0-3 as the
    // character's pair values, or $10 + a colour for paint. Columns as for
    // the side cells.
    using CellArt = std::array<std::uint8_t, 32>;
    void set_cell_art(int row, int column, const CellArt& art);
    // Carnage mode's flying things: single pixels over everything, in VIC
    // coordinates (x, raster line), shown inside the window only.
    struct Dot {
        int x{}, y{};
        std::uint8_t colour{};

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) { ar(x, y, colour); }
    };
    std::vector<Dot> dots;

    // A sprite's registers from `line` on (calls must come in line order).
    void set_sprite(int index, int line, const SpriteSettings& settings);
    void clear_sprites();

    platform::Image render() const;

private:
    struct SpriteChange {
        int line;
        SpriteSettings settings;

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) { ar(line, settings); }
    };
    std::array<LineSettings, raster_lines> lines_{};
    std::vector<std::uint8_t> side_codes_, side_colours_; // 25 rows x 2 * extension
    std::vector<std::pair<int, CellArt>> cell_art_;       // (row * 128 + column + 64, art)
    int side_index(int row, int column) const;
    std::array<std::vector<SpriteChange>, 8> sprites_{};

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) {
        ar(screens, colours, vertical_border_open, extension, dots, lines_, side_codes_, side_colours_,
           cell_art_, sprites_);
    }
};

} // namespace wizball::game
