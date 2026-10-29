#pragma once
// Circle the answer (puzzle type 5, ARM9 overlay 33): the puzzle's data.
//
// How the puzzle plays
// --------------------
// The player draws a loop around the answer in the picture, for example
// around one item among several. Each candidate item is a "region" with a
// centre point; the script says which regions are correct. When the stroke
// ends, the regions whose centres lie inside the stroke's bounding box count
// as circled (see circle_stroke.hpp). A marker then appears above the first
// circled region, and "Submit" checks the choice.
//
// Some puzzles have several pictures, each with its own group of regions
// (opcode 0x3e starts the next group), which the player pages through.
#include "system/script.hpp"

namespace picarat {

// One candidate in the picture.
struct CircleRegion {
    std::int32_t x, y; // centre on the lower screen
    // A size parameter stored as a 32-bit float. What it means (a radius?)
    // is not established; the recovered checks do not use it.
    float extent;
    bool answer; // circling this region is correct
};

struct CircleParameters {
    std::uint16_t drawingColor = 0;                     // ink colour, RGB555 (opcode 0x0c)
    std::vector<CircleRegion> regions;                  // group 0
    std::vector<std::vector<CircleRegion>> extraGroups; // groups 1..3
    int maximumCenters = 1; // at most this many regions may be circled at once

    unsigned groupCount() const { return 1 + unsigned(extraGroups.size()); }

    const std::vector<CircleRegion>& group(unsigned index) const {
        return index ? extraGroups.at(index - 1) : regions;
    }

    explicit CircleParameters(std::span<const std::uint8_t> script);
};

} // namespace picarat
