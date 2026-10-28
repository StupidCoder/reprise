#pragma once
#include "platform/image.hpp"
#include "tower_of_babel_amiga/data/assets.hpp"
#include <optional>

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

// The 3D view a tower opens with: from the first spider available, facing
// north, with the default view options and the objects in their first
// shapes (288 x 112, the original's window).
// Colour 7, which the original's copper turns into sky and ground bands,
// shows as the tower's colour 7. Empty when the tower has no such spider.
std::optional<platform::Image> start_view_image(const Tower&, const Memory& program, const Memory& main);

} // namespace babel::data
