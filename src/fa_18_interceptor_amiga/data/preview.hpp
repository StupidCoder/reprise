#pragma once
#include "fa_18_interceptor_amiga/data/assets.hpp"
#include "platform/image.hpp"

// Pictures of the decoded data, for checking it (launcher asset viewer,
// export tool). The text screens are drawn as the typing would leave them
// at its end; everything else is a sheet, not one of the game's screens.
namespace interceptor::data {

platform::Color to_color(Colour);

platform::Image picture_image(const amiga::IlbmPicture&);
// Text blocks on one 320 x 200 screen, in the given palette, lines
// `spacing` scanlines apart (the caller sets $13AA2E: 11 rows by default,
// 12 for the main menu at $101DA4).
platform::Image text_screen(const Assets&, const std::vector<int>& blocks, int palette, int spacing = 11);
// Both fonts, every character, magnified.
platform::Image font_sheet(const Assets&);
// The ten palettes, one row of 32 swatches each.
platform::Image palette_sheet(const Assets&);
// The cockpit message lines in the small font.
platform::Image message_sheet(const Assets&);
// A sample's waveform.
platform::Image waveform_image(const Sound&);

} // namespace interceptor::data
