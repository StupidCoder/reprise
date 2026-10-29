// Circle-the-answer touch input (ARM9 overlay 33, 020f7e8c..020f8004).
#include "puzzles/circle_input.hpp"
#include <algorithm>

namespace picarat {

CircleInkCommand CircleInput::update(CircleTouch t, std::span<const CircleRegion> regions,
                                     int maximumCenters, int markerHeight) {
    // Only the picture left of x = 187 is a drawing area.
    CircleInkCommand ink;
    if (t.phase == 1 || t.phase == 2) {
        if (t.initialX > 186) {
            return ink; // the stroke began outside the drawing area
        }
        const int x = std::min(t.x, 186);
        if (stroke.lastX < 0 || stroke.lastY < 0 || t.phase == 1) {
            // A new stroke replaces the previous one: clear the ink, draw a dot.
            stroke.reset();
            firstX = t.x;
            firstY = t.y;
            ink = {CircleInkCommand::Kind::Dot, true, firstX, firstY, 0, 0};
        } else {
            ink = {CircleInkCommand::Kind::Line, false, stroke.lastX, stroke.lastY, x, t.y};
        }
        stroke.lastX = std::min(t.x, 186);
        stroke.lastY = t.y;
        stroke.addPoint(x, t.y);
    } else if (t.phase == 3) {
        // Released: evaluate the stroke if it ended in the drawing area;
        // ending outside it spoils the stroke.
        stroke.lastX = stroke.lastY = -1;
        if (t.releaseX <= 186) {
            ink.clear = stroke.points.empty();
            stroke.evaluate(regions, maximumCenters, markerHeight);
        } else if (!stroke.points.empty()) {
            stroke.invalid = true;
        }
    }
    return ink;
}

} // namespace picarat
