#pragma once
#include "bruce_lee_c64/data/assets.hpp"
#include "platform/image.hpp"

// Pictures of the decoded data, for checking it (launcher asset viewer,
// export tool). They are not the game's screen: the colour changes the
// game's interrupt makes down the screen are not applied, and nothing moves.
namespace bruce_lee::data {

// A room's map as $0A9F draws it, 320 x 176 pixels, in the play colours.
platform::Image room_image(const Assets&, int room);
// A charset as 16 x 16 characters (8 x 8 each), in the play colours.
platform::Image charset_sheet(const Assets&, int set);
// The text font, 128 codes, single colour.
platform::Image text_sheet(const Assets&);
// The loading picture, 320 x 200, in the colours the loader sets.
platform::Image loading_image(const Assets&);

} // namespace bruce_lee::data
