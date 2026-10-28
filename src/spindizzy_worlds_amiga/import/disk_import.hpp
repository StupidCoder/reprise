#pragma once
#include "app/game_entry.hpp"
#include <cstdint>
#include <span>
#include <vector>

// Importing Spindizzy Worlds from the user's disk image (ADF).
//
// The disk has no file system (.analysis/spindizzy_worlds_amiga/disk.md):
// the boot block starts a loader that reads 512-byte blocks with its own
// track loader, and the main program reads byte ranges the same way through
// small file records ("A:\title.neo" with its disk offset in front). The
// import copies the ranges the program loads:
//
//   main         blocks $16-$1B9, loaded to $10000: code, texts, tables, all
//                graphics (fonts, sprites, block pieces)
//   title.neo    byte $3A200, $7D80 bytes: the title picture (NEOchrome)
//   music        byte $45E00, $9C40 bytes, loaded to $432A8: music player
//   effects      byte $4FE00, $C000 bytes, loaded to $3400: effects player
//   sd_game.dat  byte $7A600, $83C bytes: the saved-game slot as shipped
//   select.swp   byte $7B000, $5140 bytes: the cluster screen's swap file
//   world00-31   byte $80000 + n × $2000: the 32 world files ("Cluster"),
//                unscrambled (scramble.hpp)
//
// The release is recognised by the SHA-256 of `main`; every installed
// file is checked by its SHA-256 (the worlds as one range, before
// unscrambling).
namespace spindizzy::import {

app::ImportResult import_disk(std::span<const std::uint8_t> image);

// Undoes the scramble the program removes after loading a world ($11CF2):
// the first $7FE words are XORed with a running key that starts at
// $CC0C:$62C0 (the long $24B4(a5), set by $11D12 → $422FA) and changes with
// every word read. The rest of the 8 KB slot is stored plain.
void unscramble_world(std::vector<std::uint8_t>& bytes);

} // namespace spindizzy::import
