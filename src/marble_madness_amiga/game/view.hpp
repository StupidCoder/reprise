#pragma once
#include "marble_madness_amiga/data/assets.hpp"
#include "marble_madness_amiga/data/game_files.hpp"
#include "marble_madness_amiga/game/race.hpp"
#include "platform/image.hpp"

// The picture of a race: the course's tile map at the scroll position and
// the marbles as the hardware sprites the game builds for them.
//
// The game shows a 9-line score bar and under it the 288 x 200 course view
// ($107C90's RasInfo); view line i shows tile-map line (scroll + i). A
// sprite placed at (x, y) ($111EF0: HSTART = x + $80, VSTART = y + $2C)
// appears at view pixel (x, y - 9): its lines count from the top of the
// score bar (measured: pixel-exact against the original's screen). A draw
// record (16 bytes: x and y offsets, columns, ..., cell id, colour ramp) is
// placed at (screen x + x offset, $108 - (screen y + y offset)) ($1127AC).
//
// The playfield itself is a ring of 512 lines: tile rows are painted into
// it 32 rows ahead of the view as it scrolls down ($10D278; 7 rows behind
// going up, $10D384), and pictured regions blit their frames into it
// ($112204). The view keeps the course's lines as that ring would show
// them: a row is repainted from its tiles when the ring would paint it, and
// region pictures stay until then.
namespace marble_madness::game {

class View {
public:
    static constexpr int width = 288, height = 200;
    static constexpr int bar_height = 9;            // the score bar above the view
    static constexpr int sprite_top = bar_height;   // sprite line of view line 0
    static constexpr int picture_height = bar_height + height;

    View(const data::GameFiles&, const data::Assets&, const Race&);

    // After every world update: repaint the rows the scroll brought in and
    // apply the regions' pictures.
    void update(Race&);
    platform::Image draw(const Race&) const;
    // The view's colour indices (research).
    std::vector<std::uint8_t> indices(const Race&) const;

private:
    void draw_bar(platform::Image&, const Race&) const;
    void draw_marble(platform::Image&, const Race&, const Marble&) const;
    void draw_record(platform::Image&, const amiga::LoadedImage& source, std::uint32_t record, int screen_x,
                     int screen_y, const std::array<data::Rgb4, 3>& ramp, data::Course course) const;

    // A hardware sprite as the game builds it from a draw record ($1127AC):
    // its two plane words per line for each 16-pixel column, so that the
    // shine and the wall covers can change its bits as the game does.
    struct Sprite {
        int x{}, y{};               // screen position of the top left (heights count upwards)
        int columns{}, rows{};
        std::vector<std::array<std::uint16_t, 2>> words; // [column * rows + line]
        std::array<std::uint16_t, 2>& at(int column, int line) {
            return words[std::size_t(column * rows + line)];
        }
    };
    bool make_sprite(Sprite&, const amiga::LoadedImage& source, std::uint32_t record, int screen_x, int screen_y,
                     data::Course) const;
    void cut_cover(Sprite&, const Marble::Cover&, data::Course) const; // $1114F8
    void draw_sprite(platform::Image&, const Sprite&, const std::array<data::Rgb4, 3>& ramp) const;
    void draw_regions(platform::Image&, const Race&) const;
    void draw_drop_marks(platform::Image&, const Race&) const;

    // The Track's display block (header +$10): colours set by copper
    // lists at the top of the view and from given course lines down
    // ($10883C); here as the colours that apply from each line on.
    struct Move {
        std::uint32_t instruction{}; // Track address of the copper MOVE
        int colour{};                // register 0-15
    };
    struct Band {
        int first_line{};            // in scroll lines
        std::vector<Move> moves;
    };
    void read_display_block(const Race&);

    void paint_row(int row);
    void blit(std::uint32_t item, int x, int line, const Race&);

    const data::GameFiles* files_;
    const data::Assets* assets_;
    const data::CourseMap* map_;
    data::Course course_;
    std::vector<std::uint8_t> canvas_; // colour index per pixel, course line by line
    data::Palette base_palette_{};
    std::vector<Band> bands_;
    int top_row_{};
    // The shine dots each marble's sprite gets this frame (taken from the
    // marble's records, which drawing consumes: $10694C).
    std::array<std::array<std::array<std::int16_t, 4>, 4>, 2> dots_{};
};

} // namespace marble_madness::game
