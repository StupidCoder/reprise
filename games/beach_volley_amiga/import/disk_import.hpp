#pragma once
#include "app/game_entry.hpp"
#include <cstdint>
#include <span>

// Importing Beach Volley from the user's disk image (ADF).
//
// The disk is an AmigaDOS (OFS) volume, "Beach Volley" (game.md "Disk").
// Its startup-sequence runs `volley`, a small program that loads
// intro.code to $23000 and jumps to it; from there each part of the game
// loads the next through dos.library by name. The code files are stored
// as they run; every other file except `instruments` is crunched
// (amiga/backward_cruncher.hpp, framed by a trailer: first bit long,
// checksum, unpacked length).
//
// The import installs:
//
//   intro.code, volley.code, animation.code, score.code, final.code
//               the game's code files as stored (their tables and data are
//               read from them; data/assets.hpp names the load addresses)
//   instruments the sample bank, as stored
//   terrain1 … terrain8, music1 … music13, musicfinal, anim0 … anim7,
//   animation1, animation2, dnv0 … dnv7, carte, introp1, introp2, trophe,
//   score, final
//               every crunched file unpacked, under its name without ".cru"
//
// Not installed: `volley` (the starter), s/startup-sequence and the
// cracker's "Icon&Install.lha". The release is recognised by the SHA-256 of
// volley.code; every installed file is checked by the SHA-256 of its bytes.
namespace beach_volley::import {

app::ImportResult import_disk(std::span<const std::uint8_t> image);

} // namespace beach_volley::import
