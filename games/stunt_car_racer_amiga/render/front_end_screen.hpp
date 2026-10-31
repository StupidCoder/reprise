#pragma once
#include "platform/image.hpp"
#include "stunt_car_racer_amiga/data/front_end.hpp"
#include "stunt_car_racer_amiga/data/planar.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

// Drawing the front end's screens (game.md, "Front end"): pictures, the
// menu panel and its bars, portraits, and text printed as the game prints
// it, a character cell at a time from a cursor (0x1F x y moves it).
namespace stuntcar::render {

// The menu bars' colour strips in the menu picture ($64C1A): brown (the
// choice just taken), orange (the highlighted choice), light grey, the
// tall grey one, green.
enum class Strip { taken, highlight, plain, tall, green };

class FrontEndCanvas {
  public:
    explicit FrontEndCanvas(const data::FrontEndArt&);

    // The whole of a picture, and its colours.
    void picture(const data::Picture&);
    void copy(const data::Picture&, int sx, int sy, int width, int height, int dx, int dy);
    void fill(int x, int y, int width, int height, std::uint8_t colour);
    // Colour indices drawn over the screen at (x, y), leaving `clear` out.
    void overlay(const std::uint8_t* pixels, int width, int height, int x, int y, std::uint8_t clear);

    // The menu picture with its panel cleared to grey ($64C6E).
    void menu_panel();
    // A bar on text row `row` ($64B0E): 17 pixel rows (the tall strip 25);
    // each extra line adds the strip's rows 1-16, 8 rows further down.
    void bar(int row, Strip, int extra_lines = 0);
    // A portrait of a driver (0-11) with the name strip under it, cut from
    // the portrait sheet ($5893C), its top left at `at`.
    void portrait(int driver, data::Point at);
    // The standings' variant ($58B2C): two pixels at either side left out,
    // the rest moved `shift` pixels right.
    void trimmed_portrait(int driver, data::Point at, int shift);

    // Text: colours ($59806 ink, $5981A paper; no paper draws see-through),
    // the font, the cursor in character cells and the pixel offset of the
    // cell grid.
    void ink(std::uint8_t colour) { ink_ = colour; }
    void paper(std::optional<std::uint8_t> colour) { paper_ = colour; }
    void name_font(bool on) { name_font_ = on; }
    void move(int column, int row) { column_ = column, row_ = row; }
    void offset(int dx, int dy) { dx_ = dx, dy_ = dy; }
    // The menus print 8 rows higher than the cell grid (their display
    // starts 8 rows lower; $5D724 = $41); other screens do not.
    void menu_rows(bool on) { menu_rows_ = on; }
    void print(std::string_view);
    void print_number(int value); // decimal, as $5DF00
    int column() const { return column_; }

    platform::Image image() const;

  private:
    void glyph(char);

    const data::FrontEndArt& art_;
    std::array<std::uint8_t, data::screen_width * data::screen_height> pixels_{};
    std::vector<data::Colour> palette_;
    std::uint8_t ink_ = 1;
    std::optional<std::uint8_t> paper_;
    bool name_font_ = false;
    int column_ = 0, row_ = 0, dx_ = 0, dy_ = 4;
    int pending_ = 0; // bytes of a cursor move still to come
    bool menu_rows_ = true;
};

} // namespace stuntcar::render
