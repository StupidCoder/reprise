#pragma once
#include "sonic_advance_gba/game/objects.hpp"

// The interactive objects of the layouts' first list (creators 080B806C by
// kind): springs, spikes, platforms, rides and the like.
namespace sonic_advance_gba {

struct Player;
struct Sprite;

// Creates the object whose creator routine is `creator`; false if that
// object is not reconstructed.
bool create_interactive(Game& g, u32 creator, const ObjectSpawn& o);

// How a broken block's pieces look: two pictures (each drawn twice), how
// many frames they fly, how fast they grow (scale units a frame) from what
// scale, the sprite flags of the first piece (its first rotation set; the
// others follow) and a sound when they start (0: none).
struct DebrisLook {
    u16 small_animation;
    u32 small_tiles;
    u16 large_animation;
    u32 large_tiles;
    u16 frames;
    s16 growth;
    s16 start_scale;
    u32 first_set;
    u16 sound;
};

// 080234A0 / 08091780: the four pieces of a block broken at act position
// (x, y) relative to the scene transform's focus.
void create_block_debris(Game& g, s16 x, s16 y, const DebrisLook& look);

// A spring (sprite `sprite` at act position (x, y), layout record `record`:
// +3 the strength, below 4 from the table 080B84FC, else +4 x 16) met by
// player `p`. 08022610: standing on it (or Amy's hammer) throws the player
// up. 080227D4: meeting its underside throws the player down (springs
// on ceilings; up springs with gravity reversed). True when thrown.
bool spring_throws_up(Game& g, Sprite& sprite, s16 x, s16 y, const u8* record, Player& p);
bool spring_throws_down(Game& g, Sprite& sprite, s16 x, s16 y, const u8* record, Player& p);

// 08076C90: a dash panel with the creator's picture (`tiles` sprite tiles,
// animation and variant); shared by the zones' creators.
void create_dash_panel(Game& g, const ObjectSpawn& o, u32 tiles, u16 animation, u8 variant);

} // namespace sonic_advance_gba
