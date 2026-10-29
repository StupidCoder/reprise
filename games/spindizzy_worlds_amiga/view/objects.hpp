#pragma once
#include "spindizzy_worlds_amiga/data/assets.hpp"
#include "spindizzy_worlds_amiga/view/sprites.hpp"
#include "spindizzy_worlds_amiga/world/level_builder.hpp"
#include <functional>
#include <vector>

// How a level's objects look when it starts: each kind's create routine
// (the table at $148C2) and frame handler, for the kinds rebuilt so far
// (jewels, exits, push buttons, binary switches, lifts, heads, Pac aliens,
// pyramids, boinks, spark generators). Their frames are
// read from main; the frame bases and offsets below are the kinds' own
// code constants.
namespace spindizzy::view {

struct ObjectSprite {
    int kind{};
    data::SpriteFrame frame;
    SpritePlace place;
    int order{}; // the sort value +$18 (with the kind's own offset)
};

// The run-time state the drawing depends on (until the objects' own
// behaviour is rebuilt): a jewel's animation step, a lift's height, an
// exit switched on.
struct ObjectState {
    int step{};          // +$1F
    int heading{-1};     // creatures: +$28 & 3; -1 = from the level
    int x{-1}, y{-1};    // creatures: +8, +$A; -1 = the level's start cell
    int z{-1};           // +$C in 1/256 block; -1 = the level's start height
    bool shown{};        // +$20: exits shown; spark generators fired (hidden)
    bool gone{};         // the slot was freed (a jewel taken) or never made
    bool popping{};      // a creature touched: its pop list ($147E2, pair +$10)
};

// Sprites sorted for drawing (sort value, then level order, as $1440E
// sorts the slots). By default jewels show their first spin step
// (the original staggers the steps by object slot); exits stay hidden
// until switched on.
std::vector<ObjectSprite> start_sprites(const world::Level&, const world::Switches&, const data::Memory& main,
                                        const std::function<ObjectState(std::size_t)>& state = {});

// A jewel's frame address at an animation step ($10058 over the per-type
// list at $14C44), for checks against the original's records.
std::uint32_t jewel_frame(const data::Memory& main, int type, int step);

// A frame offset from an animation list in main ($10058).
std::uint32_t animation_offset(const data::Memory& main, std::uint32_t list, int step);

// The explorer drawn among the objects in the original's order.
void draw_with_explorer(Canvas&, std::vector<ObjectSprite> sprites, const ObjectSprite& explorer, const TileMap&,
                        const data::Graphics&, int left, int top);

} // namespace spindizzy::view
