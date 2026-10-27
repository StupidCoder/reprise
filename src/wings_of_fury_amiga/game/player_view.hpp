#pragma once
#include "wings_of_fury_amiga/data/assets.hpp"
#include "wings_of_fury_amiga/game/scenery.hpp"

// The player's Hellcat in the play view, as `wings` $1003A6 draws it in the
// normal (not 1/8-scale) view after the backdrop's scenery. The picture is a
// handful of shapes the flight code has chosen; this is only how they are
// put on the screen. Field names follow what each value does in the drawing;
// their meaning in the flight rules is still to be read (hypotheses marked).
namespace wings::game {

struct PlayerPicture {
    int altitude{};       // -$419E (copied from -$5F86 at the frame start)
    int ground_line{};    // -$60CC: $97, lower when the plane is above $83
    int vertical_scroll{}; // -$41A8
    // Not drawn at all while this is set (-$5C6A == 1; hypothesis: below
    // deck on the elevator).
    bool hidden{};
    // Clipped at the deck while the elevator moves (-$5C6A != 0, $10526E):
    // the bottom edge is $A2 - carrier +$E + carrier bob + scroll + 3.
    bool clip_at_deck{};
    int carrier_deck{};   // the US carrier's +$E
    int carrier_bob{};    // the US carrier's +$1A
    const data::Shape* plane{};   // -$5F82
    // The game keeps one copy of each shape and mirrors it in place when the
    // other facing is wanted ($105B58; the shape's +8 word records the
    // facing). Each part is drawn as it currently stands in memory.
    bool plane_mirrored{};
    bool deck_part_mirrored{};
    bool gear_mirrored{};
    // A part drawn first when the deck state -$5C5A is 2 and -$5C91 is set
    // (-$5BE0), and again after the plane when `upset` (below) is set.
    const data::Shape* deck_part{};
    bool deck_part_shown{};
    // A part drawn below the plane, lowered toward its rest offset one pixel a
    // frame (-$5BE4 at y + offset - 5; offset -$5CB4 -> -$5BF4; hypothesis:
    // the landing gear).
    const data::Shape* gear{};
    bool gear_shown{};
    int gear_offset{};    // after this frame's step
    // -$4086 (from -$5BF0, the turn step): while turning the gear and the
    // propeller are not drawn.
    bool upset{};
    // The propeller blur, XORed over the plane ($110E24); null on frames
    // without it (see propeller_shape).
    const data::Shape* propeller{};
    bool eighth_scale{};
};

// The propeller blur for a frame ($10063A): while the engine runs (-$5C94)
// and the plane is not upset, the counter -$5C76 steps 0..3 each frame and
// picks a step from `wings` $114B5C (-$63F6); step 0 shows nothing, step 1
// the Hellcat entry attitude - 5 + $43 (fa*), any other step attitude - 5 +
// $57 (fb*), each 10 entries further when the plane faces left (-$4084 < 0).
// `attitude` is -$5A6C. Returns null when nothing is shown.
const data::Shape* propeller_shape(const data::Assets&, bool engine_running, bool upset, int counter, int attitude,
                                   bool facing_left);

// The plane in the 1/8-scale view ($100468): not -$5F82 but an 8thscale.shp
// entry from the scenery table's names (lpn*), from the copies taken at the
// frame start: `upset` = -$4086 (the turn step), `pitch` = -$4082 (the
// vertical speed), facing -$4084.
//   upset 0:        $2A + max(-pitch >> 2, -2), + 6 facing right
//   upset 1..8:     $34 + (upset - 1) >> 2, + 2 facing right
//   upset 9..$11:   $2F + upset, + 9 when (facing right) xor (upset > $D)
//   upset >= $12:   $34 + ($19 - upset) >> 2, + 2 facing right
const data::Shape* eighth_scale_plane(const data::Assets&, int upset, int pitch, bool facing_left);

// Draws the player over a backdrop canvas. In the 1/8 view (`eighth_scale`)
// only the plane is drawn, at y = (ground - altitude) >> 3 (ground $4B8).
void draw_player(IndexedCanvas&, const PlayerPicture&);

// What the US carrier shows on its deck, drawn before the player.
struct CarrierDeckPicture {
    int camera{};
    int ground_line{};
    int vertical_scroll{};
    int carrier_x{};   // -$5C6C: the map's carrier position
    bool afloat{};     // the carrier object -$5B26 +4 is set
    int deck{};        // the carrier's +$E
    int bob{};         // the carrier's +$1A
    // $103ABC: spare planes parked on the deck, from -$5CA2 (at most 9);
    // one fewer than the count is shown, entry $26 (Cpln) each, from world
    // x carrier - $C8 leftwards every $1E.
    int spare_planes{};
    // $10409C: the elevator platform, entry $25 (elev) at the carrier's x,
    // raised by `lift` (-$5C68), clipped at the deck like the plane; not
    // drawn while the elevator state -$5C6A is 1.
    int elevator_state{};
    int lift{};
    // The 1/8 view: 8thscale planes at ((ground + scroll + bob) >> 3), x
    // scaled the same way; no elevator ($10409C draws only at full scale).
    bool eighth_scale{};
};
void draw_carrier_deck(IndexedCanvas&, const data::Assets&, const CarrierDeckPicture&);

} // namespace wings::game
