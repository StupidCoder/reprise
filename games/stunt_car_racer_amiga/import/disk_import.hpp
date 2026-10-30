#pragma once
#include "app/game_entry.hpp"
#include <cstdint>
#include <span>

// Importing Stunt Car Racer from the user's disk image (ADF).
//
// The disk has no file system (games/stunt_car_racer_amiga/game.md, "Disk"):
// the boot block reads the loader ($9800 bytes from byte $2C00) and jumps to
// it; the loader shows its loading picture and reads 805 sectors from
// sector 110 to $E700, the whole game program. Nothing else on the disk is
// read. The import installs both regions exactly as stored on disk:
//
//   loader    sectors 22-97: loader code, loading picture and its palette
//   program   sectors 110-914: the game, loaded at $E700 (the range
//             $F4B8-$1AA49 is stored XOR $80; data/assets undoes it)
//
// The release is recognised by the SHA-256 of both regions.
namespace stuntcar::import {

app::ImportResult import_disk(std::span<const std::uint8_t> image);

} // namespace stuntcar::import
