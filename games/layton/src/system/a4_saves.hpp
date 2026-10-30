#pragma once
// The fourth adventure's save slots.
//
// The game keeps a table of three slots (game data +0x5088, 0x80 bytes a
// slot) beside one game-data block per slot. Saving (ov7 020b8698) fills the
// slot's header from the game, then writes the block; loading (020b85d0)
// reads the block back. A header holds:
//   +0x00 bit 0  the slot holds a game
//   +0x04 / +5   hours and minutes played (99:59 from 100 hours on)
//   +0x06 / +7   puzzles found / solved, story and challenge together
//   +0x08        0 not finished; finished (flag "ゲームクリアー"): 1, 2 all
//                155 story puzzles solved, 3 also the 15 challenge puzzles
//   +0x0c        the player's name; +0x38 the current place's title
// Script service 3051 reads these as fields 0, 1, 10 and 20..24. The table
// also has flags of its own (020d5d70 / 020d5d98); flag 0 is set once a
// finished game exists (020b8978: the "ゲームクリアー" flag, or any slot
// with a finished header), which the title and the epilogue read (3050).
//
// The files are Picarat's own format, not a DS save: the header and the
// game data the story keeps (flags, variables, marks, puzzle progress,
// names, play time, the memo's drawing).
#include "system/a4_services.hpp"
#include <array>
#include <filesystem>
#include <optional>
#include <string>

namespace picarat::adventure4 {

struct SaveHeader {
    int hours = 0, minutes = 0, found = 0, solved = 0, cleared = 0;
    std::string name, place;
};

// What a slot keeps of the game data.
struct SaveRecord {
    SaveHeader header;
    std::map<std::string, int> flags, variables, marks;
    std::map<int, GameData::Progress> puzzles;
    std::map<int, std::string> names;
    std::uint64_t playFrames = 0;
    std::vector<std::uint16_t> memo;
    std::vector<int> passedBy;
};

// The slots in files slot0..2.sav and the table's flags in table.sav under
// a folder (none: in memory only). A damaged file reads as an empty slot.
class SaveSlots {
public:
    static constexpr int kCount = 3;

    explicit SaveSlots(std::filesystem::path folder = {});

    const SaveRecord* slot(int index) const;
    bool write(int index, SaveRecord record);
    bool erase(int index);

    // The table's flags (020d5d70 reads one, 020d5d98 sets one).
    bool flag(int index) const { return index >= 0 && index < 32 && (flags_ >> index & 1u); }

    void setFlag(int index, bool on);

private:
    std::filesystem::path file(int index) const;
    void writeTable() const;

    std::filesystem::path folder_;
    std::array<std::optional<SaveRecord>, kCount> slots_;
    std::uint32_t flags_ = 0;
};

// 020b8698's header for the game as it stands: play time (from the frames
// counted at 59.8261 Hz), the puzzle counts, the finished level, the name
// and the place's title.
SaveHeader saveHeader(Assets& assets, const GameData& data);

// The record of the game data / the game data from a record.
SaveRecord saveRecord(Assets& assets, const GameData& data);
void restore(GameData& data, const SaveRecord& record);

// The slot table as the scripts read it (GameData::slots, 3051).
void showSlots(GameData& data, const SaveSlots& slots);

// The app's checkpoint (a host convenience, not the original's): the slot
// record of the game as it stands when a room is ready, and the slot the
// game was last saved to or loaded from (where the bag saves).
struct Checkpoint {
    SaveRecord record;
    int slot = -1;
};

bool writeCheckpoint(const std::filesystem::path& file, const Checkpoint& checkpoint);
// None for a missing, damaged or foreign file.
std::optional<Checkpoint> readCheckpoint(const std::filesystem::path& file);

} // namespace picarat::adventure4
