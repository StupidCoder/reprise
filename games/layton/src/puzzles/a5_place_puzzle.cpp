// The fifth adventure's placing puzzles (catalogue type 14, loop 3110;
// puzzles 19, 46, 63, 84, 87, 145, 147, 167): pieces made of rectangles
// are dragged on a fine grid, turned in quarter steps and flipped; the
// puzzle is solved when they cover the judge rectangles exactly.
//
// Reconstructed from the loop's command handler 0x232d20 (vtable 0x4026c8,
// constructor 0x38687c; no own input or update) and its helpers: setup
// 0x233340, the flip button 0x233500, drag bounds 0x233980, grid snap
// 0x234334, the judge 0x2344d0 with the cover check 0x234bc4, the named
// piece check 0x234648, a rectangle turned and flipped 0x27e76c and a
// position to grid lines 0x27e6b4.
//
// How it plays: the pieces are the script's kind-7 objects on the lower
// screen; their areas of the judge kind (setup operand 7) are the cells
// they fill, turned by their stepped rotation (5235, four steps) and
// flipped by their scale's signs. The script drags them within the bounds
// of command 6, snapping to the grid (9), turns them (5236) and flips them
// with the flip button (5, shown next to the selected piece by 4). After
// each move the judge (11) checks the judge rectangles (2, in groups made
// by 14; optional cell counts): every cell covered by exactly one piece
// cell, and no piece cell outside them. Puzzles with "before" rectangles
// (7) first need those covered; then the listed objects (8) play their
// animation, drop to priority 110 and take no touches, and the judge counts
// cells against the optional counts.
//
// Not reconstructed: command 13 (no script of this game sends it).
#include "puzzles/a5_special.hpp"
#include <cmath>
#include <cstdio>

namespace picarat::adventure5 {
namespace {

using adventure56::Object;
using xq::Call;
using xq::Value;

struct Cell {
    int x = 0, y = 0;
};

struct Box {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0; // cells, x1 / y1 exclusive
};

class PlacePuzzle final : public SpecialPuzzle {
public:
    explicit PlacePuzzle(Game& game) : game_(game) {}

    bool command(int number, Call& c, std::size_t first) override;
    std::string describe() const override;

private:
    struct Rect {
        int x = 0, y = 0, w = 0, h = 0; // cells
    };

    struct Group {
        std::size_t first = 0, count = 0; // into judge_
    };

    // 0x2cc06c: the pieces (kind 7 on the setup's screen), in their order.
    std::vector<Object*> pieces() const {
        std::vector<Object*> list;
        for (Object* o : game_.scene.screenList(screen_)) {
            if (o->kind == 7) {
                list.push_back(o);
            }
        }
        return list;
    }

    // 0x27e6b4: a position to the nearest grid line (truncating division).
    Cell gridLine(float x, float y) const {
        const int half = size_ >> 1;
        return {std::int16_t((std::int16_t(int(x) - originX_) + half) / size_),
                std::int16_t((std::int16_t(int(y) - originY_) + half) / size_)};
    }

    // 0x27e76c: a rectangle (x0, y0, x1, y1) flipped by the scale's signs,
    // turned by the step (1: (x, y) -> (-y, x), 2: (-x, -y), 3: (y, -x)),
    // as min / max corners.
    static std::array<int, 4> turned(std::array<int, 4> r, float sx, float sy, int step) {
        const int fx = 0.0f < sx ? 1 : -1, fy = 0.0f < sy ? 1 : -1;
        int ax = std::int16_t(r[0] * fx), ay = std::int16_t(r[1] * fy);
        int bx = std::int16_t(r[2] * fx), by = std::int16_t(r[3] * fy);
        auto turn = [step](int& x, int& y) {
            const int ox = x, oy = y;
            if (step == 1) {
                x = std::int16_t(-oy);
                y = ox;
            } else if (step == 2) {
                x = std::int16_t(-ox);
                y = std::int16_t(-oy);
            } else if (step == 3) {
                x = oy;
                y = std::int16_t(-ox);
            }
        };
        turn(ax, ay);
        turn(bx, by);
        return {std::min(ax, bx), std::min(ay, by), std::max(ax, bx), std::max(ay, by)};
    }

