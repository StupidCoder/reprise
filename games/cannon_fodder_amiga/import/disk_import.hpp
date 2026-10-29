#pragma once
#include "app/game_entry.hpp"
#include <cstdint>
#include <span>
#include <string>

// Importing Cannon Fodder from the user's three disk images (ADF).
//
// The disks use Sensible Software's own file system (sensi_disk.hpp); most
// files are packed with RNC method 1 (rnc.hpp). Each disk is recognised by
// its directory and by the marker file the game itself looks for ("disk1",
// "disk2", "disk3"). Every file the directory lists is unpacked and checked
// by the SHA-256 of its unpacked bytes, then installed as
// "disk<N>/<name in lower case>":
//
//   disk1/  fodderc (the game program), fload (the loader), the title and
//           presentation screens (*.raw), the front-end pictures, the music
//           of the front end (*.sng/*.ins)
//   disk2/  missions 1-36 (mapm*.map, *.spt), jungle, ice and desert tile
//           sets, sprites, palettes, music
//   disk3/  missions 37-72, moor, interior and desert sets
//
// Disks 2 and 3 share some files; where the copies are identical the game
// may take either. Disk 1 also lists `fodders` and `fodderf` (the program for
// slow and fast memory, identical to `fodderc` on this release); they are not
// installed.
namespace fodder::import {

// Imports one of the three disks; the library combines them.
app::ImportResult import_disk(std::span<const std::uint8_t> image);

} // namespace fodder::import
