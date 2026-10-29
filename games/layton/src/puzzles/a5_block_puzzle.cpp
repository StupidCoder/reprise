// The fifth adventure's sliding block puzzles (catalogue type 12, loop
// 3170; puzzles 81, 126, 131): blocks of cells in a walled box; the player
// drags them, they slide until they hit a wall or another block, and the
// puzzle is solved when the named blocks cover their goal cells.
//
// Reconstructed from the loop's command handler 0x21c268 (vtable
// 0x402068, constructor 0x37d77c; no own input or update hooks), its
// commands (setup 0x21c780, walls 0x21cdf4, block 0x21ccdc, shape
// 0x21d3e8, reset 0x21c63c, goal 0x21c4a8, release 0x21d5bc with the goal
// check 0x21d92c) and the move 0x21de5c with the base loop's collision
// helpers (rectangle test 0x284cac, directions 0x12d374 / 0x142b74 /
// 0x284a08, pushing 0x2848dc / 0x284a88 / 0x21d110).
//
// How it plays: the box, its blocked cells and every block's collision
// rectangles are touch areas of kind 4; a block's shape (its touch areas,
// kind 2) must not overlap any of them. A drag asks the loop to move the
// block towards the pen (command 7): it steps one pixel at a time along
// the major axis (at most eight a frame), slides along an obstacle when
// the direct step is blocked, and stops when it can go no further; the
// slide sound plays while it moves. On release (command 13) the block
// glides to the nearest half cell (curve 2 over the setup's frames), or,
// once every goal cell is covered by the block named for it (the goals'
// names are the blocks' animation names), to the nearest cell, the sound
// stops and the verdict "ナゾ判定" becomes 1.
//
// In the push mode (setup operand 8) a block may push blocks whose eighth
// value is set; no block of this game sets it, so pushing never happens.
// The game also turns a 3D object "obj_slide"'s nodes with the moves; no
// data creates that object, so that part is left out.
#include "puzzles/a5_special.hpp"
#include <cmath>
#include <cstdio>

namespace picarat::adventure5 {
namespace {

using adventure56::Object;
using xq::Call;
using xq::Value;

struct Point {
    int x = 0, y = 0;
    bool operator==(const Point&) const = default;
};

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
};

class BlockPuzzle final : public SpecialPuzzle {
public:
    explicit BlockPuzzle(Game& game) : game_(game) {}

    bool command(int number, Call& c, std::size_t first) override;
    void update() override;
    std::string describe() const override;

private:
    struct Goal {
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0; // pixels
        std::uint32_t name = 0;             // the block's animation name
    };

    struct Pushed {
        std::uint32_t hash = 0;
        bool free = false;   // it may move by the displacement
        bool pushed = false; // it moves along
    };

    // 0x2cc06c: the lower screen's objects of a kind, in their order.
    std::vector<Object*> objectsOfKind(int kind) const {
        std::vector<Object*> list;
        for (Object* o : game_.scene.screenList(1)) {
            if (o->kind == kind) {
                list.push_back(o);
            }
        }
        return list;
    }

    static Point pixelPosition(const Object& o) {
        // 0x2ca270 truncated to whole pixels.
        return {int(o.position[0] + o.offset[0]), int(o.position[1] + o.offset[1])};
    }

    static void place(Object& o, float x, float y) { o.position.set({x, y, o.position[2]}); }

    // 0x34992c: a cell (clamped to the box) to its pixel position.
    Point cellPixel(int x, int y) const {
        x = std::clamp(x, 0, int(width_));
        y = std::clamp(y, 0, int(height_));
        return {std::int16_t(x * size_ + originX_), std::int16_t(y * size_ + originY_)};
    }

    // The rectangles of an object's areas of `kind`, moved to `at`.
    static std::vector<Rect> rects(const Object& o, int kind, float x, float y) {
        std::vector<Rect> list;
        for (const auto& a : o.areas) {
            if (a.kind == kind) {
                list.push_back({a.x + x, a.y + y, a.w, a.h});
            }
        }
        return list;
    }

    // 0x284cac: whether the object's shape at `at` overlaps an obstacle.
    static bool collides(const Object& o, float x, float y, const std::vector<Rect>& obstacles) {
        for (const Rect& s : rects(o, 2, x, y)) {
            for (const Rect& r : obstacles) {
                if (s.x < r.x + r.w && s.y < r.y + r.h && r.x < s.x + s.w && r.y < s.y + s.h) {
                    return true;
                }
            }
        }
        return false;
    }

