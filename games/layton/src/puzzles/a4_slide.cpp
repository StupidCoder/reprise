// The sliding-block and maze loop (overlay 21). See a4_slide.hpp.
#include "puzzles/a4_slide.hpp"
#include "puzzles/a4_loop_kit.hpp"
#include <algorithm>
#include <cstdio>

namespace picarat::adventure4 {

using namespace kit;

namespace {
// Overlay 21's load address and its strings (read from the game's code).
constexpr std::uint32_t kOverlay21 = 0x02106260;
constexpr std::uint32_t kWallName = 0x021094c4;    // "lt4_slide_wall"
constexpr std::uint32_t kNormalBlock = 0x021094d4; // piece blocks: normal,
constexpr std::uint32_t kSlideBlock = 0x021094e8;  // and while dragged
constexpr std::uint32_t kMazeImage = 0x021094fc;   // "maze0"
constexpr std::uint32_t kMazeObject = 0x02109504;  // "lt4_maze"
constexpr std::uint32_t kCounterName = 0x02109510; // "lt4_slide_count"
constexpr std::uint32_t kZeroCount = 0x02109520;   // "0000"
constexpr std::uint32_t kCountFormat = 0x02109528; // "%04d"
constexpr std::uint32_t kOnFormat = 0x02109530;    // "%s_on"
// The ball's looks by the way it rolled: left, back, right, front.
constexpr std::uint32_t kLeft = 0x02109538, kBack = 0x02109540, kRight = 0x02109548,
                        kFront = 0x02109550;
constexpr std::uint32_t kVerdictName = 0x02109558; // the verdict variable
constexpr std::uint32_t kTurnBlock = 0x02109564;   // the maze's turn block
constexpr int kPieceKind = 24;
constexpr int kSolidWall = 15, kOpenWall = 19;
constexpr int kOwnBox = 1, kWallBox = 3; // touch area ids: a piece's body, an obstacle
constexpr int kMaxMoves = 9999;

} // namespace

SlideLoop::SlideLoop(Scene& scene, Services& services, std::span<const Value> arguments)
    : Loop(scene, services, arguments) {
    reset();
}

void SlideLoop::reset() {
    // 02106450.
    size_ = 1;
    origin_ = count_ = {};
    dragSound_ = solvedSound_ = -1;
    goals_.clear();
    frames_ = 1.0f;
    solved_ = false;
    inset_ = 0;
    ball_.clear();
    ballSequence_ = 0;
    ballOn_ = false;
    colour_ = 0;
    gates_.clear();
    onGate_ = false;
    ballLook_.clear();
    solvedBlock_.clear();
}

bool SlideLoop::command(gss::Call& c, int id) {
    // 02106648: the sub-command's operands follow it.
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
    case 9:
        return wallObject(c);
    case 10:
        return addGate(c);
    case 11:
        return solvedBlock(c);
    case 12:
        return namedCounter(c);
    default:
        return Loop::command(c, id);
    }
}

// Operand i of the sub-command (after the sub-command's number).
#define OP(i) (i + 1)

bool SlideLoop::board(gss::Call& c) {
    // 02106780 {animation, x, y, across, down, size, drag sound, frames,
    // [inset], [solved sound]}: the board and its frame of four walls.
    const std::size_t n = c.count() - 1;
    if (n <= 6) {
        return false;
    }
    origin_ = {s16(c.integer(OP(1))), s16(c.integer(OP(2)))};
    count_ = {s16(c.integer(OP(3))), s16(c.integer(OP(4)))};
    size_ = c.integer(OP(5));
    frames_ = c.real(OP(7));
    if (n == 9 && c.integer(OP(8)) != 0) {
        inset_ = c.integer(OP(8));
    }
    const std::string wall = scene_.assets.codeText(21, kOverlay21, kWallName);
    Chara* w = scene_.create(wall, kSolidWall, {});
    if (const AnimationResource* a = scene_.animation(text(c, OP(0))); a && w) {
        w->visual = Chara::Visual::Animation;
        w->animation.attach(a);
        scene_.place(*w, a->screen, 6);
    }
    if (w) {
        w->touchKind = 0; // 020db6b8 with no blocks
        w->callbacks = {};
        w->enterState(0);
        const int s = size_, d = inset_;
        auto area = [&](int x, int y, int width, int height) {
            w->touchAreas.push_back({s16(x), s16(y), s16(width), s16(height), kWallBox});
        };
        area(origin_.x - s, origin_.y - s, (count_.x + 2) * s, s + d);
        area(origin_.x - s, origin_.y, s + d, count_.y * s);
        area(origin_.x + count_.x * s - d, origin_.y, s + d, count_.y * s);
        area(origin_.x - s, origin_.y + count_.y * s - d, (count_.x + 2) * s, s + d);
    }
    dragSound_ = c.integer(OP(6));
    if (n > 9) {
        solvedSound_ = c.integer(OP(9));
    }
    c.result = Value::ofInt(1);
    return true;
}

