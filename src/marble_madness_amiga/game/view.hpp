#pragma once
#include <map>
#include <optional>
#include <string>
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

    // What the program draws over the view between races (front_end.hpp):
    // texts in topaz 9 (cells of 10 x 9 pixels drawn with the pen over the
    // paper, JAM2; paper -1 leaves what is under it), filled boxes, the
    // score bar's title, and marbles of display type $2C (the options
    // screen's pointer, the end's bouncing marbles: draw record $106928
    // with the colour ramp $106888 lists).
    struct Text {
        int x{}, top{};           // view pixels
        std::string text;
        int pen{7}, paper{4};     // palette registers
    };
    struct Overlay {
        enum class Base { course, course_without_sprites, cleared, black };
        Base base{Base::course};
        struct Box {
            int x{}, y{}, width{}, height{}, pen{4};
        };
        std::vector<Box> boxes;
        std::vector<Text> texts;
        struct Ball {
            int x{}, height{}, ramp{}; // sprite position (heights count upwards), ramp index
        };
        std::vector<Ball> balls;
        bool title{};             // the bar shows "Marble Madness!!" ($10C980)
        int title_blanks{};       // vertical blanks since its colours began to pulse
        std::optional<std::int32_t> bar_score; // the bar as last drawn (the end does not redraw it)
        std::optional<int> bar_time;
    };
    platform::Image draw(const Race&, const Overlay&) const;

    // After every world update: repaint the rows the scroll brought in and
    // apply the regions' pictures.
    void update(Race&);
    platform::Image draw(const Race&) const;
    // The view's colour indices (research).
    std::vector<std::uint8_t> indices(const Race&) const;

private:
    void draw_bar(platform::Image&, const Race&, const Overlay* = nullptr) const;
    void draw_text(platform::Image&, const Text&, const Race&) const;
    // Text() in the program's font: cells as wide as the font, from `top`
    // (the pen's y less the baseline), filled with the paper first when
    // there is one (JAM2), clipped to image rows first_row..end_row - 1.
    void put_text(platform::Image&, const std::string&, int x, int top, platform::Color pen,
                  std::optional<platform::Color> paper, int first_row, int end_row) const;
    void draw_playfield(platform::Image&, const Race&) const;
    void draw_sprites(platform::Image&, const Race&) const;
    void draw_ball(platform::Image&, const Overlay::Ball&, const Race&) const;
    data::Palette palette_at(int line, const Race&) const; // course line
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
    void cut_course_parts(Sprite&, const Race&, const Marble&) const;  // $105CB0
    void cut_through(Sprite&, const Race&, std::uint32_t item, int x, int y, int sprite_top) const; // $111772
    void draw_sprite(platform::Image&, const Sprite&, const std::array<data::Rgb4, 3>& ramp) const;
    void draw_region(platform::Image&, const Race&, int index) const;
    void draw_drop_mark(platform::Image&, const Race&, int player) const;
    void draw_steelie(platform::Image&, const Race&, const Marble&) const;
    void draw_slinky(platform::Image&, const Race&, const Slinky&) const;
    void draw_acid(platform::Image&, const Race&, const Acid&) const;
    void draw_creature(platform::Image&, const Race&, std::uint32_t frame, std::int32_t screen, int lowest,
                       const std::array<Marble::Cover, 5>* covers) const;
    const data::ImageLibrary& library_of(int bank, data::Course) const;

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

    void paint_row(int row, int source = -1); // `source`: the map row drawn there
    void blit(std::uint32_t item, int x, int line, const Race&);
    void blit_wave_end(std::uint32_t item, int x, int line, int phase, const Race&);
    void link(std::uint32_t item, int background, const Race&);

    const data::GameFiles* files_;
    const data::Assets* assets_;
    const data::CourseMap* map_;
    data::Course course_;
    std::vector<std::uint8_t> canvas_; // colour index per pixel, course line by line
    std::map<std::size_t, std::size_t> links_; // course cell -> its linked background cell ($110A46)
    data::Palette base_palette_{};
    std::vector<Band> bands_;
    int top_row_{};
    // Ultimate's moving goal area: its 30 rows ($11E7B0 renders $1E rows),
    // and the first map row shown there (-1: its own).
    static constexpr int picture_rows = 0x1E;
    int picture_row_{-1};
    // The shine dots each marble's sprite gets this frame (taken from the
    // marble's records, which drawing consumes: $10694C).
    std::array<std::array<std::array<std::int16_t, 4>, 4>, 2> dots_{};
};

} // namespace marble_madness::game
