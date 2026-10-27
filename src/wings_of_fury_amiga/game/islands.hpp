#pragma once
#include "amiga/hunk.hpp"
#include "wings_of_fury_amiga/data/assets.hpp"
#include "wings_of_fury_amiga/game/scenery.hpp"
#include <array>
#include <cstdint>
#include <vector>

// The enemy on the islands: huts and dugouts with five soldiers each, the
// dugouts' anti-aircraft guns, pillboxes, and the soldiers who run out when
// a hut or dugout is hit. Found on the map as a mission starts ($102DE8);
// stepped by the tick ($101DE4: soldiers leaving) and by each drawn frame
// ($103D78 the dugouts' guns, $103DE8 the pillboxes, $103EEE the soldiers);
// hit by blasts ($1046DC, $101A84). An island is taken when all its
// soldiers and pillboxes are gone: a bonus, and once every island and ship
// is done, the mission is accomplished.
namespace wings::game {

// A hut or a dugout ($103216 / $1031C8, 16 bytes).
struct Shelter {
    bool dugout{};
    int offset{};        // +0: the drawn column's map byte offset
    int left_x{}, right_x{}; // +4, +6: where soldiers come out (running left / right)
    int inside{5};       // +8: soldiers inside (a dugout's: its gun crew)
    int island{};        // +9
    int leaving{};       // +$A: soldiers still to run out
    int leave_timer{};   // +$B: frames.. ticks to the next one
    int gun_delay{};     // +$C: a dugout's new crew waits this long before firing; a wrecked hut's puffs of smoke left
    int refill{};        // +$E: an empty dugout calls for a soldier when this runs out; a wrecked hut's frames to the next puff
};

// A pillbox ($103236, $E bytes; none on the first mission's map).
struct Pillbox {
    int offset{};        // +0
    int island{};        // +6
    bool destroyed{};    // +8 = $FFFF
    int smoke_left{};    // +$A: puffs of smoke still to come
    int smoke_timer{};   // +$C
};

// A soldier (list -$5AFE, 8 bytes).
struct Soldier {
    int x{};             // +0
    int direction{};     // +2 (signed byte): sign = running left (-) or right
    int step{};          // +3: running 0..4, dying 5..7, dead 8
    int timer{};         // +4: frames per dying step
    int island{};        // +5
    int state{};         // +6: 0 none, 1 running, 2 dying, 3 dead
};

struct IslandTargets {
    int soldiers{};      // -$5BAE + 4i: alive, inside or out
    int pillboxes{};     //            + 2
    int first_dugout{}, last_dugout{}; // -$5BBE + 4i: the outermost dugouts' offsets
};

// `wings` data the islands read.
struct IslandTables {
    std::array<int, 7> missions_per_rank{};   // -$5AB6
    std::vector<int> bonus_row;               // -$7B9F: by rank x 4 + mission
    std::vector<int> bonus;                   // -$7B82: row x 4 + island (words)
    std::array<int, 12> smoke_shift{};        // -$640E: a smoke puff's x shift while turning
    std::array<std::uint8_t, 40> damage_smoke{}; // -$5A34: when a damaged plane smokes
};
IslandTables read_island_tables(const amiga::LoadedImage& wings);

struct Islands {
    std::vector<Shelter> huts, dugouts;
    std::vector<Pillbox> pillboxes;
    std::vector<Soldier> soldiers;
    std::vector<IslandTargets> targets;  // by island
    int islands_left{};   // -$5C7B: islands with targets still to take
    int ships_left{};     // -$5C8D: enemy ships still to sink
    int soldiers_killed{};// -$42C2
    // The screen flash a hit makes (-$5BE8 frames, -$5BE6 the colour).
    int flash_frames{};
    int flash_colour{};
    // The anti-aircraft guns heard this frame (-$3BAA) and the nearest one's
    // distance (-$3BA8); at the frame's end the sound's request (-$3E9A,
    // -$3E98: louder the nearer).
    bool guns_heard{};
    int gun_distance{0x2710};
    bool gun_sound{};
    int gun_volume{};
    // What this frame draws: the dugouts' guns (with the scenery, before the
    // waves) and the soldiers (after the smoke).
    struct Sprite {
        int entry{}, x{}, y{};
        bool gun{};
    };
    std::vector<Sprite> sprites;
};

// $102DE8: the islands' targets on a map.
Islands find_islands(const data::MissionMap&);

} // namespace wings::game
