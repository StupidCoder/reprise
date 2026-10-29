#pragma once
// Circle the answer (puzzle type 5, ARM9 overlay 33): one stroke and its
// evaluation. See circle_parameters.hpp for how the puzzle plays.
//
// A stroke's points are recorded while the stylus is down. When it ends, the
// stroke counts as circling the regions whose centres lie inside its
// bounding box. The stroke is invalid when it circles no region, more
// regions than allowed, or is too small (its bounding box diagonal is under
// 10 pixels). The stroke does not need to be closed.
//
// Sampling the touch screen and drawing the ink are separate services
// (circle_input.hpp, circle_ink.hpp).
#include "puzzles/circle_parameters.hpp"
#include <utility>

namespace picarat {

struct CircleStroke {
    std::vector<std::pair<std::int32_t, std::int32_t>> points; // at most 256, no duplicates
    // Marker position above the first circled region (-1: none).
    std::int32_t markerX = -1, markerY = -1;
    std::int32_t lastX = -1, lastY = -1; // previous ink point (-1: no stroke in progress)
    bool answer = false;                 // a correct region is circled
    bool invalid = false;                // the last stroke was rejected

    // Starts a new stroke (020f7cec). The previous answer flag is kept.
    void reset();

    // Records a point of the stroke (020f83cc).
    void addPoint(std::int32_t x, std::int32_t y);

    // Evaluates the finished stroke (020f8194). `markerHeight` is the height
    // of the marker sprite's current image ("point_trace"), so the marker
    // sits above the region's centre.
    void evaluate(std::span<const CircleRegion> regions, std::int32_t maximumCenters,
                  std::int32_t markerHeight);

    // The answer check (020f7d20).
    bool solved() const;
};

} // namespace picarat
