// The sixth adventure's Partition puzzles (loop 3180; catalogue types 27,
// 30, 31, 32, 41): Fit's board and pieces (a6_fit_puzzle) plus two layers
// of things of its own, all moved by the puzzles' scripts:
// * corners: one at every corner of the board's cells, (across + 1) x
//   (down + 1), each with a value, an extra value, a turn (in eighths) and
//   the ids of its eight neighbours. The sandbag puzzles put the bags on
//   them and compare the sums around each cell; the plate puzzle gives them
//   arms (four bits in the extra value, turned with the corner) that wall
//   the cells off into regions.
// * items: up to across x down things in board cells (the board cell's
//   value holds the item's id), each with a value, an extra value and the
//   ids of its neighbours up, right, down and left. The crate puzzles stack
//   them, move them with gravity and compare the colours in each column.
// Ids are a pool index | an allocation serial << 16, as in Fit's pieces.
//
// Reconstructed from the class's command handler 0x331ba4 (vtable
// 0x3c9edc), which takes 1001..1030 and passes the rest to Fit's
// 0x29dae0. Not reconstructed: 1004 (0x33481c) and 1011 (0x333ff0), which
// none of the Partition puzzles use.
#include "puzzles/a6_fit_puzzle.hpp"
#include <algorithm>
#include <array>
#include <cstdio>

namespace picarat::adventure5::fit {
namespace {

using xq::Call;
using xq::Value;

// Directions: corners have eight (0x3335c8's offset table, clockwise from
// up), items four (up, right, down, left; 0x33438c).
constexpr std::array<std::array<int, 2>, 8> kCornerSteps{
    {{0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}}};
constexpr std::array<std::array<int, 2>, 4> kItemSteps{{{0, -1}, {1, 0}, {0, 1}, {-1, 0}}};

struct Corner { // 0x3c bytes
    std::uint32_t id = 0;
    std::uint16_t flags = 0;                   // +0xc: bits 0-2 the turn
    std::uint32_t value = 0;                   // +0x14
    std::uint32_t extra = 0;                   // +0x18
    std::array<std::uint32_t, 8> neighbours{}; // +0x1c
};

struct Item { // 0x28 bytes
    std::uint32_t id = 0;
    std::array<std::uint32_t, 4> neighbours{}; // +0x10: up, right, down, left
    std::uint32_t value = 0;                   // +0x20
    std::uint32_t extra = 0;                   // +0x24
};

// A fixed pool of records handed out in order, then from those freed
// (0x332d7c, 0x332f44); the id carries the slot and a serial.
template <typename T>
struct Pool {
    std::vector<T> slots;
    std::vector<bool> alive;
    std::vector<int> order; // live slots in allocation order (the list)
    int used = 0;           // slots handed out so far (+0x20)
    std::vector<int> freed;
    int serial = 0; // +0x24

    void reset(int capacity, bool newSerial) {
        slots.assign(std::size_t(std::max(capacity, 0)), T{});
        alive.assign(slots.size(), false);
        order.clear();
        freed.clear();
        used = 0;
        if (newSerial) {
            serial = 0;
        }
    }

    // Every record free again; the serial goes on (1026, 1027).
    void clear() {
        alive.assign(slots.size(), false);
        order.clear();
        freed.clear();
        used = 0;
    }

    T* make() {
        int slot;
        if (used < int(slots.size())) {
            slot = used++;
        } else if (!freed.empty()) {
            slot = freed.front();
            freed.erase(freed.begin());
        } else {
            return nullptr;
        }
        T& r = slots[std::size_t(slot)];
        r = T{};
        r.id = std::uint32_t(slot) | std::uint32_t(++serial) << 16;
        alive[std::size_t(slot)] = true;
        order.push_back(slot);
        return &r;
    }

    void release(T& r) {
        const int slot = int(&r - slots.data());
        alive[std::size_t(slot)] = false;
        order.erase(std::find(order.begin(), order.end(), slot));
        freed.push_back(slot);
    }

