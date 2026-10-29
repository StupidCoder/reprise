#pragma once
#include "app/game_entry.hpp"
#include <cstdint>
#include <span>
#include <string_view>

// Importing Wings of Fury from the user's disk image (ADF).
//
// The disk is an ordinary AmigaDOS (OFS) disk, volume "Wings of Fury", with
// the game in the folder Wings_of_Fury/ (.analysis/wings_of_fury_amiga/
// disk.md). The program `wings` and its music player are rebuilt natively, so
// only their data is needed. The import keeps:
//
//   wings               the main program (texts and tables are read from it)
//   songplay, wofsongs  the music player and the songs with their instruments
//   shapes/*            pictures (IFF ILBM), palettes and shape libraries
//   maps/*              the fifteen mission maps
//   sounds/*            the sound-effect samples
//   newarmyfont, fonts/army/12, fonts/army.font   the game's two fonts
//   highscore           the high-score table as found on the disk
//
// Files are installed under their base names ("world.shp", "a.map"); the army
// disk font as "army-12" and "army.font".
//
// The release is recognised by the SHA-256 of the programs; every data file
// is checked by its SHA-256 too. `highscore` is written by the game, so any
// copy of it is accepted.
namespace wings::import {

// The installed name of a disk file ("Wings_of_Fury/shapes/world.shp" ->
// "world.shp").
std::string_view installed_name(std::string_view disk_path);

app::ImportResult import_disk(std::span<const std::uint8_t> image);

} // namespace wings::import
