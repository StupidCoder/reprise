#pragma once
#include "app/game_entry.hpp"
#include <cstdint>
#include <span>

// Importing F/A-18 Interceptor from the user's disk image (ADF).
//
// The disk is an ordinary AmigaDOS (OFS) volume, "F-18 Interceptor"
// (game.md, "Disk and program"). The import copies the files
// the program opens, each checked by its SHA-256:
//
//   program        the load file "F-18 Interceptor" (code, tables, shapes,
//                  texts and fonts; read by address, data/assets.hpp)
//   pix/splsh      title picture (IFF ILBM, 320 x 200, 32 colours), $1003CE
//   pix/inst5      instrument panel (IFF ILBM, 320 x 55)
//   pix/frnt5      a 288 x 12 strip (IFF ILBM; its use is not traced yet)
//   text/text*     sounds: raw signed 8-bit samples loaded whole into the
//                  sound slots ($1458A6); the names are the original's
//   text/textply   the pilot log as shipped (written back by "update log")
//   text/textctl   the log's second file as shipped
//   config         the settings file as shipped (78 bytes)
//
// The release is recognised by the SHA-256 of the program.
namespace interceptor::import {

app::ImportResult import_disk(std::span<const std::uint8_t> image);

} // namespace interceptor::import
