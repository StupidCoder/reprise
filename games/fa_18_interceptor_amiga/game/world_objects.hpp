#pragma once
#include "amiga/hunk.hpp"
#include "fa_18_interceptor_amiga/game/aircraft.hpp"
#include <cstdint>
#include <optional>
#include <vector>

// The objects a mission puts into the world (game.md "The missions'
// objects"): each mode has an object list in the program (`$11DFB2`), per
// variant and per stage a run of 10-byte entries. An entry names the
// object's type record (shape), its type byte, the record it goes into,
// the path entry (`$11DDC0`) it starts at, and flags; its first word with
// bit 15 set or its record number tells where the object heads for. The
// spawner (`$11CF06`, `$11D318`) fills the records from them; the rest of
// the game runs them through the same object step as the player's.
namespace interceptor::game {

// A place the paths table holds and an object heads for (+$2C .. +$34):
// coarse cells x and z (<< 22), offsets (<< 8) and the height (<< 8).
struct Waypoint {
    std::int16_t cell_x{}, cell_z{}, fine_x{}, fine_z{};
    std::int32_t height{};
    bool none() const { return cell_x < 0; } // (+$2C -1)
};

// A record other than the player's.
struct WorldObject {
    Aircraft craft;
    int record{};                // 1 .. 15
    int shape_type{};            // index into the type records ($115F4C, 20 bytes each)
    std::uint8_t scale{};        // +$7D low nibble: its model's scale shift
    std::uint8_t group{};        // +$5E: records of one group never touch
    std::int8_t area{};          // +$5D: the trigger area that spawned it (-1: the list)
    std::uint8_t tag{};          // +$3A
    Waypoint target;             // +$2C
    std::uint8_t target_record{0xFF}; // +$38: $FF a point; $80 | n: record n's place
    std::uint8_t behaviour{};    // +$7A
    std::int16_t distance{};      // +$4A
    std::uint8_t pilot{};        // +$64
    std::uint8_t weapons{};      // +$63
    bool active() const { return craft.state & 0x0040; }
};

// The global choices the spawner reads.
struct ObjectChoice {
    int mode{};            // `$13AB96` (3-9; $7E, $7F: the demo's list, the tenth)
    int stage{};           // the configuration's level for the mode (`$10D034` +$12 + mode)
    int level{};           // `$13AB97`: entries up to this level
    int variant_random{};  // `$13ADE8`: the variant (bits 0-1) and the offsets' signs and sizes
    std::optional<int> again; // `$13AE0C` when the same mission is flown again (`$13AB1B`)
    std::int16_t offset_x{}, offset_z{}; // `$13AE08` / `$13AE0A` when again
};

class ObjectLists {
public:
    explicit ObjectLists(const amiga::LoadedImage& program);

    // `$11CF06`: the records a mission starts with, the first entry turned
    // towards its target (`$11CFE4`). The variant chosen is kept for
    // flying it again.
    struct Spawned {
        std::vector<WorldObject> objects;
        int variant{};
        std::int16_t offset_x{}, offset_z{};
        int aircraft_limit{}; // `$13AB9A`
    };
    Spawned spawn(const ObjectChoice& choice, const FlightTables& tables) const;

    // A path entry (`$11DDC0`, by its byte offset into the table).
    Waypoint path(int offset) const;

private:
    // `$11D318` for one entry; false when it was skipped.
    bool spawn_entry(std::uint32_t entry, int area, std::vector<WorldObject>& objects, int& aircraft,
                     int aircraft_limit, const ObjectChoice& choice, Spawned& out, const FlightTables& tables) const;

    const amiga::LoadedImage& program_;
};

} // namespace interceptor::game
