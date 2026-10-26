#pragma once
#include "platform/image.hpp"
#include "wizball_c64/data/assets.hpp"

// Pictures of the decoded data, for checking it (launcher asset viewer,
// export tool). They are not the game's screen: nothing moves, the stars and
// animated characters keep their stored shapes, and sprite colours are the
// ones named below rather than the ones the game sets.
namespace wizball::data {

// A level strip: 4 x 45 tiles (20 x 225 characters), in the grey colours of
// an unpainted level or the painted ones. `lines` > 1 wraps the strip into
// that many lines of equal width, one above the other, for viewing.
platform::Image level_image(const Assets&, int level, bool painted, int lines = 1);
// The map columns 90-95 of a place's four rows, in grey (places 8-10).
platform::Image place_image(const Assets&, int place);
// Every tile in a grid of 16 per row, with level 0's characters, grey.
platform::Image tile_sheet(const Assets&, int level);
// The playfield charset with a level's characters, 16 x 16, grey.
platform::Image play_charset_sheet(const Assets&, int level);
// The front-end charset, 16 x 16, single colour.
platform::Image front_charset_sheet(const Assets&);
// All sprite frames, 16 per row. Multicolour pairs 01/10 use $D025 = $0B
// and $D026 = $0D (the playfield split's values); the own colour is the low
// nibble of byte 63.
platform::Image sprite_sheet(const Assets&);

} // namespace wizball::data