    // A record by id: the slot must hold that very id.
    T* find(std::uint32_t id) {
        const std::size_t slot = id & 0xfff;
        return slot < slots.size() && slots[slot].id == id ? &slots[slot] : nullptr;
    }
};

// 0x213e94: a corner's arms turned by its quarter turns (bits rotate up ->
// right -> down -> left).
std::uint32_t turnedArms(const Corner& c) {
    std::uint32_t m = c.extra;
    for (int q = (c.flags & 6) >> 1; q > 0; --q) {
        m = ((m << 1) | (m >> 3)) & 0xf;
    }
    return m;
}

class PartitionPuzzle : public FitPuzzle {
public:
    using FitPuzzle::FitPuzzle;

    std::string describe() const override;

protected:
    bool ownCommand(int number, Call& c, std::size_t first) override;

private:
    void clearBoard() {
        for (Cell& b : board_) {
            b = {};
        }
    }

    // 0x213f68: the corner at grid point (x, y).
    Corner* cornerAt(int x, int y) {
        if (x < 0 || y < 0 || x >= cornersAcross_ || y >= cornersDown_) {
            return nullptr;
        }
        const std::size_t i = std::size_t(y * cornersAcross_ + x);
        return i < corners_.slots.size() ? &corners_.slots[i] : nullptr;
    }

    // The item whose id a board cell holds.
    Item* itemIn(int x, int y) {
        const Cell* b = boardCell(x, y);
        return b ? items_.find(b->value) : nullptr;
    }

    // 0x213ef4: the number of items along a direction from a slot.
    int chainLength(std::uint32_t id, int direction, int count) {
        while (direction >= 0 && direction < 4) {
            const Item* it = items_.find(id);
            if (!it) {
                break;
            }
            ++count;
            id = it->neighbours[std::size_t(direction)];
        }
        return count;
    }

    std::uint32_t cellWalls(const Item& cell);
    int fillRegion(int x, int y, int label);
    bool regionsKept(int x, int y, std::uint32_t label);
    bool moveStack(std::uint32_t id, Point16 from, Point16 to);

