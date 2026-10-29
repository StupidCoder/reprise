// The fifth adventure's sliding-lines puzzles (catalogue type 9, loop
// 3200; puzzles 53, 134, 142): pieces on a grid; a piece drags its whole
// row or column, which slides into the empty cells at its end.
//
// Reconstructed from the loop's command handler 0x235694 (vtable 0x402758,
// constructor 0x37e034) and its helpers: grab 0x2362ac, drag 0x236624,
// release 0x2364a4, undo 0x236164, reset 0x386984, the free distance of a
// line 0x27e1f0, the line shift 0x235468 and one cell's move 0x27e588.
// The puzzle script sets up the grid (10) and the pieces (40) and drives
// the drag from the pieces' slide handlers (41 grab, 43 drag, 42 release);
// 22 judges.
#include "puzzles/a5_special.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace picarat::adventure5 {
namespace {

using adventure56::Object;
using xq::Call;
using xq::Value;

class LinePuzzle final : public SpecialPuzzle {
public:
    explicit LinePuzzle(Game& game) : game_(game) {}

    bool command(int number, Call& c, std::size_t first) override;
    std::string describe() const override;

private:
    struct Cell {
        int x = 0, y = 0;  // pixel position on the lower screen
        int occupant = -1; // piece index
    };

    struct Piece {
        int targetX = 0, targetY = 0; // the cell that solves it
        std::uint32_t hash = 0;       // its object ("msic_NN")
        int startX = 0, startY = 0;   // where the reset puts it
        int x = 0, y = 0;             // the cell it is in
    };

    // One undo step (0x188, newest first, at most 30): the line moved by
    // (dx, dy) cells through the cell (x, y).
    struct Move {
        int dx = 0, dy = 0;
        int piece = -1;
        int x = 0, y = 0;
    };

    Cell* cell(int x, int y) {
        if (x < 0 || y < 0 || x >= width_ || y >= height_) {
            return nullptr;
        }
        return &cells_[std::size_t(y * width_ + x)];
    }

    Piece* piece(int index) {
        return index >= 0 && index < int(pieces_.size()) ? &pieces_[std::size_t(index)] : nullptr;
    }

    int findPiece(std::uint32_t hash) const {
        for (std::size_t i = 0; i < pieces_.size(); ++i) {
            if (pieces_[i].hash == hash) {
                return int(i);
            }
        }
        return -1;
    }

    Object* objectOf(const Piece& p) { return game_.scene.object(p.hash); }

    // 0x2ca208: the object's position at once.
    static void place(Object& o, int x, int y) {
        o.position.set({float(x), float(y), o.position[2]});
    }

    void clearSelection();
    bool reset();
    bool undo();
    void grid(int width, int height, int x0, int y0, int size, int minimum);
    std::string add(int x, int y, int tx, int ty, std::uint32_t resource, const Value& animation,
                    const std::string& normal, const std::string& slide);
    bool grab(std::uint32_t hash, int x, int y);
    bool drag(std::uint32_t hash, int x, int y);
    bool release(std::uint32_t hash);
    int freeCells(int dx, int dy, int x, int y);

    // Sixth adventure only (0x276bd0): every piece that can slide some way
    // shows at colour 1, the others at 0.3. Run by the reset (0x11eaec), the
    // undo (0x305a14) and command 51; not after a move.
    void shade() {
        if (game_.edition != Edition::Sixth) {
            return;
        }
        for (const Piece& p : pieces_) {
            const bool moves = freeCells(-1, 0, p.x, p.y) || freeCells(0, -1, p.x, p.y) ||
                               freeCells(1, 0, p.x, p.y) || freeCells(0, 1, p.x, p.y);
            if (Object* o = game_.scene.object(p.hash)) {
                const float v = moves ? 1.0f : 0.3f;
                o->colour.set({v, v, v});
            }
        }
    }

    void shift(int x, int y, int dx, int dy);
    void moveCell(int fromX, int fromY, int toX, int toY);

