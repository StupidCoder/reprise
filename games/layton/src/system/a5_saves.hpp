#pragma once
// The fifth adventure's save slots.
//
// The game keeps three slot records (game +0x6e90, 0x60 bytes each) beside
// the progress block (game +0x67f8, 0x866c bytes) that a save writes out
// whole (10101 -> 0x2c41d8, a background job 0x333334 -> 0x332710) and a
// load reads back (10100 -> 0x332df4). The slot record (0x2acb98) holds:
//   +0x00  the player's name                       (16010 field 0)
//   +0x30  1: the slot holds a game                (field 1)
//   +0x34  finished: 0 no; with ゲームクリアー set 1 while とけたナゾ数 is
//          under 135 (150 in the DX version), else 2 while
//          挑戦状とけたナゾ数 is under 15, else 3      (field 2)
//   +0x38  seconds played                          (field 3)
//   +0x3c  総取得ピカラット                          (field 4)
//   +0x40  現在のフェーズ                            (field 5)
//   +0x44  現在のプレイス (a place id)               (field 6)
//   +0x48  みつけたナゾ総数, +0x4c とけたナゾ総数      (fields 7, 8)
//   +0x50  ひらめきコイン取得数, +0x54 所持数          (fields 9, 10)
//   +0x58  the flag 過去                            (field 11)
// A failed save or load sets bit 0 of game +0x197e0 (16001 reads it).
//
// The files are Picarat's own format, not a 3DS save: the slot record and
// the progress the port keeps (flags, the variable block, puzzle progress,
// the puzzles passed by, the names, the ruins' record, the rabbit's record
// (a trailing section; files from before it read as an untouched rabbit).
#include "system/a5_game.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace picarat::adventure5 {

struct SaveHeader {
    std::string name;
    int cleared = 0, seconds = 0, picarats = 0, phase = 0, place = 0;
    int found = 0, solved = 0, coinsFound = 0, coinsHeld = 0, past = 0;
};

// What a slot keeps of the game.
struct SaveRecord {
    SaveHeader header;
    std::vector<int> flags;
    std::vector<std::uint8_t> variables;
    std::map<int, Game::PuzzleProgress> progress;
    std::vector<int> passedBy;
    std::array<std::string, 4> names;
    Game::RuinRecord ruins;
    Game::RabbitRecord rabbit;
};

// The slots in files slot0..2.sav under a folder (none: in memory only).
// A damaged file reads as an empty slot.
class SaveSlots {
public:
    static constexpr int kCount = 3;

    explicit SaveSlots(std::filesystem::path folder = {});

    const SaveRecord* slot(int index) const;
    bool write(int index, SaveRecord record);

private:
    std::filesystem::path file(int index) const;

    std::filesystem::path folder_;
    std::array<std::optional<SaveRecord>, kCount> slots_;
};

// 0x2acb98's slot record for the game as it stands.
SaveHeader saveHeader(const Game& game);
// The record of the game / the game from a record (play time included).
SaveRecord saveRecord(const Game& game);
void restore(Game& game, const SaveRecord& record);
// A slot record's field as 16010 reads it (0 the name is text: see there).
int headerField(const SaveHeader& header, int field);

// The app's checkpoint (a host convenience, not one of the game's slots): the
// same record in a file of its own (Story::checkpointFile). A damaged or
// missing file reads as none.
bool writeCheckpoint(const std::filesystem::path& file, const SaveRecord& record);
std::optional<SaveRecord> readCheckpoint(const std::filesystem::path& file);

} // namespace picarat::adventure5
