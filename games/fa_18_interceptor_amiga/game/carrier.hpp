#pragma once
#include "amiga/hunk.hpp"
#include "fa_18_interceptor_amiga/game/aircraft.hpp"
#include <array>
#include <cstdint>
#include <vector>

// Free flight's one moving object: the aircraft carrier (game.md "The
// carrier"). Free flight's object list (`$11DFB2` → `$11E61C`) spawns it
// ($11D318) as record 14, type $20, shape type 8, at rest at the place of
// path $42 (`$11DDC0`). Its deck is an outline of its model's points at
// height 112 that the collision test ($11B6AA) checks the aircraft's nose
// and tail points against: over it the aircraft rolls on the deck
// (bits $40 / $80 of byte +4, the height in +$4E); in the arresting-wire
// zone, on the deck and heading along it, the wheel brake goes on. The
// deck is six convex outlines one after the other (each ending with bit 15
// on its last point, $FFFF after the last).
namespace interceptor::game {

class Carrier {
public:
    explicit Carrier(const amiga::LoadedImage& program);

    // 32-bit position (world units x 256) and body matrix ($4000 = 1).
    std::array<std::int32_t, 3> position{};
    std::array<std::int16_t, 9> body{0x4000, 0, 0, 0, 0x4000, 0, 0, 0, 0x4000};
    int shape_type = 8;
    int deck_height = 112; // world units (the type's data block `$12E340` +2)

    // $10D750: an aircraft at the start on the deck (place 4).
    Aircraft start_on_deck(const amiga::LoadedImage& program, const FlightTables& tables) const;
    // The carrier part of $11B6AA for the player, after the flight step:
    // the deck bits and height, the wires' brake; true when the aircraft
    // runs into the hull (a crash).
    bool touch(Aircraft& aircraft, const amiga::LoadedImage& program) const;

private:
    // The carrier's model points in world units relative to it (its
    // nearest level's model, shifted by its scale nibble).
    std::array<double, 2> point(int index) const;
    std::vector<std::array<int, 3>> model_;  // raw model points
    int shift_ = 3;
    std::vector<std::vector<int>> deck_;     // the deck's outlines: point indices
};

} // namespace interceptor::game
