// The path the player draws for the mouse (the original's class Path, embedded in Player).
//
// Points arrive from the finger, are re-spaced every 8 units ("resampled") and smoothed with a
// [1 2 4 2 1] kernel; the mouse consumes them from the front. Two progress marks split the
// array: points before `resampledEnd` are evenly spaced, points before `smoothedEnd` are final.
// Exact ports of 1.1.1's Path::resample @0x11ee50, Path::smooth @0x11e980, Path::trim @0x11d780.
#pragma once

#include "game/geometry.hpp"
#include "game/level_geometry.hpp"

#include <array>
#include <cstdint>

namespace doc::game {

struct PathPoint {
    float x = 0, y = 0;
    // 1.1.1 marks the point a path snapped to beside a cheese (SceneGame::snapPath): smoothing
    // keeps such a point where it is (once). 1.0.5 kept a z here, always 0.
    std::uint16_t flag = 0;
    Vec2 xy() const { return {x, y}; }
    // Quick saves (state_io.hpp).
    template <class Io>
    void io(Io& io) { io(x, y, flag); }
};
inline constexpr std::uint16_t kFixedPoint = 99;

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

    // Both push the points they write off the walls (radius 12) when given the floor's walls;
    // smooth writes nothing without them (1.1.1).
    void resample(bool appendLastPoint, bool advanceResampled, const LevelGeometry* walls, float viewLeft);
    void smooth(bool closing, bool advanceSmoothed, const LevelGeometry* walls, float viewLeft);
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
public:
    // Quick saves (state_io.hpp).
    template <class Io>
    void io(Io& io) { io(fingerOffset, drawing, snapped, snapTarget, resampledEnd, smoothedEnd, points_,
                           count_); }
};

}  // namespace doc::game
