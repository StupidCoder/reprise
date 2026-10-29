// The fifth adventure's balloon string puzzles (catalogue type 47, loop
// 3270; puzzles 5 and 132): balloon strings run down a board of knots to the
// hand at the bottom; a button between four knots swaps the two strings'
// pieces between its rows, so the strings cross. The puzzle is solved when
// every string runs straight down again.
//
// Reconstructed from the loop's command handler 0x2e0c18 (vtable 0x402d20,
// constructor 0x37e5c4) with setup 0x2e10a4, reset 0x2e0e78, swap 0x2e1f80,
// scripted swap 0x2e2240, colours 0x2e23d4, its update 0x2e0bf0 (strings
// 0x2e1d8c with the straight 0x34b1a0 and curved 0x34ae64 pieces) and the
// frame pieces 0x27b52c.
//
// How it plays: the setup (0) lays out knots "bln_ref_CR" (column, row; no
// picture) on a grid, an anchor "bln_ref_topC" above each column and
// "ref_btm" at the bottom; a string joins each anchor to its column's knots
// and the last knot to "ref_btm" ("bln_lin_obj", in the column's colour,
// 6). Between each 2x2 group of knots sits a button "bln_btn_CR" (the
// script's "Nazo_BalloonBtnTouch"); its values name the four knots' slots.
// A swap (2) moves the knot in each slot to the slot beside it in the same
// row (left and right swap, in both rows) over the given frames, and the
// anchors follow their knots; the script's own scrambling (3, at once)
// also makes the new places the reset places. A string piece between
// knots at the same x is straight; otherwise it is a smooth S-curve, and
// the judge (4) counts those. The puzzle is solved when each column's
// knots (by their names) stand at one x again. The board's frame pieces
// (cell "frame" in "mrg_frm") leave out the two under a swap while the
// script's "left_frm" / "right_frm" slide; buttons are drawn as pieces
// (cell "button") in "mrg_btn".
#include "puzzles/a5_special.hpp"
#include <cmath>
#include <cstdio>

namespace picarat::adventure5 {
namespace {

using adventure56::Object;
using xq::Call;
using xq::Value;

// 0x34b194 / 0x34b198 / 0x34b19c: a curved string has one piece per four
// pixels down, along sin from -pi/2 to pi/2.
constexpr float kPieceHeight = 4.0f;
constexpr float kPi = 3.14159274f; // 0x40490fdb
// 0x34b41c: a straight string is drawn this far to the left.
constexpr float kStraightShift = 0.01f;

class BalloonPuzzle final : public SpecialPuzzle {
public:
    explicit BalloonPuzzle(Game& game) : game_(game) {}

    bool command(int number, Call& c, std::size_t first) override;
    void update() override;

private:
    // A slot of the knot grid (8 bytes): the knot there now and at reset.
    struct Slot {
        std::uint32_t now = 0, reset = 0;
    };

    // A string piece (12 bytes): from one object to another, in a column's
    // colour.
    struct Link {
        std::uint32_t from = 0, to = 0;
        int column = 0;
    };

    static std::string name(const char* format, int a, int b = 0) {
        char text[32];
        std::snprintf(text, sizeof text, format, a, b);
        return text;
    }

    // 0x2aa814: a cell of the object's animation set by name.
    static int cellIndex(const Object& o, const std::string& cell) {
        if (!o.animation) {
            return -1;
        }
        const std::uint32_t h = xq::hash(cell);
        for (std::size_t i = 0; i < o.animation->cells.size(); ++i) {
            if (o.animation->cells[i].hash == h) {
                return int(i);
            }
        }
        return -1;
    }

    int valueInt(const Object& o, std::size_t i) const {
        const Value v = game_.objectValue(o, i);
        return std::int16_t(v.toInt());
    }

    bool setup(Call& c, std::size_t first, std::size_t count);
    bool reset(float frames);
    bool swap(const int slots[4], float frames, bool atOnce, int hiddenFrame);
    void rebuildFrames(int hidden);
    void drawStrings();
    void addStraight(Object& o, std::array<float, 2> a, std::array<float, 2> b,
                     const std::array<float, 3>& colour);
    void addCurve(Object& o, std::array<float, 2> a, std::array<float, 2> b,
                  const std::array<float, 3>& colour);

