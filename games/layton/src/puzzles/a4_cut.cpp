// The cutting loop (overlay 18). See a4_cut.hpp.
#include "puzzles/a4_cut.hpp"
#include "puzzles/a4_loop_kit.hpp"
#include <cstdio>

// Each float operation rounds on its own, as the game's soft-float calls do.
#pragma STDC FP_CONTRACT OFF

namespace picarat::adventure4 {

using namespace kit;

namespace {
// Overlay 18's load address and its strings (read from the game's code).
constexpr std::uint32_t kOverlay18 = 0x02106260;
constexpr std::uint32_t kDotFormat = 0x02108ac4;   // "%03d_%03d"
constexpr std::uint32_t kLockedFlag = 0x02108bc8;  // the "puzzle operation off" flag
constexpr std::uint32_t kNoJudgeFlag = 0x02108bdc; // set while nothing is cut
constexpr std::uint32_t kArrow = 0x02108bf4;       // "arw"
// The pen only starts left of this (the buttons are right of it).
constexpr float kRightEdge = 200.0f;
// The dot under the pen is looked for this far (512 pixels: the nearest).
constexpr std::int32_t kAnywhere = 0x200000;

// 02029318: float to integer, toward zero.
std::int32_t toInt(float v) {
    return std::int32_t(v);
}

std::int32_t fx(float v) {
    return std::int32_t(4096.0f * v);
}

// A 20.12 value in whole pixels, toward zero.
int whole(std::int32_t v) {
    return v / kOne;
}

// The squared length of a 20.12 vector, in 20.12.
std::int32_t squared(std::int64_t x, std::int64_t y) {
    return std::int32_t((x * x + y * y) / kOne);
}

#define OP(i) (i + 1)
} // namespace

CutLoop::CutLoop(Scene& scene, Services& services, std::span<const Value> arguments)
    : Loop(scene, services, arguments) {}

CutLoop::~CutLoop() {
    scene_.loopShapes.clear();
}

std::string CutLoop::text(std::uint32_t address) const {
    return scene_.assets.codeText(18, kOverlay18, address);
}

bool CutLoop::command(gss::Call& c, int id) {
    // 021069c0.
    switch (id) {
    case 0:
        return setup(c);
    case 1:
        return setColour(c);
    case 2:
        return markLine(c, false);
    case 3:
        reset();
        c.result = Value::ofInt(1);
        return true;
    case 4:
        // 02107118: no answer lines.
        for (Edge& e : edges_) {
            e.answer = false;
        }
        c.result = Value::ofInt(1);
        return true;
    case 5:
        return markLine(c, true);
    case 6:
        c.result = Value::ofInt(judge() ? 1 : 0);
        return true;
    case 7:
        return addBucket(c);
    case 8: {
        // 02107444 {bucket, n}: was it filled n-th.
        if (c.count() - 1 < 2 || buckets_.empty()) {
            return false;
        }
        const std::uint32_t want = c.unsignedInt(OP(0));
        const Bucket* found = nullptr;
        for (auto it = buckets_.rbegin(); it != buckets_.rend(); ++it) {
            if (it->id == want) {
                found = &*it;
                break;
            }
        }
        c.result = Value::ofInt(found && found->order == c.integer(OP(1)) ? 1 : 0);
        return true;
    }
    case 9:
        // 021074f4 {x, y}: a dot the path may start at (four at most).
        if (c.count() - 1 < 2 || startCount_ == 4) {
            return false;
        }
        starts_[std::size_t(startCount_++)] = dotName(s16(c.integer(OP(0))), s16(c.integer(OP(1))));
        c.result = Value::ofInt(1);
        return true;
    default:
        return Loop::command(c, id);
    }
}

std::uint32_t CutLoop::dotName(int x, int y) const {
    char name[32];
    std::snprintf(name, sizeof name, text(kDotFormat).c_str(), x, y);
    return nameHash(name);
}

std::uint32_t CutLoop::dot(int x, int y) {
    char name[32];
    std::snprintf(name, sizeof name, text(kDotFormat).c_str(), x, y);
    const std::uint32_t h = nameHash(name);
    if (!scene_.findPlace(h)) {
        const float px = float(size_ * x + originX_), py = float(size_ * y + originY_);
        scene_.createPlace(name, {fx(px), fx(py)}, {});
    }
    return h;
}

std::array<int, 2> CutLoop::cellOf(const Place& p) const {
    return {s16((whole(p.position[0]) - originX_) / size_),
            s16((whole(p.position[1]) - originY_) / size_)};
}

std::uint32_t CutLoop::startAt(int i) const {
    if (i == -1) {
        return pathEnd_;
    }
    return i >= 0 && i < 4 ? starts_[std::size_t(i)] : 0;
}

int CutLoop::edgeIndex(int x, int y, int dx, int dy) const {
    // Rows of lines across first (across_ per row, down_ + 1 rows), then
    // rows of lines down (across_ + 1 per row).
    const int down = std::abs(dy);
    const int ex = s16(x - (dx < 0 ? 1 : 0)), ey = s16(y - (dy < 0 ? 1 : 0));
    return across_ * (down_ + 1) * down + ey * (across_ + down) + ex;
}

CutLoop::Edge* CutLoop::edge(int x, int y, int dx, int dy) {
    const int i = edgeIndex(x, y, dx, dy);
    return i >= 0 && std::size_t(i) < edges_.size() ? &edges_[std::size_t(i)] : nullptr;
}

bool CutLoop::lineAt(int x, int y, int dx, int dy) {
    if (x < 0 || across_ < x + dx || y < 0 || down_ < y + dy) {
        return false;
    }
    const Edge* e = edge(x, y, dx, dy);
    return e && e->exists;
}

bool CutLoop::setup(gss::Call& c) {
    // 02106abc {x, y, across, down, cell size, width, reach, sound, [path
    // mode, buckets]}.
    const std::size_t n = c.count() - 1;
    if (n < 8) {
        return false;
    }
    originX_ = s16(c.integer(OP(0)));
    originY_ = s16(c.integer(OP(1)));
    across_ = s16(c.integer(OP(2)));
    down_ = s16(c.integer(OP(3)));
    size_ = c.integer(OP(4));
    width_ = c.real(OP(5));
    reach_ = c.real(OP(6));
    sound_ = c.integer(OP(7));
    edges_.assign(std::size_t(std::max(0, down_ + down_ * across_ * 2 + across_)), {});
    buckets_.clear();
    if (n > 9) {
        buckets_.reserve(std::size_t(std::max(0, c.integer(OP(9)))));
        pathMode_ = c.integer(OP(8)) != 0;
    }
    services_.data.flags[text(kNoJudgeFlag) + "#0"] = 1;
    c.result = Value::ofInt(1);
    return true;
}

bool CutLoop::setColour(gss::Call& c) {
    // 02106d80 {red, green, blue, [flag]}: 0..255 each.
    if (c.count() - 1 < 3) {
        return false;
    }
    auto channel = [&](std::size_t i) {
        return std::uint16_t(std::clamp(c.integer(OP(i)) >> 3, 0, 31));
    };
    const int flag = c.count() - 1 >= 4 ? c.integer(OP(3)) : 1;
    colour_ =
        std::uint16_t(channel(0) | channel(1) << 5 | channel(2) << 10 | (flag > 0 ? 0x8000 : 0));
    c.result = Value::ofInt(1);
    return true;
}

bool CutLoop::markLine(gss::Call& c, bool answer) {
    // 02106e68 / 0210716c {x1, y1, x2, y2}: a straight run of lines (that
    // exist, or that are the answer).
    if (c.count() - 1 < 4) {
        return false;
    }
    int x1 = s16(c.integer(OP(0))), y1 = s16(c.integer(OP(1)));
    int x2 = s16(c.integer(OP(2))), y2 = s16(c.integer(OP(3)));
    // 021080bc: on the grid, straight and not a single dot.
    if (across_ < x1 || across_ < x2 || down_ < y1 || down_ < y2 || (x1 != x2 && y1 != y2) ||
        (x1 == x2 && y1 == y2)) {
        return false;
    }
    if (x1 + y1 > x2 + y2) {
        std::swap(x1, x2);
        std::swap(y1, y2);
    }
    const int dx = x1 != x2 ? 1 : 0, dy = y1 != y2 ? 1 : 0;
    while (x1 != x2 || y1 != y2) {
        if (Edge* e = edge(x1, y1, dx, dy)) {
            if (answer) {
                e->answer = true;
            } else {
                e->exists = true;
                e->a = dot(x1, y1);
                e->b = dot(x1 + dx, y1 + dy);
            }
        }
        x1 = s16(x1 + dx);
        y1 = s16(y1 + dy);
    }
    c.result = Value::ofInt(1);
    return true;
}

void CutLoop::reset() {
    // 02107020: everything uncut, the path and the buckets emptied.
    for (Edge& e : edges_) {
        e.cut = false;
    }
    start_ = -1;
    over_ = current_ = pathEnd_ = 0;
    lock_ = {0, 0};
    filled_ = 0;
    cuts_ = 0;
    services_.data.flags[text(kNoJudgeFlag) + "#0"] = 1;
    if (pathMode_) {
        for (Bucket& b : buckets_) {
            b.order = -1;
        }
    }
}

bool CutLoop::judge() const {
    // 021072dc: the cuts are exactly the answer lines.
    for (auto it = edges_.rbegin(); it != edges_.rend(); ++it) {
        if (it->exists && it->answer != it->cut) {
            return false;
        }
    }
    return true;
}

bool CutLoop::addBucket(gss::Call& c) {
    // 0210734c {bucket, x, y, block}.
    if (c.count() - 1 < 4 || buckets_.capacity() == 0) {
        return false;
    }
    buckets_.push_back({c.unsignedInt(OP(0)), dotName(s16(c.integer(OP(1))), s16(c.integer(OP(2)))),
                        -1, kit::text(c, OP(3))});
    c.result = Value::ofInt(1);
    return true;
}

void CutLoop::toggle(int x, int y, int dx, int dy) {
    // 02107f24; the "nothing cut" flag follows the count.
    Edge* e = edge(x, y, dx, dy);
    if (!e || !e->exists) {
        return;
    }
    e->cut = !e->cut;
    if (e->cut) {
        if (++cuts_ == 1) {
            services_.data.flags[text(kNoJudgeFlag) + "#0"] = 0;
        }
    } else if (--cuts_ == 0) {
        services_.data.flags[text(kNoJudgeFlag) + "#0"] = 1;
    }
}

void CutLoop::press(float x, float y, bool inside) {
    // 021075b4: on a dot; in path mode only the path's end or, before a
    // path, a start dot.
    if (!inside) {
        return;
    }
    const Place* p = scene_.nearestPlace({fx(x), fx(y)}, fx(reach_));
    if (!p) {
        return;
    }
    const std::uint32_t h = p->hash;
    if (!pathMode_) {
        current_ = h;
        return;
    }
    const auto atStart = [&](std::uint32_t v) {
        int i = 0;
        while (i < 4 && starts_[std::size_t(i)] != v) {
            ++i;
        }
        return i;
    };
    if (atStart(pathEnd_) == 4 && pathEnd_ != 0 && pathEnd_ != h) {
        return;
    }
    const int i = atStart(h);
    if (i == 4 || start_ >= 0) {
        if (pathEnd_ == h) {
            current_ = h;
        }
        return;
    }
    start_ = i;
    current_ = h;
    pathEnd_ = h;
}

void CutLoop::drag(float x, float y) {
    // 02107700: when the pen comes near the next dot along a line, the line
    // to it is cut (or mended) and the cutting goes on from there.
    if (current_ == 0) {
        return;
    }
    const int px = toInt(x), py = toInt(y);
    int cx = s16((px - originX_) / size_), cy = s16((py - originY_) / size_);
    if (px - originX_ < 0) {
        cx = -1;
    }
    if (py - originY_ < 0) {
        cy = -1;
    }
    // Is any line of the puzzle around the cell under the pen?
    const bool lines = lineAt(cx, cy, 0, 1) || lineAt(s16(cx + 1), cy, 0, 1) ||
                       lineAt(cx, cy, 1, 0) || lineAt(cx, s16(cy + 1), 1, 0);
    const std::array<std::int32_t, 2> pen{fx(x), fx(y)};
    const Place* hit = scene_.nearestPlace(pen, kAnywhere);
    if (!hit) {
        return;
    }
    over_ = hit->hash;
    const Place* from = scene_.findPlace(current_);
    if (!from) {
        return;
    }
    const std::int32_t dx = hit->position[0] - pen[0], dy = hit->position[1] - pen[1];
    // Within half a cell of the dot across or down.
    bool near = std::abs(whole(dx)) < size_ / 2 || std::abs(whole(dy)) < size_ / 2;
    if (!lines && near) {
        // Off the lines: only toward the dot from the side of the cutting.
        float ox = 0, oy = 0;
        if (std::abs(whole(dx)) > std::abs(whole(dy))) {
            ox = float(whole(dx) < 0 ? -size_ : size_);
        } else {
            oy = float(whole(dy) < 0 ? -size_ : size_);
        }
        const std::int32_t bx = hit->position[0] + fx(ox), by = hit->position[1] + fx(oy);
        near = squared(std::int64_t(from->position[0]) - bx, std::int64_t(from->position[1]) - by) <
               squared(std::int64_t(from->position[0]) - hit->position[0],
                       std::int64_t(from->position[1]) - hit->position[1]);
    }
    if (!near || over_ == current_) {
        return;
    }
    const auto [x1, y1] = cellOf(*from);
    const auto [x2, y2] = cellOf(*hit);
    int stepX = 0, stepY = 0;
    bool found = false;
    auto test = [&](int sx, int sy) {
        const Edge* e = edge(x1, y1, sx, sy);
        stepX = sx;
        stepY = sy;
        found = e && e->exists;
    };
    if (y1 < y2) {
        test(0, 1);
    }
    if (y1 > y2 && !found) {
        test(0, -1);
    }
    if (x1 < x2 && !found) {
        test(1, 0);
    }
    if (x1 > x2 && !found) {
        test(-1, 0);
    }
    if (!found) {
        return;
    }
    const std::uint32_t next = dotName(s16(x1 + stepX), s16(y1 + stepY));
    const int step = pathMode_ ? pathStep(stepX, stepY, next) : 0;
    if (step >= 2) {
        return;
    }
    toggle(x1, y1, stepX, stepY);
    current_ = next;
    if (pathMode_) {
        pathEnd_ = next;
        if (next == startAt(start_) && step == 1) {
            lock_ = {-stepX, -stepY};
        } else {
            lock_ = {0, 0};
        }
    }
    if (sound_ != 0) {
        services_.effect(sound_);
    }
}

int CutLoop::pathStep(int dx, int dy, std::uint32_t next) {
    // 02108310.
    int step = 0;
    for (auto it = edges_.rbegin(); it != edges_.rend(); ++it) {
        if (((it->a == next && it->b != current_) || (it->b == next && it->a != current_)) &&
            it->cut) {
            ++step;
        }
    }
    if ((lock_[0] != 0 || lock_[1] != 0) && (lock_[0] != dx || lock_[1] != dy)) {
        step = 2;
    }
    if (starts_[1] != 0 && next == startAt(start_) && cuts_ != 1) {
        step = 2;
    }
    for (auto it = buckets_.rbegin(); it != buckets_.rend(); ++it) {
        Bucket& b = *it;
        if (b.place == next && b.order == -1 && step == 0) {
            b.order = ++filled_;
            const std::array<Value, 3> args{Value::ofInt(std::int32_t(b.id)), Value::ofInt(filled_),
                                            Value::ofInt(1)};
            callback(0, b.block, args);
        }
        if (b.place == current_ && (b.order >= 0 ? step == 1 : b.order == 0)) {
            b.order = -1;
            const std::array<Value, 3> args{Value::ofInt(std::int32_t(b.id)), Value::ofInt(filled_),
                                            Value::ofInt(0)};
            callback(0, b.block, args);
            --filled_;
        }
    }
    return step;
}

void CutLoop::release() {
    // 02107eec: a path that never left its start forgets it.
    over_ = current_ = 0;
    const bool atStart = start_ >= 0 && pathEnd_ == startAt(start_);
    if (atStart && cuts_ == 0) {
        start_ = -1;
    }
}

void CutLoop::placeArrow() {
    // 02108644: the arrow on the path's end, turned along the line it came
    // in by.
    if (current_ == 0) {
        return;
    }
    Chara* arrow = scene_.find(text(kArrow));
    if (!arrow) {
        return;
    }
    arrow->alpha.set(0);
    const Place* p = scene_.findPlace(current_);
    if (!p) {
        return;
    }
    const auto [x, y] = cellOf(*p);
    auto cut = [&](int dx, int dy) {
        const Edge* e = edge(x, y, dx, dy);
        return e && e->cut;
    };
    int quarter = -1;
    if (cut(0, 1)) {
        quarter = 0;
    } else if (cut(0, -1) && y - 1 >= 0) {
        quarter = 2;
    } else if (cut(1, 0)) {
        quarter = 3;
    } else if (cut(-1, 0) && x - 1 >= 0) {
        quarter = 1;
    }
    if (quarter < 0 || start_ < 0 || pathEnd_ == startAt(start_)) {
        return;
    }
    arrow->setStep(quarter);
    // 020db4a0: onto the dot.
    arrow->placeHash = p->hash;
    arrow->parentMatrix = p->matrix;
    arrow->parentTranslation = p->position;
    arrow->alpha.set(kOne);
}

void CutLoop::afterPen() {
    // 02106614.
    if (services_.data.flags[text(kLockedFlag) + "#0"] != 0) {
        return;
    }
    const auto& t = services_.touch;
    const float x = float(t.x), y = float(t.y);
    if (t.state == 1) {
        press(x, y, x <= kRightEdge);
    }
    if (t.state == 1 || t.state == 2) {
        drag(x, y);
    } else if (t.state == 3) {
        release();
    }
    if (pathMode_) {
        placeArrow();
    }
}

void CutLoop::endFrame() {
    // 02106744 (drawn this frame): a dot where the cutting is (in path mode
    // only at the start, before the path turns back to it) and every cut.
    auto& shapes = scene_.loopShapes;
    shapes.clear();
    const std::int32_t side = fx(2.0f * width_), half = fx(width_);
    bool showDot = current_ != 0;
    if (showDot && pathMode_) {
        showDot = pathEnd_ == startAt(start_) && lock_[0] == 0 && lock_[1] == 0;
    }
    if (showDot) {
        if (const Place* p = scene_.findPlace(current_)) {
            shapes.push_back(
                {true, {p->position[0] - half, p->position[1] - half}, {side, side}, 0, colour_});
        }
    }
    for (auto it = edges_.rbegin(); it != edges_.rend(); ++it) {
        if (!it->cut) {
            continue;
        }
        const Place* a = scene_.findPlace(it->a);
        const Place* b = scene_.findPlace(it->b);
        if (a && b) {
            shapes.push_back({false, a->position, b->position, fx(width_), colour_});
        }
    }
}

} // namespace picarat::adventure4
