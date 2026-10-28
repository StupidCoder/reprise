#pragma once
#include "app/game_entry.hpp"
#include <cstdint>
#include <span>

// Importing Tower of Babel from the user's disk image (ADF).
//
// The disk has no file system (.analysis/tower_of_babel_amiga/disk.md): the
// boot block loads a packed program, and the program reads raw 512-byte
// blocks with its own track loader. The import unpacks the program
// (decruncher.hpp) and copies the block ranges the program loads:
//
//   program   memory $800-$193FF as the program sees it at its entry ($7A5E);
//             code, texts, menus, palettes and tables are read from it by
//             address ($18200 on is overwritten by `title` when it runs)
//   title     blocks $A9-$CA, loaded to $18200: the title logo and globe
//   main      blocks $CB-$333, loaded to $1C5F0: graphics, shapes, sound
//             waveforms, the jingle sample, every tower
//   panel1-3  blocks $334-$39F, three sets of $24 blocks, one of which the
//             options load to $23BC0 (the program calls this "Panel Style";
//             set 1 is the copy already inside `main`)
//   music     blocks $3A0-$537, loaded to $1C5F0 before `main`: the title
//             music, one long sample
//
// The release is recognised by the SHA-256 of the packed loader; every
// installed file is checked by its SHA-256.
namespace babel::import {

app::ImportResult import_disk(std::span<const std::uint8_t> image);

} // namespace babel::import
