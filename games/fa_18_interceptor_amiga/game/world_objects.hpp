#pragma once
#include "amiga/hunk.hpp"
#include "fa_18_interceptor_amiga/game/autopilot.hpp"
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

// A record other than the player's.
struct WorldObject {
    Aircraft craft;
    int record{};                // 1 .. 15
    int shape_type{};            // index into the type records ($115F4C, 20 bytes each)
    std::uint8_t scale{};        // +$7D low nibble: its model's scale shift
    std::uint8_t group{};        // +$5E: records of one group never touch
    std::int8_t area{};          // +$5D: the trigger area that spawned it (-1: the list)
    Course course;               // +$2C .. +$38, +$3A, +$4A, +$7A
    std::uint8_t pilot{};        // +$64
    std::uint8_t weapons{};      // +$63
    std::uint8_t damage{};       // +$3C: rounds taken (low nibble)
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
        int aircraft{};       // `$13AB99`: the aircraft spawned that count towards it
        std::uint32_t first_entry{}; // `$13AA2A`: the stage's first entry
    };
    Spawned spawn(const ObjectChoice& choice, const FlightTables& tables) const;

    // A path entry (`$11DDC0`, by its byte offset into the table).
    Waypoint path(int offset) const;

    // The trigger areas (`$11DF00`, up to the negative word): a box of
    // cells (x and z, inclusive) and the entries it spawns.
    struct TriggerArea {
        std::int16_t min_x{}, max_x{}, min_z{}, max_z{};
        std::uint32_t entries{};
        int count{};
        bool contains(std::int16_t x, std::int16_t z) const {
            return x >= min_x && x <= max_x && z >= min_z && z <= max_z;
        }
    };
    const std::vector<TriggerArea>& areas() const { return areas_; }
    const amiga::LoadedImage& program() const { return program_; }
    // `$11D2FA`: an area's entries into the records (missions only: not in
    // modes up to 2 nor the practice's $7D), not turned. `index` is the
    // area walk's bit number; the result is what the spawner leaves in that
    // register (game.md "The trigger areas"): `index` when nothing is
    // tried, else 0 when the last entry was spawned, -1 when skipped.
    int spawn_area(const TriggerArea& area, int index, std::vector<WorldObject>& objects, int& aircraft,
                   int aircraft_limit, const ObjectChoice& choice, Spawned& out, const FlightTables& tables) const;
    // `$1181E6`: the path offset of a record's entry, searched from `from`
    // up to a negative word.
    std::optional<int> entry_place(std::uint32_t from, int record) const;

private:
    // `$11D318` for one entry into its record (replacing the record's
    // last use); null when it was skipped.
    WorldObject* spawn_entry(std::uint32_t entry, int area, std::vector<WorldObject>& objects, int& aircraft,
                     int aircraft_limit, const ObjectChoice& choice, Spawned& out, const FlightTables& tables) const;

    const amiga::LoadedImage& program_;
    std::vector<TriggerArea> areas_;
};

} // namespace interceptor::game
