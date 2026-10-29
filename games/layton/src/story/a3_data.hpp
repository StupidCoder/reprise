#pragma once
// The third adventure's story tables (rc/ge/lt3_rc/*), as the game reads them
// when the GAME loop starts (020dcf70 runs each file's rows through a table
// of row handlers, 020dd00c).
//
//  * lt3_evt (5401): every event - its folder and number (evt/GG/GG_IIIIII),
//    its name (scripts connect events by name), its program, sound set,
//    puzzle and memo;
//  * lt3_prg (5701, 5810..5813, 5910..5922): event programs - variants of
//    an event chosen by conditions, each starting the event's MAIN at a
//    label and changing the game state before it runs - and the story
//    phases (the goal shown to the player, what is set when a phase begins
//    and what completes it);
//  * lt3_plc (5101): places - number, name, title, sound set, map, guide;
//  * lt3_map (5201): the top-screen maps.
//
// Names are kept as texts; the game compares their hashes (0203b69c).
#include "graphics/a3_assets.hpp"
#include "system/a3_lscr.hpp"
#include <array>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace picarat::adventure3 {

// A row of commands stored in a table (5812/5813/5920/5921): an opcode and
// its operands.
struct TableCommand {
    int opcode = 0;
    std::vector<lscr::Value> operands;
};

struct EventInfo {
    int group = 0, id = 0;
    std::string name, program;
    int soundSet = -1; // -1: none (020ea198: +0x5f76)
    std::string puzzle, memo, extra;
};

// An event program's variant (5912 rows, 0x20 bytes in the game): chosen
// when its conditions (5920) hold, it starts the event's MAIN at `label`;
// `before` (5921) changes the game state as the event starts (020daaf8),
// `after` (5922) when it ends (020dab08).
struct ProgramVariant {
    std::string base, label;
    std::vector<TableCommand> conditions, before, after;
};

struct PlaceInfo {
    int id = 0;
    std::string name, title;
    int soundSet = -1;
    std::string map, guide;
};

struct MapInfo {
    int id = 0;
    std::string name, picture;
};

// A character (lt3_chr 5001 {id, name, animation, name plate, typing
// sound}; 020db818): its animation is chr/<animation>.cani, its name plate
// chr/?/<plate>.cimg.
struct CharacterInfo {
    int id = 0;
    std::string name, animation, plate;
    int typingSound = -1;
};

// Something the player collects (lt3_gmd 5521 {id, name, value, title,
// members...}; 020dc144): mysteries, memo pages, puzzles, modes, sets of
// them. `mask` is its kind by the name's prefix (＜ふかまるナゾ＞ 4,
// ＜ちょうさメモ＞ 2, ＜ナゾ＞ 8, ＜３つのモード＞ 0x10, ＜セット＞ 0x20,
// ＜乗り物到着＞ 0x40, ＜ゲーム＞ 0x80, others 1).
struct ItemInfo {
    int id = 0, mask = 1, value = 0;
    std::string name, title;
    std::vector<int> members; // ids (sets)
};

// A puzzle's catalogue row (lt3_nazo 3001; 020db524): number, label,
// title, record, type and its label, picarats for 0 / 1 / 2+ failures,
// title picture.
struct PuzzleInfo {
    int number = 0, type = 0;
    std::string label, title, record, typeLabel, titlePicture;
    std::array<int, 3> picarats{};
};

// A puzzle slot (lt3_nazo 3011, in order; 020db6f4): the game keeps its
// records by slot - 0..152 the story's puzzles, 153..167 the challenges,
// 168.. the downloadable ones. The place it is found at, the phase in
// which it becomes available again, and the item solving it brings.
struct PuzzleSlot {
    int number = -1;
    std::string place, phase, item;
};

// A guide map (lt3_gmp 5241 {id, name}, 5243 {place, x, y}..., 5244
// {phase, x, y, direction}...): where places and the current goal show.
struct GuideInfo {
    int id = 0;
    std::string name;
    std::map<std::string, std::array<int, 2>> places;
    std::map<std::string, std::array<int, 3>> goals;
};

// A story phase (5810..5813).
struct Phase {
    int id = 0;
    std::string name, save, goal;
    std::vector<TableCommand> actions;    // 5812: when the phase begins
    std::vector<TableCommand> conditions; // 5813: what completes it
};

class StoryData {
public:
    void load(Assets& assets);
    // The item kinds' name prefixes, read from overlay 6 (020dc2ec..).
    std::vector<std::pair<std::string, int>> itemPrefixes;

    const EventInfo* event(int id) const;
    const EventInfo* event(const std::string& name) const;
    const std::vector<ProgramVariant>* program(const std::string& name) const;
    const PlaceInfo* place(int id) const;
    const PlaceInfo* place(const std::string& name) const;

    const std::map<int, PlaceInfo>& places() const { return places_; }

    const MapInfo* map(int id) const;
    const MapInfo* map(const std::string& name) const;
    const Phase* phase(int id) const;
    const Phase* phase(const std::string& name) const;

    const std::vector<Phase>& phases() const { return phases_; }

    const CharacterInfo* character(const std::string& name) const;
    const ItemInfo* item(const std::string& name) const;
    const ItemInfo* item(int id) const;
    // A guide (lt3_gmp 5241 {id, name}) by name, -1 if none.
    int guide(const std::string& name) const;
    const GuideInfo* guideInfo(int id) const;
    const PuzzleInfo* puzzleInfo(int number) const;

    const std::vector<PuzzleSlot>& puzzleSlots() const { return slots_; }

    // The slot holding puzzle `number`, -1 if none.
    int slotOf(int number) const;
    // Program timings (lt3_prg 5701 {name, a, b, c}).
    const std::array<int, 3>* programTimings(const std::string& name) const;
    // A puzzle's number from its label (lt3_nazo 3001 {number, label});
    // labels and numbers both name puzzles in the scripts. -1 if none.
    int puzzle(const lscr::Value& id) const;

private:
    std::map<int, EventInfo> events_;
    std::map<std::string, int> eventsByName_;
    std::map<std::string, std::vector<ProgramVariant>> programs_;
    std::map<int, PlaceInfo> places_;
    std::map<std::string, int> placesByName_;
    std::map<int, MapInfo> maps_;
    std::map<std::string, int> mapsByName_;
    std::vector<Phase> phases_;
    std::map<std::string, int> guides_, puzzles_;
    std::map<std::string, CharacterInfo> characters_;
    std::map<int, ItemInfo> items_;
    std::map<int, GuideInfo> guideInfos_;
    std::map<int, PuzzleInfo> puzzleInfos_;
    std::vector<PuzzleSlot> slots_;
    std::map<std::string, std::array<int, 3>> timings_;
    std::map<std::string, int> itemsByName_;
};

} // namespace picarat::adventure3
