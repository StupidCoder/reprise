#pragma once
#include "platform/image.hpp"
#include "tower_of_babel_amiga/data/assets.hpp"

// Pictures of the decoded data, for checking it (launcher asset viewer,
// export tool). Except for the title screen they are not the game's screens.
namespace babel::data {

platform::Color to_color(Colour);

// The title screen as the game composes it (320 x 200): logo, globe and the
// copper's three palette bands.
platform::Image title_image(const TitleScreen&);

// A tower's four floors seen from above, side by side, with its objects
// marked (initials of the designer's names) and its 18 colours below.
platform::Image tower_image(const Tower&, const Memory& program);

} // namespace babel::data
