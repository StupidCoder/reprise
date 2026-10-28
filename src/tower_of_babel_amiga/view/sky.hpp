#pragma once
#include "tower_of_babel_amiga/data/assets.hpp"
#include "tower_of_babel_amiga/view/canvas.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <vector>

// The view's sky: the colour bands the copper paints colour 7 with, stars,
// the moon, lightning and meteors ($D33A, $CF56, $CFFC, $D120 and the
// copper builder $88EA). The original changes the effects' state while it
// draws them; here the state advances in `SkyEffects::advance` (once per
// frame, before the view is drawn) and drawing only reads it.
namespace babel::view {

// A tower's sky settings (the record's $E34-$E42 and flags 6, 7).
struct SkySettings {
    int stars{};          // $E34: 0-3, 21 stars per step
    int sky_type{};       // $E38: which line list the gradient follows
    int moon_heading{};   // $E3A: the heading the moon is seen in
    int moon_type{};      // $E3C
    int moon_x{};         // $E3E: in 16-pixel columns of the view
    int moon_y{};         // $E40: lines below the view's top (beyond $37 no moon)
    int moon_rotation{};  // $E42
    bool lightning{};     // flag 6 clear
    bool meteors{};       // flag 7 clear
};
SkySettings sky_settings(const data::Tower&);

// The state the effects keep between frames ($E44, $FC4-$FD4, the random
// number $890).
struct SkyState {
    int moon_frame{};                 // $E44
    int lightning_wait{}, thunder_wait{}, lightning_heading{}; // $FC4, $FCA, $FC6
    int meteor_wait{}, meteor_heading{}, meteor_x{}, meteor_y{}, meteor_frame{}; // $FCC-$FD4
    std::uint16_t random{};           // $890: x * $5E5 + $29 (16 bits)

    // What this frame shows besides the static sky (set by advance).
    std::optional<int> bolt_column, bolt_image; // lightning bolt (16-pixel column, image 0-3)
    bool flash{};                     // $FC8: the palette flashes this frame
    bool meteor_visible{};
    bool thunder{}, crack{};          // sounds to start ($30F2, $3096)
};

class SkyEffects {
public:
    // `even_frame` is the original's buffer parity ($20(a6) = 0): lightning
    // only advances on those frames.
    static void advance(SkyState&, const SkySettings&, int heading, bool even_frame);
};

// Draws stars, moon, meteor and bolt into the view (window at (16, 16),
// 288 x 112; centre column 160). `program` and `main` hold the tables and
// images; `pens` the object pens ($892(a6)) the stars use.
void draw_sky(Canvas&, const SkySettings&, SkyState&, int heading, const data::Memory& program,
              const data::Memory& main, const std::array<Pen, 80>& pens, int left = 16);

// The colours of one frame: the 16 registers at the top of the screen and
// the lines where colour 7 changes (sky gradient, then the ground at the
// horizon line 72), from the tower's 18 colours ($12392 gradient, copper
// mode 2 at $89D2, line lists $8ACA). `flash`: lightning's palette ($2592).
struct FramePalette {
    std::array<data::Colour, 16> colours{};
    std::vector<std::pair<int, data::Colour>> colour7; // (screen line, colour) in order
};
FramePalette frame_palette(const data::Tower&, const data::Memory& program, bool flash);

} // namespace babel::view
