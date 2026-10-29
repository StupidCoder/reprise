#pragma once
#include "sonic_advance_gba/game/types.hpp"

// The act's objects: three layouts (interactive objects, item boxes,
// enemies) in cells of 256x256 pixels, and the spawner that creates the
// objects coming into range of the camera (08008784-08009684).
namespace sonic_advance_gba {

class Game;

// 08008784
void start_spawner(Game& g);

// What an object's creator gets: its layout record (x and y in 8-pixel
// units within the cell, kind, then parameters), the cell and its index in
// the cell's list. Setting the record's x to FDh (or less, as a signed
// byte) keeps the spawner from creating it again; objects put back their
// x when they leave.
struct ObjectSpawn {
    u8* record;
    u16 cell_x, cell_y;
    u8 index;

    // The object's position in the act.
    s32 x() const { return cell_x * 0x100 + record[0] * 8; }

    s32 y() const { return cell_y * 0x100 + record[1] * 8; }
};

} // namespace sonic_advance_gba