    Object* addWallObject();
    bool move(Object& block, Point target);
    std::vector<Pushed> pushable(Object& block, Point d, const std::vector<Rect>& obstacles);
    bool canPush(Object& block, Point d, std::vector<Pushed>& list);
    void applyPush(Object& block, Point from, Point d, const std::vector<Pushed>& list);
    void checkGoals();
    void release(Object& block);
    Point landing(const Object& block) const;
    int* panel(int x, int y);
    void dragPanel(Object& block, Point start);
    bool connected(int from, int to);

    Game& game_;
    int originX_ = 0, originY_ = 0; // +0x160 / +0x162
    std::int16_t width_ = 0;        // +0x164: cells across
    std::int16_t height_ = 0;       // +0x166: cells down
    int size_ = 0;                  // +0x168: cell size
    float modelScale_ = 0;          // +0x16c (the 3D object's moves)
    int slideSound_ = 0;            // +0x170
    std::vector<Goal> goals_;       // +0x174 (at most 16)
    bool solved_ = false;           // +0x238
    bool push_ = false;             // +0x23c
    float glideFrames_ = 0;         // +0x240
    int walls_ = 0;                 // +0x244: the wall object in use
    // Sixth edition only: the panel board's values by cell (+0x198; 0
    // empty, 2..5 path corners, 11 a turn-back, 6..10 the edge's marks),
    // and the frames the slide sound has left (+0x284).
    std::vector<int> panels_;
    int soundFrames_ = 0;
};

std::string BlockPuzzle::describe() const {
    std::string out;
    char line[128];
    std::snprintf(line, sizeof line, "grid %d %d origin %d %d size %d\n", width_, height_, originX_,
                  originY_, size_);
    out += line;
    for (const Goal& g : goals_) {
        std::snprintf(line, sizeof line, "goal %d %d %d %d name %u\n", g.x0, g.y0, g.x1, g.y1,
                      g.name);
        out += line;
    }
    for (std::size_t i = 0; width_ > 0 && i < panels_.size(); i += std::size_t(width_)) {
        out += "panels";
        for (std::size_t x = i; x < std::min(panels_.size(), i + std::size_t(width_)); ++x) {
            out += ' ' + std::to_string(panels_[x]);
        }
        out += '\n';
    }
    for (const int kind : {6, 7}) {
        for (const Object* o : objectsOfKind(kind)) {
            const Point p = pixelPosition(*o);
            const std::uint32_t name =
                o->values.size() > 4 && o->values[4].isText() ? xq::hash(o->values[4].text) : 0;
            std::snprintf(line, sizeof line, "%s %s at %d %d name %u vis %d\n",
                          kind == 6 ? "wall" : "block", o->name.c_str(), p.x, p.y, name,
                          int(o->visible));
            out += line;
            for (const auto& a : o->areas) {
                std::snprintf(line, sizeof line, "  area %d %g %g %g %g\n", a.kind, a.x, a.y, a.w,
                              a.h);
                out += line;
            }
        }
    }
    return out;
}

// "lt5_slide_wall%02d": priority 112, kind 6.
Object* BlockPuzzle::addWallObject() {
    char name[32];
    std::snprintf(name, sizeof name, "lt5_slide_wall%02d", walls_);
    return &game_.createObject(name, 1, 112, 6, {});
}

// 0x2848dc: the pushable blocks (shown, updating, eighth value set) and
// whether each may move by d.
std::vector<BlockPuzzle::Pushed> BlockPuzzle::pushable(Object& block, Point d,
                                                       const std::vector<Rect>& obstacles) {
    std::vector<Pushed> list;
    for (Object* o : objectsOfKind(7)) {
        if (!o->visible || o == &block || !o->updating() || o->values.size() <= 7 ||
            o->values[7].toInt() == 0) {
            continue;
        }
        const Point p = pixelPosition(*o);
        list.push_back(
            {o->hash, !collides(*o, float(p.x + d.x), float(p.y + d.y), obstacles), false});
    }
    return list;
}

// 0x284a88: whether the block can move by d, pushing the blocks it would
// overlap (each of which must be free and able to push on in turn).
bool BlockPuzzle::canPush(Object& block, Point d, std::vector<Pushed>& list) {
    const Point p = pixelPosition(block);
    for (Pushed& entry : list) {
        Object* o = game_.scene.object(entry.hash);
        if (!o || o == &block) {
            continue;
        }
        const Point q = pixelPosition(*o);
        if (!collides(block, float(p.x + d.x), float(p.y + d.y),
                      rects(*o, 2, float(q.x), float(q.y)))) {
            continue;
        }
        if (entry.free && canPush(*o, d, list)) {
            entry.pushed = true;
            continue;
        }
        return false;
    }
    return true;
}

// 0x21d110: the pushed blocks and the block move by d.
void BlockPuzzle::applyPush(Object& block, Point from, Point d, const std::vector<Pushed>& list) {
    for (const Pushed& entry : list) {
        if (Object* o = entry.pushed ? game_.scene.object(entry.hash) : nullptr) {
            place(*o, o->position[0] + o->offset[0] + float(d.x),
                  o->position[1] + o->offset[1] + float(d.y));
        }
    }
    place(block, float(from.x + d.x), float(from.y + d.y));
}

// 0x21de5c: the block towards `target` (pixels), at most eight steps.
bool BlockPuzzle::move(Object& block, Point target) {
    if (const int id = game_.flagId(names::inputOff); id >= 0 && game_.flag(id)) {
        return false;
    }
    // The obstacles: the walls' and the other shown blocks' rectangles.
    std::vector<Rect> obstacles;
    for (const int kind : {6, 7}) {
        for (Object* o : objectsOfKind(kind)) {
            if (!o->visible || o == &block) {
                continue;
            }
            const float ox = o->position[0] + o->offset[0], oy = o->position[1] + o->offset[1];
            for (const Rect& r : rects(*o, 4, ox, oy)) {
                if (obstacles.size() <= 0x80) {
                    obstacles.push_back(r);
                }
            }
        }
    }
    static const Point kSteps[5] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}, {0, 0}};
    const Point start = pixelPosition(block);
    Point at = start;
    for (int steps = 8; steps > 0;) {
        if (at == target) {
            break;
        }
        const int dx = std::int16_t(target.x - at.x), dy = std::int16_t(target.y - at.y);
        Point next;
        if (std::abs(dx) < std::abs(dy)) {
            const float slope = float(dx) / float(dy);
            next = dy < 0 ? Point{at.x - int(slope - 0.5f), at.y - 1}
                          : Point{int(slope + 0.5f) + at.x, at.y + 1};
        } else {
            const float slope = float(dy) / float(dx);
            next = dx < 0 ? Point{at.x - 1, at.y - int(slope - 0.5f)}
                          : Point{at.x + 1, int(slope + 0.5f) + at.y};
        }
        next = {std::int16_t(next.x), std::int16_t(next.y)};
        if (collides(block, float(next.x), float(next.y), obstacles)) {
            // Along the major axis alone (0x12d374), else along the minor
            // one (0x142b74).
            int major = -1;
            if (dx != 0 || dy != 0) {
                major = std::abs(dx) < std::abs(dy) ? (dy < 0 ? 3 : 1) : (dx < 0 ? 2 : 0);
            }
            if (major != -1) {
                const Point alt{at.x + kSteps[major].x, at.y + kSteps[major].y};
                if (!collides(block, float(alt.x), float(alt.y), obstacles)) {
                    at = alt;
                    --steps;
                    continue;
                }
            }
            int minor = -1;
            if (std::abs(dx) < std::abs(dy)) {
                if (dx != 0) {
                    minor = dx < 1 ? 2 : 0;
                }
            } else if (dy != 0) {
                minor = dy < 1 ? 3 : 1;
            }
            if (minor == -1) {
                break;
            }
            const Point alt{at.x + kSteps[minor].x, at.y + kSteps[minor].y};
            if (collides(block, float(alt.x), float(alt.y), obstacles)) {
                break;
            }
            next = alt;
        }
        at = next;
        --steps;
    }
    if (at == start) {
        return false;
    }
    const Point d{std::int16_t(at.x - start.x), std::int16_t(at.y - start.y)};
    if (!push_) {
        place(block, float(at.x), float(at.y));
        return true;
    }
    std::vector<Pushed> list = pushable(block, d, obstacles);
    if (canPush(block, d, list)) {
        applyPush(block, start, d, list);
        return true;
    }
    // Shorter moves: across alone, then down alone, each towards zero.
    int sx = 0;
    for (int x = d.x; x != 0; x += x < 1 ? 1 : -1) {
        list = pushable(block, {x, 0}, obstacles);
        if (canPush(block, {x, 0}, list)) {
            sx = x;
            break;
        }
    }
    int sy = 0;
    for (int y = d.y; y != 0; y += y < 1 ? 1 : -1) {
        list = pushable(block, {0, y}, obstacles);
        if (canPush(block, {0, y}, list)) {
            sy = y;
            break;
        }
    }
    if (sx == 0 && sy == 0) {
        return false;
    }
    list = pushable(block, {sx, sy}, obstacles);
    if (canPush(block, {sx, sy}, list)) {
        applyPush(block, start, {sx, sy}, list);
    }
    return true;
}

