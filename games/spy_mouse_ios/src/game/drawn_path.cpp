#include "game/drawn_path.hpp"

namespace doc::game {

void DrawnPath::begin(Vec2 start, Vec2 offset) {
    // SceneGame::update @0x103528 (touch began): one point, the finger offset remembered.
    // The progress marks are left as they were; resample/smooth reset stale values.
    snapTarget = -1;
    snapped = false;
    fingerOffset = offset;
    count_ = 1;
    points_[0].x = start.x;
    points_[0].y = start.y;
    drawing = true;
}

PathPoint& DrawnPath::append(Vec2 p) {
    PathPoint& point = points_[count_];
    point.x = p.x;
    point.y = p.y;
    point.flag = 0;  // (SceneGame::updateTouchPoints @0x1023f8)
    ++count_;
    return point;
}

void DrawnPath::clear() {
    count_ = 0;
    drawing = false;
}

void DrawnPath::trim(int n) {
    if (n < 1) return;
    if (n < count_)
        for (int i = 0; i + n < count_; ++i) points_[i] = points_[i + n];
    // The marks only move while they stay positive (the original's rule).
    if (smoothedEnd - n > 0) smoothedEnd -= n;
    if (resampledEnd - n > 0) resampledEnd -= n;
    count_ -= n;
}

namespace {

// Off the walls, as both passes do with their output (MapCollision::collideSphere, radius 12).
void pushOff(PathPoint& point, const LevelGeometry& walls, float viewLeft) {
    Vec2 p = point.xy();
    walls.collideSphere(p, 12.0f, viewLeft);
    point.x = p.x;
    point.y = p.y;
}

}  // namespace

void DrawnPath::resample(bool appendLastPoint, bool advanceResampled, const LevelGeometry* walls, float viewLeft) {
    if (resampledEnd >= count_) resampledEnd = 0;
    const int start = resampledEnd;
    if (count_ < 2 || count_ - start < 2) return;

    constexpr float kSpacing = 8.0f;
    std::array<PathPoint, kCapacity> respaced{};
    int emitted = 0;
    float remaining = kSpacing;
    Vec2 p = points_[start].xy();
    std::uint16_t reachedFlag = points_[start].flag;  // of the last source point reached
    bool firstOfSegment = true;  // the next new point is the first since reaching it
    int target = start + 1;      // segment end point
    int lastTarget = start;      // segment end of the last emitted point
    Vec2 q = points_[target].xy();
    for (;;) {
        float dx = q.x - p.x;
        float dy = q.y - p.y;
        float length = std::sqrt(dx * dx + dy * dy);
        if (remaining >= length) {
            if (count_ <= target + 1) break;  // no further segment
            remaining = remaining - length;
            p = q;
            reachedFlag = points_[target].flag;
            ++target;
            q = points_[target].xy();
            firstOfSegment = true;
            continue;
        }
        float t = remaining * (1.0f / length);
        // The first point after a source point carries that point's flag along.
        if (firstOfSegment) respaced[emitted].flag = reachedFlag;
        p.x = p.x + dx * t;
        p.y = p.y + dy * t;
        respaced[emitted].x = p.x;
        respaced[emitted].y = p.y;
        ++emitted;
        firstOfSegment = false;
        remaining = kSpacing;
        lastTarget = target;
        if (kCapacity <= emitted) break;
    }

    int total = emitted;
    int advance = emitted;
    if (appendLastPoint) {
        if (kCapacity > emitted) {
            respaced[emitted] = points_[count_ - 1];
            total = advance = emitted + 1;
        }
    } else {
        // Keep the original points after the last segment used (not yet respaced).
        int source = lastTarget + 1;
        int copied = 0;
        while (count_ > source + copied && kCapacity > emitted + copied) {
            respaced[emitted + copied] = points_[source + copied];
            ++copied;
        }
        total = emitted + copied;
    }
    if (walls)
        for (int i = 0; i < total; ++i) pushOff(respaced[std::size_t(i)], *walls, viewLeft);

    int written = 0;
    while (written < total) {
        int destination = resampledEnd + written + 1;
        if (kCapacity <= destination) break;
        points_[destination] = respaced[written];
        ++written;
    }
    count_ = resampledEnd + written + 1;
    if (advanceResampled) resampledEnd = resampledEnd + advance;
}

void DrawnPath::smooth(bool closing, bool advanceSmoothed, const LevelGeometry* walls, float viewLeft) {
    if (smoothedEnd >= resampledEnd) smoothedEnd = 0;
    if (count_ == 0) return;
    if (!(resampledEnd >= 2 && resampledEnd > smoothedEnd + 1)) return;
    if (resampledEnd >= count_) return;

    std::array<PathPoint, kCapacity> smoothed{};
    auto& p = points_;
    // Kernels with the original's summation order.
    auto fiveTap = [&](int i, int j, int k) {  // centred on j = i + 1: weights 1 2 4 2 1, /10
        smoothed[k].x = ((((p[i - 1].x + (p[i].x + p[i].x)) + p[j].x * 4.0f) + (p[i + 2].x + p[i + 2].x)) + p[i + 3].x) * 0.1f;
        smoothed[k].y = ((((p[i - 1].y + (p[i].y + p[i].y)) + p[j].y * 4.0f) + (p[i + 2].y + p[i + 2].y)) + p[i + 3].y) * 0.1f;
    };
    auto threeTap = [&](int i, int j, int k) {  // weights 1 2 1, /4
        smoothed[k].x = ((p[i].x + (p[j].x + p[j].x)) + p[i + 2].x) * 0.25f;
        smoothed[k].y = ((p[i].y + (p[j].y + p[j].y)) + p[i + 2].y) * 0.25f;
    };
    auto keep = [&](int j, int k) {  // a fixed point (kFixedPoint) stays where it is
        smoothed[k].x = p[j].x;
        smoothed[k].y = p[j].y;
    };

    int counted = 0;  // points whose smoothing is final (advances smoothedEnd)
    int i = smoothedEnd;
    int k = 0;
    if (!closing) {
        for (;;) {
            int j = i + 1;
            if (resampledEnd - 1 <= j) break;
            if (p[j].flag == kFixedPoint) {
                keep(j, k++);
                ++counted;
            } else if (j >= 2 && resampledEnd - 2 >= j) {
                fiveTap(i, j, k++);
                ++counted;
            } else {
                threeTap(i, j, k++);
                // The original counts this point only if another one follows.
                if (resampledEnd - 1 > j) ++counted;
            }
            i = j;
        }
    } else {
        for (;;) {
            int j = i + 1;
            if (!(resampledEnd - 1 > j)) {
                // Closing kernel for the point after the last one smoothed.
                if (i < 1) {
                    smoothed[k].x = (p[i].x + (p[j].x + p[j].x)) * 0.33333334f;
                    smoothed[k].y = (p[i].y + (p[j].y + p[j].y)) * 0.33333334f;
                } else {
                    smoothed[k].x = ((p[i - 1].x + (p[i].x + p[i].x)) + p[j].x * 4.0f) * 0.14285715f;
                    smoothed[k].y = ((p[i - 1].y + (p[i].y + p[i].y)) + p[j].y * 4.0f) * 0.14285715f;
                }
                ++counted;
                break;
            }
            if (p[j].flag == kFixedPoint)
                keep(j, k);
            else if (j >= 2 && resampledEnd - 2 >= j)
                fiveTap(i, j, k);
            else
                threeTap(i, j, k);
            ++k;
            ++counted;
            i = j;
        }
    }

    // Write back the points after smoothedEnd (with their flags cleared, as the scratch buffer
    // holds none), pushed off the walls. Without walls nothing is written.
    const int first = smoothedEnd;
    if (walls && !(resampledEnd - 1 <= first + 1)) {
        for (int m = first;; ++m) {
            p[m + 1] = smoothed[m - first];
            pushOff(p[m + 1], *walls, viewLeft);
            if (resampledEnd - 1 <= m + 2) break;
        }
    }
    if (advanceSmoothed) smoothedEnd += counted;
}

}  // namespace doc::game
