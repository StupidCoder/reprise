// The path the player draws for the mouse (the original's class Path, embedded in Player).
//
// Points arrive from the finger, are re-spaced every 8 units ("resampled") and smoothed with a
// [1 2 4 2 1] kernel; the mouse consumes them from the front. Two progress marks split the
// array: points before `resampledEnd` are evenly spaced, points before `smoothedEnd` are final.
// Exact ports of Path::resample @0x11ee50, Path::smooth @0x11e980, Path::trim @0x11d780.
#pragma once

#include "game/geometry.hpp"
#include "game/level_geometry.hpp"

#include <array>

namespace doc::game {

struct PathPoint {
    float x = 0, y = 0, z = 0;  // z is carried along unchanged (always 0 in drawn paths)
    Vec2 xy() const { return {x, y}; }
};

class DrawnPath {
public:
    static constexpr int kCapacity = 192;

    int size() const { return count_; }
    bool empty() const { return count_ == 0; }
    const PathPoint& operator[](int i) const { return points_[i]; }
    PathPoint& operator[](int i) { return points_[i]; }
    PathPoint& back() { return points_[count_ - 1]; }

    // Starts a new path at `start`; `fingerOffset` = start - finger position.
    void begin(Vec2 start, Vec2 fingerOffset);
    // Appends a point (the caller checks capacity). Returns the new point.
    PathPoint& append(Vec2 p);
    void clear();
    void removeLast() { --count_; }
    void truncate(int n) { count_ = n; }  // drops points from the end, nothing else

    void resample(bool appendLastPoint, bool advanceResampled);
    void smooth(bool closing, bool advanceSmoothed, const LevelGeometry* walls);
    void trim(int n);  // drops n points from the front

    Vec2 fingerOffset;      // added to the finger position while drawing; decays toward 0
    bool drawing = false;   // the finger is drawing this path
    bool snapped = false;   // the path ended in a hole it snapped onto
    int snapTarget = -1;    // object id the end of the path snapped onto, -1 if none
    int resampledEnd = 0;
    int smoothedEnd = 0;

private:
    std::array<PathPoint, kCapacity> points_{};
    int count_ = 0;
};

}  // namespace doc::game