// 0x21d92c: once, when every goal cell is covered by a collision
// rectangle of the block named for it (at its nearest cell): the sound
// stops and the verdict becomes 1. While not solved, the sound stops when
// the pen is up.
void BlockPuzzle::checkGoals() {
    if (!solved_ && !goals_.empty()) {
        std::vector<bool> hit(goals_.size(), false);
        for (Object* o : objectsOfKind(7)) {
            if (!o->visible || o->values.size() <= 4 || !o->values[4].isText()) {
                continue;
            }
            const std::uint32_t name = xq::hash(o->values[4].text);
            for (std::size_t i = 0; i < goals_.size(); ++i) {
                if (goals_[i].name != name) {
                    continue;
                }
                if (game_.edition == Edition::Sixth) {
                    // 0x2e4930: the goal's corner must be exactly where one
                    // of the block's rectangles starts, the block taken at
                    // its half-cell landing.
                    const Point at = landing(*o);
                    for (const auto& a : o->areas) {
                        if (a.kind == 4 &&
                            int(std::floor(a.x + 0.5f)) == std::int16_t(goals_[i].x0 - at.x) &&
                            int(std::floor(a.y + 0.5f)) == std::int16_t(goals_[i].y0 - at.y)) {
                            hit[i] = true;
                            break;
                        }
                    }
                    continue;
                }
                const Point p = pixelPosition(*o);
                const int half = size_ >> 1;
                const Point cell = cellPixel((std::int16_t(p.x - originX_) + half) / size_,
                                             (std::int16_t(p.y - originY_) + half) / size_);
                for (const auto& a : o->areas) {
                    if (a.kind == 4 && std::int16_t(int(a.x)) < goals_[i].x1 - cell.x &&
                        std::int16_t(int(a.y)) < goals_[i].y1 - cell.y &&
                        goals_[i].x0 - cell.x < std::int16_t(int(a.x + a.w)) &&
                        goals_[i].y0 - cell.y < std::int16_t(int(a.y + a.h))) {
                        hit[i] = true;
                        break;
                    }
                }
            }
        }
        solved_ = std::all_of(hit.begin(), hit.end(), [](bool b) { return b; });
        if (solved_) {
            game_.stopEffect(slideSound_, 0);
            game_.setVariable(names::verdict, 1);
            return;
        }
    }
    if (!game_.input.touching) {
        game_.stopEffect(slideSound_, 0);
    }
}