    Game& game_;
    int width_ = 0, height_ = 0;    // +0x1a0 / +0x1a2
    int originX_ = 0, originY_ = 0; // +0x19c / +0x19e
    int size_ = 0;                  // +0x196: a cell's side in pixels
    int minimum_ = 0;               // +0x198: pieces a line needs to slide
    // Sixth adventure only (50, 0x305990: the list at +0x1c4): holes, cells
    // the free distance does not count (0x26eb48).
    std::vector<std::pair<int, int>> holes_;
    std::vector<Cell> cells_;
    std::vector<Piece> pieces_;
    std::vector<Move> history_;
    // The drag (0x160...): the piece, its cell, the axis (0 undecided, 1
    // the row, 2 the column), the touch where it began, the offset in
    // pixels, the limits (left, up: negative; right, down) and the frames
    // the slide sound has sounded without movement.
    int selected_ = -1, selectedCell_ = -1;
    int axis_ = 0;
    int startX_ = 0, startY_ = 0;
    int offsetX_ = 0, offsetY_ = 0;
    int left_ = 0, up_ = 0, right_ = 0, down_ = 0;
    int quiet_ = 0;
};

constexpr int kSlideSound = 520; // 0x208

std::string LinePuzzle::describe() const {
    std::string out;
    char line[96];
    std::snprintf(line, sizeof line, "grid %d %d origin %d %d size %d minimum %d\n", width_,
                  height_, originX_, originY_, size_, minimum_);
    out += line;
    for (const Piece& p : pieces_) {
        std::snprintf(line, sizeof line, "piece %d %d target %d %d\n", p.x, p.y, p.targetX,
                      p.targetY);
        out += line;
    }
    for (const auto& [x, y] : holes_) {
        std::snprintf(line, sizeof line, "hole %d %d\n", x, y);
        out += line;
    }
    return out;
}

void LinePuzzle::clearSelection() {
    selected_ = selectedCell_ = -1;
    axis_ = 0;
    left_ = up_ = right_ = down_ = 0;
}

// 0x386984: every piece back to its first cell (the history cleared).
bool LinePuzzle::reset() {
    selected_ = selectedCell_ = -1;
    axis_ = 0;
    history_.clear();
    for (auto& c : cells_) {
        c.occupant = -1;
    }
    for (std::size_t i = 0; i < pieces_.size(); ++i) {
        Piece& p = pieces_[i];
        p.x = p.startX;
        p.y = p.startY;
        if (Cell* c = cell(p.x, p.y)) {
            c->occupant = int(i);
            if (Object* o = objectOf(p)) {
                place(*o, c->x, c->y);
            }
        }
    }
    return true;
}

// 0x236164: the newest move taken back; every piece snaps to its cell.
bool LinePuzzle::undo() {
    if (history_.empty()) {
        return false;
    }
    const Move m = history_.front();
    shift(m.x, m.y, -m.dx, -m.dy);
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            Cell* c = cell(x, y);
            if (Piece* p = piece(c->occupant)) {
                if (Object* o = objectOf(*p)) {
                    place(*o, c->x, c->y);
                }
            }
        }
    }
    history_.erase(history_.begin());
    return true;
}

// Command 10: the grid's columns, rows, first cell's position, cell size
// and the pieces a line needs to slide.
void LinePuzzle::grid(int width, int height, int x0, int y0, int size, int minimum) {
    width_ = width;
    height_ = height;
    originX_ = x0;
    originY_ = y0;
    size_ = size;
    minimum_ = minimum;
    cells_.assign(std::size_t(std::max(0, width * height)), Cell{});
    pieces_.clear();
    pieces_.reserve(cells_.size());
    history_.clear();
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            Cell* c = cell(x, y);
            c->x = originX_ + x * size_;
            c->y = originY_ + y * size_;
        }
    }
}

