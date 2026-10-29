// The sixth adventure's piece puzzles (loop 3140, class "Fit"; catalogue
// type 20, fifteen puzzles): pieces made of cells are dragged and turned
// onto a board of cells. Partition (3180) and Transfer (3170) build on the
// same class.
//
// Reconstructed from the class's command handler 0x29dae0 (vtable
// 0x3c7130), its input hook 0x29ec8c and the board helpers it shares
// (pixel -> cell 0x22fb74, cell -> pixel 0x22fa38, a piece's footprint on
// the board 0x22f4d0, the board rebuild 0x29f4c8).
//
// The play itself is script: each piece is a 2D object whose handler blocks
// (the puzzles' library: SeqFitDrive, SeqFitExit, SeqFitRot...) drag, turn
// and drop it, asking this class where it lands (command 8), how it is
// turned (10..12) and what the board holds (13, 16..); with flag 8 the
// input hook hands every pen contact over the board to the script's
// Fit_PenTouchBegin / Drive / End.
//
// Not reconstructed yet: the ray counts (38, 39). The board history (41..44)
// keeps every snapshot (the original reuses the oldest record when its pool
// is full; the pool's size is not known yet).
#include "puzzles/a6_fit_puzzle.hpp"
#include <array>
#include <cmath>
#include <cstdio>
#include <list>

