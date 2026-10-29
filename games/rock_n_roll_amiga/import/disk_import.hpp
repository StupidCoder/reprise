#pragma once
#include "app/game_entry.hpp"
#include <cstdint>
#include <span>
#include <string>

// Importing Rock 'n Roll from the user's disk image (ADF).
//
// The image this was recovered from is a one-disk crack of the two-disk
// original (games/rock_n_roll_amiga/game.md "Disk and boot"): an AmigaDOS (OFS) disk,
// volume "Rock&Roll", whose startup-sequence runs the cracker's intro, which
// installs a file loader at $A000 and starts `Main`. Main is a self-unpacking
// program that takes over the machine and loads the game's numbered files
// through that loader. Their names are the game's file numbers in hex, except
// that numbers $3D and $3E are read from "33" and "39" (the loader's rule at
// $A02A; those files are identical).
//
// The import unpacks everything (cruncher.hpp) and installs:
//
//   main        the unpacked program as it runs from $1A578 (Program); its
//               tables, texts and some graphics are read from it
//   00 … 41     each of the game's files by its own number in hex, unpacked
//               ("3d" and "3e" are the copies the loader reads; "3f" is the
//               program itself and not installed)
//
// The release is recognised by the SHA-256 of the unpacked program; every
// data file is checked by the SHA-256 of its unpacked bytes.
namespace rocknroll::import {

// The installed name of game file `number` ("0a").
std::string file_name(int number);

app::ImportResult import_disk(std::span<const std::uint8_t> image);

} // namespace rocknroll::import
