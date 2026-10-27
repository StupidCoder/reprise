#pragma once
#include "platform/image.hpp"
#include "wings_of_fury_amiga/data/assets.hpp"

// Pictures of the decoded data, for checking it (launcher asset viewer,
// export tool). They are not the game's screens.
namespace wings::data {

platform::Color to_color(Rgb4);

// An ILBM picture in its own palette (hires pictures are shown at their
// stored width).
platform::Image picture_image(const amiga::IlbmPicture&);
// The colours of a palette as swatches.
platform::Image palette_image(const Palette&);
// Every shape of a library over a dark backdrop, in rows at most `width`
// pixels wide, drawn as the blitter leaves them over colour 0.
platform::Image library_sheet(const ShapeLibrary&, const Palette&, int width = 640);
// A map's scenery as the drawer lays it out (shapes at x - 8 - offset_x,
// $97 + drop - offset_y per column), over the sky colour, folded into rows
// `width` pixels wide; the carrier position is marked with a red tick.
platform::Image map_image(const Assets&, const MissionMap&, const Palette&, int width = 1280);
// A sample as a waveform.
platform::Image sample_image(const Sample&);

// The palette with this name from the assets (a palette file, or a picture's
// CMAP); throws std::runtime_error when there is none.
const Palette& find_palette(const Assets&, std::string_view name);

} // namespace wings::data
