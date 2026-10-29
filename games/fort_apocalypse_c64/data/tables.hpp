#pragma once
#include "fort_apocalypse_c64/data/assets.hpp"
#include <array>
#include <cstdint>

// The game's rule tables and small graphics, read from the game file. The
// addresses are where the original code reads them (see tables.cpp); the
// meaning of each is documented with the code that uses it.
namespace fort_apocalypse::data {

// The three option values index these ($F4 gravity, $F6 pilot skill,
// $F8 robo pilots).
struct DifficultyTables {
    std::array<std::uint8_t, 3> gravity_mask{};   // $8DFC: sink when (frame & mask) == 0
    std::array<std::uint8_t, 3> enemy_mask{};     // $8DFE: enemy moves when (frame & mask) == 0
    std::array<std::uint8_t, 3> lives{};          // $8E01: pilots (decimal)
    std::array<std::uint8_t, 3> barrier_step{};   // $8E04: barrier timer step
    std::array<std::uint8_t, 3> last_mine{};      // $8E07: highest armed mine slot
    std::array<std::uint8_t, 3> tank_period{};    // $8E0A: main-loop passes per tank step
    std::array<std::uint8_t, 3> missile_period{}; // $8E0B: passes per missile step
    std::array<std::uint8_t, 3> sweep_period{};   // $8E0E: frames per sweeping-wall step
    std::array<std::int8_t, 3> rank_adjust{};     // $916B: debrief rank by gravity / pilot skill
};

// Where a level starts (indexed by level 0-2).
struct LevelStart {
    std::uint8_t multicolour{};                  // $9107
    std::uint8_t sprite_x{}, sprite_y{};         // $910A
    std::uint8_t camera_column{}, camera_row{};  // $9110
    std::array<std::uint8_t, 6> tank_column{};   // $911C / $9128 (object columns)
    std::array<std::uint8_t, 6> tank_row{};      // $9122 / $912E
    std::uint8_t enemy_colour{};                 // $9C4F
};

// A cavern drop point of the barrier teleport ($98DA, four of them).
struct DropPoint {
    std::uint8_t fine_x{}, fine_y{}, camera_column{}, camera_row{}, sprite_x{}, sprite_y{};
};

struct GameTables {
    DifficultyTables difficulty;
    std::array<LevelStart, 3> levels{};
    // Enemy helicopter patrol points ($9CBA columns, $9CCA rows): entries
    // 0-7 for level 0, 8-15 for level 1 (and 2, which draws from 0-7).
    std::array<std::uint8_t, 16> enemy_column{}, enemy_row{};
    std::array<DropPoint, 4> drop_points{};

    // Character graphics the game draws into its playfield set at run time.
    std::array<std::uint8_t, 32> barrier_pattern{}; // $8907: characters $01-$04 / $05-$08 lit
    Glyph barrier_cap_a{}, barrier_cap_b{};         // $891F, $8917: character $09
    Glyph static_pattern{};                         // $A927: characters $20 and $3F
    std::array<std::uint8_t, 32> shimmer{};         // $AF80: characters $4C-$4F, alternate

    // Character codes of the moving objects.
    std::array<std::uint8_t, 8> mine_characters{};    // $963C: four phase pairs
    std::array<std::uint8_t, 3> tank_characters{};    // $A469: body
    std::array<std::uint8_t, 22> obstacles{};         // $A45D: what tanks turn at
    std::array<std::uint8_t, 8> prisoner_characters{};// $AC12: torso pairs, leg sets
    std::array<std::uint8_t, 5> landing_characters{}; // $AC16: what the craft may land on
    std::array<std::uint8_t, 4> teleport_barriers{};  // $B264

    // Bullet steps by direction 1-5 ($A472, $A477), radar pixel masks
    // ($ADB5), the attract game's joystick ($AA4F, 108 values).
    std::array<std::int8_t, 6> bullet_dx{}, bullet_dy{};
    std::array<std::uint8_t, 8> radar_mask{};
    std::array<std::uint8_t, 108> attract_joystick{};

    // The fort explosion: four 6-byte bit patterns ($9875-$9889 through the
    // pointers at $988A), each bit a 3x3 block of explosion cells.
    std::array<std::array<std::uint8_t, 6>, 4> fort_explosion{};
    // The fuel depot drawings ($ACF8): stage s is 5 rows of 6 cells read from
    // offset 6 * s, rows 6 bytes apart (so neighbouring stages overlap).
    std::array<std::uint8_t, 54> depot{};

    // Messages and debrief texts.
    Text entering, vaults, crystalline, get_ready, pilots_left, men_to_rescue, low_on_fuel;
    Text high_score, mission, aborted, completed, your_rank, class_text;
    std::array<Text, 4> birds;
};

GameTables decode_tables(const c64::Program& game_file);

} // namespace fort_apocalypse::data
