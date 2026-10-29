#pragma once
#include "tower_of_babel_amiga/data/assets.hpp"
#include "tower_of_babel_amiga/view/canvas.hpp"
#include "platform/image.hpp"
#include <array>
#include <optional>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

// The screen the menus, the panel and the game draw on: the original's
// 320 x 200 picture in 16 colours, with the drawing kit its front end uses
// (rectangles in pens, framed and bevelled boxes, windows that keep what was
// under them, pictures, the proportional font with its layout codes, and the
// menus' click areas). Pens are the program's 16-pixel 2-row patterns at
// $892(a6): fills use both rows, lines and text only the first.
namespace babel::front {

// An address of the original's variables (A6 = $806 throughout).
constexpr std::uint32_t var(std::uint32_t offset) { return 0x806 + offset; }

// The 16 colours shown, and how the copper changes them down the screen.
enum class CopperMode {
    plain,  // mode 0: colours 0-15 of the palette (menus)
    split,  // mode 1: the same, colours 7 and 8 from 16 and 17 below line $48 (the designer)
    game,   // mode 2: the tower's view (colour 7 bands, panel colours from line $82)
    title,  // mode 3: three bands of 16 (lines $36 and $71)
};

class Screen {
public:
    static constexpr int width = 320, height = 200;

    explicit Screen(const data::Assets&);

    view::Canvas& canvas() { return canvas_; }
    const view::Canvas& canvas() const { return canvas_; }
    const view::Pen& pen(int n) const { return pens_[std::size_t(n)]; }
    // The game changes some pens while it runs (the blink of pens 23-24).
    void set_pen(int n, const view::Pen& p) { pens_[std::size_t(n)] = p; }
    void invert_blink_pens(); // $AB44

    void clear(); // $119BE: everything colour 0
    void fill(int x0, int y0, int x1, int y1, int pen); // $11532 (inclusive)
    void line(int x0, int y0, int x1, int y1, int pen); // $14DE6
    void plot(int x, int y, int pen);                   // $14F7A
    void frame(int x0, int y0, int x1, int y1, int pen);       // $11A6C: four lines
    void bevel_box(int x0, int y0, int x1, int y1, int pen);   // $11AA6: a panel-like box
    void invert(int x0, int y0, int x1, int y1, int pen);      // $116C8: XOR with the pen
    // $11B40: keeps what is under the box, fills it with pen `fill_pen`,
    // frames it in pen 15 with a line under its top text row. $11B7E puts the
    // last one back.
    void open_window(int x0, int y0, int x1, int y1, int fill_pen);
    void close_window();
    void title_bar(); // $11A1C: the top line's box (pen 0, frame pen 15)

    // $11862: a picture in the program's format at (x & ~15, y).
    void picture(const data::Memory&, std::uint32_t address, int x, int y, int width_words, int rows);
    // $11134: a 16 x 16 icon, per row a keep mask and four plane words, at any x.
    void icon16(const data::Memory&, std::uint32_t address, int x, int y);
    // $111F6: an 8 x 8 icon, per row a keep mask and four plane bytes.
    void icon8(const data::Memory&, std::uint32_t address, int x, int y);
    // $14FF2: a 2 x 2 dot (the moon's place) at an even x.
    void dot(int x, int y, int pen);
    // The designer's cell cursor: hardware sprite 1 ($2AFA(a6): 16 rows of
    // two plane words) over the picture at (x, y), in sprite colours 17-19
    // of the mouse style (table $2B4E(a6)), or none.
    std::optional<std::array<int, 2>> marker;
    int mouse_style{};
    // Copies `rows` lines of 16-pixel columns from (x, y) (the panel blits).
    void copy_block(std::span<const std::uint8_t> planes, int x, int y, int width_words, int rows);

    // Text ($11D20). Layout codes: $1F x.w y.w move, $1B x.w move across,
    // $1E pen ink, $1D pen paper, $1C ink 15 on paper 0, $0D new line (x 0,
    // y + 12), $0B up 8, $0A down 8, $09 right 8, $08 left 8.
    struct Cursor {
        int x{}, y{};
        int paper{}, ink{15}; // $4BE, $4BF
    };
    Cursor cursor;
    void text(std::string_view);
    void text_at(std::uint32_t address); // a zero-terminated text in the program
    std::string string_at(std::uint32_t address) const; // the same with its codes, to centre
    void glyph(int code);                // $11F36
    int text_width(std::string_view) const; // $12026
    void centre(std::string_view);       // $12058: x so that the text is centred
    void message(int index);             // $11D04: the program's numbered texts
    std::string message_text(int index) const;
    // $11E7E-$11EEA: `digits` decimal digits with leading zeros; $11F26 one digit.
    void number(long value, int digits);
    // $113C2: radio buttons at the (x, y) list in the program, the chosen one filled.
    void radio(std::uint32_t list, int chosen);

    // Palette (48 colours: three bands of 16 for the title) and the copper mode.
    std::array<data::Colour, 48> palette{};
    CopperMode copper{CopperMode::plain};
    // For the game mode: the 48 colours are the tower's 18, the sky
    // gradient's 15 and the panel's 15 (view::tower_palette); the copper
    // shows them by the sky type, flashing for lightning.
    int sky_type{};
    bool flash{};
    void set_palette(const data::Memory& program, std::uint32_t address, int count = 48);

    platform::Image image() const { return image_of(canvas_, 0); }
    // The same from a wider canvas (widescreen: the picture `margin` columns
    // in from its left edge).
    platform::Image image_of(const view::Canvas&, int margin) const;

private:
    struct SavedBox {
        int x0, y0, x1, y1;
        std::vector<std::uint8_t> pixels;
    };
    const data::Assets& assets_;
    view::Canvas canvas_{width, height};
    std::array<view::Pen, 80> pens_{};
    data::Memory font_;
    std::vector<SavedBox> saved_;
    void put(int x, int y, std::uint8_t c) {
        if (x >= 0 && x < width && y >= 0 && y < height) canvas_.set(x, y, c);
    }
};

// A menu's click areas (10-byte records at an address in the program, ended
// by a negative first byte): pen, flags (bit 0 highlight by inverting, bit 1
// by a frame in the record's pen, bit 5 the area is everywhere outside the
// box), x0, y0, x1, y1 in low-res pixels. $11404 keeps the area under the
// pointer highlighted; $112EA waits for a click on one.
class Menu {
public:
    Menu() = default;
    Menu(const data::Memory& program, std::uint32_t address);
    int count() const { return int(items_.size()); }
    // The area under (x, y), or -1 (the last one counts, as $11404 keeps the last hit).
    int at(int x, int y) const;
    // Highlights the area under the pointer and unhighlights the others ($11404).
    void hover(Screen&, int x, int y);
    void unhover(Screen& screen) { hover(screen, 0x1F40, 0x8000); } // $113F2

private:
    struct Item {
        int pen{}, flags{};
        int x0{}, y0{}, x1{}, y1{};
        bool lit{};
    };
    std::vector<Item> items_;
    bool inside(const Item&, int x, int y) const;
};

} // namespace babel::front