// Where a released block comes to rest: the nearest half cell inside the
// box, or, once solved, the nearest cell (0x221180 / 0x221214 in the sixth
// edition, which also answers command 21 with it).
Point BlockPuzzle::landing(const Object& block) const {
    const int px = int(block.position[0]) - originX_, py = int(block.position[1]) - originY_;
    const int half = size_ >> 1;
    if (!solved_) {
        const int quarter = size_ >> 2;
        const int hx = std::clamp(int(std::int16_t((px + quarter) / half)), 0, width_ * 2);
        const int hy = std::clamp(int(std::int16_t((py + quarter) / half)), 0, height_ * 2);
        return {std::int16_t(hx * half + originX_), std::int16_t(hy * half + originY_)};
    }
    return cellPixel(std::int16_t((px + half) / size_), std::int16_t((py + half) / size_));
}

// 0x21d5bc: the goal check, then the block glides to its landing over the
// setup's frames.
void BlockPuzzle::release(Object& block) {
    checkGoals();
    const Point to = landing(block);
    block.position.start({float(to.x), float(to.y), block.position[2]}, glideFrames_, 2);
}

// Sixth edition (0x2e29e0): the slide sound stops once a panel drag has
// not moved it for ten frames.
void BlockPuzzle::update() {
    if (soundFrames_ > 0 && --soundFrames_ == 0 && game_.effectsSounding(slideSound_)) {
        game_.stopEffect(slideSound_, 0);
    }
}