    // A piece's areas of `kind` as cell boxes; false when one leaves the
    // grid's first row or column (a negative cell).
    bool pieceCells(const Object& o, int kind, std::vector<Box>& out,
                    std::vector<std::size_t>* mine = nullptr) const {
        const float px = o.position[0] + o.offset[0], py = o.position[1] + o.offset[1];
        for (const auto& a : o.areas) {
            if (a.kind != kind) {
                continue;
            }
            const auto r = turned({std::int16_t(int(a.x)), std::int16_t(int(a.y)),
                                   std::int16_t(int(a.x + a.w)), std::int16_t(int(a.y + a.h))},
                                  o.scale[0], o.scale[1], o.stepIndex);
            const Cell c0 = gridLine(float(r[0]) + px, float(r[1]) + py);
            const Cell c1 = gridLine(float(r[2]) + px, float(r[3]) + py);
            if (c0.x < 0 || c0.y < 0) {
                return false;
            }
            if (mine) {
                mine->push_back(out.size());
            }
            out.push_back({c0.x, c0.y, c1.x, c1.y});
            if (out.size() > 0x80) {
                break;
            }
        }
        return true;
    }

    // Sixth adventure only. A piece's judge-kind cell boxes (at most
    // `limit`); `negative` tells whether one starts at a negative cell.
    std::vector<Box> boxesOf(const Object& o, std::size_t limit, bool* negative) const {
        std::vector<Box> out;
        const float px = o.position[0] + o.offset[0], py = o.position[1] + o.offset[1];
        for (const auto& a : o.areas) {
            if (a.kind != judgeKind_ || out.size() >= limit) {
                continue;
            }
            const auto r = turned({std::int16_t(int(a.x)), std::int16_t(int(a.y)),
                                   std::int16_t(int(a.x + a.w)), std::int16_t(int(a.y + a.h))},
                                  o.scale[0], o.scale[1], o.stepIndex);
            const Cell c0 = gridLine(float(r[0]) + px, float(r[1]) + py);
            const Cell c1 = gridLine(float(r[2]) + px, float(r[3]) + py);
            if (negative && (c0.x < 0 || c0.y < 0)) {
                *negative = true;
            }
            out.push_back({c0.x, c0.y, c1.x, c1.y});
        }
        return out;
    }

    static bool overlaps(const Box& a, const Box& b) {
        return a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1;
    }

    float coverage();
    bool insideGroup(std::uint32_t piece, const Group& g);
    bool apart(std::uint32_t piece);

    bool check(std::size_t first, std::size_t count, int kind);
    bool checkPiece(std::size_t first, std::size_t count, std::uint32_t name);
    bool judge();
    void showTurnButton(Object* piece);

