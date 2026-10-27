#pragma once
#include "app/game_entry.hpp"
#include <cstdint>
#include <span>

// Importing Marble Madness from the user's disk image (ADF).
//
// The disk is an ordinary AmigaDOS (OFS) disk (.analysis/marble_madness_amiga/
// disk.md). Workbench starts the launcher "MarbleMadness!", which runs the
// decryptor c/zzz on c/xxx (the protection's track check), then again on
// c/MarbleMadness!.dat, the game program, with a key that c/xxx builds and
// the launcher mixes with a checksum of c/xxx. The import repeats those
// steps (loader_cipher.hpp) and keeps the plain game program and the data
// files the program loads by name:
//
//   program.hunks           the game program as a plain load file (347 hunks)
//   <course>.mlb            tile maps, one per course (ByteRun1-packed)
//   <course>.ilb, *.vlb     image libraries (cells; ByteRun1-packed)
//   *Track / *track         course layouts (load files of data and code)
//   *Snd                    course sound (load files, chip-memory samples)
//   marbdat, defcop         marble data, the default copper list
//   splash.iff              the title picture the loader shows (c/splash)
//
// The release is recognised by the SHA-256 of the decrypted program, so other
// dumps of the same disk are accepted.
namespace marble_madness::import {

app::ImportResult import_disk(std::span<const std::uint8_t> image);

// The game's optional second file: a Kickstart ROM, from which the import
// keeps only the topaz 9 font the program draws its texts with
// (topaz9.font, amiga/rom_font.hpp). Without it Joyport's own letters
// stand in.
app::ImportResult import_kickstart(std::span<const std::uint8_t> rom);

} // namespace marble_madness::import