// Command 40: a piece at cell (x, y) that solves the puzzle in cell
// (tx, ty): an object "msic_NN" (screen 1, priority 111, kind 7) at the
// cell, touchable over the cell, with the normal and slide handler blocks
// and the animation set's animation. The result is its name.
std::string LinePuzzle::add(int x, int y, int tx, int ty, std::uint32_t resource,
                            const Value& animation, const std::string& normal,
                            const std::string& slide) {
    Cell* at = cell(x, y);
    if (!at) {
        return {};
    }
    char name[16];
    std::snprintf(name, sizeof name, "msic_%02x", unsigned(pieces_.size()));
    const Value values[] = {Value::ofText(name), Value::ofInt(7), Value::ofInt(size_ / 2)};
    Object& o = game_.createObject(name, 1, 111, 7, values);
    Piece p;
    p.hash = o.hash;
    p.startX = p.x = x;
    p.startY = p.y = y;
    p.targetX = tx;
    p.targetY = ty;
    pieces_.push_back(p);
    at->occupant = int(pieces_.size()) - 1;
    place(o, originX_ + x * size_, originY_ + y * size_);
    // 0x2cff84: a rectangle touch area over the cell.
    const float half = float(size_ / 2);
    o.areas.push_back({2, -half, -half, float(size_), float(size_)});
    // 0x28f450: handler mode 3 (normal, slide, none).
    const std::string handlers[] = {normal, slide, std::string()};
    game_.setHandlers(o, 3, handlers);
    game_.setAnimationSet(o, resource);
    game_.playAnimation(o, animation, 0);
    return name;
}

// 0x27e1f0: how far the line through (x, y) can slide in direction
// (dx, dy) (one of them -1 or 1): nothing unless the line holds at least
// the minimum of pieces, is not full and its pieces are contiguous;
// otherwise the empty cells between its pieces and that end.
int LinePuzzle::freeCells(int dx, int dy, int x, int y) {
    if (dx == 0 && dy == 0) {
        return 0;
    }
    const bool row = dy == 0;
    const int length = row ? width_ : height_;
    auto at = [&](int i) {
        return row ? cell(i, y) : cell(x, i);
    };
    unsigned mask = 0;
    int count = 0;
    for (int i = 0; i < length; ++i) {
        const Cell* c = at(i);
        if (!c) {
            break;
        }
        if (c->occupant >= 0) {
            mask |= 1u << i;
            ++count;
        }
    }
    if (count < minimum_ || count == length) {
        return 0;
    }
    while ((mask & 1) == 0) {
        mask >>= 1;
    }
    for (int run = 1; run < count; ++run) {
        mask >>= 1;
        if ((mask & 1) == 0) {
            return 0;
        }
    }
    const int direction = row ? dx : dy;
    // The sixth adventure (0x26eb48) skips holes: an empty hole adds
    // nothing but does not end the run.
    auto hole = [&](int i) {
        const int hx = row ? i : x, hy = row ? y : i;
        for (const auto& [px, py] : holes_) {
            if (px == hx && py == hy) {
                return true;
            }
        }
        return false;
    };
    int free = 0;
    for (int k = 0; k < length; ++k) {
        const int i = direction < 0 ? k : length - 1 - k;
        const Cell* c = at(i);
        if ((c ? c->occupant : 0) >= 0) {
            return free;
        }
        if (!hole(i)) {
            ++free;
        }
    }
    return free;
}

// 0x27e588: the piece in cell `from` moves to cell `to` and glides to its
// position (0x2ca1c0: five frames, curve 2, easing out).
void LinePuzzle::moveCell(int fromX, int fromY, int toX, int toY) {
    Cell* from = cell(fromX, fromY);
    Piece* p = from ? piece(from->occupant) : nullptr;
    Cell* to = cell(toX, toY);
    if (!p || !to) {
        return;
    }
    if (from != to) {
        to->occupant = from->occupant;
        from->occupant = -1;
        p->x = toX;
        p->y = toY;
    }
    if (Object* o = objectOf(*p)) {
        o->position.start({float(to->x), float(to->y), o->position[2]}, 5, 2);
    }
}

// 0x235468: the column x moves by dy and the row y by dx cells, each
// walked from the end it moves towards; without movement every piece
// glides back to its cell.
void LinePuzzle::shift(int x, int y, int dx, int dy) {
    if (dy > 0) {
        for (int i = height_ - 1; i >= 0; --i) {
            moveCell(x, i, x + dx, i + dy);
        }
    } else if (dy < 0) {
        for (int i = 0; i < height_; ++i) {
            moveCell(x, i, x + dx, i + dy);
        }
    }
    if (dx > 0) {
        for (int i = width_ - 1; i >= 0; --i) {
            moveCell(i, y, i + dx, y + dy);
        }
    } else if (dx < 0) {
        for (int i = 0; i < width_; ++i) {
            moveCell(i, y, i + dx, y + dy);
        }
    }
    if (dx == 0 && dy == 0) {
        for (int j = 0; j < height_; ++j) {
            for (int i = 0; i < width_; ++i) {
                moveCell(i, j, i, j);
            }
        }
    }
}

