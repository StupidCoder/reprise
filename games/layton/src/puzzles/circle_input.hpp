#pragma once
// Circle the answer (puzzle type 5, ARM9 overlay 33): touch input and ink.
#include "puzzles/circle_stroke.hpp"

namespace picarat {

// The touch state as the game's input service keeps it (overlay 2 context
// fields +00 phase, +10/+14 current position, +20 release x, +28 press x).
// Phases: 1 pressed, 2 held, 3 released.
struct CircleTouch {
    int phase = 0, x = -1, y = -1, releaseX = -1, initialX = -1;
};

// A drawing command for the ink layer: a dot or a line, optionally after
// clearing the layer.
struct CircleInkCommand {
    enum class Kind { None, Dot, Line };
    Kind kind = Kind::None;
    bool clear = false;
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};

// Input handling for a picture with one region group (020f7e8c..020f8004).
// The common puzzle buttons and the arrows of multi-picture puzzles are
// handled before this point by the puzzle scene.
struct CircleInput {
    CircleStroke stroke;
    int firstX = 0, firstY = 0; // first point of the current stroke

    // One frame of input; returns the ink to draw.
    CircleInkCommand update(CircleTouch touch, std::span<const CircleRegion> regions,
                            int maximumCenters, int markerHeight);
};

} // namespace picarat
