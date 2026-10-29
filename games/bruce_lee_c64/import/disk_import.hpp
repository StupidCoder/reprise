#pragma once
#include "app/game_entry.hpp"
#include <cstdint>
#include <span>

// Importing Bruce Lee from the user's disk image (G64).
//
// The original disk loads in three encrypted stages (see disk_import.cpp and
// .analysis/bruce_lee_c64/disk.md). The import decodes the disk the way the
// loader does and keeps what the loader leaves in memory for the game:
//
//   game.prg     $0400  code and data, $0400-$A3FF
//   high.prg     $E000  code and data under the KERNAL ROM, $E000-$FFFF
//   loading.prg  $6000  the picture shown while the game loads (bitmap mode)
//
// The release is recognised by the SHA-256 of each decoded file, so other
// dumps of the same disk are accepted. The disk's copy protection (a block
// with weak bits on track 18) only decides whether the original loader goes
// on; it does not change the decoded data, so it is not required here.
namespace bruce_lee::import {

app::ImportResult import_disk(std::span<const std::uint8_t> image);

} // namespace bruce_lee::import