namespace picarat::adventure5::fit {

using adventure56::Object;
using xq::Call;
using xq::Value;

// 0x22f4d0. The piece's turn as a matrix (c, s; -s, c) for eighths of a
// turn (the sense is a hypothesis); its half extents (0.48 of its pixel
// size, turned) give the top left cell under the box, clamped into the
// board unless flag 1; the turned size (x 1.01) the bottom right.
Footprint FitPuzzle::footprint(const Piece& p, int px, int py) const {
    Footprint f;
    const float a = float(p.turn() * 45) * 0.017453292f;
    const float m00 = std::cos(a), m01 = std::sin(a), m10 = -std::sin(a), m11 = std::cos(a);
    const float hx = float(std::int16_t(pitchX() * p.w)) * 0.48f;
    const float hy = float(std::int16_t(pitchY() * p.h)) * 0.48f;
    const float ex = std::fabs(m00 * hx + m10 * hy), ey = std::fabs(m01 * hx + m11 * hy);
    const float left = std::min(float(px) - ex, float(px) + ex);
    const float top = std::min(float(py) - ey, float(py) + ey);
    cellAt(int(left + float(pitchX() >> 1)), int(top + float(pitchY() >> 1)), f.first.x, f.first.y);
    f.span = {int(std::fabs(m00 * float(p.w - 1) * 1.01f + m10 * float(p.h - 1) * 1.01f)),
              int(std::fabs(m01 * float(p.w - 1) * 1.01f + m11 * float(p.h - 1) * 1.01f))};
    const bool clamp = !(flags_ & 1);
    auto outside = [&](Point16 q) {
        return q.x < 0 || q.x >= across_ || q.y < 0 || q.y >= down_;
    };
    if (clamp && outside(f.first)) {
        f.first.x = std::max(0, f.first.x);
        f.first.y = std::max(0, f.first.y);
    }
    f.last = {f.first.x + f.span.x, f.first.y + f.span.y};
    if (clamp && outside(f.last)) {
        const int dx = f.last.x >= across_ ? f.last.x - across_ + 1 : 0;
        const int dy = f.last.y >= down_ ? f.last.y - down_ + 1 : 0;
        f.first = {f.first.x - dx, f.first.y - dy};
        f.last = {f.last.x - dx, f.last.y - dy};
    }
    int x0, y0, x1, y1;
    cellPixel(f.first.x, f.first.y, true, x0, y0);
    cellPixel(f.last.x, f.last.y, true, x1, y1);
    f.centre = {(x0 + x1) / 2, (y0 + y1) / 2};
    // Board steps to piece cells: int(1.2 x the matrix's columns).
    f.columnStep = {int(m00 * 1.2f), int(m10 * 1.2f)};
    f.rowStep = {int(m01 * 1.2f), int(m11 * 1.2f)};
    const int sx = f.rowStep.x + f.columnStep.x, sy = f.rowStep.y + f.columnStep.y;
    f.start = {sx < 0 ? p.w - 1 : 0, sy < 0 ? p.h - 1 : 0};
    return f;
}

// A snapshot's cells written back (0x29fbac's loop; cells outside the
// board are skipped).
void FitPuzzle::restore(const Snapshot& s) {
    for (const Snapshot::Entry& e : s.cells) {
        if (Cell* b = boardCell(e.x, e.y)) {
            *b = e.cell;
        }
    }
}

// 0x29f4c8 (command 13): the board from the pieces on it, in list order.
// A piece cell on an occupied board cell marks both as overlapping (status
// 1); a piece off the board makes status 2 (and stays out unless flag 4).
// With flag 2 the cells' four byte lanes merge instead, turned a lane per
// quarter turn.
void FitPuzzle::rebuild() {
    status_ = 0;
    for (Cell& c : board_) {
        c = {};
    }
    const int bx0 = originX_, by0 = originY_;
    for (Piece& p : pieces_) {
        const bool on = bx0 <= p.position.x && p.position.x <= bx0 + boardW() &&
                        by0 <= p.position.y && p.position.y <= by0 + boardH();
        if (!on) {
            p.flags &= ~Piece::kOnBoard;
            status_ |= 2;
            if (!(flags_ & 4)) {
                continue;
            }
        }
        p.flags = std::uint16_t((p.flags & ~Piece::kOverlap) | Piece::kOnBoard);
        const Footprint f = footprint(p, p.position.x, p.position.y);
        for (int row = 0; row <= f.span.y; ++row) {
            for (int col = 0; col <= f.span.x; ++col) {
                const int cx = f.start.x + f.rowStep.x * row + f.columnStep.x * col;
                const int cy = f.start.y + f.rowStep.y * row + f.columnStep.y * col;
                const Cell* source = p.cell(cx, cy);
                Cell* target = boardCell(f.first.x + col, f.first.y + row);
                if (!source || source->value == 0 || !target) {
                    continue;
                }
                if (!(flags_ & 2)) {
                    if (!(target->flags & Cell::kFilled)) {
                        target->flags = p.flags;
                        target->value = source->value;
                        target->extra = source->extra;
                    } else {
                        status_ |= 1;
                        target->flags |= Cell::kOverlap;
                        p.flags |= Piece::kOverlap;
                    }
                    continue;
                }
                // Lane merge: the piece's lanes turned by its quarter turns.
                std::uint32_t v = source->value, e = source->extra;
                for (int i = 0; i < (source->flags & 7) >> 1; ++i) {
                    v = (v >> 8) | (v << 24);
                    e = (e >> 8) | (e << 24);
                }
                std::uint32_t lane = 0;
                for (int k = 0; k < 4 && !lane; ++k) {
                    lane = (v & (0xffu << (8 * k))) ? 0xffu << (8 * k) : 0;
                }
                const bool clash = (lane & target->value) != 0;
                if (clash) {
                    status_ |= 1;
                    p.flags |= Piece::kOverlap;
                }
                std::uint32_t tv = target->value, te = target->extra;
                for (int k = 0; k < 4; ++k) {
                    const std::uint32_t m = 0xffu << (8 * k);
                    if (!(tv & m)) {
                        tv |= v & m;
                    }
                    if (!(te & m)) {
                        te |= e & m;
                    }
                }
                target->value = tv;
                target->extra = te;
                target->flags =
                    std::uint16_t(clash ? p.flags | Cell::kOverlap : p.flags & ~Cell::kOverlap);
            }
        }
    }
}

// 0x29ec8c: with flag 8 and the puzzle taking input, the pen's cell goes to
// the script (in the board, start cell, current cell).
void FitPuzzle::input() {
    if (!(flags_ & 8) || game_.objectTouchLocked) {
        return;
    }
    if (const int id = game_.flagId(names::inputOff); id >= 0 && game_.flag(id)) {
        return;
    }
    // 0x29ec8c reads the pen position at game +0x520 in every phase; after
    // a release it still holds where the pen was lifted.
    if (game_.input.touching) {
        lastTouch_ = {game_.input.x, game_.input.y};
    }
    int cx = 0, cy = 0;
    cellAt(lastTouch_.x, lastTouch_.y, cx, cy);
    const bool in = cx >= 0 && cx < across_ && cy >= 0 && cy < down_;
    const char* block = nullptr;
    Point16 from = penStart_;
    if (game_.pressed()) {
        penStart_ = {cx, cy};
        from = penStart_;
        block = "Fit_PenTouchBegin";
    } else if (game_.input.touching) {
        block = "Fit_PenTouchDrive";
    } else if (game_.released()) {
        block = "Fit_PenTouchEnd";
    }
    if (!block) {
        return;
    }
    const Value args[] = {Value::ofInt(in ? 1 : 0), Value::ofInt(from.x), Value::ofInt(from.y),
                          Value::ofInt(cx), Value::ofInt(cy)};
    if (xq::Thread* t = game_.loopThreads[0]) {
        t->callback(block, args);
    }
}

bool FitPuzzle::command(int number, Call& c, std::size_t first) {
    const std::size_t count = c.count() - first;
    auto integer = [&](std::size_t i) {
        return int(std::int16_t(c.integer(first + i)));
    };
    auto word = [&](std::size_t i) {
        return c.unsignedInt(first + i);
    };
    auto hashOf = [&](std::size_t i) {
        const Value& v = c.at(first + i);
        return v.isText() ? xq::hash(v.text) : v.toUint();
    };
    auto store = [&](std::size_t i, Value v) {
        if (i < count) {
            if (Value* r = c.reg(first + i)) {
                *r = v;
            }
        }
    };
    auto result = [&](Value v) {
        c.result = v;
        return true;
    };
    auto yes = [&](bool b) {
        return result(Value::ofInt(b ? 1 : 0));
    };
    c.result = Value::ofInt(0);
    switch (number) {
    case 1: {
        // 0x2a0170: cells across, down; cell width, height; gaps; origin.
        if (count < 8) {
            return false;
        }
        across_ = integer(0);
        down_ = integer(1);
        cellW_ = integer(2);
        cellH_ = integer(3);
        gapX_ = integer(4);
        gapY_ = integer(5);
        originX_ = integer(6);
        originY_ = integer(7);
        board_.assign(std::size_t(std::max(0, across_ * down_)), Cell{});
        return yes(true);
    }
    case 2:
        // 0x29f324: the piece pool.
        if (count == 0) {
            return false;
        }
        poolSize_ = integer(0);
        pieces_.clear();
        created_ = 0;
        return yes(true);
    case 3: {
        // 0x29f0a4: a piece of w x h cells [, its object] -> its id.
        if (count < 2 || created_ >= poolSize_) {
            return result(Value::ofInt(0));
        }
        Piece p;
        p.w = integer(0);
        p.h = integer(1);
        p.cells.assign(std::size_t(std::max(0, p.w * p.h)), Cell{});
        p.flags = Piece::kAlive;
        p.id = created_++ | (++serial_ << 16);
        if (count > 2) {
            p.object = hashOf(2);
        }
        lastPiece_ = p.id;
        pieces_.push_back(p);
        return result(Value::ofInt(p.id));
    }
    case 4: {
        // 0x2a0f8c: a piece cell (id, 0 the newest; x, y [, value [, extra]]).
        if (count < 3) {
            return false;
        }
        const int id = int(word(0));
        Piece* p = piece(id == 0 ? lastPiece_ : id);
        Cell* cell = p ? p->cell(integer(1), integer(2)) : nullptr;
        if (!cell) {
            return false;
        }
        cell->value = count > 3 ? std::uint32_t(integer(3)) : 0;
        cell->flags = Cell::kFilled;
        cell->extra = count > 4 ? std::uint32_t(integer(4)) : 0;
        return yes(true);
    }
    case 6: {
        // 0x2a0034: the piece's object to (x, y).
        Piece* p = count >= 3 ? piece(int(word(0))) : nullptr;
        if (!p) {
            return false;
        }
        if (Object* o = game_.scene.object(p->object)) {
            o->position.set({float(integer(1)), float(integer(2)), o->position[2]});
        }
        return yes(true);
    }
    case 7: {
        Piece* p = count >= 1 ? piece(int(word(0))) : nullptr;
        return p ? result(Value::ofUint(p->object)) : false;
    }
    case 8: {
        // 0x29fe54: a piece put at (x, y), on top; on the board it snaps to
        // the footprint's centre [into two registers]; 1 when on the board.
        if (count < 3) {
            return false;
        }
        Piece* p = piece(int(word(0)));
        if (!p) {
            return yes(true);
        }
        const int x = integer(1), y = integer(2);
        p->position = {x, y};
        p->flags &= ~Piece::kOnBoard;
        const int id = p->id;
        toTop(id);
        p = piece(id);
        if (!insideBoard(x, y)) {
            return yes(true) && result(Value::ofInt(0));
        }
        p->flags |= Piece::kOnBoard;
        const Footprint f = footprint(*p, x, y);
        p->position = f.centre;
        if (count > 4) {
            store(3, Value::ofInt(f.centre.x));
            store(4, Value::ofInt(f.centre.y));
        }
        return yes(true);
    }
    case 9: {
        // 0x2a05fc: the size of an animation of the piece object's set, its
        // box's maximum minus minimum (0x3707e8 by name; id, name, width and
        // height registers). Turning needs it (SeqFitRotate's object value
        // 5 is its smaller side).
        Piece* p = count >= 4 ? piece(int(word(0))) : nullptr;
        const Object* o = p ? game_.scene.object(p->object) : nullptr;
        if (!o || !o->animation) {
            return false;
        }
        const std::uint32_t h = hashOf(1);
        for (const auto& an : o->animation->animations) {
            if (an.hash == h) {
                p->animSize = {int(an.box[3] - an.box[0]), int(an.box[4] - an.box[1])};
                store(2, Value::ofInt(p->animSize.x));
                store(3, Value::ofInt(p->animSize.y));
                return yes(true);
            }
        }
        return false;
    }
    case 10: {
        // 0x2a0ea0: the turn set from an angle; the original's loop shifts a
        // zero, so the piece always ends unturned.
        Piece* p = count >= 2 ? piece(int(word(0))) : nullptr;
        if (!p) {
            return false;
        }
        p->flags &= ~7;
        return yes(true);
    }
    case 11: {
        // The turn as an angle in radians (turn x 45 x pi / 180, 0x29dee4)
        // [also into a register].
        Piece* p = count >= 1 ? piece(int(word(0))) : nullptr;
        if (!p) {
            return false;
        }
        const float angle = float(p->turn() * 45) * 0.017453292f;
        store(1, Value::ofFloat(angle));
        return result(Value::ofFloat(angle));
    }
    case 12: {
        // 0x2a0ce4: turned by steps of 45 degrees (negative: the other way);
        // the new angle in radians [the old one, and the old one plus the
        // change wrapped into -180..180 degrees, into registers, so a turn
        // over 0 tweens the short way].
        Piece* p = count >= 2 ? piece(int(word(0))) : nullptr;
        if (!p) {
            return false;
        }
        const int steps = c.integer(first + 1);
        const float before = float(p->turn() * 45);
        for (int i = 0; i < std::abs(steps); ++i) {
            const int t = (p->turn() + (steps < 0 ? 1 : -1)) & 7;
            p->flags = std::uint16_t((p->flags & ~7) | t);
        }
        const float after = float(p->turn() * 45);
        float change = after - before;
        if (change >= 180.0f) {
            change -= 360.0f;
        } else if (change <= -180.0f) {
            change += 360.0f;
        }
        constexpr float kRadian = 0.017453292f;
        if (count > 3) {
            store(2, Value::ofFloat(before * kRadian));
            store(3, Value::ofFloat(before * kRadian + change * kRadian));
        }
        return result(Value::ofFloat(after * kRadian));
    }
    case 13:
        rebuild();
        return yes(true);
    case 14:
        if (count < 2) {
            return false;
        }
        store(0, Value::ofInt(across_));
        store(1, Value::ofInt(down_));
        return yes(true);
    case 15:
        if (count < 2) {
            return false;
        }
        store(0, Value::ofInt(cellW_));
        store(1, Value::ofInt(cellH_));
        store(2, Value::ofInt(gapX_));
        store(3, Value::ofInt(gapY_));
        store(4, Value::ofInt(gapX_ + cellW_));
        store(5, Value::ofInt(gapY_ + cellH_));
        return yes(true);
    case 16: {
        // A board cell (x, y; value [, flags [, extra]] registers).
        if (count < 3) {
            return false;
        }
        const Cell* b = boardCell(integer(0), integer(1));
        const Cell none;
        const Cell& v = b ? *b : none;
        store(2, Value::ofUint(v.value));
        store(3, Value::ofUint(v.flags));
        store(4, Value::ofUint(v.extra));
        return yes(b != nullptr);
    }
    case 17: {
        if (count < 3) {
            return false;
        }
        const Cell* b = boardCell(integer(0), integer(1));
        store(2, Value::ofUint(b ? b->extra : 0));
        return yes(b != nullptr);
    }
    case 18: {
        if (count < 3) {
            return false;
        }
        if (Cell* b = boardCell(integer(0), integer(1))) {
            b->extra = word(2);
        }
        return yes(true);
    }
    case 19:
        return result(Value::ofInt(int(status_)));
    case 20: {
        // The share of the pieces on the board.
        int all = 0, on = 0;
        for (const Piece& p : pieces_) {
            ++all;
            on += (p.flags & Piece::kOnBoard) ? 1 : 0;
        }
        return result(Value::ofFloat(all ? float(on) / float(all) : 0.0f));
    }
    case 21: {
        // 0x2a09ac: a piece's footprint on the board (cell x, y, width,
        // height into registers).
        Piece* p = count >= 5 ? piece(int(word(0))) : nullptr;
        if (!p || !insideBoard(p->position.x, p->position.y)) {
            return yes(true) && result(Value::ofInt(0));
        }
        const Footprint f = footprint(*p, p->position.x, p->position.y);
        store(1, Value::ofInt(f.first.x));
        store(2, Value::ofInt(f.first.y));
        store(3, Value::ofInt(f.last.x - f.first.x + 1));
        store(4, Value::ofInt(f.last.y - f.first.y + 1));
        return yes(true);
    }
    case 22:
        // The board emptied; each piece where its object is.
        for (Cell& b : board_) {
            b = {};
        }
        for (Piece& p : pieces_) {
            p.position = objectPosition(p);
        }
        return yes(true);
    case 23: {
        // A piece cell (id, x, y; value [, flags [, extra]] registers).
        if (count < 4) {
            return false;
        }
        Piece* p = piece(int(word(0)));
        const Cell* pc = p ? p->cell(integer(1), integer(2)) : nullptr;
        const Cell none;
        const Cell& v = pc ? *pc : none;
        store(3, Value::ofUint(v.value));
        store(4, Value::ofUint(v.flags));
        store(5, Value::ofUint(v.extra));
        return yes(true);
    }
    case 24:
        if (count == 0) {
            return false;
        }
        snap_ = std::uint16_t(integer(0));
        return yes(true);
    case 25:
        store(0, Value::ofInt(std::int16_t(snap_)));
        return result(Value::ofInt(std::int16_t(snap_)));
    case 26:
        // The board's rectangle (the original gives the width twice).
        if (count < 2) {
            return false;
        }
        store(0, Value::ofInt(originX_));
        store(1, Value::ofInt(originY_));
        store(2, Value::ofInt(boardW()));
        store(3, Value::ofInt(boardW()));
        return yes(true);
    case 27: {
        // A piece's cells across, down [and pixel size].
        Piece* p = count >= 3 ? piece(int(word(0))) : nullptr;
        if (!p) {
            return false;
        }
        store(1, Value::ofInt(p->w));
        store(2, Value::ofInt(p->h));
        store(3, Value::ofInt(std::int16_t(p->w * pitchX())));
        store(4, Value::ofInt(std::int16_t(p->h * pitchY())));
        return yes(true);
    }
    case 28: {
        // 0x2a0b30: whether the piece object's box (unturned) lies on the
        // board.
        Piece* p = count >= 1 ? piece(int(word(0))) : nullptr;
        if (!p) {
            return false;
        }
        const Point16 at = objectPosition(*p);
        const int hw = std::int16_t(p->w * pitchX()) >> 1, hh = std::int16_t(p->h * pitchY()) >> 1;
        const int l = at.x - hw, t = at.y - hh, r = at.x + hw, b = at.y + hh;
        const int bx1 = originX_ + boardW(), by1 = originY_ + boardH();
        return yes(originX_ <= l && l <= bx1 && originY_ <= t && t <= by1 && originX_ <= r &&
                   r <= bx1 && originY_ <= b && b <= by1);
    }
    case 29: {
        // Flags: assigned, or set / cleared with a second operand.
        if (count == 0) {
            return false;
        }
        std::uint32_t m = word(0) & ~0x40000000u;
        if (count > 1) {
            m = integer(1) != 0 ? flags_ | m : flags_ & ~m;
        }
        flags_ = m;
        return yes(true);
    }
    case 30:
        if (count == 0) {
            return result(Value::ofInt(int(flags_)));
        }
        return yes((word(0) & ~flags_) == 0);
    case 31: {
        // A piece's footprint at (x, y) (corner cells into registers).
        if (count < 7) {
            return false;
        }
        Piece* p = piece(int(word(0)));
        const int x = integer(1), y = integer(2);
        if (!p || !insideBoard(x, y)) {
            return yes(true) && result(Value::ofInt(0));
        }
        const Footprint f = footprint(*p, x, y);
        store(3, Value::ofInt(f.first.x));
        store(4, Value::ofInt(f.first.y));
        store(5, Value::ofInt(f.last.x));
        store(6, Value::ofInt(f.last.y));
        return yes(true);
    }
    case 32: {
        // The piece cell under the turned piece's cell (x, y) (value [,
        // flags [, extra]] registers).
        if (count < 4) {
            return false;
        }
        Piece* p = piece(int(word(0)));
        Cell found;
        if (p) {
            const Footprint f = footprint(*p, p->position.x, p->position.y);
            const int col = integer(1), row = integer(2);
            if (const Cell* pc = p->cell(f.start.x + f.rowStep.x * row + f.columnStep.x * col,
                                         f.start.y + f.rowStep.y * row + f.columnStep.y * col)) {
                found = *pc;
            }
        }
        store(3, Value::ofUint(found.value));
        store(4, Value::ofUint(found.flags));
        store(5, Value::ofUint(found.extra));
        return yes(true);
    }
    case 33: {
        // The turned piece's cells across, down [and pixel size].
        Piece* p = count >= 3 ? piece(int(word(0))) : nullptr;
        if (!p) {
            return false;
        }
        int w = p->w, h = p->h;
        if (p->turn() != 0) {
            const float a = float(p->turn() * 45) * 0.017453292f;
            const float m00 = std::cos(a), m01 = std::sin(a), m10 = -std::sin(a), m11 = std::cos(a);
            w = int(std::fabs(m00 * float(p->w) * 1.01f + m10 * float(p->h) * 1.01f));
            h = int(std::fabs(m01 * float(p->w) * 1.01f + m11 * float(p->h) * 1.01f));
        }
        store(1, Value::ofInt(w));
        store(2, Value::ofInt(h));
        store(3, Value::ofInt(std::int16_t(w * pitchX())));
        store(4, Value::ofInt(std::int16_t(h * pitchY())));
        return yes(true);
    }
    case 35:
        if (count == 0) {
            return false;
        }
        toTop(int(word(0)));
        return yes(true);
    case 36: {
        // Cell -> pixel (x, y [, centre]; x, y registers).
        if (count < 4) {
            return false;
        }
        const bool centre = count > 4 && integer(2) != 0;
        int px = 0, py = 0;
        cellPixel(integer(0), integer(1), centre, px, py);
        store(count > 4 ? 3 : 2, Value::ofInt(px));
        store(count > 4 ? 4 : 3, Value::ofInt(py));
        return yes(true);
    }
    case 37: {
        // A board cell's values (x, y, value [, extra [, flags]]).
        if (count < 2) {
            return false;
        }
        if (Cell* b = boardCell(integer(0), integer(1))) {
            if (count > 2) {
                b->value = word(2);
            }
            if (count > 3) {
                b->extra = word(3);
            }
            if (count > 4) {
                b->flags = std::uint16_t(word(4));
            }
        }
        return yes(true);
    }
    case 40: {
        // Pixel -> cell (x, y [, flag]; x, y registers).
        if (count < 4) {
            return false;
        }
        int cx = 0, cy = 0;
        cellAt(integer(0), integer(1), cx, cy);
        store(count > 4 ? 3 : 2, Value::ofInt(cx));
        store(count > 4 ? 4 : 3, Value::ofInt(cy));
        return yes(true);
    }
    case 41: {
        // 0x29fd74 / 0x29f9e4: the occupied cells (value not 0) saved as
        // the newest snapshot, with up to three values (x, y, a number).
        Snapshot s;
        s.x = count > 0 ? integer(0) : 0;
        s.y = count > 1 ? integer(1) : 0;
        s.value = count > 2 ? c.real(first + 2) : 0.0f;
        for (int y = 0; y < down_; ++y) {
            for (int x = 0; x < across_; ++x) {
                const Cell& b = board_[std::size_t(y * across_ + x)];
                if (b.value != 0 && s.cells.size() < 0x400) {
                    s.cells.push_back({x, y, b});
                }
            }
        }
        history_.push_back(std::move(s));
        return yes(true);
    }
    case 42:
        history_.clear();
        return yes(true);
    case 43:
        // The newest snapshot dropped.
        if (!history_.empty()) {
            history_.pop_back();
        }
        return yes(true);
    case 44: {
        // 0x29fbac: the newest snapshot's cells written back (the others
        // stay), its values into registers; then it is dropped.
        if (history_.empty()) {
            return yes(false);
        }
        const Snapshot s = history_.back();
        history_.pop_back();
        restore(s);
        store(0, Value::ofInt(s.x));
        store(1, Value::ofInt(s.y));
        store(2, Value::ofInt(int(s.value)));
        return yes(true);
    }
    case 45:
        for (Piece& p : pieces_) {
            p.position = objectPosition(p);
        }
        return yes(true);
    case 46: {
        // 0x2a11bc: whether the piece's values fit the board's at cell (x,
        // y): no cell where both have a different value.
        Piece* p = count >= 3 ? piece(int(word(0))) : nullptr;
        if (!p) {
            return false;
        }
        bool clash = false;
        for (int y = 0; y < p->h; ++y) {
            for (int x = 0; x < p->w; ++x) {
                const Cell* pc = p->cell(x, y);
                const Cell* b = boardCell(x + integer(1), y + integer(2));
                if (pc && b && pc->value != 0 && b->value != 0 && pc->value != b->value) {
                    clash = true;
                }
            }
        }
        return yes(!clash);
    }
    case 47: {
        // The footprint's top left cell at (x, y) into two registers.
        Piece* p = count >= 5 ? piece(int(word(0))) : nullptr;
        if (!p) {
            return false;
        }
        const Footprint f = footprint(*p, integer(1), integer(2));
        store(3, Value::ofInt(f.first.x));
        store(4, Value::ofInt(f.first.y));
        return yes(true);
    }
    case 48: {
        // 0x2a1380: whether a piece on the board overlaps nothing [into a
        // register: 0 on an overlap].
        if (count < 2) {
            return yes(false);
        }
        store(1, Value::ofInt(0));
        Piece* p = piece(int(word(0)));
        if (!p || !insideBoard(p->position.x, p->position.y)) {
            return yes(true);
        }
        // The piece's whole grid (object position -/+ half its cells x the
        // pitch) must lie inside the board too (0x258224, 0x22fb50).
        const Point16 at = objectPosition(*p);
        const int hx = (p->w * pitchX()) >> 1, hy = (p->h * pitchY()) >> 1;
        if (!insideBoard(at.x - hx, at.y - hy) || !insideBoard(at.x + hx, at.y + hy)) {
            return yes(true);
        }
        const Footprint f = footprint(*p, p->position.x, p->position.y);
        for (int row = 0; row <= f.last.y - f.first.y; ++row) {
            for (int col = 0; col <= f.last.x - f.first.x; ++col) {
                const Cell* pc = p->cell(col, row);
                const Cell* b = boardCell(f.first.x + col, f.first.y + row);
                if (pc && pc->value != 0 && b && b->flags != 0 && (b->flags & Cell::kOverlap) &&
                    (p->flags & Piece::kOverlap)) {
                    return yes(true);
                }
            }
        }
        store(1, Value::ofInt(1));
        return yes(true);
    }
    default:
        return ownCommand(number, c, first) || arrows_.command(game_, number, c, first);
    }
}

std::string FitPuzzle::describe() const {
    std::string out;
    char line[160];
    std::snprintf(line, sizeof line, "board %d %d cell %d %d gap %d %d origin %d %d flags %u\n",
                  across_, down_, cellW_, cellH_, gapX_, gapY_, originX_, originY_, flags_);
    out += line;
    for (int y = 0; y < down_; ++y) {
        out += "row";
        for (int x = 0; x < across_; ++x) {
            const Cell& c = board_[std::size_t(y * across_ + x)];
            std::snprintf(line, sizeof line, " %x/%x", c.value, c.flags);
            out += line;
        }
        out += '\n';
    }
    for (const Piece& p : pieces_) {
        std::snprintf(line, sizeof line, "piece %x %dx%d at %d %d turn %d flags %x object %u\n",
                      p.id, p.w, p.h, p.position.x, p.position.y, p.turn(), p.flags, p.object);
        out += line;
        for (int y = 0; y < p.h; ++y) {
            out += "  cells";
            for (int x = 0; x < p.w; ++x) {
                std::snprintf(line, sizeof line, " %x", p.cells[std::size_t(y * p.w + x)].value);
                out += line;
            }
            out += '\n';
        }
    }
    return out;
}

} // namespace picarat::adventure5::fit

namespace picarat::adventure5 {

std::unique_ptr<SpecialPuzzle> makeFitPuzzle(Game& game) {
    return std::make_unique<fit::FitPuzzle>(game);
}

} // namespace picarat::adventure5
