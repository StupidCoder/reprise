// The sixth adventure's Transfer puzzles (loop 3170; catalogue type 26:
// 015, 019, "to the other bank"): boats move along the rows of a board
// (each a step of its own per turn, wrapping round the board's edge) and a
// character hops from boat to boat to cross. Fit's board and commands
// (a6_fit_puzzle) carry it; the boats and the character are records of
// this class: a board cell's value holds the boat on it, its extra value
// the character, so a boat moving carries the character standing on it.
// Ids are a slot | a kind << 12 (1 the character, 2 a boat) | a serial
// << 16 (0x21669c tells the kinds apart).
//
// Reconstructed from the class's command handler 0x32d540 (vtable
// 0x3c9cfc), which takes 1001..1020 and passes the rest to Fit's 0x29dae0.
#include "puzzles/a6_fit_puzzle.hpp"
#include <cstdio>

namespace picarat::adventure5::fit {
namespace {

using xq::Call;
using xq::Value;

struct Boat {             // 0x24 bytes at +0x24c
    std::uint32_t id = 0; // +0xc
    Point16 at;           // +0x10
    Point16 start;        // +0x14
    Point16 step;         // +0x18: the move per turn (1003)
    Point16 pixel;        // +0x1c: where the last move went, in pixels
    Point16 reached;      // +0x20: the same in cells, before wrapping
};

struct Walker {           // the character (+0x26c)
    std::uint32_t id = 0; // +0x278
    Point16 at;           // +0x27c
    Point16 start;
};

class TransferPuzzle : public FitPuzzle {
public:
    using FitPuzzle::FitPuzzle;

    std::string describe() const override;

protected:
    bool ownCommand(int number, Call& c, std::size_t first) override;

private:
    // 0x21669c: a record's cell by id (a boat or the character).
    bool cellOf(std::uint32_t id, Point16& at) const {
        if (const Boat* b = boat(id)) {
            at = b->at;
            return true;
        }
        if (walker_.id != 0 && id == walker_.id) {
            at = walker_.at;
            return true;
        }
        return false;
    }

    const Boat* boat(std::uint32_t id) const {
        const std::size_t slot = id & 0xfff;
        return ((id >> 12) & 0xf) == 2 && slot < boats_.size() && boats_[slot].id == id
                   ? &boats_[slot]
                   : nullptr;
    }

    Boat* boat(std::uint32_t id) { return const_cast<Boat*>(std::as_const(*this).boat(id)); }

    Point16 centre(Point16 cell, bool topLeft) const {
        int x = 0, y = 0;
        cellPixel(cell.x, cell.y, !topLeft, x, y);
        return {x, y};
    }

    std::vector<Boat> boats_; // +0x240 pool
    int boatsMade_ = 0;
    int serial_ = 0;
    Walker walker_;

    // 1018 / 1019: saved places (0x298 list: up to 32 entries a record).
    struct Saved {
        std::vector<std::pair<std::uint32_t, Point16>> places;
    };