// A panel board cell; null outside the block grid or the board.
int* BlockPuzzle::panel(int x, int y) {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) {
        return nullptr;
    }
    const std::size_t i = std::size_t(y * width_ + x);
    return i < panels_.size() ? &panels_[i] : nullptr;
}

// Sixth edition (0x2e4418, command 18): a panel follows the drag from the
// cell it started in, one cell at most into each empty neighbour of the
// cell it is in now (the start cell counts as empty: the board still holds
// the panel there until the release). A cell off the board is treated as
// taken (the original reads through a null entry there; the boards keep
// their panels away from it).
void BlockPuzzle::dragPanel(Object& block, Point start) {
    const int half = size_ >> 1;
    const int cx = std::int16_t((int(block.position[0]) - originX_ + half) / size_);
    const int cy = std::int16_t((int(block.position[1]) - originY_ + half) / size_);
    auto empty = [&](int x, int y) {
        const int* v = panel(x, y);
        return v && *v == 0;
    };
    Point lo{empty(cx - 1, cy) ? cx - 1 : cx, empty(cx, cy - 1) ? cy - 1 : cy};
    Point hi{empty(cx + 1, cy) ? cx + 1 : cx, empty(cx, cy + 1) ? cy + 1 : cy};
    if (cx - 1 == start.x) {
        lo.x = start.x;
    } else if (cx + 1 == start.x) {
        hi.x = start.x;
    }
    if (cy - 1 == start.y) {
        lo.y = start.y;
    } else if (cy + 1 == start.y) {
        hi.y = start.y;
    }
    const float x = std::clamp(float(start.x * size_ + originX_ + game_.touchX - game_.pressX),
                               float(lo.x * size_ + originX_), float(hi.x * size_ + originX_));
    const float y = std::clamp(float(start.y * size_ + originY_ + game_.touchY - game_.pressY),
                               float(lo.y * size_ + originY_), float(hi.y * size_ + originY_));
    if (!(lo == hi) && (block.position[0] != x || block.position[1] != y)) {
        if (!game_.effectsSounding(slideSound_)) {
            game_.playEffect(slideSound_);
        }
        soundFrames_ = 10;
    }
    place(block, x, y);
}

// Sixth edition (0x2e40b4, command 20): whether a path of corner panels
// leads from the mark `from` on the board's edge to the mark `to`. The
// walk enters from the edge the mark sits on (left, right, top; otherwise
// upwards) and follows each corner; a turn-back panel (11) reverses it;
// anything else ends it.
bool BlockPuzzle::connected(int from, int to) {
    enum { Up, Down, Left, Right };

    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const int* v = panel(x, y);
            if (!v || *v != from) {
                continue;
            }
            int heading = Up;
            if (x == 0 && y != 0) {
                heading = Right;
            } else if (x == width_ - 1 && y != 0) {
                heading = Left;
            } else if (y == 0 && x != 0) {
                heading = Down;
            }
            while (true) {
                x += heading == Left ? -1 : heading == Right ? 1 : 0;
                y += heading == Up ? -1 : heading == Down ? 1 : 0;
                const int* at = panel(x, y);
                if (!at) {
                    return false;
                }
                if (*at == to) {
                    return true;
                }
                // Each corner joins two sides: 2 top and right, 3 top and
                // left, 4 bottom and right, 5 bottom and left.
                static constexpr int kTurns[][4] = {
                    // heading up, down, left, right
                    {-1, Right, Up, -1},   // 2
                    {-1, Left, -1, Up},    // 3
                    {Right, -1, Down, -1}, // 4
                    {Left, -1, -1, Down},  // 5
                };
                int next = -1;
                if (*at >= 2 && *at <= 5) {
                    next = kTurns[*at - 2][heading];
                } else if (*at == 11) {
                    next = heading ^ 1;
                }
                if (next < 0) {
                    return false;
                }
                heading = next;
            }
        }
    }
    return false;
}

