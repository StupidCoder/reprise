// The pushing-block loop (overlay 22). See a4_slide2.hpp.
#include "puzzles/a4_slide2.hpp"
#include <algorithm>
#include <cstdio>

namespace picarat::adventure4 {

using namespace kit;

namespace {
// Overlay 22's load address and its strings (read from the game's code).
constexpr std::uint32_t kOverlay22 = 0x02106260;
constexpr std::uint32_t kWallName = 0x02108aa4;    // "lt4_slide2_wall"
constexpr std::uint32_t kNormalBlock = 0x02108ab4; // piece blocks: normal,
constexpr std::uint32_t kSlideBlock = 0x02108acc;  // and while dragged
constexpr std::uint32_t kCounterName = 0x02108ae0; // "lt4_slide2_count"
constexpr std::uint32_t kZeroCount = 0x02108af4;   // "0000"
constexpr std::uint32_t kCountFormat = 0x02108afc; // "%04d"
constexpr std::uint32_t kVerdictName = 0x02108b04; // the verdict variable
constexpr int kPieceKind = 24, kWallKind = 15;
constexpr int kOwnBox = 1, kWallBox = 3;
constexpr int kMaxMoves = 9999;

#define OP(i) (i + 1)
} // namespace

Slide2Loop::Slide2Loop(Scene& scene, Services& services, std::span<const Value> arguments)
    : Loop(scene, services, arguments) {
    reset();
}

void Slide2Loop::reset() {
    // 02106430.
    size_ = 1;
    origin_ = count_ = {};
    dragSound_ = solvedSound_ = -1;
    goals_.clear();
    solved_ = false;
    moves_ = 0;
    frames_ = 1.0f;
}

bool Slide2Loop::command(gss::Call& c, int id) {
    // 02106540.
    switch (id) {
    case 0:
        return board(c);
    case 1:
        return wallCells(c);
    case 2:
        return pieceLook(c);
    case 3:
        return pieceBoxes(c);
    case 4:
        return restart(c);
    case 5:
        return addGoal(c);
    case 6:
        return counter(c);
    case 7:
        return drag(c);
    case 8:
        return countMove(c);
    default:
        return Loop::command(c, id);
    }
}

bool Slide2Loop::board(gss::Call& c) {
    // 02106628 {animation, x, y, across, down, size, drag sound, frames,
    // solved sound}: the board and its frame of four walls.
    if (c.count() - 1 <= 7) {
        return false;
    }
    origin_ = {s16(c.integer(OP(1))), s16(c.integer(OP(2)))};
    count_ = {s16(c.integer(OP(3))), s16(c.integer(OP(4)))};
    size_ = c.integer(OP(5));
    frames_ = c.real(OP(7));
    Chara* w = scene_.create(scene_.assets.codeText(22, kOverlay22, kWallName), kWallKind, {});
    if (w) {
        attach(scene_, *w, text(c, OP(0)), 6);
        w->touchKind = 0; // 020db6b8 with no blocks
        w->callbacks = {};
        w->enterState(0);
        const int s = size_;
        auto area = [&](int x, int y, int width, int height) {
            w->touchAreas.push_back({s16(x), s16(y), s16(width), s16(height), kWallBox});
        };
        area(origin_[0] - s, origin_[1] - s, (count_[0] + 2) * s, s);
        area(origin_[0] - s, origin_[1], s, count_[1] * s);
        area(origin_[0] + count_[0] * s, origin_[1], s, count_[1] * s);
        area(origin_[0] - s, origin_[1] + count_[1] * s, (count_[0] + 2) * s, s);
    }
    dragSound_ = c.integer(OP(6));
    solvedSound_ = c.integer(OP(8));
    c.result = Value::ofInt(1);
    return true;
}

bool Slide2Loop::wallCells(gss::Call& c) {
    // 02106b28 {x, y, across, down}.
    if (c.count() - 1 < 4) {
        return false;
    }
    Chara* w = scene_.find(scene_.assets.codeText(22, kOverlay22, kWallName));
    if (!w) {
        return false;
    }
    w->touchAreas.push_back(
        {s16(c.integer(OP(0)) * size_ + origin_[0]), s16(c.integer(OP(1)) * size_ + origin_[1]),
         s16(c.integer(OP(2)) * size_), s16(c.integer(OP(3)) * size_), kWallBox});
    c.result = Value::ofInt(1);
    return true;
}

bool Slide2Loop::pieceLook(gss::Call& c) {
    // 02106c64 {piece, animation, sequence, x, y}.
    if (c.count() - 1 < 6) {
        return false;
    }
    Chara* o = scene_.find(text(c, OP(0)));
    if (!o) {
        return false;
    }
    attach(scene_, *o, text(c, OP(1)), 6);
    o->touchKind = 3; // 020db7f0: normal and dragging blocks
    o->callbacks = {};
    o->callbacks[0] = scene_.assets.codeText(22, kOverlay22, kNormalBlock);
    o->callbacks[1] = scene_.assets.codeText(22, kOverlay22, kSlideBlock);
    o->enterState(0);
    play(*o, text(c, OP(2)));
    o->position.setGrid({{}, origin_, count_, size_, std::int32_t(4096.0f * frames_)});
    o->position.setCell({s16(c.integer(OP(3))), s16(c.integer(OP(4)))});
    c.result = Value::ofInt(1);
    return true;
}

bool Slide2Loop::pieceBoxes(gss::Call& c) {
    // 02106db8 {piece, passable, (x, y, across, down)...} in cells.
    const std::size_t n = c.count() - 1;
    if (n < 6) {
        return false;
    }
    Chara* o = scene_.find(text(c, OP(0)));
    if (!o) {
        return false;
    }
    for (std::size_t k = 0; k < (n & ~std::size_t(3)); k += 4) {
        const std::array<int, 5> box{
            s16(c.integer(OP(2 + k)) * size_), s16(c.integer(OP(3 + k)) * size_),
            s16(c.integer(OP(4 + k)) * size_), s16(c.integer(OP(5 + k)) * size_), 0};
        if (c.integer(OP(1)) == 0) {
            auto wall = box;
            wall[4] = kWallBox;
            o->touchAreas.push_back(wall);
        }
        auto own = box;
        own[4] = kOwnBox;
        o->touchAreas.push_back(own);
    }
    c.result = Value::ofInt(1);
    return true;
}

bool Slide2Loop::restart(gss::Call& c) {
    // 02106fa0: every piece back to its first cell; no moves counted.
    for (Chara* o : scene_.objects()) {
        if (active(*o) && o->kind == kPieceKind) {
            o->position.setCell({s16(valueInt(*o, 3)), s16(valueInt(*o, 4))});
        }
    }
    if (Chara* counter = scene_.find(scene_.assets.codeText(22, kOverlay22, kCounterName))) {
        counter->animation.showText(scene_.assets.codeText(22, kOverlay22, kZeroCount), 6, 0, 0);
        moves_ = 0;
    }
    c.result = Value::ofInt(1);
    return true;
}

bool Slide2Loop::addGoal(gss::Call& c) {
    // 021070bc {x, y, sequence}.
    if (c.count() - 1 < 3 || goals_.size() >= 16) {
        return false;
    }
    const int x = c.integer(OP(0)), y = c.integer(OP(1));
    goals_.push_back({{s16(size_ * x + origin_[0]), s16(size_ * y + origin_[1]),
                       s16(size_ * (x + 1) + origin_[0]), s16(size_ * (y + 1) + origin_[1])},
                      nameHash(text(c, OP(2)))});
    c.result = Value::ofInt(1);
    return true;
}

bool Slide2Loop::counter(gss::Call& c) {
    // 0210724c {animation, x, y}.
    if (c.count() - 1 < 2) {
        return false;
    }
    if (Chara* o =
            scene_.create(scene_.assets.codeText(22, kOverlay22, kCounterName), kWallKind, {})) {
        attach(scene_, *o, text(c, OP(0)), 4);
        o->animation.showText(scene_.assets.codeText(22, kOverlay22, kZeroCount), 6, 0, 0);
        o->position.set({s16(c.integer(OP(1))) * kOne, s16(c.integer(OP(2))) * kOne});
    }
    c.result = Value::ofInt(1);
    return true;
}

bool Slide2Loop::countMove(gss::Call& c) {
    // 0210737c.
    if (moves_ < kMaxMoves) {
        ++moves_;
    }
    if (Chara* counter = scene_.find(scene_.assets.codeText(22, kOverlay22, kCounterName))) {
        char digits[32];
        std::snprintf(digits, sizeof digits,
                      scene_.assets.codeText(22, kOverlay22, kCountFormat).c_str(), moves_);
        counter->animation.showText(digits, 6, 0, 0);
    }
    c.result = Value::ofInt(1);
    return true;
}

bool Slide2Loop::drag(gss::Call& c) {
    // 02107418 {piece, x, y}; the sliding sound runs while it moves.
    if (c.count() - 1 <= 2) {
        return false;
    }
    Chara* o = scene_.find(text(c, OP(0)));
    if (o && move(*o, {s16(c.integer(OP(1))), s16(c.integer(OP(2)))})) {
        if (!services_.effectPlaying(dragSound_)) {
            services_.effect(dragSound_);
        }
    } else {
        services_.stopEffect(dragSound_);
    }
    c.result = Value::ofInt(1);
    return true;
}

bool Slide2Loop::move(Chara& piece, std::array<int, 2> target) {
    // 021074e8. Up to eight pixels a frame against the walls (any object's
    // wall boxes but the piece's own), then the pushing.
    const auto walls = obstacles(scene_, 0, -1, kWallBox, &piece);
    const auto start = pixels(piece);
    std::array<int, 2> at = start;
    int steps = 8;
    while (steps > 0) {
        if (target == at) {
            break;
        }
        const int dx = s16(target[0] - at[0]), dy = s16(target[1] - at[1]);
        const auto next = stepToward(at, target);
        if (!overlaps(piece, next, walls)) {
            at = next;
            --steps;
            continue;
        }
        bool moved = false;
        if (const auto m = mainStep(dx, dy)) {
            const std::array<int, 2> alt{s16(at[0] + (*m)[0]), s16(at[1] + (*m)[1])};
            if (!overlaps(piece, alt, walls)) {
                at = alt;
                --steps;
                continue;
            }
        }
        if (const auto side = sideStep(dx, dy)) {
            const std::array<int, 2> alt{s16(at[0] + (*side)[0]), s16(at[1] + (*side)[1])};
            if (!overlaps(piece, alt, walls)) {
                at = alt;
                moved = true;
            }
        }
        if (!moved) {
            break;
        }
        --steps;
    }
    const std::array<int, 2> delta{s16(at[0] - start[0]), s16(at[1] - start[1])};
    if (delta[0] == 0 && delta[1] == 0) {
        return false;
    }
    {
        auto list = pushable(piece, delta, walls);
        if (canPush(piece, delta, list)) {
            shift(piece, start, delta, list);
            return true;
        }
    }
    // The whole move could not be pushed: the longest move along each axis
    // alone that can, then both together.
    int dx = delta[0];
    while (dx != 0) {
        auto list = pushable(piece, {dx, 0}, walls);
        if (canPush(piece, {dx, 0}, list)) {
            break;
        }
        dx += dx < 1 ? 1 : -1;
    }
    int dy = delta[1];
    while (dy != 0) {
        auto list = pushable(piece, {0, dy}, walls);
        if (canPush(piece, {0, dy}, list)) {
            break;
        }
        dy += dy < 1 ? 1 : -1;
    }
    if (dx != 0 || dy != 0) {
        auto list = pushable(piece, {dx, dy}, walls);
        if (canPush(piece, {dx, dy}, list)) {
            shift(piece, start, {dx, dy}, list);
        }
    }
    return true;
}

std::vector<Slide2Loop::Push> Slide2Loop::pushable(const Chara& piece, std::array<int, 2> delta,
                                                   const std::vector<Box>& walls) {
    // 0210802c: the other live pushable pieces (value 5 not zero) and
    // whether each could move by `delta` against the walls.
    std::vector<Push> out;
    for (Chara* o : scene_.objects()) {
        if (o == &piece || !active(*o) || o->kind != kPieceKind) {
            continue;
        }
        if (o->thread.values.size() <= 5 || o->thread.values[5].toFloat() == 0.0f) {
            continue;
        }
        const auto p = pixels(*o);
        out.push_back(
            {o, !overlaps(*o, {s16(p[0] + delta[0]), s16(p[1] + delta[1])}, walls), false});
    }
    return out;
}

bool Slide2Loop::canPush(const Chara& piece, std::array<int, 2> delta, std::vector<Push>& list) {
    // 02107d38: every listed piece the moved piece would run into must be
    // free and able to push on in turn. The original walks the object list
    // and the pushed list side by side and only steps on in the pushed list
    // at a piece other than `piece`; reaching `piece` itself (in a nested
    // push) leaves the rest of the list unchecked.
    const auto at = pixels(piece);
    const std::array<int, 2> moved{s16(at[0] + delta[0]), s16(at[1] + delta[1])};
    std::size_t k = 0;
    for (Chara* o : scene_.objects()) {
        if (k >= list.size()) {
            break;
        }
        if (o != list[k].object) {
            continue;
        }
        if (o == &piece) {
            continue;
        }
        std::vector<Box> body;
        const auto p = pixels(*o);
        for (const auto& a : o->touchAreas) {
            if (a[4] == kOwnBox && body.size() < 128) {
                body.push_back({s16(a[0] + p[0]), s16(a[1] + p[1]), s16(a[0] + a[2] + p[0]),
                                s16(a[1] + a[3] + p[1])});
            }
        }
        if (overlaps(piece, moved, body)) {
            if (!list[k].free || !canPush(*o, delta, list)) {
                return false;
            }
            list[k].pushed = true;
        }
        ++k;
    }
    return true;
}

void Slide2Loop::shift(Chara& piece, std::array<int, 2> start, std::array<int, 2> delta,
                       const std::vector<Push>& list) {
    // 02107bbc: the pushed pieces by `delta`, then the piece itself.
    for (const Push& p : list) {
        if (p.pushed) {
            const auto at = pixels(*p.object);
            p.object->position.set({(delta[0] + at[0]) * kOne, (delta[1] + at[1]) * kOne});
        }
    }
    piece.position.set({(start[0] + delta[0]) * kOne, (start[1] + delta[1]) * kOne});
}

std::array<int, 2> Slide2Loop::cellPixel(const Chara& piece) const {
    // 020abc90 then 02108428.
    const auto at = pixels(piece);
    const auto cell = piece.position.cellAt({at[0] * kOne, at[1] * kOne});
    const int cx = std::clamp(cell[0], 0, count_[0]), cy = std::clamp(cell[1], 0, count_[1]);
    return {s16(origin_[0] + s16(cx * size_)), s16(origin_[1] + s16(cy * size_))};
}

void Slide2Loop::afterPen() {
    // 021084dc: solved when every goal is overlapped by the body of a piece
    // showing the goal's sequence (at the piece's nearest cell).
    if (busy()) {
        return;
    }
    if (!solved_ && !goals_.empty()) {
        std::vector<bool> met(goals_.size(), false);
        for (Chara* o : scene_.objects()) {
            if (!active(*o) || o->kind != kPieceKind || !o->animation.sequence()) {
                continue;
            }
            const std::uint32_t look = nameHash(o->animation.sequence()->name);
            for (std::size_t j = 0; j < goals_.size(); ++j) {
                const auto p = cellPixel(*o);
                const Box& g = goals_[j].box;
                const Box rel{s16(g.x1 - p[0]), s16(g.y1 - p[1]), s16(g.x2 - p[0]),
                              s16(g.y2 - p[1])};
                for (const auto& a : o->touchAreas) {
                    if (a[4] == kOwnBox && a[0] < rel.x2 && a[1] < rel.y2 && rel.x1 < a[0] + a[2] &&
                        rel.y1 < a[1] + a[3] && goals_[j].sequence == look) {
                        met[j] = true;
                        break;
                    }
                }
            }
        }
        solved_ = std::all_of(met.begin(), met.end(), [](bool b) { return b; });
        if (solved_) {
            services_.stopEffect(dragSound_);
            if (solvedSound_ > 0) {
                services_.effect(solvedSound_);
            }
            services_.data.variables[scene_.assets.codeText(22, kOverlay22, kVerdictName) + "#0"] =
                1;
        }
    }
}

} // namespace picarat::adventure4