    std::vector<Saved> saved_;
};

bool TransferPuzzle::ownCommand(int number, Call& c, std::size_t first) {
    const std::size_t count = c.count() - first;
    auto integer = [&](std::size_t i) {
        return int(std::int16_t(c.integer(first + i)));
    };
    auto word = [&](std::size_t i) {
        return c.unsignedInt(first + i);
    };
    auto store = [&](std::size_t i, int v) {
        if (i < count) {
            if (Value* r = c.reg(first + i)) {
                *r = Value::ofInt(v);
            }
        }
    };
    auto yes = [&](bool b) {
        c.result = Value::ofInt(b ? 1 : 0);
        return true;
    };
    auto id = [&](std::uint32_t v) {
        c.result = Value::ofInt(std::int32_t(v));
        return true;
    };
    c.result = Value::ofInt(0);
    switch (number) {
    case 1001:
        // 0x32eef0: room for a number of boats.
        if (count == 0) {
            return false;
        }
        boats_.assign(std::size_t(std::max(0, integer(0))), Boat{});
        boatsMade_ = 0;
        return yes(true);
    case 1002: {
        // 0x32e8d8: a boat on the empty cell (x, y) -> its id.
        Cell* b = count > 1 ? boardCell(integer(0), integer(1)) : nullptr;
        if (!b || b->value != 0 || boatsMade_ >= int(boats_.size())) {
            return false;
        }
        Boat& n = boats_[std::size_t(boatsMade_)];
        n = {};
        n.id = std::uint32_t(boatsMade_) | 2u << 12 | std::uint32_t(++serial_) << 16;
        n.at = n.start = {integer(0), integer(1)};
        ++boatsMade_;
        b->value = n.id;
        return id(n.id);
    }
    case 1003: {
        // A boat's step per turn (dx, dy).
        Boat* n = count > 2 ? boat(word(0)) : nullptr;
        if (!n) {
            return true;
        }
        n->step = {integer(1), integer(2)};
        return yes(true);
    }
    case 1004: {
        const Boat* n = count > 2 ? boat(word(0)) : nullptr;
        if (!n) {
            return true;
        }
        store(1, n->step.x);
        store(2, n->step.y);
        return yes(true);
    }
    case 1005: {
        // A record's cell in pixels (id [, top left instead of centre]).
        Point16 at;
        if (count < 3 || !cellOf(word(0), at)) {
            return false;
        }
        const std::size_t k = count > 3 ? 2 : 1;
        const Point16 p = centre(at, count > 3 && word(1) != 0);
        store(k, p.x);
        store(k + 1, p.y);
        return yes(true);
    }
    case 1006: {
        // 0x32e2bc: a boat takes its step (wrapping round the board) with
        // what stands on it; the new cell [, pixels of the cell it reached,
        // and that cell unwrapped] into registers.
        Boat* n = count > 0 ? boat(word(0)) : nullptr;
        if (!n) {
            return false;
        }
        std::size_t k = 1;
        bool topLeft = false;
        if (count > 3) {
            topLeft = word(1) != 0;
            k = 2;
        }
        const Point16 reached{n->at.x + n->step.x, n->at.y + n->step.y};
        Point16 to = reached;
        if (!boardCell(to.x, to.y)) {
            to.x = ((to.x % across_) + across_) % across_;
            to.y = ((to.y % down_) + down_) % down_;
        }
        Cell* from = boardCell(n->at.x, n->at.y);
        Cell* into = boardCell(to.x, to.y);
        if (from && into) {
            const Cell moved = *from;
            *from = {};
            *into = moved;
        }
        n->at = to;
        n->pixel = centre(reached, topLeft);
        n->reached = reached;
        for (int v : {to.x, to.y, n->pixel.x, n->pixel.y, reached.x, reached.y}) {
            store(k++, v);
        }
        return yes(true);
    }
    case 1007:
    case 1008: {
        // A boat's last move: pixels (1007) or the cell unwrapped (1008).
        const Boat* n = count > 0 ? boat(word(0)) : nullptr;
        if (!n) {
            return false;
        }
        const Point16 p = number == 1007 ? n->pixel : n->reached;
        store(1, p.x);
        store(2, p.y);
        return yes(true);
    }
    case 1009: {
        Point16 at;
        if (count == 0 || !cellOf(word(0), at)) {
            return false;
        }
        store(1, at.x);
        store(2, at.y);
        return yes(true);
    }
    case 1010:
        return count > 1 ? yes(boardCell(integer(0), integer(1)) != nullptr) : false;
    case 1011: {
        // 0x32e19c: the character on cell (x, y) (its extra value) -> id.
        Cell* b = count > 1 ? boardCell(integer(0), integer(1)) : nullptr;
        if (!b || b->extra != 0) {
            return false;
        }
        walker_.id = 1u << 12 | std::uint32_t(++serial_) << 16;
        walker_.at = walker_.start = {integer(0), integer(1)};
        b->extra = walker_.id;
        return id(walker_.id);
    }
    case 1012: {
        // 0x32eb34: a cell's boat and character into registers.
        const Cell* b = count > 1 ? boardCell(integer(0), integer(1)) : nullptr;
        store(2, b ? int(b->value) : 0);
        store(3, b ? int(b->extra) : 0);
        return yes(b != nullptr);
    }
    case 1013:
    case 1014: {
        // 0x32e7a8 / 0x32ea10: the character (1013: the second id, 1014:
        // the first) steps to a record's cell (1013: the first id) or to a
        // cell (1014: x, y).
        Point16 to, from;
        const std::uint32_t who = number == 1013 ? word(1) : word(0);
        if (count < (number == 1013 ? 2u : 3u) || !cellOf(who, from)) {
            return false;
        }
        if (number == 1013) {
            if (!cellOf(word(0), to)) {
                return false;
            }
        } else {
            to = {integer(1), integer(2)};
        }
        Cell* a = boardCell(to.x, to.y);
        Cell* b = boardCell(from.x, from.y);
        if (!a || !b) {
            return false;
        }
        a->extra = b->extra;
        b->extra = 0;
        if (who == walker_.id) {
            walker_.at = to;
        }
        return yes(true);
    }
    case 1015: {
        // The character on a record's cell (its id, 0 for none).
        Point16 at;
        const Cell* b = count > 0 && cellOf(word(0), at) ? boardCell(at.x, at.y) : nullptr;
        return b ? id(b->extra) : false;
    }
    case 1016: {
        // The character, if it stands on a record's cell, takes that place.
        Point16 at;
        const Cell* b = count > 0 && cellOf(word(0), at) ? boardCell(at.x, at.y) : nullptr;
        if (!b || walker_.id == 0 || b->extra != walker_.id) {
            return false;
        }
        walker_.at = at;
        return yes(true);
    }
    case 1017:
        // The board as it started: every boat and the character back.
        for (Cell& b : board_) {
            b = {};
        }
        for (int i = 0; i < boatsMade_; ++i) {
            Boat& n = boats_[std::size_t(i)];
            n.at = n.start;
            if (Cell* b = boardCell(n.at.x, n.at.y)) {
                b->value = n.id;
            }
        }
        if (walker_.id != 0) {
            walker_.at = walker_.start;
            if (Cell* b = boardCell(walker_.at.x, walker_.at.y)) {
                b->extra = walker_.id;
            }
        }
        saved_.clear();
        return yes(true);
    case 1018: {
        // 0x32e578: every boat's and the character's cell saved (32 at most).
        Saved s;
        for (int i = 0; i < boatsMade_ && s.places.size() < 32; ++i) {
            s.places.push_back({boats_[std::size_t(i)].id, boats_[std::size_t(i)].at});
        }
        if (walker_.id != 0 && s.places.size() < 32) {
            s.places.push_back({walker_.id, walker_.at});
        }
        saved_.push_back(std::move(s));
        return yes(true);
    }
    case 1019: {
        // 0x32dddc: the newest saved cells back, the board made from them.
        if (saved_.empty()) {
            return true;
        }
        for (Cell& b : board_) {
            b = {};
        }
        for (const auto& [who, at] : saved_.back().places) {
            Cell* b = boardCell(at.x, at.y);
            if (!b) {
                continue;
            }
            if (Boat* n = boat(who)) {
                n->at = at;
                b->value = who;
            } else if (who == walker_.id) {
                walker_.at = at;
                b->extra = who;
            }
        }
        saved_.pop_back();
        return yes(true);
    }
    case 1020: {
        // 0x32ec34: a cell in pixels (x, y [, top left]) into registers.
        if (count < 4) {
            return false;
        }
        const std::size_t k = count > 4 ? 3 : 2;
        const Point16 p = centre({integer(0), integer(1)}, count > 4 && word(2) != 0);
        store(k, p.x);
        store(k + 1, p.y);
        return yes(true);
    }
    default:
        return false;
    }
}

std::string TransferPuzzle::describe() const {
    std::string out = FitPuzzle::describe();
    char line[160];
    for (int i = 0; i < boatsMade_; ++i) {
        const Boat& n = boats_[std::size_t(i)];
        std::snprintf(line, sizeof line, "boat %x at %d %d step %d %d start %d %d\n", n.id, n.at.x,
                      n.at.y, n.step.x, n.step.y, n.start.x, n.start.y);
        out += line;
    }
    std::snprintf(line, sizeof line, "walker %x at %d %d\n", walker_.id, walker_.at.x,
                  walker_.at.y);
    out += line;
    return out;
}

} // namespace
} // namespace picarat::adventure5::fit

namespace picarat::adventure5 {

std::unique_ptr<SpecialPuzzle> makeTransferPuzzle(Game& game) {
    return std::make_unique<fit::TransferPuzzle>(game);
}

} // namespace picarat::adventure5
