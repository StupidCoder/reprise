#pragma once
#include "app/game_entry.hpp"
#include <cstdint>
#include <span>

// Importing Mind Walker from the user's disk image (ADF).
//
// The disk is an ordinary AmigaDOS (OFS) disk, volume "MWDisk", without
// protection (.analysis/mindwalker_amiga/disk.md). Its loader "Mind Walker"
// runs the game as separate programs, one stage at a time; their behaviour is
// rebuilt natively, so only their data is needed. The import keeps:
//
//   MindWlkS, MindWlkT, MindWlkA, MindWlkB, MindWlkC, MindWlkP, MindWlkE
//                    the stage programs (load files; images, palettes, texts,
//                    tables and sound samples are read from them)
//   MindB            the title picture
//   MindF1-MindF6    the six portraits
//   MindHighs        the high-score table as found on the disk
//   system-configuration  Workbench's preferences (devs/): the mouse pointer
//
// The release is recognised by the SHA-256 of each stage program. MindB and
// MindHighs are files the game writes, so any copy of them is accepted.
namespace mindwalker::import {

app::ImportResult import_disk(std::span<const std::uint8_t> image);

} // namespace mindwalker::import
