#pragma once
#include "spindizzy_worlds_amiga/data/assets.hpp"
#include "spindizzy_worlds_amiga/play/explorer.hpp"
#include "spindizzy_worlds_amiga/play/objects.hpp"
#include "spindizzy_worlds_amiga/play/surfaces.hpp"
#include "spindizzy_worlds_amiga/view/camera.hpp"
#include "spindizzy_worlds_amiga/view/starfield.hpp"
#include "spindizzy_worlds_amiga/world/level_builder.hpp"
#include <array>
#include <cstdint>
#include <optional>

// One level being played ($11F72 starts it, the loop $112F6 runs it) and
// the state that outlives it while a world is played ($2064 … $3620(a5)).
namespace spindizzy::play {

enum class ExplorerPhase { playing, arriving, lost, out_of_fuel };

// An object's animation-list stepper ($10058): +$1E counts the current
// entry's ticks down, +$1F is the entry; a new list starts with the count
// at 1, so its first entry is passed at once.
struct ListStepper {
    int count{1};
    int entry{};
    // One drawn frame (slow mode: one count per frame); the frame offset.
    std::uint32_t advance(const data::Memory& main, std::uint32_t list, bool slow = true);
};

// Fuel costs per difficulty step ($10E5E from the tables at $10DFE …
// $10E16): the bonus for a level's first visit, a death's cost, an alien's.
struct FuelCosts {
    std::int16_t first_visit{}; // $205C
    std::int16_t unknown{};     // $205E (not read in the code traced)
    std::int16_t death{};       // $2060
    std::int16_t alien{};       // $2062
};

// What a world's play keeps from level to level.
struct Progress {
    int world{};                      // $251C
    int level{1};                     // $251E
    std::int32_t fuel{0x80 << 16};    // $2064, 16.16 ($100 fills both pillars)
    int fuel_shown{};                 // $2072: the gauge's rows, one step a frame toward the fuel
    std::int32_t fuel_change{};       // $206C, taken in once a frame
    std::int32_t drain{};             // $2068 per frame
    std::int32_t reserve{};           // $2074
    std::int32_t reserve_at_start{};  // $2078
    std::int16_t jewel_fuel{};        // $2070
    FuelCosts costs;
    int difficulty{};                 // $207C: worlds done × 2 ($36B0)
    std::array<std::uint32_t, 16> level_jewels{}; // $2524: per level, bit 31 = not visited yet
    std::uint32_t jewels{};           // $2566: the current level's
    int jewels_found{};               // $2564: jewels seen in the world's visited levels
    int jewels_taken{};               // $2565
    world::Switches switches;
    std::uint16_t timer{};            // $360E
    bool timer_expired{};             // $2492
    std::uint32_t clues_left{0xFFFFFFFF}; // $3610: bit n = clue n not shown yet
    int rotation{};                   // $248B
    std::uint32_t random{};           // $2094: the generator $112CA (stars, Pacs)
    std::int16_t turn_side{2};        // $15618: the Pacs' turn toggle, kept in the code from boot
    std::uint16_t arrival_place{};    // $24C0
    bool game_over{};                 // $2490
    Explorer explorer;

    // $16D44 for a world's start and $10E1E (the difficulty's fuel costs).
    void start_world(const data::Memory& main, int number, int difficulty);
};

// A clue or message to show over the playfield ($15FC0): the game waits.
struct ShownClue {
    int number;
};

class LevelPlay {
public:
    // $11F72: builds `progress.level` of the world, creates the explorer
    // (kind −4) and the objects, places the explorer ($12714).
    LevelPlay(const data::Assets&, const SurfaceTable&, Progress&);

    // One rules tick ($11510): the explorer, the objects, the rest of the
    // explorer's tick.
    void tick(std::uint8_t joystick);
    // The space bar ($11314): stops dead when allowed.
    void brake();
    // Once per drawn frame after the ticks: the camera ($14138), fuel
    // ($10E96), the frame handlers' steps, the condition re-test and
    // rebuild ($162FE).
    void frame(bool slow = true);
    // The scroll position (it decides which sprites animate, so it is
    // part of the game's state).
    const view::Camera& camera() const { return camera_; }

    // The level loop ended ($2498 = 0): an exit or timer chose the next
    // level (`progress.level`; 0 = the world is done), or the game is over.
    bool ended() const { return !running_; }
    // A clue to show now (once).
    std::optional<ShownClue> take_clue();
    // Header bit 12 ("Add reserve fuel"): kind −9 shows the bonus-level
    // notice ($13888) on its first frame and waits for fire ($1386E).
    bool take_notice() { const bool n = notice_; notice_ = false; return n; }

    const world::Level& level() const { return level_; }
    const world::Switches& switches() const { return progress_.switches; }
    std::uint16_t level_flags() const { return level_flags_; }
    const Explorer& explorer() const { return progress_.explorer; }
    ExplorerPhase phase() const { return phase_; }
    const LevelObjects& objects() const { return *objects_; }
    // The starfield made at the level's start ($15AFC; seed 0 draws from
    // the running generator).
    const std::vector<view::Star>& stars() const { return stars_; }
    const Progress& progress() const { return progress_; }
    // Changes each time the level was rebuilt (the tile map must follow).
    int build_count() const { return builds_; }

    // The explorer's frame address this frame (call once per drawn frame:
    // it steps the animations like the original's frame handlers).
    std::uint32_t explorer_frame(const data::Memory& main, bool slow = true);

private:
    ObjectWorld object_world();
    void place_explorer(std::uint16_t place);
    void arrive();
    void die(const Death&);
    void show_clue(int number, bool always);
    void rebuild();

    const data::Assets& assets_;
    const SurfaceTable& surfaces_;
    Progress& progress_;
    world::Level level_;
    data::LevelHeader header_;
    std::uint16_t level_flags_{};
    std::optional<LevelObjects> objects_;
    std::vector<bool> condition_results_; // $3660: each recorded condition when built
    ExplorerPhase phase_{ExplorerPhase::playing};
    int phase_ticks_{};  // +$2C of the lost/arriving/dying kinds
    ListStepper stepper_;
    int spin_{0x100};    // +$1E of the explorer kind
    bool flash_{};       // +$21 bit 0
    bool running_{true}; // $2498
    bool moved_{};
    bool notice_{};
    view::Camera camera_;
    std::vector<view::Star> stars_;
    std::optional<ShownClue> clue_;
    int builds_{};
};

} // namespace spindizzy::play
