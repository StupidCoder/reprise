#pragma once
#include "marble_madness_amiga/data/assets.hpp"
#include "platform/image.hpp"

// Pictures of the decoded data, for checking it (launcher asset viewer,
// export tool). They are not the game's screen: colours 10-15 of a course
// come from the course's display block as the view scrolls (the map file
// holds the top band's values), and sprite colours are a grey ramp, not the
// ramps the game's draw records give them.
namespace marble_madness::data {

platform::Color to_color(Rgb4);

// The whole course, 288 pixels wide, every stored row (rows below the
// scroll's reach are drawn after a red line).
platform::Image course_image(const Assets&, Course);
// Every tile, 32 per row, in the course's palette.
platform::Image tile_sheet(const Assets&, Course);
// The cells of a library side by side in rows (at most `width` pixels wide);
// scenery cells in `palette`, sprite cells in a grey ramp.
platform::Image library_sheet(const ImageLibrary&, const Palette& palette, int width = 640);
// The loader's title picture.
platform::Image splash_image(const Assets&);

} // namespace marble_madness::data