// Command 41, 0x2362ac: the drag of a piece begins at the touch (x, y)
// when nothing is selected and its lines can slide; the limits become
// pixels.
bool LinePuzzle::grab(std::uint32_t hash, int x, int y) {
    if (selected_ >= 0 || selectedCell_ >= 0) {
        return false;
    }
    const int index = findPiece(hash);
    if (index < 0 || !cell(pieces_[std::size_t(index)].x, pieces_[std::size_t(index)].y)) {
        return false;
    }
    const Piece& p = pieces_[std::size_t(index)];
    startX_ = x;
    startY_ = y;
    selected_ = index;
    selectedCell_ = p.y * width_ + p.x;
    axis_ = 0;
    offsetX_ = offsetY_ = 0;
    left_ = freeCells(-1, 0, p.x, p.y);
    up_ = freeCells(0, -1, p.x, p.y);
    right_ = freeCells(1, 0, p.x, p.y);
    down_ = freeCells(0, 1, p.x, p.y);
    if (left_ == 0 && up_ == 0 && right_ == 0 && down_ == 0) {
        selected_ = selectedCell_ = -1;
        return false;
    }
    left_ *= -size_;
    up_ *= -size_;
    right_ *= size_;
    down_ *= size_;
    return true;
}

// Command 43, 0x236624: the pen at (x, y). The first movement beyond three
// pixels picks the axis (the larger one, if its line can slide); the line
// follows the pen within the limits at once. The slide sound starts when
// the line moves and stops after ten frames standing still.
bool LinePuzzle::drag(std::uint32_t hash, int x, int y) {
    const int index = findPiece(hash);
    if (index < 0) {
        return false;
    }
    const Piece& p = pieces_[std::size_t(index)];
    if (!cell(p.x, p.y)) {
        return false;
    }
    // The movement as halfwords (the game keeps shorts).
    const int dx = std::int16_t(x - startX_), dy = std::int16_t(y - startY_);
    int ox = 0, oy = 0;
    if (axis_ == 1 || (axis_ == 0 && std::abs(dy) <= std::abs(dx))) {
        ox = dx;
    } else {
        oy = dy;
    }
    ox = std::min(std::max(ox, left_), right_);
    oy = std::min(std::max(oy, up_), down_);
    if (axis_ == 0) {
        if (std::abs(ox) <= 3 && std::abs(oy) <= 3) {
            return false;
        }
        if (ox == 0) {
            if (oy == 0) {
                return false;
            }
            if (up_ != 0 || down_ != 0) {
                axis_ = 2;
            }
        } else if (left_ != 0 || right_ != 0) {
            axis_ = 1;
        }
        offsetX_ = ox;
        offsetY_ = oy;
    }
    if (offsetX_ == ox && offsetY_ == oy) {
        // 0x33b570: the effect player still sounding.
        if (game_.effectsSounding(kSlideSound) && ++quiet_ > 10) {
            game_.stopEffect(kSlideSound, 0);
        }
    } else if (!game_.effectsSounding(kSlideSound)) {
        game_.playEffect(kSlideSound);
        quiet_ = 0;
    }
    const int cx = selectedCell_ % width_, cy = selectedCell_ / width_;
    auto follow = [&](Cell* c) {
        Piece* q = c ? piece(c->occupant) : nullptr;
        if (Object* o = q ? objectOf(*q) : nullptr) {
            place(*o, c->x + ox, c->y + oy);
        }
    };
    if (axis_ == 1) {
        for (int i = 0; i < width_; ++i) {
            follow(cell(i, cy));
        }
    } else if (axis_ == 2) {
        for (int i = 0; i < height_; ++i) {
            follow(cell(cx, i));
        }
    }
    offsetX_ = ox;
    offsetY_ = oy;
    return true;
}