bool SlideLoop::wallCells(gss::Call& c) {
    // 02106cd8 {x, y, across, down}: an inner wall over whole cells.
    if (c.count() - 1 < 4) {
        return false;
    }
    Chara* w = scene_.find(scene_.assets.codeText(21, kOverlay21, kWallName));
    if (!w) {
        return false;
    }
    w->touchAreas.push_back(
        {s16(c.integer(OP(0)) * size_ + origin_.x), s16(c.integer(OP(1)) * size_ + origin_.y),
         s16(c.integer(OP(2)) * size_), s16(c.integer(OP(3)) * size_), kWallBox});
    c.result = Value::ofInt(1);
    return true;
}

bool SlideLoop::pieceLook(gss::Call& c) {
    // 02107130 {piece, animation, sequence, x, y, maze}: the piece's look,
    // its blocks, the grid and its cell.
    if (c.count() - 1 < 6) {
        return false;
    }
    Chara* o = scene_.find(text(c, OP(0)));
    if (!o) {
        return false;
    }
    if (const AnimationResource* a = scene_.animation(text(c, OP(1)))) {
        o->visual = Chara::Visual::Animation;
        o->animation.attach(a);
        scene_.place(*o, a->screen, 6);
    }
    if (c.integer(OP(5)) != 0) {
        ball_ = text(c, OP(0));
        ballSequence_ = nameHash(text(c, OP(2)));
        ballLook_ = text(c, OP(2));
        setColour(0);
    }
    // 020db7f0: a slide button with the normal and the dragging block.
    o->touchKind = 3;
    o->callbacks = {};
    o->callbacks[0] = scene_.assets.codeText(21, kOverlay21, kNormalBlock);
    o->callbacks[1] = scene_.assets.codeText(21, kOverlay21, kSlideBlock);
    o->enterState(0);
    play(*o, text(c, OP(2)));
    // 020abc78: cells of the board; snaps take the board's frames.
    const auto frames = std::int32_t(4096.0f * frames_);
    o->position.setGrid({{}, {origin_.x, origin_.y}, {count_.x, count_.y}, size_, frames});
    // 02083230: straight onto a cell (clamped to the board).
    const int cx = std::clamp<int>(s16(c.integer(OP(3))), 0, count_.x);
    const int cy = std::clamp<int>(s16(c.integer(OP(4))), 0, count_.y);
    o->position.setCell({cx, cy});
    c.result = Value::ofInt(1);
    return true;
}