    Game& game_;
    int originX_ = 0, originY_ = 0;                                 // +0x160 / +0x162
    int width_ = 0, height_ = 0;                                    // +0x164 / +0x166: cells
    int size_ = 1;                                                  // +0x168
    std::vector<Rect> judge_;                                       // +0x16c (at most 32)
    std::vector<int> counts_ = std::vector<int>(32, -1);            // +0x2cc
    std::vector<Group> groups_ = {Group{}};                         // +0x270 (at most 9)
    bool solved_ = false;                                           // +0x2c4
    std::vector<Rect> before_;                                      // +0x34c (at most 32)
    bool beforeDone_ = true;                                        // +0x450
    std::vector<std::pair<std::uint32_t, std::uint32_t>> onBefore_; // +0x454
    int judgeKind_ = 1;                                             // +0x4d8
    int screen_ = 1;                                                // +0x4d9
    std::uint32_t turnHash_ = 0;                                    // "lt5_ltgrm_turn"
};

// 0x303684 (sixth adventure, command 13): the pieces that are shown and
// take touches, in order, each dropped when one of its boxes overlaps a box
// of any earlier piece; then per group the cells of its rectangles that
// some kept box covers over the group's total (each rectangle's count when
// it has one, else its area); the best group's share.
float PlacePuzzle::coverage() {
    std::vector<std::vector<Box>> kept;
    std::vector<std::vector<Box>> all;
    for (Object* o : pieces()) {
        if (!o->visible || !o->touchOn || all.size() >= 64) {
            continue;
        }
        std::vector<Box> mine = boxesOf(*o, 16, nullptr);
        bool valid = true;
        for (const auto& earlier : all) {
            for (const Box& a : mine) {
                for (const Box& b : earlier) {
                    valid = valid && !overlaps(a, b);
                }
            }
        }
        all.push_back(mine);
        if (valid) {
            kept.push_back(mine);
        }
    }
    float best = 0;
    for (const Group& g : groups_) {
        int covered = 0, total = 0;
        for (std::size_t k = 0; k < g.count; ++k) {
            const Rect& r = judge_[g.first + k];
            for (int y = 0; y < r.h; ++y) {
                for (int x = 0; x < r.w; ++x) {
                    const int cx = r.x + x, cy = r.y + y;
                    bool hit = false;
                    for (const auto& piece : kept) {
                        for (const Box& b : piece) {
                            hit = hit || (b.x0 <= cx && cx < b.x1 && b.y0 <= cy && cy < b.y1);
                        }
                    }
                    covered += hit ? 1 : 0;
                }
            }
            total += counts_[k] >= 0 ? counts_[k] : r.w * r.h;
        }
        if (total > 0) {
            best = std::max(best, float(covered) / float(total));
        }
    }
    return best;
}

// 0x302cb8 (sixth adventure, command 15): the named piece (shown, taking
// touches) has every cell of its boxes (at most 9) inside the group's
// rectangles; false when a box starts at a negative cell; true when the
// piece is not found.
bool PlacePuzzle::insideGroup(std::uint32_t piece, const Group& g) {
    for (Object* o : pieces()) {
        if (o->hash != piece || !o->visible || !o->touchOn) {
            continue;
        }
        bool negative = false;
        const std::vector<Box> mine = boxesOf(*o, 9, &negative);
        if (negative) {
            return false;
        }
        for (const Box& b : mine) {
            for (int y = b.y0; y < b.y1; ++y) {
                for (int x = b.x0; x < b.x1; ++x) {
                    bool in = false;
                    for (std::size_t k = 0; k < g.count && !in; ++k) {
                        const Rect& r = judge_[g.first + k];
                        in = r.x <= x && x < r.x + r.w && r.y <= y && y < r.y + r.h;
                    }
                    if (!in) {
                        return false;
                    }
                }
            }
        }
        return true;
    }
    return true;
}

// 0x302600 (sixth adventure, command 16): no box of another piece (shown,
// taking touches; boxes at negative cells ignored) overlaps the named
// piece's boxes (at most 9); false when one of those starts at a negative
// cell; true when the piece is not found.
bool PlacePuzzle::apart(std::uint32_t piece) {
    std::vector<Box> mine;
    for (Object* o : pieces()) {
        if (o->hash == piece && o->visible && o->touchOn) {
            bool negative = false;
            mine = boxesOf(*o, 9, &negative);
            if (negative) {
                return false;
            }
            break;
        }
    }
    for (Object* o : pieces()) {
        if (o->hash == piece || !o->visible || !o->touchOn) {
            continue;
        }
        for (const Box& b : boxesOf(*o, 0x80, nullptr)) {
            if (b.x0 < 0 || b.y0 < 0) {
                continue;
            }
            for (const Box& a : mine) {
                if (overlaps(a, b)) {
                    return false;
                }
            }
        }
    }
    return true;
}

// 0x234bc4: every cell of the rectangles covered by at most one piece cell
// and fully (or, after the "before" stage, by the rectangle's count when it
// has one); with `kind` 4 also no piece cell outside them.
bool PlacePuzzle::check(std::size_t first, std::size_t count, int kind) {
    std::vector<Box> cells;
    for (Object* o : pieces()) {
        if (!o->visible || !o->touchOn) {
            continue;
        }
        if (!pieceCells(*o, judgeKind_, cells)) {
            return false;
        }
    }
    bool ok = true;
    for (std::size_t i = 0; i < count && ok; ++i) {
        const Rect& r = judge_[first + i];
        int covered = 0;
        for (int y = 0; y < r.h; ++y) {
            for (int x = 0; x < r.w; ++x) {
                const int cx = std::int16_t(r.x + x), cy = std::int16_t(r.y + y);
                int here = 0;
                for (const Box& b : cells) {
                    if (cx >= b.x0 && cy >= b.y0 && cx < b.x1 && cy < b.y1) {
                        ++covered;
                        if (++here > 1) {
                            return false;
                        }
                    }
                }
            }
        }
        // The counts go by the position in the list passed (group or not).
        const int target = counts_[i];
        if (!beforeDone_ || target < 0) {
            if (r.w * r.h - covered != 0 && covered <= r.w * r.h) {
                ok = false;
            }
        } else if (covered < target) {
            return false;
        }
    }
    if (!ok) {
        return false;
    }
    if (!beforeDone_ && !before_.empty()) {
        return ok;
    }
    if (kind != 4) {
        return ok;
    }
    if (judgeKind_ != 4) {
        for (Object* o : pieces()) {
            if (o->visible && o->touchOn) {
                pieceCells(*o, 4, cells);
            }
        }
    }
    for (const Box& b : cells) {
        if (b.y1 - b.y0 < 1 || b.x1 - b.x0 < 1) {
            return false;
        }
        for (int y = b.y0; y < b.y1; ++y) {
            for (int x = b.x0; x < b.x1; ++x) {
                bool inside = false;
                for (std::size_t i = 0; i < count && !inside; ++i) {
                    const Rect& r = judge_[first + i];
                    inside = x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
                }
                if (!inside) {
                    return false;
                }
            }
        }
    }
    return true;
}

// 0x234648: the named piece overlaps no other piece within the rectangles
// and lies wholly inside them.
bool PlacePuzzle::checkPiece(std::size_t first, std::size_t count, std::uint32_t name) {
    std::vector<Box> cells;
    std::vector<std::size_t> mine;
    for (Object* o : pieces()) {
        if (!o->visible || !o->touchOn) {
            continue;
        }
        if (!pieceCells(*o, judgeKind_, cells, o->hash == name ? &mine : nullptr)) {
            return false;
        }
    }
    auto isMine = [&](std::size_t i) {
        return std::find(mine.begin(), mine.end(), i) != mine.end();
    };
    for (std::size_t i = 0; i < count; ++i) {
        const Rect& r = judge_[first + i];
        for (int y = 0; y < r.h; ++y) {
            for (int x = 0; x < r.w; ++x) {
                const int cx = std::int16_t(r.x + x), cy = std::int16_t(r.y + y);
                int here = 0;
                for (std::size_t k = 0; k < cells.size(); ++k) {
                    const Box& b = cells[k];
                    if (cx >= b.x0 && cy >= b.y0 && cx < b.x1 && cy < b.y1 && ++here > 1 &&
                        isMine(k)) {
                        return false;
                    }
                }
            }
        }
    }
    for (std::size_t k : mine) {
        const Box& b = cells[k];
        if (b.y1 - b.y0 < 1) {
            return false;
        }
        for (int y = b.y0; y < b.y1; ++y) {
            bool any = false;
            for (int x = b.x0; x < b.x1; ++x) {
                bool inside = false;
                for (std::size_t i = 0; i < count && !inside; ++i) {
                    const Rect& r = judge_[first + i];
                    inside = x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
                }
                if (!inside || count == 0) {
                    return false;
                }
                any = true;
            }
            if (!any) {
                return false;
            }
        }
    }
    return true;
}

// 0x2344d0: the "before" stage first (then its objects play their
// animation, drop to priority 110 and take no touch); then each group.
bool PlacePuzzle::judge() {
    solved_ = false;
    if (!beforeDone_) {
        // The before rectangles are judged as a list of their own.
        const std::vector<Rect> saved = judge_;
        const std::vector<int> savedCounts = counts_;
        judge_ = before_;
        const bool ok = check(0, before_.size(), 4);
        judge_ = saved;
        counts_ = savedCounts;
        if (!ok) {
            return false;
        }
        for (const auto& [object, animation] : onBefore_) {
            if (Object* o = game_.scene.object(object)) {
                game_.scene.setPriority(*o, 110);
                game_.playAnimation(*o, Value::ofUint(animation), 0);
                o->touchOn = false;
            }
        }
        beforeDone_ = true;
    }
    if (!judge_.empty()) {
        bool any = false;
        for (const Group& g : groups_) {
            any |= check(g.first, g.count, judgeKind_);
        }
        if (any) {
            solved_ = true;
            if (Object* t = game_.scene.object(turnHash_)) {
                t->visible = false;
            }
        }
    }
    return solved_;
}

// 0x233500: the flip button beside the piece's outline corner that faces
// the grid's middle ("turn1".."turn4"); hidden without a piece.
void PlacePuzzle::showTurnButton(Object* piece) {
    Object* t = game_.scene.object(turnHash_);
    if (!t) {
        return;
    }
    if (!piece) {
        t->visible = false;
        return;
    }
    if (t->values.size() < 2) {
        t->values.resize(2);
    }
    t->values[1] = Value::ofText(piece->name);
    t->visible = true;
    const int px = std::int16_t(int(piece->position[0])),
              py = std::int16_t(int(piece->position[1]));
    const int midX = std::int16_t(originX_ + (width_ / 2) * size_),
              midY = std::int16_t(originY_ + (height_ / 2) * size_);
    // The union of the piece's areas (0x57c), turned and flipped.
    float ux0 = 0, uy0 = 0, ux1 = 0, uy1 = 0;
    for (std::size_t i = 0; i < piece->areas.size(); ++i) {
        const auto& a = piece->areas[i];
        ux0 = i ? std::min(ux0, a.x) : a.x;
        uy0 = i ? std::min(uy0, a.y) : a.y;
        ux1 = i ? std::max(ux1, a.x + a.w) : a.x + a.w;
        uy1 = i ? std::max(uy1, a.y + a.h) : a.y + a.h;
    }
    const auto r = turned({std::int16_t(int(ux0)), std::int16_t(int(uy0)), std::int16_t(int(ux1)),
                           std::int16_t(int(uy1))},
                          piece->scale[0], piece->scale[1], piece->stepIndex);
    const int x0 = std::int16_t(r[0] + px), y0 = std::int16_t(r[1] + py);
    const int x1 = std::int16_t(r[2] + px), y1 = std::int16_t(r[3] + py);
    const char* name;
    int x, y;
    if (px < midX) {
        name = py < midY ? "turn3" : "turn1";
        x = x1;
        y = py < midY ? y1 : y0;
    } else {
        name = py < midY ? "turn4" : "turn2";
        x = x0;
        y = py < midY ? y1 : y0;
    }
    game_.playAnimation(*t, Value::ofText(name), 0);
    t->position.set({float(x), float(y), t->position[2]});
}

std::string PlacePuzzle::describe() const {
    std::string out;
    char line[160];
    std::snprintf(line, sizeof line, "grid %d %d origin %d %d size %d judgekind %d turn %d\n",
                  width_, height_, originX_, originY_, size_, judgeKind_, turnHash_ ? 1 : 0);
    out += line;
    for (std::size_t g = 0; g < groups_.size(); ++g) {
        for (std::size_t i = groups_[g].first; i < groups_[g].first + groups_[g].count; ++i) {
            const Rect& r = judge_[i];
            std::snprintf(line, sizeof line, "judge %zu %d %d %d %d count %d\n", g, r.x, r.y, r.w,
                          r.h, counts_[i - groups_[g].first]);
            out += line;
        }
    }
    for (const Rect& r : before_) {
        std::snprintf(line, sizeof line, "before %d %d %d %d\n", r.x, r.y, r.w, r.h);
        out += line;
    }
    for (const Object* o : pieces()) {
        std::snprintf(line, sizeof line,
                      "piece %s at %g %g offset %g %g step %d/%d scale %g %g vis %d touch %d\n",
                      o->name.c_str(), o->position[0], o->position[1], o->offset[0], o->offset[1],
                      o->stepIndex, o->steps, o->scale[0], o->scale[1], int(o->visible),
                      int(o->touchOn));
        out += line;
        for (const auto& a : o->areas) {
            std::snprintf(line, sizeof line, "  area %d %g %g %g %g\n", a.kind, a.x, a.y, a.w, a.h);
            out += line;
        }
    }
    return out;
}

bool PlacePuzzle::command(int number, Call& c, std::size_t first) {
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
    auto setRegister = [&](std::size_t i, Value v) {
        if (Value* r = c.reg(first + i)) {
            *r = v;
        }
    };
    switch (number) {
    case 0: {
        // 0x233340: first cell x, y; cells across, down; cell size; the
        // flip button [and its animation resource]; the judge's area kind;
        // the pieces' screen.
        if (count < 6) {
            return false;
        }
        originX_ = std::int16_t(integer(0));
        originY_ = std::int16_t(integer(1));
        width_ = std::int16_t(integer(2));
        height_ = std::int16_t(integer(3));
        size_ = integer(4);
        const std::string* resource = c.text(first + 6);
        if (c.at(first + 5).truthy() && resource) {
            Object& t = game_.createObject("lt5_ltgrm_turn", 1, 112, 0, {});
            turnHash_ = t.hash;
            game_.setAnimationSet(t, xq::hash(*resource));
            const std::string handlers[] = {std::string(), "Nazo_GridTurnTouch"};
            game_.setHandlers(t, 1, handlers);
            t.visible = false;
        }
        if (count > 7) {
            judgeKind_ = std::int8_t(integer(7));
        }
        if (count > 8) {
            screen_ = std::int8_t(integer(8));
        }
        return result(1);
    }
    case 1:
        // Every piece back: its animation (value 4), place (10, 11), step
        // (8), opaque, unflipped.
        for (Object* o : pieces()) {
            if (o->values.size() > 4) {
                game_.playAnimation(*o, o->values[4], 0);
            }
            const float x = o->values.size() > 10 ? o->values[10].toFloat() : 0.0f;
            const float y = o->values.size() > 11 ? o->values[11].toFloat() : 0.0f;
            o->position.set({x, y, o->position[2]});
            // 0x175bac: the step at once (none without steps).
            if (o->steps == 0) {
                o->stepIndex = 0;
            } else if (o->values.size() > 8) {
                o->stepIndex = int(o->values[8].toUint() % std::uint32_t(o->steps));
                o->stepAngle.set({std::remainder(
                    6.28318531f * float(o->stepIndex) / float(o->steps), 6.28318531f)});
            }
            o->alpha.set({1.0f});
            o->scale.set({1.0f, 1.0f, o->scale[2]});
        }
        if (!before_.empty()) {
            beforeDone_ = false;
        }
        return result(1);
    case 2:
        // A judge rectangle (x, y, across, down [, cells to cover]).
        if (count < 4 || judge_.size() >= 32) {
            return false;
        }
        judge_.push_back({std::int16_t(integer(0)), std::int16_t(integer(1)),
                          std::int16_t(integer(2)), std::int16_t(integer(3))});
        if (count == 5) {
            counts_[judge_.size() - 1] = integer(4);
        }
        if (!groups_.empty()) {
            ++groups_.back().count;
        }
        return result(1);
    case 3:
        if (count == 0 || !game_.scene.object(name(0))) {
            return false;
        }
        return result(1);
    case 4:
        if (count == 0) {
            return false;
        }
        if (!solved_) {
            showTurnButton(game_.scene.object(name(0)));
            return result(1);
        }
        return true;
    case 5: {
        // Flip: across while the step is even, else down; then judge.
        if (count == 0) {
            return false;
        }
        Object* o = game_.scene.object(name(0));
        if (!o) {
            return false;
        }
        if ((o->stepIndex & 1) == 0) {
            o->scale.set({-o->scale[0], o->scale[1], o->scale[2]});
        } else {
            o->scale.set({o->scale[0], -o->scale[1], o->scale[2]});
        }
        return result(judge() ? 1 : 0);
    }
    case 6: {
        // 0x233980: where the piece's place may go (into four registers),
        // from its outline turned and flipped.
        if (count < 5) {
            return false;
        }
        Object* o = game_.scene.object(name(0));
        if (!o) {
            return false;
        }
        float ux = 0, uy = 0, uw = 0, uh = 0;
        if (!o->areas.empty()) {
            float x0 = o->areas[0].x, y0 = o->areas[0].y;
            float x1 = x0 + o->areas[0].w, y1 = y0 + o->areas[0].h;
            for (const auto& a : o->areas) {
                x0 = std::min(x0, a.x);
                y0 = std::min(y0, a.y);
                x1 = std::max(x1, a.x + a.w);
                y1 = std::max(y1, a.y + a.h);
            }
            ux = x0;
            uy = y0;
            uw = x1 - x0;
            uh = y1 - y0;
        }
        int step = o->stepIndex;
        float sy = o->scale[1];
        if (o->scale[0] < 0.0f) {
            sy = -sy;
            step = (step + 2) % 4;
        }
        float l = 0, t = 0, r = 0, b = 0;
        const float xr = uw + ux, yb = uh + uy;
        if (sy > 0.0f) {
            switch (step) {
            case 0:
                l = -ux, t = -uy, r = xr, b = yb;
                break;
            case 1:
                l = yb, t = -ux, r = -uy, b = xr;
                break;
            case 2:
                l = xr, t = yb, r = -ux, b = -uy;
                break;
            case 3:
                l = -uy, t = xr, r = yb, b = -ux;
                break;
            }
        } else {
            switch (step) {
            case 0:
                l = -ux, t = yb, r = xr, b = -uy;
                break;
            case 1:
                l = -uy, t = -ux, r = yb, b = xr;
                break;
            case 2:
                l = xr, t = -uy, r = -ux, b = yb;
                break;
            case 3:
                l = yb, t = xr, r = -uy, b = -ux;
                break;
            }
        }
        const int right = std::int16_t(originX_ + width_ * size_);
        const int bottom = std::int16_t(originY_ + height_ * size_);
        setRegister(1, Value::ofFloat(float(originX_) + l));
        setRegister(2, Value::ofFloat(float(originY_) + t));
        setRegister(3, Value::ofFloat(float(right) - r));
        setRegister(4, Value::ofFloat(float(bottom) - b));
        return result(1);
    }
    case 7:
        // A "before" rectangle.
        if (count < 4 || before_.size() >= 32) {
            return false;
        }
        before_.push_back({std::int16_t(integer(0)), std::int16_t(integer(1)),
                           std::int16_t(integer(2)), std::int16_t(integer(3))});
        beforeDone_ = false;
        return result(1);
    case 8:
        // An object and the animation it plays once the before stage is
        // done.
        if (count < 2) {
            return false;
        }
        onBefore_.push_back({name(0), name(1)});
        return result(1);
    case 9: {
        // 0x234334: the piece to the nearest grid point (of its place, or
        // of the given x, y), within the grid.
        if (count == 0) {
            return false;
        }
        Object* o = game_.scene.object(name(0));
        if (!o) {
            return false;
        }
        const Cell g = count < 3 ? gridLine(o->position[0], o->position[1])
                                 : gridLine(c.real(first + 1), c.real(first + 2));
        const int x = std::clamp(g.x, 0, width_), y = std::clamp(g.y, 0, height_);
        o->position.set({float(std::int16_t(originX_ + x * size_)),
                         float(std::int16_t(originY_ + y * size_)), o->position[2]});
        return result(1);
    }
    case 10: {
        // x, y to the grid's pitch (into two registers).
        if (count < 4) {
            return false;
        }
        const int x = integer(0), y = integer(1);
        setRegister(2, Value::ofInt(x - x % size_ + originX_ % size_));
        setRegister(3, Value::ofInt(y - y % size_ + originY_ % size_));
        return result(1);
    }
    case 11:
        return result(judge() ? 1 : 0);
    case 12: {
        if (count == 0) {
            return false;
        }
        bool any = false;
        for (const Group& g : groups_) {
            any |= checkPiece(g.first, g.count, name(0));
        }
        return result(any ? 1 : 0);
    }
    case 13: {
        // Sixth adventure only (0x3024ac -> 0x303684): the best share of a
        // group's cells the pieces cover, into a register; result 1.
        if (game_.edition != Edition::Sixth || count == 0) {
            return false;
        }
        if (Value* r = c.reg(first)) {
            *r = Value::ofFloat(coverage());
        }
        return result(1);
    }
    case 15: {
        // Sixth adventure only (0x302514 -> 0x302cb8): whether the piece lies
        // wholly inside some group's rectangles.
        if (game_.edition != Edition::Sixth || count == 0) {
            return false;
        }
        bool any = false;
        for (const Group& g : groups_) {
            any = insideGroup(name(0), g) || any;
        }
        return result(any ? 1 : 0);
    }
    case 16:
        // Sixth adventure only (0x302598 -> 0x302600): whether the piece
        // overlaps no other piece.
        if (game_.edition != Edition::Sixth || count == 0) {
            return false;
        }
        return result(apart(name(0)) ? 1 : 0);
    case 14:
        // A new group of judge rectangles.
        if (groups_.size() >= 9) {
            return result(0);
        }
        groups_.push_back({judge_.size(), 0});
        return result(1);
    default:
        return false;
    }
}

} // namespace

std::unique_ptr<SpecialPuzzle> makePlacePuzzle(Game& game) {
    return std::make_unique<PlacePuzzle>(game);
}

} // namespace picarat::adventure5