bool BlockPuzzle::command(int number, Call& c, std::size_t first) {
    const std::size_t count = c.count() - first;
    auto integer = [&](std::size_t i) {
        return c.integer(first + i);
    };
    auto name = [&](std::size_t i) {
        const Value& v = c.at(first + i);
        return v.isText() ? xq::hash(v.text) : v.toUint();
    };
    auto result = [&](int v) {
        c.result = Value::ofInt(v);
        return true;
    };
    switch (number) {
    case 0: {
        // 0x21c780: name, first cell x, y, cells across, down, cell size,
        // slide sound, glide frames, push mode, model scale; the box's four
        // walls.
        if (count < 10) {
            return false;
        }
        originX_ = std::int16_t(integer(1));
        originY_ = std::int16_t(integer(2));
        width_ = std::int16_t(integer(3));
        height_ = std::int16_t(integer(4));
        size_ = integer(5);
        glideFrames_ = c.real(first + 7);
        push_ = integer(8) != 0;
        Object* o = addWallObject();
        const float s = float(size_), x0 = float(originX_), y0 = float(originY_);
        const float w = float(width_) * s, h = float(height_) * s;
        o->areas.push_back({4, x0 - s, y0 - s, w + 2 * s, s});
        o->areas.push_back({4, x0 - s, y0, s, h});
        o->areas.push_back({4, x0 + w, y0, s, h});
        o->areas.push_back({4, x0 - s, y0 + h, w + 2 * s, s});
        slideSound_ = integer(6);
        modelScale_ = c.real(first + 9);
        return result(1);
    }
    case 1: {
        // 0x21cdf4: blocked cells (x, y, across, down), on the wall object
        // while it has room for them (16 areas), else on a new one.
        if (count < 4) {
            return false;
        }
        char wall[32];
        std::snprintf(wall, sizeof wall, "lt5_slide_wall%02d", walls_);
        Object* o = game_.scene.object(xq::hash(wall));
        if (!o) {
            return false;
        }
        const Object::Area area{4, float(originX_ + integer(0) * size_),
                                float(originY_ + integer(1) * size_), float(integer(2) * size_),
                                float(integer(3) * size_)};
        if (o->areas.size() >= 16) {
            ++walls_;
            o = addWallObject();
        }
        o->areas.push_back(area);
        return result(1);
    }
    case 2: {
        // 0x21ccdc: a block (made by the script, kind 7) at a cell, with
        // its handler blocks.
        if (count < 3) {
            return false;
        }
        Object* o = game_.scene.object(name(0));
        if (!o) {
            return false;
        }
        const std::string normal = push_ ? "Nazo_Slide2ObjNormal" : "Nazo_SlideObjNormal";
        const std::string handlers[] = {normal, "Nazo_SlideObjSlide", std::string()};
        game_.setHandlers(*o, 3, handlers);
        const Point p = cellPixel(std::int16_t(integer(1)), std::int16_t(integer(2)));
        place(*o, float(p.x), float(p.y));
        return result(1);
    }
    case 3: {
        // 0x21d3e8: a block's shape: rectangles of cells (x, y, across,
        // down) after a flag; each is a touch area (kind 2) and, unless
        // the flag is set, a collision rectangle (kind 4).
        if (count < 6) {
            return false;
        }
        Object* o = game_.scene.object(name(0));
        if (!o) {
            return false;
        }
        const bool noCollision = c.at(first + 1).truthy();
        for (std::size_t i = 0; i < (count & ~std::size_t(3)); i += 4) {
            const float x = float(integer(2 + i) * size_), y = float(integer(3 + i) * size_);
            const float w = float(integer(4 + i) * size_), h = float(integer(5 + i) * size_);
            if (!noCollision && o->areas.size() < 16) {
                o->areas.push_back({4, x, y, w, h});
            }
            if (o->areas.size() < 16) {
                o->areas.push_back({2, x, y, w, h});
            }
        }
        return result(1);
    }
    case 4:
        // 0x21c63c: every block back to its first cell (values 5, 6).
        for (Object* o : objectsOfKind(7)) {
            const int x = o->values.size() > 5 ? std::int16_t(o->values[5].toInt()) : 0;
            const int y = o->values.size() > 6 ? std::int16_t(o->values[6].toInt()) : 0;
            const Point p = cellPixel(x, y);
            place(*o, float(p.x), float(p.y));
        }
        return result(1);
    case 5: {
        // 0x21c4a8: a goal cell (x, y) for the block whose animation is
        // named so.
        if (count < 3 || goals_.size() >= 16) {
            return false;
        }
        const int x = std::int16_t(integer(0)), y = std::int16_t(integer(1));
        goals_.push_back({originX_ + x * size_, originY_ + y * size_, originX_ + (x + 1) * size_,
                          originY_ + (y + 1) * size_, name(2)});
        return result(1);
    }
    case 7: {
        // The drag: the block towards (x, y); the slide sound while it
        // moves, stopped when it cannot.
        if (count < 3) {
            return false;
        }
        if (game_.objectTouchLocked) {
            return true;
        }
        Object* o = game_.scene.object(name(0));
        const Point target{std::int16_t(integer(1)), std::int16_t(integer(2))};
        if (!o || !move(*o, target)) {
            game_.stopEffect(slideSound_, 0);
        } else if (!game_.effectsSounding(slideSound_)) {
            game_.playEffect(slideSound_);
        }
        return result(1);
    }
    case 13: {
        if (count == 0) {
            return false;
        }
        if (Object* o = game_.scene.object(name(0))) {
            release(*o);
        } else {
            checkGoals();
        }
        return result(1);
    }
    case 14: {
        // Sixth edition: the panel board, empty, with room for across x
        // down cells (the block grid's without operands).
        int n = width_ * height_;
        if (count == 1) {
            n = integer(0);
        } else if (count >= 2) {
            n = integer(0) * integer(1);
        }
        panels_.assign(std::size_t(std::uint16_t(n)), 0);
        return result(1);
    }
    case 15: {
        // A board row: y, then one value per cell across.
        if (count == 0 || int(count) - 1 != width_) {
            return false;
        }
        const int y = std::int16_t(integer(0));
        for (int x = 0; x < width_; ++x) {
            if (int* v = panel(x, y)) {
                *v = integer(1 + std::size_t(x));
            }
        }
        return result(1);
    }
    case 16:
        // A board cell's value (x, y, value).
        if (count < 3) {
            return false;
        }
        if (int* v = panel(std::int16_t(integer(0)), std::int16_t(integer(1)))) {
            *v = integer(2);
        }
        return result(1);
    case 17: {
        // A board cell's value (x, y). Off the board the original reads
        // through a null entry; the scripts stay on it.
        if (count < 2) {
            return false;
        }
        const int* v = panel(std::int16_t(integer(0)), std::int16_t(integer(1)));
        return result(v ? *v : 0);
    }
    case 18: {
        // The panel drag: start cell x, y and the block.
        if (count < 3) {
            return false;
        }
        Object* o = game_.scene.object(name(2));
        if (!o) {
            return false;
        }
        dragPanel(*o, {std::int16_t(integer(0)), std::int16_t(integer(1))});
        return result(1);
    }
    case 19: {
        // A panel moved on the board: from x, y to x, y, its value.
        if (count < 5) {
            return false;
        }
        if (int* v = panel(std::int16_t(integer(0)), std::int16_t(integer(1)))) {
            *v = 0;
        }
        if (int* v = panel(std::int16_t(integer(2)), std::int16_t(integer(3)))) {
            *v = integer(4);
        }
        return result(1);
    }
    case 20:
        // Whether a path joins the marks (from, to); no mark, no result.
        if (count < 2) {
            return false;
        }
        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                if (const int* v = panel(x, y); v && *v == integer(0)) {
                    return result(connected(integer(0), integer(1)) ? 1 : 0);
                }
            }
        }
        return false;
    case 22:
        // The original walks the board and does nothing with it.
        return result(1);
    case 21: {
        // Sixth edition (0x2e4820): where the block would land, into two
        // registers; the script compares it with the old cell to count a
        // move.
        if (count < 3) {
            return false;
        }
        const Object* o = game_.scene.object(name(0));
        if (!o) {
            return result(0);
        }
        const Point p = landing(*o);
        for (const auto& [i, v] : {std::pair{1, p.x}, std::pair{2, p.y}}) {
            if (Value* r = c.reg(first + std::size_t(i))) {
                *r = Value::ofInt(v);
            }
        }
        return result(1);
    }
    default:
        return false;
    }
}

} // namespace

std::unique_ptr<SpecialPuzzle> makeBlockPuzzle(Game& game) {
    return std::make_unique<BlockPuzzle>(game);
}

} // namespace picarat::adventure5
