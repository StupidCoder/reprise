#pragma once
#include "cannon_fodder_amiga/data/assets.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

// The objects of a mission: soldiers, enemies, buildings, trees, animals,
// shots... (.analysis/cannon_fodder_amiga/game.md "Objects").
//
// The original keeps them in 44 slots of 118 bytes at $FDB2C (a 45th marks
// the end; free slots start with $8000, $A3976). Only the fields the
// rebuilt rules use so far are named here; more follow with the logic.
namespace fodder::game {

constexpr int object_slots = 44;
// The scans that look at one object a tick ($98E64, $98EFE, $967B8) run
// through the first 43 ($13D2 / $76) and start again.
constexpr int scanned_slots = 43;

struct GameObject {
    bool used{};
    std::int32_t x{}, y{}; // 16.16 map position (+$00, +$04): the spawner puts x 16 right of the placement
    int graphic{};         // frame group in the program's tables (+$08)
    int frame{};           // frame within the group (+$0A)
    int height{};          // drawn above the ground by this much (+$1E, hypothesis)
    int kind{};            // the placement's type (+$18): 0 soldier, 5 enemy soldier, ...
    int side{};            // 0 player, 1 enemy (+$22; the first update sets it)
    int home_x{}, home_y{}; // +$26, +$28: the placement
    int squad{};           // +$32: player soldiers before this one / 8
    int start_delay{};     // +$4A: 0 for soldiers, 20, 30, 40 ... for the others
    int hidden_lines{};    // +$52: lines cut from the bottom when drawn
    int layer{};           // +$2C: < 0 drawn first, 0 sorted by y, > 0 drawn last
    bool removed{};        // +$24: freed when the next draw list is built
    bool on_screen{};      // +$5C: in the last draw list
};

struct MissionObjects {
    std::array<GameObject, object_slots> slots{};
    int enemies{};    // $81F5E: placements of kind 5 and $24
    int kind_6a_48{}; // $81F60: placements of kind $6A and $48 (meaning open)
    // A search for a free slot that found none blocks every later search of
    // its kind until an object is removed through +$24 ($AB8B4 clears both).
    bool full{};          // $826EA: all 43 slots ($98104)
    bool full_below_30{}; // $826EC: slots 0-29 ($9819A)
};

// The spawner ($8A594): one slot per placement, in file order. Every slot
// starts with graphic $7C (no picture until its first update), x + 16,
// the placement copied to home; kind $24 is marked as an enemy at once.
MissionObjects spawn_objects(const std::vector<data::Placement>& placements);

// A frame of an object picture: where it is in its sheet and how it sits
// relative to the object ($81FEA table in the program, 16 bytes each).
struct FrameInfo {
    int sheet{};          // 0 = the landscape's army sheet, 1 = its vehicle sheet ("copt")
    int sheet_x{}, sheet_y{};
    int width_words{};    // the drawn width is (width_words - 1) x 16 pixels
    int height{};
    int offset_x{}, offset_y{};
};

class FrameTables {
public:
    // From the installed program file `fodderc` (table at $8DCDA, groups
    // of 16-byte entries; sheets load to $57F60 (army) and $3294A (copt)).
    explicit FrameTables(std::span<const std::uint8_t> program_file);

    std::optional<FrameInfo> frame(int graphic, int frame) const;
    // Number of groups in the table (graphic ids 0 .. groups()-1).
    int groups() const { return int(groups_.size()); }

private:
    std::vector<std::uint8_t> program_;
    std::vector<std::uint32_t> groups_; // group offsets from the program start (unrelocated)
};

} // namespace fodder::game