    Pool<Corner> corners_;  // +0x240
    int cornersAcross_ = 0; // +0x268
    int cornersDown_ = 0;
    Pool<Item> items_;    // +0x26c
    int itemsAcross_ = 0; // +0x294
    int itemsDown_ = 0;
};

// 0x3326e8: a cell's walls (1 up, 2 right, 4 down, 8 left) from the arms of
// its corners: top left's right and down arms, top right's left and down,
// bottom right's left and up, bottom left's right and up.
std::uint32_t PartitionPuzzle::cellWalls(const Item& cell) {
    std::uint32_t w = 0;
    auto arms = [&](int k) -> std::uint32_t {
        const Corner* c = corners_.find(cell.neighbours[std::size_t(k)]);
        return c ? turnedArms(*c) : 0;
    };
    const std::uint32_t tl = arms(0), tr = arms(1), br = arms(2), bl = arms(3);
    w |= (tl & 2) ? 1 : 0;
    w |= (tl & 4) ? 8 : 0;
    w |= (tr & 8) ? 1 : 0;
    w |= (tr & 4) ? 2 : 0;
    w |= (br & 8) ? 4 : 0;
    w |= (br & 1) ? 2 : 0;
    w |= (bl & 2) ? 4 : 0;
    w |= (bl & 1) ? 8 : 0;
    return w;
}

// 0x213d4c: a region label spread from a cell to every cell it reaches
// without crossing a wall; the next label when this one was used.
int PartitionPuzzle::fillRegion(int x, int y, int label) {
    Cell* b = boardCell(x, y);
    if (!b || b->extra != 0) {
        return label;
    }
    b->extra = std::uint32_t(label);
    const std::uint32_t walls = b->value;
    for (int d = 0; d < 4; ++d) {
        if (!(walls & (1u << d))) {
            fillRegion(x + kItemSteps[std::size_t(d)][0], y + kItemSteps[std::size_t(d)][1], label);
        }
    }
    return label + 1;
}

// 0x213bdc: every wall of the cell has another region behind it (a wall
// inside a region fails); label 0 starts at the cell's own.
bool PartitionPuzzle::regionsKept(int x, int y, std::uint32_t label) {
    const Cell* b = boardCell(x, y);
    if (!b) {
        return true;
    }
    if (label != 0) {
        return b->extra != label;
    }
    bool kept = true;
    for (int d = 0; d < 4; ++d) {
        if (b->value & (1u << d)) {
            kept = regionsKept(x + kItemSteps[std::size_t(d)][0], y + kItemSteps[std::size_t(d)][1],
                               b->extra) &&
                   kept;
        }
    }
    return kept;
}

// 0x333238: an item moved from one cell to another with everything stacked
// on it (its up neighbours, one row higher each).
bool PartitionPuzzle::moveStack(std::uint32_t id, Point16 from, Point16 to) {
    const Item* it = items_.find(id);
    if (!it) {
        return true;
    }
    if (!boardCell(to.x, to.y) || !boardCell(from.x, from.y)) {
        return false;
    }
    if (!moveStack(it->neighbours[0], {from.x, from.y - 1}, {to.x, to.y - 1})) {
        return false;
    }
    Cell* src = boardCell(from.x, from.y);
    *boardCell(to.x, to.y) = *src;
    *src = {};
    return true;
}

bool PartitionPuzzle::ownCommand(int number, Call& c, std::size_t first) {
    const std::size_t count = c.count() - first;
    auto integer = [&](std::size_t i) {
        return int(std::int16_t(c.integer(first + i)));
    };
    auto word = [&](std::size_t i) {
        return c.unsignedInt(first + i);
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
    auto number32 = [&](std::uint32_t v) {
        return result(Value::ofInt(std::int32_t(v)));
    };
    c.result = Value::ofInt(0);
    switch (number) {
    case 1001: {
        // 0x3335c8: the corner grid (with each corner's eight neighbours)
        // and an item per cell holding its four corners (top left, top
        // right, bottom right, bottom left); the board emptied.
        clearBoard();
        cornersAcross_ = across_ + 1;
        cornersDown_ = down_ + 1;
        corners_.reset(cornersAcross_ * cornersDown_, true);
        itemsAcross_ = across_;
        itemsDown_ = down_;
        items_.reset(across_ * down_, false);
        for (int i = 0; i < cornersAcross_ * cornersDown_; ++i) {
            corners_.make();
        }
        for (int y = 0; y < cornersDown_; ++y) {
            for (int x = 0; x < cornersAcross_; ++x) {
                Corner* k = cornerAt(x, y);
                for (std::size_t d = 0; d < 8; ++d) {
                    const Corner* n = cornerAt(x + kCornerSteps[d][0], y + kCornerSteps[d][1]);
                    k->neighbours[d] = n ? n->id : 0;
                }
            }
        }
        constexpr std::array<std::array<int, 2>, 4> kCellCorners{{{0, 0}, {1, 0}, {1, 1}, {0, 1}}};
        for (int y = 0; y < itemsDown_; ++y) {
            for (int x = 0; x < itemsAcross_; ++x) {
                Item* it = items_.make();
                for (std::size_t k = 0; k < 4; ++k) {
                    const Corner* n = cornerAt(x + kCellCorners[k][0], y + kCellCorners[k][1]);
                    it->neighbours[k] = n ? n->id : 0;
                }
            }
        }
        return yes(true);
    }
    case 1002: {
        // A corner at grid point (x, y) gets a value [, flags [, extra]];
        // its id.
        if (count < 3) {
            return false;
        }
        Corner* k = cornerAt(integer(1), integer(2));
        if (!k) {
            return false;
        }
        k->value = word(0);
        k->flags = count > 3 ? std::uint16_t(word(3)) : 0;
        k->extra = count > 4 ? word(4) : 0;
        return number32(k->id);
    }
    case 1003: {
        // 0x333e44: each cell's walls from its corners' arms into the
        // board's values, then the regions numbered from 1 into the
        // extra values; the next free number.
        clearBoard();
        for (int y = 0; y < itemsDown_; ++y) {
            for (int x = 0; x < itemsAcross_; ++x) {
                const std::size_t i = std::size_t(y * itemsAcross_ + x);
                if (i < items_.slots.size()) {
                    if (Cell* b = boardCell(x, y)) {
                        b->value = cellWalls(items_.slots[i]);
                    }
                }
            }
        }
        int label = 1;
        for (int y = 0; y < down_; ++y) {
            for (int x = 0; x < across_; ++x) {
                label = fillRegion(x, y, label);
            }
        }
        return result(Value::ofInt(label));
    }
    case 1005: {
        // 0x334934: a corner's flags, extra value, turned arms and value.
        const Corner* k = count > 1 ? corners_.find(word(0)) : nullptr;
        if (!k) {
            return false;
        }
        store(1, Value::ofInt(k->flags));
        store(2, Value::ofUint(k->extra));
        store(3, Value::ofUint(turnedArms(*k)));
        store(4, Value::ofUint(k->value));
        return yes(true);
    }
    case 1006:
    case 1008: {
        // The corner at (x, y) turned one step (1006) or set to a turn.
        Corner* k =
            count >= (number == 1006 ? 2u : 3u) ? cornerAt(integer(0), integer(1)) : nullptr;
        if (!k) {
            return false;
        }
        const int turn = number == 1006 ? (k->flags + 1) & 7 : integer(2) & 7;
        k->flags = std::uint16_t((k->flags & ~7) | turn);
        return yes(true);
    }
    case 1007:
    case 1009: {
        // A corner by id turned two steps (a quarter, 1007) or set to a turn.
        Corner* k = count >= (number == 1007 ? 1u : 2u) ? corners_.find(word(0)) : nullptr;
        if (!k) {
            return false;
        }
        const int turn = number == 1007 ? (k->flags + 2) & 7 : integer(1) & 7;
        k->flags = std::uint16_t((k->flags & ~7) | turn);
        return yes(true);
    }
    case 1010: {
        // A board cell's value and extra value (here walls and region).
        const Cell* b = count > 2 ? boardCell(integer(0), integer(1)) : nullptr;
        if (!b) {
            return false;
        }
        store(2, Value::ofUint(b->value));
        store(3, Value::ofUint(b->extra));
        return yes(true);
    }
    case 1012:
        // The first corner (in the list's order) holding a value: its id.
        if (count == 0) {
            return false;
        }
        for (int slot : corners_.order) {
            if (corners_.slots[std::size_t(slot)].value == word(0)) {
                return number32(corners_.slots[std::size_t(slot)].id);
            }
        }
        return true;
    case 1013: {
        // 0x333d20: two corners swap value, extra value and flags.
        Corner* a = count > 1 ? corners_.find(word(0)) : nullptr;
        Corner* b = count > 1 ? corners_.find(word(1)) : nullptr;
        if (!a || !b) {
            return false;
        }
        std::swap(a->value, b->value);
        std::swap(a->extra, b->extra);
        std::swap(a->flags, b->flags);
        return yes(true);
    }
    case 1014: {
        // A corner by id gets value [, extra [, flags]].
        Corner* k = count > 0 ? corners_.find(word(0)) : nullptr;
        if (!k) {
            return false;
        }
        if (count > 1) {
            k->value = word(1);
        }
        if (count > 2) {
            k->extra = word(2);
        }
        if (count > 3) {
            k->flags = std::uint16_t(word(3));
        }
        return yes(true);
    }
    case 1015: {
        // 0x3341c0: each cell's value the sum of its corners' extra values;
        // true when every cell's sum is the same.
        for (int y = 0; y < itemsDown_; ++y) {
            for (int x = 0; x < itemsAcross_; ++x) {
                const Item& cell = items_.slots[std::size_t(y * itemsAcross_ + x)];
                std::uint32_t sum = 0;
                for (std::uint32_t id : cell.neighbours) {
                    if (const Corner* k = corners_.find(id)) {
                        sum += k->extra;
                    }
                }
                if (Cell* b = boardCell(x, y)) {
                    b->value = sum;
                }
            }
        }
        for (std::size_t i = 0; i + 1 < board_.size(); ++i) {
            if (board_[i].value != board_[i + 1].value) {
                return yes(false);
            }
        }
        return yes(true);
    }
    case 1016:
        // The board emptied and room made for across x down items.
        clearBoard();
        itemsAcross_ = across_;
        itemsDown_ = down_;
        items_.reset(across_ * down_, false);
        return yes(true);
    case 1017: {
        // 0x3339e4: a new item (value, cell x, y [, extra]) put in the cell;
        // its id (0, and the item dropped, off the board).
        if (count < 3) {
            return false;
        }
        Item* it = items_.make();
        if (!it) {
            return true;
        }
        it->value = word(0);
        it->extra = count > 3 ? word(3) : 0;
        if (Cell* b = boardCell(integer(1), integer(2))) {
            b->value = it->id;
            return number32(it->id);
        }
        items_.release(*it);
        return true;
    }
    case 1018:
        // 0x33438c: every item's neighbours from the cells around it.
        for (int y = 0; y < down_; ++y) {
            for (int x = 0; x < across_; ++x) {
                if (Item* it = itemIn(x, y)) {
                    for (std::size_t d = 0; d < 4; ++d) {
                        const Cell* n = boardCell(x + kItemSteps[d][0], y + kItemSteps[d][1]);
                        it->neighbours[d] = n ? n->value : 0;
                    }
                }
            }
        }
        return yes(true);
    case 1019: {
        // 0x3345c8: an item's four neighbours into registers.
        const Item* it = count > 0 ? items_.find(word(0)) : nullptr;
        if (count == 0) {
            return false;
        }
        for (std::size_t d = 0; d < 4; ++d) {
            store(1 + d, Value::ofUint(it ? it->neighbours[d] : 0));
        }
        return yes(true);
    }
    case 1020: {
        // An item's value and extra value into registers.
        const Item* it = count > 0 ? items_.find(word(0)) : nullptr;
        if (count == 0) {
            return false;
        }
        store(1, Value::ofUint(it ? it->value : 0));
        store(2, Value::ofUint(it ? it->extra : 0));
        return yes(true);
    }
    case 1021: {
        // 0x333394 / 0x332bb8: an item moved to (x, y) of another column
        // (the cell must be empty) falls while the cell below is empty, and
        // carries what is stacked on it; where it ends into registers.
        if (count < 5) {
            return false;
        }
        const std::uint32_t id = word(0);
        Point16 to{integer(1), integer(2)}, from{-1, -1};
        for (int i = 0; i < int(board_.size()) && from.x < 0; ++i) {
            if (board_[std::size_t(i)].value == id) {
                from = {i % across_, i / across_};
            }
        }
        bool moved = false;
        Point16 end{0, 0};
        const Cell* target = boardCell(to.x, to.y);
        if (from.x >= 0 && target && from.x != to.x && target->value == 0) {
            Point16 at = to;
            for (;;) {
                const Cell* below = boardCell(at.x, at.y + 1);
                if (!below || (below->value != 0 && below->value != id)) {
                    break;
                }
                ++at.y;
            }
            if (moveStack(id, from, at)) {
                moved = true;
                end = at;
            }
        }
        store(3, Value::ofInt(end.x));
        store(4, Value::ofInt(end.y));
        return yes(moved);
    }
    case 1022: {
        // 0x3346e4: from the item in cell (x, y), how many items follow one
        // another in a direction (the item itself counted).
        if (count < 3) {
            return false;
        }
        const int direction = integer(2);
        const Item* it = itemIn(integer(0), integer(1));
        int n = 0;
        if (it && direction >= 0 && direction < 4) {
            n = chainLength(it->neighbours[std::size_t(direction)], direction, 1);
        }
        return result(Value::ofInt(n));
    }
    case 1023: {
        // The same from an item by id.
        if (count < 2) {
            return false;
        }
        const int direction = integer(1);
        const Item* it = items_.find(word(0));
        int n = 0;
        if (it && direction >= 0 && direction < 4) {
            n = chainLength(it->neighbours[std::size_t(direction)], direction, 1);
        }
        return result(Value::ofInt(n));
    }
    case 1024:
        // 0x333468: the board emptied and set back to the newest snapshot
        // (Fit's history, command 41), which is dropped.
        if (history_.empty()) {
            return true;
        }
        clearBoard();
        restore(history_.back());
        history_.pop_back();
        return yes(true);
    case 1025: {
        // 0x333b9c: in the column x (every column without one) the item in
        // the bottom row and those following it in a direction all share
        // its extra value [and that one equals a given value].
        if (count == 0) {
            return false;
        }
        const int direction = integer(0);
        const int only = count > 1 ? integer(1) : -1;
        const std::uint32_t wanted = count > 2 ? word(2) : 0;
        for (int x = only < 0 ? 0 : only; x < across_; ++x) {
            const Item* it = itemIn(x, down_ - 1);
            if (it && direction >= 0 && direction < 4) {
                if (wanted != 0 && it->extra != wanted) {
                    return true;
                }
                // 0x334144: along the chain, each item's extra value
                // against the one before (unless that one is 0).
                std::uint32_t before = it->extra;
                std::uint32_t id = it->neighbours[std::size_t(direction)];
                while (const Item* n = items_.find(id)) {
                    if (before != 0 && n->extra != before) {
                        return true;
                    }
                    before = n->extra;
                    id = n->neighbours[std::size_t(direction)];
                }
            }
            if (only >= 0) {
                break;
            }
        }
        return yes(true);
    }
    case 1026:
        items_.clear();
        return true;
    case 1027:
        corners_.clear();
        return true;
    case 1028:
        clearBoard();
        return true;
    case 1029: {
        // A corner's eight neighbours into registers.
        const Corner* k = count > 1 ? corners_.find(word(0)) : nullptr;
        if (!k) {
            return false;
        }
        for (std::size_t d = 0; d < 8; ++d) {
            store(1 + d, Value::ofUint(k->neighbours[d]));
        }
        return yes(true);
    }
    case 1030: {
        // 0x333144: every wall lies between two regions, and the cell (x, y)
        // given (if any) is walled on all four sides.
        const int x = count > 0 ? integer(0) : -1, y = count > 1 ? integer(1) : -1;
        for (int cy = 0; cy < down_; ++cy) {
            for (int cx = 0; cx < across_; ++cx) {
                if (!regionsKept(cx, cy, 0)) {
                    return yes(false);
                }
            }
        }
        if (const Cell* b = boardCell(x, y); b && (~b->value & 0xf) != 0) {
            return yes(false);
        }
        return yes(true);
    }
    default:
        return false;
    }
}

std::string PartitionPuzzle::describe() const {
    std::string out = FitPuzzle::describe();
    char line[160];
    for (int y = 0; y < cornersDown_; ++y) {
        out += "corners";
        for (int x = 0; x < cornersAcross_; ++x) {
            const Corner& k = corners_.slots[std::size_t(y * cornersAcross_ + x)];
            std::snprintf(line, sizeof line, " %x/%x/%x", k.value, k.extra, k.flags & 7);
            out += line;
        }
        out += '\n';
    }
    for (int slot : items_.order) {
        const Item& it = items_.slots[std::size_t(slot)];
        std::snprintf(line, sizeof line, "item %x value %x extra %x around %x %x %x %x\n", it.id,
                      it.value, it.extra, it.neighbours[0], it.neighbours[1], it.neighbours[2],
                      it.neighbours[3]);
        out += line;
    }
    return out;
}

} // namespace
} // namespace picarat::adventure5::fit

namespace picarat::adventure5 {

std::unique_ptr<SpecialPuzzle> makePartitionPuzzle(Game& game) {
    return std::make_unique<fit::PartitionPuzzle>(game);
}

} // namespace picarat::adventure5
