#pragma once
#include "platform/image.hpp"
#include "rock_n_roll_amiga/data/assets.hpp"

// Pictures of the decoded data, for checking it (launcher asset viewer,
// export tool). They are not the game's screens.
namespace rocknroll::data {

using Palette = std::vector<amiga::Rgb4>;

platform::Color to_color(amiga::Rgb4);

// An ILBM picture in its own palette.
platform::Image picture_image(const amiga::IlbmPicture&);
// The whole graphics bank in a palette.
platform::Image bank_image(const GraphicsBank&, const Palette&);
// A level map drawn from its world's tiles; `cell_size` 16 is the game's
// scale, smaller sizes average the tile's pixels. Abyss cells show the abyss
// pattern as it lies with the camera on a tile boundary.
platform::Image level_image(const Level&, const World&, int cell_size = 16);
// The colours of a palette as swatches.
platform::Image palette_image(const Palette&);

} // namespace rocknroll::data
