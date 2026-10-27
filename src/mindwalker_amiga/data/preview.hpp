#pragma once
#include "mindwalker_amiga/data/assets.hpp"
#include "platform/image.hpp"

// Pictures of the decoded data, for checking it (launcher asset viewer,
// export tool). They are not the game's screens: the shared image sets have
// no colours of their own (each stage gives them its sprite colours), so they
// are drawn in a grey ramp.
namespace mindwalker::data {

platform::Color to_color(Rgb4);

// The title picture decoded as the HAM screen shows it.
platform::Image ham_image(const HamPicture&);
// A bitmap in a palette (index 0 drawn too).
platform::Image bitmap_image(const Bitmap&, const Palette&);
// A portrait, white on black.
platform::Image portrait_image(const Bitmap&);
// Frames side by side in rows, 2 pixels apart, at most `width` pixels wide;
// with an empty palette in a grey ramp.
platform::Image frame_sheet(const std::vector<Bitmap>&, const Palette& palette = {}, int width = 320);
// The tunnel's colours for its hands: B draws them in planes 0-2 and then
// sets plane 3 to their silhouette ($3025AA: D = A | B | C), so a hand pixel
// of value v shows colour 8 + v.
Palette hands_palette(const Assets&);
// The colours of a palette as swatches.
platform::Image palette_image(const Palette&);
// A sample as a waveform, one column per byte group.
platform::Image sample_image(const Sample&);

} // namespace mindwalker::data
