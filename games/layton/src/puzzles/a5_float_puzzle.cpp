// The fifth adventure's floating block puzzles (catalogue type 16, loop
// 3240; puzzles 103, 140): a piece walks over a grid of land and water on
// the upper screen; blocks floating on the water slide when the console is
// tilted, and the piece may step onto them. It must reach the goal cell.
//
// Reconstructed from the loop's command handler 0x2153ec (vtable 0x401eb8,
// constructor 0x385d3c), its input hook 0x215ea4 and helpers: reset
// 0x21651c, the piece's step 0x285e68, the piece 0x216c60, blocks
// 0x21674c, the blocks' slide 0x2168c8 / 0x216444.
//
// How it plays: water cells (11) cannot be walked on unless a block floats
// there; holding cells (12) are water that catches a block for good. The
// piece ("駒", command 30) steps one cell at a time over its own frames,
// with the pad (held) or while an arrow (50) is held, and never while a
// block moves, the question window is up or the puzzle takes no input.
// Tilting the console beyond the threshold (52) along its stronger axis
// slides every block that way (the front ones first) until it meets land,
// another block or a holding cell - four frames a cell - unless the piece
// stands on a block. A block coming to rest plays the stop sound (480), a
// caught one its own (482) and its fifth value's animation. Reaching the
// goal (21) sets the verdict "ナゾ判定".
//
// The motion sensor is started by the setup (0x339fc8) and stopped by
// command 20 (0x3bbf90); its readings come in Input::tiltX / tiltZ.
#include "puzzles/a5_special.hpp"
#include <cmath>
#include <cstdio>

namespace picarat::adventure5 {
namespace {

using adventure56::Object;
using xq::Call;
using xq::Value;

struct Cell2 {
    int x = 0, y = 0;
    bool operator==(const Cell2&) const = default;
};

class FloatPuzzle final : public SpecialPuzzle {
public:
    explicit FloatPuzzle(Game& game) : game_(game) {}

    bool command(int number, Call& c, std::size_t first) override;
    void input() override;
    std::string describe() const override;

private:
    struct Square {
        int px = 0, py = 0;   // pixels
        bool water = false;   // +4
        bool holding = false; // +8
        int piece = -1;       // +10
        int block = -1;       // +12
    };

    struct Piece {
        std::uint32_t object = 0;
        Cell2 start, at;
        float frames = 0; // a step's frames
    };

    struct Block {
        std::uint32_t object = 0;
        Cell2 start, at;
        bool caught = false;  // +0x10
        bool stopped = false; // +0x14
        bool moved = false;   // +0x18
    };

    Square* square(Cell2 c) {
        if (unsigned(c.x) >= unsigned(width_) || unsigned(c.y) >= unsigned(height_)) {
            return nullptr;
        }
        return &squares_[std::size_t(c.y * width_ + c.x)];
    }

    bool moving(std::uint32_t object) {
        // 0x2bec84: the position still changing.
        const Object* o = game_.scene.object(object);
        return o && o->position.moving();
    }

    void place(std::uint32_t object, const Square& s) {
        if (Object* o = game_.scene.object(object)) {
            o->position.set({float(s.px), float(s.py), o->position[2]});
        }
    }

    void glide(std::uint32_t object, const Square& s, float frames) {
        if (Object* o = game_.scene.object(object)) {
            o->position.start({float(s.px), float(s.py), o->position[2]}, frames, 0);
        }
    }

    bool atGoal() const { return !pieces_.empty() && pieces_[0].at == goal_; }

    bool flagOn(const char* name) {
        const int id = game_.flagId(name);
        return id >= 0 && game_.flag(id);
    }

    void reset();
    bool step(int dx, int dy);
    bool slide(int dx, int dy);
    Cell2 slideEnd(Cell2 from, int dx, int dy);

