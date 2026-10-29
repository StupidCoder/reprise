#pragma once
#include "app/game_entry.hpp"
#include <cstdint>
#include <span>

// Importing Wizball from the user's disk image (G64).
//
// The disk is a CBM DOS disk with three files (.analysis/wizball_c64/disk.md):
// "LOADER" (loads at $0314 over the KERNAL vectors, so it starts itself),
// "F" (a fast loader at $4000) and "WIZBALL", the whole game, $0400-$FD00.
// The fast loader reads "WIZBALL" by its ordinary sector chain and stores
// it unchanged, with all RAM switched in; the game starts at $6389. So the
// import keeps that one file:
//
//   wizball.prg  $0400  the game: code, levels, charsets, sprites, sound
//
// The release is recognised by the SHA-256 of the file's bytes, so other
// dumps of the same disk are accepted. The copy protection (the loader asks
// the drive for sector 22/10, whose data checksum is wrong on the original,
// and goes on only when that read fails) does not change the file, so it is
// not required here.
namespace wizball::import {

app::ImportResult import_disk(std::span<const std::uint8_t> image);

} // namespace wizball::import