bool SlideLoop::pieceBoxes(gss::Call& c) {
    // 021072f4 {piece, passable, (x, y, across, down)...}: the piece's body
    // in cells relative to its own cell, inset from the cell edges; a body
    // that is not passable also blocks the other pieces.
    const std::size_t n = c.count() - 1;
    if (n < 6) {
        return false;
    }
    Chara* o = scene_.find(text(c, OP(0)));
    if (!o) {
        return false;
    }
    for (std::size_t k = 0; k < (n & ~std::size_t(3)); k += 4) {
        const std::array<int, 5> box{s16(c.integer(OP(2 + k)) * size_ + inset_),
                                     s16(c.integer(OP(3 + k)) * size_ + inset_),
                                     s16(c.integer(OP(4 + k)) * size_ - 2 * inset_),
                                     s16(c.integer(OP(5 + k)) * size_ - 2 * inset_), 0};
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

bool SlideLoop::restart(gss::Call& c) {
    // 021074ec: every piece back to its first cell (values 3 and 4) and
    // look; the maze back to its first picture; no moves counted.
    for (Chara* o : scene_.objects()) {
        if (!active(*o) || o->kind != kPieceKind) {
            continue;
        }
        if (!ballLook_.empty()) {
            play(*o, ballLook_);
            if (o->thread.values.size() > 2) {
                o->thread.values[2] = Value::ofText(ballLook_);
            }
        }
        const auto& v = o->thread.values;
        o->position.setCell(
            {v.size() > 3 ? s16(v[3].toInt()) : 0, v.size() > 4 ? s16(v[4].toInt()) : 0});
    }
    if (!gates_.empty()) {
        setColour(0);
        Chara* maze = scene_.find(scene_.assets.codeText(21, kOverlay21, kMazeObject));
        const ImageResource* image =
            scene_.image(scene_.assets.codeText(21, kOverlay21, kMazeImage));
        if (maze && image) {
            maze->visual = Chara::Visual::Image;
            maze->image = image;
            scene_.place(*maze, image->screen, -1);
        }
    }
    if (Chara* counter = scene_.find(scene_.assets.codeText(21, kOverlay21, kCounterName))) {
        counter->animation.showText(scene_.assets.codeText(21, kOverlay21, kZeroCount), 6, 0, 0);
        moves_ = 0;
    }
    c.result = Value::ofInt(1);
    return true;
}

bool SlideLoop::addGoal(gss::Call& c) {
    // 021076a8 {x, y, sequence}: a cell a piece showing the sequence must
    // cover.
    if (c.count() - 1 < 3 || goals_.size() >= 16) {
        return false;
    }
    const int x = c.integer(OP(0)), y = c.integer(OP(1));
    goals_.push_back({{s16(size_ * x + origin_.x), s16(size_ * y + origin_.y),
                       s16(size_ * (x + 1) + origin_.x), s16(size_ * (y + 1) + origin_.y)},
                      nameHash(text(c, OP(2)))});
    c.result = Value::ofInt(1);
    return true;
}

bool SlideLoop::counter(gss::Call& c) {
    // 02107838 {animation, x, y}: the move counter, showing 0000.
    if (c.count() - 1 < 2) {
        return false;
    }
    Chara* o = scene_.create(scene_.assets.codeText(21, kOverlay21, kCounterName), kSolidWall, {});
    if (o) {
        if (const AnimationResource* a = scene_.animation(text(c, OP(0)))) {
            o->visual = Chara::Visual::Animation;
            o->animation.attach(a);
            scene_.place(*o, a->screen, 4);
        }
        moves_ = 0;
        o->animation.showText(scene_.assets.codeText(21, kOverlay21, kZeroCount), 6, 0, 0);
        o->position.set({s16(c.integer(OP(1))) * kOne, s16(c.integer(OP(2))) * kOne});
    }
    c.result = Value::ofInt(1);
    return true;
}

bool SlideLoop::drag(gss::Call& c) {
    // 02107dc8 {piece, x, y}: a block puzzle's piece toward a point; a
    // maze's ball toward the pen instead.
    if (c.count() - 1 < 3) {
        return false;
    }
    if (busy()) {
        ballOn_ = false;
        return true;
    }
    if (ball_.empty()) {
        Chara* o = scene_.find(text(c, OP(0)));
        if (o) {
            move(*o, {s16(c.integer(OP(1))), s16(c.integer(OP(2)))}, 0);
        }
    } else {
        Chara* o = scene_.find(ball_);
        if (!o) {
            return false;
        }
        if (!ballOn_) {
            char name[64];
            std::snprintf(name, sizeof name,
                          scene_.assets.codeText(21, kOverlay21, kOnFormat).c_str(),
                          valueText(*o, 2).c_str());
            play(*o, name);
            ballOn_ = true;
        }
        if (!sliding(*o)) {
            const int half = s16(size_ >> 1);
            move(*o, {s16(services_.touch.x - half), s16(services_.touch.y - half)},
                 gates_.empty() ? 1 : 2);
        }
    }
    c.result = Value::ofInt(1);
    return true;
}

bool SlideLoop::move(Chara& piece, Cell target, int mode) {
    // 02107fbc. The obstacles: every other object's wall boxes on the lower
    // screen (any object for a piece, solid walls only for a ball; 020ddd14
    // takes at most 128).
    const auto found = obstacles(scene_, 0, mode == 0 ? -1 : kSolidWall, kWallBox, &piece);
    std::vector<Box> walls;
    for (const kit::Box& b : found) {
        walls.push_back({b.x1, b.y1, b.x2, b.y2});
    }
    const auto start = pixels(piece);
    Cell at{start[0], start[1]};
    int steps = mode == 2 ? 4 : mode == 0 ? 8 : 0x100;
    const int half = size_ >> 1;
    do {
        if (mode == 1) {
            // The ball turns to face the way it rolled once it is more
            // than half a cell from where it started.
            const int dx = start[0] - at.x, dy = start[1] - at.y;
            if (half < dx || half < dy || half <= -dx || half <= -dy) {
                std::uint32_t look = kLeft;
                if (dx <= half) {
                    look = kBack;
                    if (dy <= half) {
                        look = kFront;
                        if (half <= -dx) {
                            look = kRight;
                        }
                    }
                }
                const std::string name = scene_.assets.codeText(21, kOverlay21, look);
                if (piece.thread.values.size() > 2) {
                    piece.thread.values[2] = Value::ofText(name);
                }
                char on[64];
                std::snprintf(on, sizeof on,
                              scene_.assets.codeText(21, kOverlay21, kOnFormat).c_str(),
                              name.c_str());
                play(piece, on);
                services_.effect(dragSound_);
                break;
            }
        }
        if (target.x == at.x && target.y == at.y) {
            break;
        }
        // One pixel along the longer axis, the other following the slope.
        const int dx = s16(target.x - at.x), dy = s16(target.y - at.y);
        const auto n = stepToward({at.x, at.y}, {target.x, target.y});
        const Cell next{n[0], n[1]};
        if (collides(piece, next, walls)) {
            // A ball stops at the gate of its colour.
            if (!onGate_ && !gates_.empty()) {
                bool gate = false;
                for (const Gate& g : gates_) {
                    if (g.colour == colour_ && std::abs(g.box.x1 - next.x) < 2 &&
                        std::abs(g.box.y1 - next.y) < 2) {
                        gate = true;
                        break;
                    }
                }
                if (gate) {
                    break;
                }
            }
            // Else slide along the obstacle: the main direction alone, then
            // the side direction alone.
            if (const auto m = mainStep(dx, dy)) {
                const Cell alt{s16(at.x + (*m)[0]), s16(at.y + (*m)[1])};
                if (!collides(piece, alt, walls)) {
                    at = alt;
                    continue;
                }
            }
            const auto side = sideStep(dx, dy);
            if (!side) {
                break;
            }
            const Cell alt{s16(at.x + (*side)[0]), s16(at.y + (*side)[1])};
            if (collides(piece, alt, walls)) {
                break;
            }
            at = alt;
            continue;
        }
        at = next;
    } while (mode == 1 || --steps > 0);
    if (mode == 1) {
        piece.position.snap({at.x * kOne, at.y * kOne}, 2);
        return true;
    }
    if (start[0] == at.x && start[1] == at.y) {
        services_.stopEffect(dragSound_);
        return false;
    }
    if (!services_.effectPlaying(dragSound_)) {
        services_.effect(dragSound_);
    }
    piece.position.set({at.x * kOne, at.y * kOne});
    return true;
}

bool SlideLoop::collides(const Chara& piece, Cell at, const std::vector<Box>& walls) const {
    // 021086fc.
    std::vector<kit::Box> boxes;
    for (const Box& w : walls) {
        boxes.push_back({w.x1, w.y1, w.x2, w.y2});
    }
    return overlaps(piece, {at.x, at.y}, boxes);
}

bool SlideLoop::countMove(gss::Call& c) {
    // 0210796c: one more move (up to 9999) on the counter.
    if (moves_ < kMaxMoves) {
        ++moves_;
    }
    showCount();
    c.result = Value::ofInt(1);
    return true;
}

void SlideLoop::showCount() {
    if (Chara* counter = scene_.find(scene_.assets.codeText(21, kOverlay21, kCounterName))) {
        char digits[32];
        std::snprintf(digits, sizeof digits,
                      scene_.assets.codeText(21, kOverlay21, kCountFormat).c_str(), moves_);
        counter->animation.showText(digits, 6, 0, 0);
    }
}

bool SlideLoop::wallObject(gss::Call& c) {
    // 02106e14 {name, kind, value, animation, (x1, y1, x2, y2)...}: an object
    // whose wall boxes cover cell rectangles (a coloured wall when its kind
    // is 15 or 19 and its value names the colour).
    const std::size_t n = c.count() - 1;
    if (n <= 7) {
        return false;
    }
    std::vector<Value> values{c.at(OP(0)), c.at(OP(1)), c.at(OP(2))};
    Chara* o = scene_.create(text(c, OP(0)), c.integer(OP(1)), values);
    if (const AnimationResource* a = scene_.animation(text(c, OP(3)))) {
        if (Chara* target = scene_.find(text(c, OP(0)))) {
            target->visual = Chara::Visual::Animation;
            target->animation.attach(a);
            scene_.place(*target, a->screen, 6);
        }
    }
    if (o) {
        o->touchKind = 0; // 020db6b8 with no blocks
        o->callbacks = {};
        o->enterState(0);
        for (std::size_t k = 0; k < ((n - 4) & ~std::size_t(3)); k += 4) {
            const int x1 = c.integer(OP(4 + k)), y1 = c.integer(OP(5 + k));
            const int x2 = c.integer(OP(6 + k)), y2 = c.integer(OP(7 + k));
            o->touchAreas.push_back({s16(x1 * size_ + origin_.x - inset_),
                                     s16(y1 * size_ + origin_.y - inset_),
                                     s16((x2 - x1) * size_ + inset_ * 2),
                                     s16((y2 - y1) * size_ + inset_ * 2), kWallBox});
        }
    }
    c.result = Value::ofInt(1);
    return true;
}

bool SlideLoop::addGate(gss::Call& c) {
    // 02107a08 {x, y, to x, to y, colour, next colour}.
    if (c.count() - 1 < 4 || gates_.size() >= 48) {
        return false;
    }
    const int x = c.integer(OP(0)), y = c.integer(OP(1));
    Gate g;
    g.box = {s16(size_ * x + origin_.x), s16(size_ * y + origin_.y),
             s16(size_ * (x + 1) + origin_.x), s16(size_ * (y + 1) + origin_.y)};
    g.x = s16(size_ * c.integer(OP(2)) + origin_.x);
    g.y = s16(size_ * c.integer(OP(3)) + origin_.y);
    g.colour = c.count() > OP(4) ? c.integer(OP(4)) : 0;
    g.next = c.count() > OP(5) ? c.integer(OP(5)) : 0;
    gates_.push_back(g);
    c.result = Value::ofInt(1);
    return true;
}

bool SlideLoop::solvedBlock(gss::Call& c) {
    // 02107c0c {block}: called instead of setting the verdict.
    if (c.count() - 1 == 0) {
        return false;
    }
    solvedBlock_ = text(c, OP(0)).substr(0, 35);
    c.result = Value::ofInt(1);
    return true;
}

bool SlideLoop::namedCounter(gss::Call& c) {
    // 02107c60 {name, animation, x, y}: a counter of its own name.
    if (c.count() - 1 < 4) {
        return false;
    }
    if (Chara* o = scene_.create(text(c, OP(0)), kSolidWall, {})) {
        if (const AnimationResource* a = scene_.animation(text(c, OP(1)))) {
            o->visual = Chara::Visual::Animation;
            o->animation.attach(a);
            scene_.place(*o, a->screen, 6);
        }
        char digits[32];
        std::snprintf(digits, sizeof digits,
                      scene_.assets.codeText(21, kOverlay21, kCountFormat).c_str(), moves_);
        o->animation.showText(digits, 6, 0, 0);
        o->position.set({s16(c.integer(OP(2))) * kOne, s16(c.integer(OP(3))) * kOne});
    }
    c.result = Value::ofInt(1);
    return true;
}

#undef OP

bool SlideLoop::setColour(int colour) {
    // 021091b8.
    const Chara* wall = scene_.find(scene_.assets.codeText(21, kOverlay21, kWallName));
    if (!wall) {
        return false;
    }
    for (Chara* o : scene_.objects()) {
        if (!active(*o) || o == wall || (o->kind != kSolidWall && o->kind != kOpenWall)) {
            continue;
        }
        const int own = o->thread.values.size() > 2 ? o->thread.values[2].toInt() : 0;
        o->kind = own == colour ? kSolidWall : kOpenWall;
    }
    colour_ = colour;
    return true;
}

SlideLoop::Cell SlideLoop::cellPixel(const Chara& piece) const {
    // 020abc90 then 021089bc: the nearest cell, clamped to the board.
    const auto at = pixels(piece);
    const auto cell = piece.position.cellAt({at[0] * kOne, at[1] * kOne});
    const int cx = std::clamp(cell[0], 0, count_.x), cy = std::clamp(cell[1], 0, count_.y);
    return {s16(origin_.x + s16(cx * size_)), s16(origin_.y + s16(cy * size_))};
}

bool SlideLoop::covers(const Chara& piece, const Box& box) const {
    // A wall box of the piece (at its nearest cell) overlaps the box.
    const Cell p = cellPixel(piece);
    const Box rel{s16(box.x1 - p.x), s16(box.y1 - p.y), s16(box.x2 - p.x), s16(box.y2 - p.y)};
    for (const auto& a : piece.touchAreas) {
        if (a[4] == kWallBox && a[0] < rel.x2 && a[1] < rel.y2 && rel.x1 < a[0] + a[2] &&
            rel.y1 < a[1] + a[3]) {
            return true;
        }
    }
    return false;
}

void SlideLoop::afterPen() {
    // 02108a70.
    if (busy()) {
        return;
    }
    const bool penDown = services_.touch.down();
    if (!solved_) {
        if (!onGate_ && !goals_.empty()) {
            std::vector<bool> met(goals_.size(), false);
            for (Chara* o : scene_.objects()) {
                if (!active(*o) || o->kind != kPieceKind || !o->animation.sequence()) {
                    continue;
                }
                const std::uint32_t look = nameHash(o->animation.sequence()->name);
                for (std::size_t j = 0; j < goals_.size(); ++j) {
                    const Goal& g = goals_[j];
                    const Cell p = cellPixel(*o);
                    const Box rel{s16(g.box.x1 - p.x), s16(g.box.y1 - p.y), s16(g.box.x2 - p.x),
                                  s16(g.box.y2 - p.y)};
                    for (const auto& a : o->touchAreas) {
                        if (a[4] != kWallBox || !(a[0] < rel.x2 && a[1] < rel.y2 &&
                                                  rel.x1 < a[0] + a[2] && rel.y1 < a[1] + a[3])) {
                            continue;
                        }
                        if (!ball_.empty()) {
                            if (colour_ != 0) {
                                break;
                            }
                            if (ballSequence_ == g.sequence) {
                                met[j] = true;
                                break;
                            }
                        }
                        if (g.sequence == look) {
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
                if (!solvedBlock_.empty()) {
                    const std::string block = solvedBlock_;
                    reset();
                    callback(0, block, {});
                    return;
                }
                services_.data
                    .variables[scene_.assets.codeText(21, kOverlay21, kVerdictName) + "#0"] = 1;
                return;
            }
        }
        if (!gates_.empty()) {
            Chara* ball = scene_.find(ball_);
            if (!ball) {
                return;
            }
            bool hit = false;
            for (const Gate& g : gates_) {
                if (g.colour != colour_ || !covers(*ball, g.box)) {
                    continue;
                }
                hit = true;
                if (!onGate_) {
                    setColour(g.next);
                    onGate_ = true;
                    play(*ball, ballSequence_);
                    const Value args[] = {Value::ofInt(g.next), Value::ofInt(g.x),
                                          Value::ofInt(g.y), Value::ofInt((size_ >> 2) - inset_)};
                    callback(0, scene_.assets.codeText(21, kOverlay21, kTurnBlock), args);
                }
                break;
            }
            if (!hit) {
                onGate_ = false;
            }
        }
        if (ballOn_ && !penDown) {
            if (Chara* ball = scene_.find(ball_)) {
                play(*ball, ballSequence_);
                ballOn_ = false;
            }
        }
    }
    if (!penDown) {
        services_.stopEffect(dragSound_);
    }
}

} // namespace picarat::adventure4
