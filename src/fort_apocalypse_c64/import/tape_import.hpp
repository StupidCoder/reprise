#pragma once
#include "app/game_entry.hpp"
#include <cstdint>
#include <span>

// Importing Fort Apocalypse from the user's tape image.
//
// The tape holds a KERNAL-format boot program ("FORT", a BASIC line that
// starts the fast loader) and one fast loader stream with the loading screen
// program ($E000-$E6FF and $EE00-$F1FF) and the game ($7000-$B8FF). The
// import keeps these four programs as PRG files:
//
//   boot.prg       $0801  boot program with the fast loader's setup
//   loader-e000.prg $E000 loading screen: code, display script
//   loader-ee00.prg $EE00 loading screen: scroll text, music stream
//   game.prg       $7000  the game
//
// The release is recognised by the SHA-256 of each decoded program, so other
// recordings of the same tape are accepted.
namespace fort_apocalypse::import {

app::ImportResult import_tape(std::span<const std::uint8_t> image);

} // namespace fort_apocalypse::import