    Game& game_;
    std::vector<Piece> pieces_;     // +0x168
    std::vector<Square> squares_;   // +0x174
    std::vector<Block> blocks_;     // +0x180
    int originX_ = 0, originY_ = 0; // +0x18c / +0x18e
    int width_ = 0, height_ = 0;    // +0x190 / +0x192
    int size_ = 0;                  // +0x194
    bool reached_ = false;          // +0x19c
    float tiltThreshold_ = 0;       // +0x1a0
    Cell2 goal_;                    // +0x1a4
    std::string resource_;          // +0x1a8
    int arrowX_ = 0, arrowY_ = 0;   // +0x1e0 / +0x1e4
    unsigned flags_ = 0;            // +0x1e8: 1 tilt on, 2 walking
};

std::string FloatPuzzle::describe() const {
    std::string out;
    char line[96];
    std::snprintf(line, sizeof line, "grid %d %d goal %d %d threshold %g\n", width_, height_,
                  goal_.x, goal_.y, tiltThreshold_);
    out += line;
    for (int y = 0; y < height_; ++y) {
        std::string row = "row ";
        for (int x = 0; x < width_; ++x) {
            const Square& q = squares_[std::size_t(y * width_ + x)];
            row += q.holding ? 'H' : q.water ? '~' : '.';
        }
        out += row + "\n";
    }
    for (const Piece& p : pieces_) {
        std::snprintf(line, sizeof line, "piece %d %d\n", p.at.x, p.at.y);
        out += line;
    }
    for (const Block& b : blocks_) {
        std::snprintf(line, sizeof line, "block %d %d caught %d\n", b.at.x, b.at.y, int(b.caught));
        out += line;
    }
    return out;
}

// 0x21651c: the pieces and blocks back at their first cells.
void FloatPuzzle::reset() {
    for (Square& s : squares_) {
        s.piece = s.block = -1;
    }
    for (std::size_t i = 0; i < pieces_.size(); ++i) {
        Piece& p = pieces_[i];
        p.at = p.start;
        if (Square* s = square(p.at)) {
            s->piece = int(i);
            place(p.object, *s);
        }
    }
    for (std::size_t i = 0; i < blocks_.size(); ++i) {
        Block& b = blocks_[i];
        b.at = b.start;
        b.caught = b.stopped = b.moved = false;
        if (Square* s = square(b.at)) {
            s->block = int(i);
            place(b.object, *s);
            if (Object* o = game_.scene.object(b.object); o && o->values.size() > 4) {
                game_.playAnimation(*o, o->values[4], 0);
            }
        }
    }
}

// 0x285e68: the piece one cell on, onto land or a block (step sound 481).
bool FloatPuzzle::step(int dx, int dy) {
    if (dx == 0 && dy == 0) {
        return true;
    }
    if (pieces_.empty()) {
        return false;
    }
    Piece& p = pieces_[0];
    Square* from = square(p.at);
    const Cell2 to{std::int16_t(p.at.x + dx), std::int16_t(p.at.y + dy)};
    Square* s = square(to);
    if (!s || (s->water && (s->block < 0 || s->block >= int(blocks_.size())))) {
        return false;
    }
    if (from) {
        from->piece = -1;
    }
    s->piece = 0;
    p.at = to;
    glide(p.object, *s, p.frames);
    game_.playEffect(0x1e1);
    return true;
}

// 0x216444: where a block sliding into `from` (open water) comes to rest.
Cell2 FloatPuzzle::slideEnd(Cell2 from, int dx, int dy) {
    const Cell2 next{std::int16_t(from.x + dx), std::int16_t(from.y + dy)};
    const Square* s = square(next);
    if (!s || !s->water) {
        return from;
    }
    if (s->block >= 0) {
        return from;
    }
    return s->holding ? next : slideEnd(next, dx, dy);
}

// 0x2168c8: every free block slides (front ones first).
bool FloatPuzzle::slide(int dx, int dy) {
    std::vector<std::size_t> order(blocks_.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        order[i] = i;
    }
    for (std::size_t i = 0; i + 1 < order.size(); ++i) {
        for (std::size_t j = i + 1; j < order.size(); ++j) {
            const Block& a = blocks_[order[i]];
            const Block& b = blocks_[order[j]];
            bool swap = false;
            if (dx < 0) {
                swap = a.at.x > b.at.x;
            } else if (dx > 0) {
                swap = a.at.x < b.at.x;
            } else if (dy < 0) {
                swap = b.at.y < a.at.y;
            } else if (dy > 0) {
                swap = a.at.y < b.at.y;
            }
            if (swap) {
                std::swap(order[i], order[j]);
            }
        }
    }
    bool any = false;
    for (std::size_t k : order) {
        Block& b = blocks_[k];
        if (b.caught) {
            continue;
        }
        const Cell2 target{std::int16_t(b.at.x + dx), std::int16_t(b.at.y + dy)};
        const Square* t = square(target);
        Cell2 end = b.at;
        if (t && t->water) {
            if (t->block < 0) {
                end = t->holding ? target : slideEnd(target, dx, dy);
            }
        }
        if (end == b.at) {
            continue;
        }
        Square* from = square(b.at);
        Square* to = square(end);
        if (!from || !to) {
            continue;
        }
        from->block = -1;
        to->block = int(k);
        const int cells = std::abs(std::int16_t(end.x - b.at.x) + std::int16_t(end.y - b.at.y));
        b.at = end;
        glide(b.object, *to, float(cells << 2));
        b.caught = to->holding;
        b.moved = true;
        any = true;
    }
    return any;
}

// 0x215ea4: after the touch dispatch.
void FloatPuzzle::input() {
    for (Block& b : blocks_) {
        if (b.moved && !b.stopped && game_.scene.object(b.object) && !moving(b.object)) {
            if (b.caught) {
                game_.playEffect(0x1e2);
                b.stopped = true;
                Object* o = game_.scene.object(b.object);
                if (o->values.size() > 5) {
                    game_.playAnimation(*o, o->values[5], 0);
                }
            } else {
                game_.playEffect(0x1e0);
            }
            b.moved = false;
        }
    }
    if (game_.objectTouchLocked) {
        return;
    }
    if (const Object* q = game_.scene.object(xq::hash("msg_ques_scr_a")); q && q->visible) {
        return;
    }
    if (flagOn(names::inputOff)) {
        return;
    }
    for (const Block& b : blocks_) {
        if (moving(b.object)) {
            return;
        }
    }
    const std::uint32_t piece = pieces_.empty() ? 0 : pieces_[0].object;
    bool walked = false;
    if ((flags_ & 2) && !moving(piece)) {
        reached_ = atGoal();
        if (reached_) {
            game_.setVariable(names::verdict, 1);
            flags_ &= ~2u;
            arrowX_ = arrowY_ = 0;
            return;
        }
        if (game_.input.touching) {
            step(arrowX_, arrowY_);
            walked = arrowX_ != 0 || arrowY_ != 0;
        }
    }
    if (!walked && !moving(piece)) {
        int dx = 0, dy = 0;
        const std::uint32_t pad = game_.input.buttons;
        if (pad & Input::kLeft) {
            dx = -1, dy = 0;
        }
        if (pad & Input::kRight) {
            dx = 1, dy = 0;
        }
        if (pad & Input::kUp) {
            dx = 0, dy = -1;
        }
        if (pad & Input::kDown) {
            dx = 0, dy = 1;
        }
        if ((dx != 0 || dy != 0) && (step(dx, dy), true)) {
            flags_ |= 2;
        } else {
            flags_ &= ~2u;
        }
    }
    if (flags_ & 1) {
        int dx = 0, dy = 0;
        const float x = game_.input.tiltX, z = game_.input.tiltZ;
        if (std::fabs(x) <= std::fabs(z)) {
            if (tiltThreshold_ < z) {
                dy = -1;
            } else if (z < -tiltThreshold_) {
                dy = 1;
            }
        } else if (tiltThreshold_ < x) {
            dx = -1;
        } else {
            dx = x < -tiltThreshold_ ? 1 : 0;
        }
        // Not while the piece stands on a floating block.
        if (!pieces_.empty()) {
            if (const Square* s = square(pieces_[0].at); s && s->block >= 0 && !s->holding) {
                return;
            }
        }
        if (dx != 0 || dy != 0) {
            slide(dx, dy);
        }
    }
}

bool FloatPuzzle::command(int number, Call& c, std::size_t first) {
    const std::size_t count = c.count() - first;
    auto integer = [&](std::size_t i) {
        return c.integer(first + i);
    };
    auto text = [&](std::size_t i) {
        const std::string* t = c.text(first + i);
        return t ? *t : std::string();
    };
    auto result = [&](int v) {
        c.result = Value::ofInt(v);
        return true;
    };
    switch (number) {
    case 0:
        flags_ |= 1;
        reset();
        return result(1);
    case 10: {
        // Cells across, down; first cell x, y; cell size; animation
        // resource; numbers of pieces and blocks.
        if (count < 10) {
            return false;
        }
        width_ = std::int16_t(integer(0));
        height_ = std::int16_t(integer(1));
        originX_ = std::int16_t(integer(2));
        originY_ = std::int16_t(integer(3));
        size_ = integer(4) & 0xffff;
        resource_ = text(5);
        pieces_.clear();
        blocks_.clear();
        pieces_.reserve(std::size_t(std::max(0, integer(6))));
        blocks_.reserve(std::size_t(std::max(0, integer(7))));
        squares_.assign(std::size_t(std::max(0, width_ * height_)), Square{});
        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                Square* s = square({x, y});
                s->px = std::int16_t(originX_ + x * size_);
                s->py = std::int16_t(originY_ + y * size_);
            }
        }
        flags_ |= 1;
        return result(1);
    }
    case 11: {
        // Water (x, y, across, down).
        if (count < 4) {
            return false;
        }
        const int x0 = std::int16_t(integer(0)), y0 = std::int16_t(integer(1));
        for (int j = 0; j < std::int16_t(integer(3)); ++j) {
            for (int i = 0; i < std::int16_t(integer(2)); ++i) {
                if (Square* s = square({std::int16_t(x0 + i), std::int16_t(y0 + j)})) {
                    s->water = true;
                }
            }
        }
        return result(1);
    }
    case 12:
        // A holding cell (x, y).
        if (count < 2) {
            return false;
        }
        if (Square* s = square({std::int16_t(integer(0)), std::int16_t(integer(1))})) {
            s->water = s->holding = true;
        }
        return result(1);
    case 20:
        game_.setVariable(names::verdict, 1);
        return result(1);
    case 21:
        if (count < 2) {
            return false;
        }
        goal_ = {std::int16_t(integer(0)), std::int16_t(integer(1))};
        return result(1);
    case 22:
        if (!reached_) {
            reached_ = atGoal();
        }
        return result(reached_ ? 1 : 0);
    case 30: {
        // The piece "駒" (animation, x, y, step frames) on the upper
        // screen (priority 111, kind 7).
        if (count < 4) {
            return false;
        }
        const Cell2 at{std::int16_t(integer(1)), std::int16_t(integer(2))};
        Square* s = square(at);
        if (!s) {
            return false;
        }
        const std::string animation = text(0);
        const Value values[] = {Value::ofText("\x8b\xee"), Value::ofInt(7),
                                Value::ofText(animation)};
        Object& o = game_.createObject("\x8b\xee", 0, 111, 7, values);
        pieces_.push_back({o.hash, at, at, c.real(first + 3)});
        o.position.set({float(s->px), float(s->py), o.position[2]});
        game_.setAnimationSet(o, xq::hash(resource_));
        game_.playAnimation(o, Value::ofText(animation), 0);
        s->piece = int(pieces_.size()) - 1;
        return result(1);
    }
    case 31:
        if (count < 2) {
            return false;
        }
        return result(step(integer(0), integer(1)) ? 1 : 0);
    case 32:
        return result(!pieces_.empty() && moving(pieces_[0].object) ? 1 : 0);
    case 40: {
        // A block (the script's object, x, y).
        if (count < 3) {
            return false;
        }
        const Cell2 at{std::int16_t(integer(1)), std::int16_t(integer(2))};
        Square* s = square(at);
        const std::string name = text(0);
        Object* o = game_.scene.object(xq::hash(name));
        if (s && o) {
            blocks_.push_back({o->hash, at, at});
            o->position.set({float(s->px), float(s->py), o->position[2]});
            s->block = int(blocks_.size()) - 1;
        }
        return result(1);
    }
    case 41:
        return true;
    case 42:
        for (const Block& b : blocks_) {
            if (moving(b.object)) {
                return result(1);
            }
        }
        return result(0);
    case 43: {
        // Whether the piece stands on a free block.
        const Square* s = pieces_.empty() ? nullptr : square(pieces_[0].at);
        return result(s && s->block >= 0 && !s->holding ? 1 : 0);
    }
    case 50:
        // An arrow held: walk that way.
        flags_ |= 2;
        arrowX_ = integer(0);
        arrowY_ = integer(1);
        return result(1);
    case 51:
        flags_ &= ~2u;
        arrowX_ = arrowY_ = 0;
        reached_ = atGoal();
        return result(reached_ ? 1 : 0);
    case 52:
        tiltThreshold_ = c.real(first);
        return result(1);
    default:
        return false;
    }
}

} // namespace

std::unique_ptr<SpecialPuzzle> makeFloatPuzzle(Game& game) {
    return std::make_unique<FloatPuzzle>(game);
}

} // namespace picarat::adventure5
