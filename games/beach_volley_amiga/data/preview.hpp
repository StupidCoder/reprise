#pragma once
#include "beach_volley_amiga/data/assets.hpp"
#include "app/game_entry.hpp"
#include "platform/image.hpp"

// Pictures of decoded data for the launcher's viewer and the asset tool.
namespace beach_volley::data {

platform::Color to_color(Rgb4 colour);
platform::Image to_image(const Picture& picture, const Palette& palette);
platform::Image palette_image(const Palette& palette);

// A sound as its waveform (the viewer plays it when clicked).
platform::Image waveform_image(const app::PreviewSound& sound);

} // namespace beach_volley::data