// Command 42, 0x2364a4: the line settles in the nearest cells (half a cell
// rounds away) and the move is remembered when it moved.
bool LinePuzzle::release(std::uint32_t hash) {
    const Piece* p = piece(selected_);
    if (!p || p->hash != hash || selectedCell_ < 0 || selectedCell_ >= int(cells_.size())) {
        return false;
    }
    const int cx = selectedCell_ % width_, cy = selectedCell_ / width_;
    auto cells = [&](int offset) {
        const int n = (std::abs(offset) + size_ / 2) / size_;
        return offset < 0 ? -n : n;
    };
    const int dx = cells(offsetX_), dy = cells(offsetY_);
    shift(cx, cy, dx, dy);
    if (dx != 0 || dy != 0) {
        if (history_.size() >= 30) {
            history_.pop_back();
        }
        history_.insert(history_.begin(), Move{dx, dy, selected_, cx, cy});
    }
    clearSelection();
    return true;
}

bool LinePuzzle::command(int number, Call& c, std::size_t first) {
    const std::size_t count = c.count() - first;
    auto integer = [&](std::size_t i) {
        return int(std::int16_t(c.integer(first + i)));
    };
    auto text = [&](std::size_t i) {
        const std::string* t = c.text(first + i);
        return t ? *t : std::string();
    };
    // 0x2d4900: a name operand hashed (a hash passes through).
    auto reference = [&](std::size_t i) {
        const Value& v = c.at(first + i);
        return v.isText() ? xq::hash(v.text) : v.toUint();
    };
    auto result = [&](int v) {
        c.result = Value::ofInt(v);
        return true;
    };
    switch (number) {
    case 0: {
        const bool ok = reset();
        shade();
        return result(ok ? 1 : 0);
    }
    case 1: {
        const bool ok = undo();
        shade();
        return result(ok ? 1 : 0);
    }
    case 10:
        if (count < 6) {
            return false;
        }
        grid(integer(0), integer(1), integer(2), integer(3), int(std::uint16_t(integer(4))),
             int(std::uint16_t(integer(5))));
        return result(1);
    case 11:
    case 21:
        return result(1);
    case 20:
        // The verdict variable "ナゾ判定" becomes 1 (when it exists).
        game_.setVariable(names::verdict, 1);
        return result(1);
    case 22: {
        // 0x16: solved when the occupant of every piece's target cell is
        // that piece.
        bool solved = true;
        for (std::size_t i = 0; i < pieces_.size(); ++i) {
            const Cell* t = cell(pieces_[i].targetX, pieces_[i].targetY);
            const Piece* q = t ? piece(t->occupant) : nullptr;
            if (!q || q->hash != pieces_[i].hash) {
                solved = false;
                break;
            }
        }
        return result(solved ? 1 : 0);
    }
    case 30: {
        // 0x1e: the pieces on the grid.
        int n = 0;
        for (const Piece& p : pieces_) {
            n += cell(p.x, p.y) ? 1 : 0;
        }
        return result(n);
    }
    case 31:
        return result(0);
    case 40: {
        if (count < 6) {
            return false;
        }
        const std::string name = add(integer(0), integer(1), integer(2), integer(3), reference(4),
                                     c.at(first + 5), text(6), text(7));
        if (name.empty()) {
            return false;
        }
        c.result = Value::ofText(name);
        return true;
    }
    case 41:
        if (count == 0 || !grab(reference(0), integer(1), integer(2))) {
            return false;
        }
        return result(1);
    case 42:
        if (count == 0) {
            return false;
        }
        return result(release(reference(0)) ? 1 : 0);
    case 43:
        if (count < 3) {
            return false;
        }
        return result(drag(reference(0), integer(1), integer(2)) ? 1 : 0);
    case 50:
        // Sixth adventure only: a hole at (x, y).
        if (game_.edition != Edition::Sixth || count < 2) {
            return false;
        }
        holes_.push_back({integer(0), integer(1)});
        return result(1);
    case 51:
        // Sixth adventure only (0x3059f4): the pieces shaded again.
        if (game_.edition != Edition::Sixth) {
            return false;
        }
        shade();
        return result(1);
    default:
        return false;
    }
}

} // namespace

std::unique_ptr<SpecialPuzzle> makeLinePuzzle(Game& game) {
    return std::make_unique<LinePuzzle>(game);
}

} // namespace picarat::adventure5
