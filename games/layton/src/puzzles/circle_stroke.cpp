// Circle-the-answer stroke evaluation (ARM9 overlay 33). See circle_stroke.hpp.
#include "puzzles/circle_stroke.hpp"
#include <algorithm>
#include <bit>
#include <cmath>

namespace picarat {

namespace {
// 32-bit subtraction with wrap-around, as the ARM code computes it.
std::int32_t subtract(std::int32_t a, std::int32_t b) {
    return std::bit_cast<std::int32_t>(std::uint32_t(a) - std::uint32_t(b));
}
} // namespace

void CircleStroke::reset() {
    // 020f7cec. The previous answer flag is deliberately kept.
    lastX = lastY = markerX = markerY = -1;
    points.clear();
    invalid = false;
}

void CircleStroke::addPoint(std::int32_t x, std::int32_t y) {
    // 020f83cc: a point already recorded anywhere in the stroke is skipped,
    // not just a repeat of the previous one.
    if (points.size() >= 256 || x < 0 || y < 0) {
        return;
    }
    const auto point = std::pair{x, y};
    if (std::find(points.begin(), points.end(), point) == points.end()) {
        points.push_back(point);
    }
}

void CircleStroke::evaluate(std::span<const CircleRegion> regions, std::int32_t maximumCenters,
                            std::int32_t markerHeight) {
    // 020f8194. An empty stroke just clears the choice (the original also
    // clears its drawing layer here; the caller does that).
    if (points.empty()) {
        markerX = markerY = 0;
        answer = false;
        invalid = false;
        return;
    }
    answer = false;

    // Bounding box of the stroke.
    std::int32_t minX = 1000, minY = 1000, maxX = 0, maxY = 0;
    for (const auto& [x, y] : points) {
        minX = std::min(minX, x);
        minY = std::min(minY, y);
        maxX = std::max(maxX, x);
        maxY = std::max(maxY, y);
    }
    auto reject = [&] {
        markerX = markerY = -1;
        answer = false;
        invalid = true;
    };
    if (minX < 0 || minY < 0) {
        reject();
        return;
    }
    points.clear();
    if (regions.empty()) {
        return; // the original keeps the marker and invalid flag here
    }

    // Count the regions whose centres lie inside the box; the marker goes
    // above the first of them.
    std::int32_t count = 0;
    for (const auto& region : regions) {
        if (region.x < minX || region.y < minY || region.x > maxX || region.y > maxY) {
            continue;
        }
        ++count;
        if (region.answer) {
            answer = true;
        }
        if (count == 1) {
            markerX = region.x;
            markerY = subtract(region.y, markerHeight);
        }
    }
    if (count > maximumCenters || count == 0) {
        reject();
    }

    // Too small a stroke is rejected: the box's diagonal must be at least 10
    // pixels (020f8398 accepts exactly 10). The length is computed like the
    // game's distance function (overlay 2, 0206b0c4): signed differences
    // converted to float, squared, added, square-rooted.
    const float dx = float(subtract(minX, maxX));
    const float dy = float(subtract(minY, maxY));
    const float xx = dx * dx;
    const float yy = dy * dy;
    if (std::sqrt(xx + yy) < 10.0f) {
        reject();
    }
}

bool CircleStroke::solved() const {
    // 020f7d20: a valid stroke circling a correct region. As in the original
    // the marker position takes part (it must be on screen).
    return !invalid && markerX > 2 && markerY >= 0 && answer;
}

} // namespace picarat
