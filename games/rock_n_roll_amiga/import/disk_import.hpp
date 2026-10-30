#pragma once
#include "app/game_entry.hpp"
#include <cstdint>
#include <span>
#include <string>

// Importing Rock 'n Roll from the user's disk image (ADF).
//
// The supported image is the one-disk release on Rainbow Arts' 1991
// "5th Anniversary" compilation (games/rock_n_roll_amiga/game.md "Disk and
// boot"). It has no file system: its boot block reads a packed program that
// shows the Rainbow Arts logo, then a track loader that reads the band intro
// (file $44), runs it, and loads the game (file $43, holding `Main`) and its
// numbered files by a table of tracks and offsets. Main is a self-unpacking
// program that takes over the machine and loads the files through that loader.
//
// The import unpacks everything (packers.hpp, cruncher.hpp) and installs:
//
//   main        the unpacked program as it runs from $1A578 (Program); its
//               tables, texts and some graphics are read from it
//   00 … 41     each of the game's files by its own number in hex, unpacked
//               ("3d" and "3e" are copies of "33" and "39"; "3f" is the
//               program itself and not installed)
//   logo        the Rainbow Arts logo program as it runs from $30000
//   intro       the band intro as it runs from $1087E
//
// The disk is recognised by its boot block; every unpacked part is checked
// by its SHA-256. The one-disk crack used before (no band intro) is refused
// with a message saying which disk is supported.
namespace rocknroll::import {

// The installed name of game file `number` ("0a").
std::string file_name(int number);

app::ImportResult import_disk(std::span<const std::uint8_t> image);

} // namespace rocknroll::import
