#pragma once
// The third adventure's three save slots (the game data's slot table at
// +0x14, 0x80 bytes a slot, 020dacc8). What a slot shows on the save and
// load screens is its header, filled when the game is saved (020ddcc8):
//   +0x00 bit 0  the slot holds a game
//   +0x04 / +5   hours and minutes played (99:59 from 100 hours on)
//   +0x06 / +7   puzzles found / solved, story and challenge together
//   +0x08        0 not cleared; cleared: 1, 2 all 153 story puzzles solved,
//                3 also the 15 challenge puzzles ("ゲームクリアー")
//   +0x0c        the player's name; +0x2c the current place's title
// (script command 1690 reads these as fields 0, 1, 10 and 20..24).
//
// The record itself is Picarat's own format, not a DS save file: the game
// state's saved block, the puzzle records, the names and the time played.
#include "puzzles/a3_puzzle_loop.hpp"
#include <array>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace picarat::adventure3 {

struct SaveHeader {
    int hours = 0, minutes = 0, found = 0, solved = 0, cleared = 0;
    std::string name, place;
};

struct SaveRecord {
    SaveHeader header;
    std::vector<std::uint8_t> state;
    std::map<int, PuzzleRecord> puzzles;
    std::array<std::string, 2> names;
    std::uint64_t playFrames = 0;
};

// The slots, kept in files slot0..2 under a folder (none: in memory only).
// A damaged file reads as an empty slot. Beside them the app's checkpoint
// (index kCheckpoint, checkpoint.sav): the same record, written whenever a
// room is ready (a host convenience, not one of the game's slots).
class SaveSlots {
public:
    static constexpr int kCount = 3, kCheckpoint = 3;

    explicit SaveSlots(std::filesystem::path folder = {});

    const SaveRecord* slot(int index) const;
    bool write(int index, SaveRecord record);
    // Empties a slot and removes its file (the app's save management; the
    // game itself never erases a slot).
    bool erase(int index);

private:
    std::filesystem::path file(int index) const;

    std::filesystem::path folder_;
    std::array<std::optional<SaveRecord>, kCount + 1> slots_;
};

} // namespace picarat::adventure3