    Game& game_;
    std::vector<Link> links_;       // +0x168
    float lineWidth_ = 0;           // +0x174
    int curved_ = 0;                // +0x17c
    std::uint16_t colours_[8] = {}; // +0x180: 5-5-5-1 per column
    std::uint32_t strings_ = 0;     // +0x190: "bln_lin_obj"
    bool redraw_ = false;           // +0x194
    std::vector<Slot> slots_;       // +0x198
    int originX_ = 0, originY_ = 0; // +0x1a4 / +0x1a6: the first knot
    int columns_ = 0, rows_ = 0;    // +0x1a8 / +0x1aa: knots across, down
    int cellW_ = 0, cellH_ = 0;     // +0x1ac / +0x1ae
    std::uint32_t set_ = 0;         // the animation set
};

// 0x2e10a4: animation set; first column x, first row y; columns; rows of
// cells; cell width, height; string width; (one more value, read and
// dropped).
bool BalloonPuzzle::setup(Call& c, std::size_t first, std::size_t count) {
    if (count < 9) {
        return false;
    }
    auto integer = [&](std::size_t i) {
        return std::int16_t(c.integer(first + i));
    };
    const std::string* setName = c.text(first);
    set_ = setName ? xq::hash(*setName) : 0;
    cellW_ = integer(5);
    cellH_ = integer(6);
    const int halfW = cellW_ >> 1, halfH = cellH_ >> 1;
    originX_ = std::int16_t(integer(1) + halfW);
    originY_ = integer(2);
    columns_ = integer(3);
    rows_ = std::int16_t(integer(4) + 1);
    lineWidth_ = c.real(first + 7);
    slots_.clear();
    links_.clear();

    // "ref_btm" (screen 1, priority 111) at the bottom middle.
    game_.createObject("ref_btm", 1, 0x6f, 0, {}).position.set({160, 240, 0});
    // The knots, row by row; a knot's values: name, 7, x, y, its anchor
    // (first row only).
    for (int r = 0; r < rows_; ++r) {
        for (int col = 0; col < columns_; ++col) {
            const std::string knot = name("bln_ref_%d%d", col, r);
            const int x = std::int16_t(cellW_ * col + originX_);
            const int y = std::int16_t(cellH_ * r + originY_);
            const std::string top = r == 0 ? name("bln_ref_top%d", col) : std::string();
            const Value values[] = {Value::ofText(knot), Value::ofInt(7), Value::ofInt(x),
                                    Value::ofInt(y), Value::ofText(top)};
            Object& o = game_.createObject(knot, 1, 0x6f, 0, values);
            o.position.set({float(x), float(y), o.position[2]});
            slots_.push_back({o.hash, o.hash});
        }
    }
    Object& frames = game_.createObject("mrg_frm", 1, 0x6e, 0, {});
    game_.setAnimationSet(frames, set_);
    rebuildFrames(-1);
    Object& buttons = game_.createObject("mrg_btn", 1, 0x6f, 0, {});
    game_.setAnimationSet(buttons, set_);
    for (const char* slide : {"left_frm", "right_frm"}) {
        Object& o = game_.createObject(slide, 1, 0x6e, 0, {});
        game_.setAnimationSet(o, set_);
        game_.playAnimation(o, Value::ofText("mov_frame"), 0); // hash at 0x4129c8
        o.touchOn = false;
        o.drawn = false;
    }
    // The buttons between each 2x2 group of knots: values name, 7, the
    // slots (row, column) (r, c), (r + 1, c), (r, c + 1), (r + 1, c + 1),
    // and the names "bln_frm_CR" of the two cells.
    const int buttonIndex = cellIndex(buttons, "button");
    for (int r = 0; r < rows_ - 1; ++r) {
        for (int col = 0; col < columns_ - 1; ++col) {
            const std::string button = name("bln_btn_%d%d", col, r);
            const Value values[] = {Value::ofText(button),
                                    Value::ofInt(7),
                                    Value::ofInt(r * columns_ + col),
                                    Value::ofInt((r + 1) * columns_ + col),
                                    Value::ofInt(r * columns_ + col + 1),
                                    Value::ofInt((r + 1) * columns_ + col + 1),
                                    Value::ofUint(xq::hash(name("bln_frm_%d%d", col, r))),
                                    Value::ofUint(xq::hash(name("bln_frm_%d%d", col + 1, r)))};
            Object& o = game_.createObject(button, 1, 0x70, 0, values);
            const int x = std::int16_t(cellW_ * col + originX_ + halfW);
            const int y = std::int16_t(cellH_ * r + originY_ + halfH);
            o.position.set({float(x), float(y), o.position[2]});
            // 0x2e1b5c / 0x2e1b58: a 25 x 25 touch square from -12.
            o.areas.push_back({1, -12, -12, 25, 25});
            const std::string handlers[] = {"Nazo_BalloonBtnNormal", "Nazo_BalloonBtnTouch"};
            game_.setHandlers(o, 1, handlers);
            buttons.pieces.push_back(
                {buttonIndex, float(std::int16_t(x - 8)), float(std::int16_t(y - 8))});
        }
    }
    // The anchors and the strings, column by column.
    for (int col = 0; col < columns_; ++col) {
        const std::string top = name("bln_ref_top%d", col);
        Object& o = game_.createObject(top, 1, 0x6f, 0, {});
        o.position.set({float(std::int16_t(cellW_ * col + originX_)), 0, o.position[2]});
        links_.push_back({o.hash, xq::hash(name("bln_ref_%d%d", col, 0)), col});
        for (int r = 0; r < rows_; ++r) {
            const std::uint32_t to =
                r < rows_ - 1 ? xq::hash(name("bln_ref_%d%d", col, r + 1)) : xq::hash("ref_btm");
            links_.push_back({xq::hash(name("bln_ref_%d%d", col, r)), to, col});
        }
    }
    // The strings' quad list (priority 111), not touchable.
    Object& lines = createQuadObject(game_, "bln_lin_obj");
    lines.touchOn = false;
    strings_ = lines.hash;
    redraw_ = true;
    c.result = Value::ofInt(1);
    return true;
}

// 0x2e0e78: the knots back to their reset places (their values 2, 3) and
// the anchors above their knots, over `frames`; the slots as at reset.
bool BalloonPuzzle::reset(float frames) {
    for (int r = 0; r < rows_; ++r) {
        for (int col = 0; col < columns_; ++col) {
            Object* knot = game_.scene.object(xq::hash(name("bln_ref_%d%d", col, r)));
            if (knot) {
                const float x = float(valueInt(*knot, 2)), y = float(valueInt(*knot, 3));
                knot->position.start({x, y, knot->position[2]}, frames, 0);
            }
            if (Object* cell = game_.scene.object(xq::hash(name("bln_frm_%d%d", col, r)))) {
                cell->offset.start({0, 0, cell->offset[2]}, frames, 0);
            }
            if (knot) {
                const Value top = game_.objectValue(*knot, 4);
                Object* anchor = top.isText() ? game_.scene.object(xq::hash(top.text)) : nullptr;
                if (anchor) {
                    anchor->position.start({float(valueInt(*knot, 2)), 0, anchor->position[2]},
                                           frames, 0);
                }
            }
        }
    }
    for (Slot& s : slots_) {
        s.now = s.reset;
    }
    redraw_ = true;
    return true;
}

// 0x2e1f80: the knots in the four slots (r, c), (r + 1, c), (r, c + 1),
// (r + 1, c + 1) move to the slot beside them in the same row (curve 2);
// each knot's anchor follows it along x. At once, the new places also
// become the reset places (the slots' reset knots and the knots' values
// 2, 3). Otherwise the frame pieces leave out the two at `hiddenFrame`.
bool BalloonPuzzle::swap(const int slots[4], float frames, bool atOnce, int hiddenFrame) {
    constexpr int kOther[4] = {2, 3, 0, 1};
    Slot* slot[4];
    std::uint32_t knot[4];
    Object* object[4];
    for (int i = 0; i < 4; ++i) {
        if (slots[i] < 0 || slots[i] >= int(slots_.size())) {
            return false;
        }
        slot[i] = &slots_[std::size_t(slots[i])];
        knot[i] = slot[i]->now;
        object[i] = game_.scene.object(knot[i]);
        if (!object[i]) {
            return false;
        }
    }
    std::array<float, 2> target[4];
    for (int i = 0; i < 4; ++i) {
        const int j = kOther[i];
        target[i] = {object[j]->position[0], object[j]->position[1]};
        if (atOnce) {
            slot[i]->reset = knot[j];
            game_.setObjectValue(*object[i], 2, Value::ofInt(int(target[i][0])));
            game_.setObjectValue(*object[i], 3, Value::ofInt(int(target[i][1])));
        }
    }
    for (int i = 0; i < 4; ++i) {
        Object& o = *object[i];
        o.position.start({target[i][0], target[i][1], o.position[2]}, frames, 2);
        slot[i]->now = knot[kOther[i]];
        const Value top = game_.objectValue(o, 4);
        if (Object* anchor = top.isText() ? game_.scene.object(xq::hash(top.text)) : nullptr) {
            anchor->position.start({float(std::int16_t(int(target[i][0]))), 0, anchor->position[2]},
                                   frames, 2);
        }
    }
    if (!atOnce) {
        rebuildFrames(hiddenFrame);
    }
    redraw_ = true;
    return true;
}

// 0x27b52c: the frame pieces under the cells (rows of cells, columns of
// knots), counted row by row, all but numbers hidden - 1 and hidden.
void BalloonPuzzle::rebuildFrames(int hidden) {
    Object* o = game_.scene.object(xq::hash("mrg_frm"));
    if (!o) {
        return;
    }
    o->pieces.clear();
    const int frame = cellIndex(*o, "frame");
    const int halfW = cellW_ >> 1;
    int n = 0;
    for (int r = 0; r < rows_ - 1; ++r) {
        for (int col = 0; col < columns_; ++col, ++n) {
            if (n == hidden - 1 || n == hidden) {
                continue;
            }
            o->pieces.push_back({frame, float(std::int16_t(cellW_ * col + originX_ - halfW)),
                                 float(std::int16_t(cellH_ * r + originY_))});
        }
    }
}

// 0x34b1a0: a straight piece from a to b, the string's width wide and half
// of it past both ends, shifted a hundredth of a pixel to the left.
void BalloonPuzzle::addStraight(Object& o, std::array<float, 2> a, std::array<float, 2> b,
                                const std::array<float, 3>& colour) {
    if (a == b) {
        return;
    }
    const std::size_t before = o.quads.size();
    addLineQuad(o, a, b, lineWidth_);
    for (std::size_t i = before; i < o.quads.size(); ++i) {
        o.quads[i][0] -= kStraightShift;
    }
    o.quadColours.resize(o.quads.size() / 4, colour);
}

// 0x34ae64: an S-curve from a to b: one piece per four pixels down (the
// count cut to a whole number), x following a + dx / 2 (1 + sin(t pi -
// pi / 2)); each piece a line quad of the string's width.
void BalloonPuzzle::addCurve(Object& o, std::array<float, 2> a, std::array<float, 2> b,
                             const std::array<float, 3>& colour) {
    if (a == b) {
        return;
    }
    const float dx = b[0] - a[0], dy = b[1] - a[1];
    const float halfX = dx * 0.5f;
    const float pieces = dy * (1.0f / kPieceHeight);
    const float stepY = pieces == 0.0f ? 0.0f : dy * (1.0f / pieces);
    const int n = int(pieces);
    const float rate = kPi / dy;
    std::array<float, 2> p = a;
    for (int i = 0; i < n; ++i) {
        const float angle = stepY * (float(i + 1) * rate) - kPi * 0.5f;
        const std::array<float, 2> q{a[0] + (halfX + halfX * std::sin(angle)), p[1] + stepY};
        addLineQuad(o, p, q, lineWidth_);
        o.quadColours.resize(o.quads.size() / 4, colour);
        p = q;
    }
}

// 0x2e1d8c: the strings anew, each piece in its column's colour (the
// table at 0x3e72ac: n / 31); the curved pieces are counted.
void BalloonPuzzle::drawStrings() {
    Object* o = game_.scene.object(strings_);
    if (!o) {
        return;
    }
    o->quads.clear();
    o->quadColours.clear();
    curved_ = 0;
    for (const Link& l : links_) {
        const Object* from = game_.scene.object(l.from);
        const Object* to = game_.scene.object(l.to);
        if (!from || !to) {
            continue;
        }
        const std::array<float, 2> a{from->position[0] + from->offset[0],
                                     from->position[1] + from->offset[1]};
        const std::array<float, 2> b{to->position[0] + to->offset[0],
                                     to->position[1] + to->offset[1]};
        const std::uint16_t c = colours_[l.column & 7];
        const std::array<float, 3> colour{float(c >> 11) / 31.0f, float((c >> 6) & 31) / 31.0f,
                                          float((c >> 1) & 31) / 31.0f};
        if (b[0] == a[0]) {
            addStraight(*o, a, {a[0], b[1]}, colour);
        } else {
            addCurve(*o, a, b, colour);
            ++curved_;
        }
    }
}

// 0x2e0bf0: the strings follow the knots while a change is on.
void BalloonPuzzle::update() {
    if (redraw_) {
        drawStrings();
    }
}

bool BalloonPuzzle::command(int number, Call& c, std::size_t first) {
    const std::size_t count = c.count() - first;
    auto integer = [&](std::size_t i) {
        return std::int16_t(c.integer(first + i));
    };
    auto result = [&](int v) {
        c.result = Value::ofInt(v);
        return true;
    };
    switch (number) {
    case 0:
        return setup(c, first, count);
    case 1:
        if (count < 1) {
            return false;
        }
        reset(c.real(first));
        return result(1);
    case 2: {
        // Four slots, the two cell names (unused here), frames, at once.
        if (count < 8) {
            return false;
        }
        const int slots[4] = {integer(0), integer(1), integer(2), integer(3)};
        return swap(slots, c.real(first + 6), c.at(first + 7).truthy(), integer(2)) ? result(1)
                                                                                    : false;
    }
    case 3: {
        // 0x2e2240: the button "bln_btn_CR" (column, row) swaps at once.
        if (count < 2) {
            return false;
        }
        Object* o = game_.scene.object(xq::hash(name("bln_btn_%d%d", integer(0), integer(1))));
        if (!o) {
            return false;
        }
        const int slots[4] = {valueInt(*o, 2), valueInt(*o, 3), valueInt(*o, 4), valueInt(*o, 5)};
        return swap(slots, 0, true, slots[2]) ? result(1) : false;
    }
    case 4: {
        // The curved pieces into the register; solved when each column's
        // knots stand at one x (0x2e0e70: 0 also marks "no x yet").
        if (count < 1) {
            return false;
        }
        if (Value* r = c.reg(first)) {
            *r = Value::ofInt(curved_);
        }
        for (int col = 0; col < columns_; ++col) {
            float x = 0;
            for (int r = 0; r < rows_; ++r) {
                const Object* o = game_.scene.object(xq::hash(name("bln_ref_%d%d", col, r)));
                if (!o) {
                    continue;
                }
                if (x == 0.0f) {
                    x = o->position[0];
                } else if (o->position[0] != x) {
                    return result(0);
                }
            }
        }
        return result(1);
    }
    case 5:
        // Whether the strings follow the knots; off restores the frame.
        if (count < 1) {
            return false;
        }
        redraw_ = c.at(first).truthy();
        if (!redraw_) {
            rebuildFrames(-1);
        }
        return result(1);
    case 6: {
        // A column's string colour (red, green, blue 0..255, cut to five
        // bits).
        if (count < 4 || unsigned(integer(0)) >= 8) {
            return false;
        }
        auto five = [&](std::size_t i) {
            return std::uint16_t(std::clamp(int(c.integer(first + i)) >> 3, 0, 31));
        };
        colours_[integer(0)] = std::uint16_t(five(1) << 11 | five(2) << 6 | five(3) << 1 | 1);
        return result(1);
    }
    default:
        return false;
    }
}

} // namespace

std::unique_ptr<SpecialPuzzle> makeBalloonPuzzle(Game& game) {
    return std::make_unique<BalloonPuzzle>(game);
}

} // namespace picarat::adventure5
