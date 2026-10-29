// The sixth adventure's piece-board class "Fit" (loop 3140) and the parts
// its subclasses (Partition 3180, Transfer 3170) build on: a board of
// cells, pieces of cells placed on it, and the commands of 0x29dae0.
// Implementation: a6_fit_puzzle.cpp.
#pragma once

#include "puzzles/a5_special.hpp"
#include "puzzles/a6_arrow_pad.hpp"
#include <list>
#include <vector>

namespace picarat::adventure5::fit {

struct Point16 {
    int x = 0, y = 0;
};

// A cell of the board or of a piece (0x18 bytes: +0xc flags, +0x10 four
// byte lanes, +0x14 a second value).
struct Cell {
    enum : std::uint16_t { kOverlap = 0x2000, kFilled = 0x8000 };

    std::uint16_t flags = 0;
    std::uint32_t value = 0;
    std::uint32_t extra = 0;

    bool empty() const { return flags == 0 && value == 0 && extra == 0; }
};

struct Piece {
    enum : std::uint16_t { kOverlap = 0x2000, kOnBoard = 0x4000, kAlive = 0x8000 };

    int id = 0;               // +0x44: pool index | serial << 16
    int w = 0, h = 0;         // +0x34
    std::vector<Cell> cells;  // +0xc
    std::uint16_t flags = 0;  // +0x24: bits 0-2 the turn in eighths
    Point16 animSize;         // +0x38
    Point16 position;         // +0x3c
    std::uint32_t object = 0; // +0x40

    int turn() const { return flags & 7; }

    Cell* cell(int x, int y) {
        return x >= 0 && y >= 0 && x < w && y < h ? &cells[std::size_t(y * w + x)] : nullptr;
    }
};

// The footprint (0x22f4d0): where a piece lies on the board when its
// centre is at a pixel position, and how board cells map to its cells.
struct Footprint {
    Point16 first, last; // board cells, top left and bottom right
    Point16 span;        // the turned size minus one
    Point16 centre;      // the snapped centre (pixels)
    Point16 start;       // the piece cell at the top left
    Point16 rowStep;     // piece cell step per board row
    Point16 columnStep;  // piece cell step per board column
};

class FitPuzzle : public SpecialPuzzle {
public:
    explicit FitPuzzle(Game& game) : game_(game) {}

    bool command(int number, xq::Call& c, std::size_t first) override;
    void input() override;
    std::string describe() const override;

protected:
    // Commands a subclass adds (Partition, Transfer: from 1000); false when
    // the number is not one of them.
    virtual bool ownCommand(int, xq::Call&, std::size_t) { return false; }

    // ---- the board (+0x1a8)
    Cell* boardCell(int x, int y) {
        return x >= 0 && y >= 0 && x < across_ && y < down_ ? &board_[std::size_t(y * across_ + x)]
                                                            : nullptr;
    }

    int pitchX() const { return cellW_ + gapX_; }

    int pitchY() const { return cellH_ + gapY_; }

    int boardW() const { return pitchX() * across_ + gapX_; }

    int boardH() const { return pitchY() * down_ + gapY_; }

    bool insideBoard(int x, int y) const {
        return originX_ <= x && x <= originX_ + boardW() && originY_ <= y &&
               y <= originY_ + boardH();
    }

    // 0x22fb74: the cell under a pixel (floor division by the pitch from
    // the origin plus half a gap); true when the pixel lies on the cell
    // rather than in the gap after it.
    bool cellAt(int px, int py, int& cx, int& cy) const {
        if (pitchX() == 0 || pitchY() == 0) {
            cx = cy = 0;
            return false;
        }
        int rx = std::int16_t(px - (originX_ + (gapX_ >> 1)));
        int ry = std::int16_t(py - (originY_ + (gapY_ >> 1)));
        if (rx < 0) {
            rx -= pitchX();
        }
        if (ry < 0) {
            ry -= pitchY();
        }
        cx = std::int16_t(rx / pitchX());
        cy = std::int16_t(ry / pitchY());
        int mx = rx % pitchX(), my = ry % pitchY();
        if (mx < 0) {
            mx += pitchX();
        }
        if (my < 0) {
            my += pitchY();
        }
        return mx < cellW_ && (gapX_ >> 1) <= mx && my < cellH_ && (gapY_ >> 1) <= my;
    }

    // 0x22fa38: a cell's top left (or centre) pixel; true inside the board.
    bool cellPixel(int cx, int cy, bool centre, int& px, int& py) const {
        px = std::int16_t(pitchX() * cx + originX_ + (gapX_ >> 1));
        py = std::int16_t(pitchY() * cy + originY_ + (gapY_ >> 1));
        if (centre) {
            px += pitchX() >> 1;
            py += pitchY() >> 1;
        }
        return cx >= 0 && cx < across_ && cy >= 0 && cy < down_;
    }

    Footprint footprint(const Piece& p, int px, int py) const;
    void rebuild();

    // ---- pieces (+0x1e0)
    Piece* piece(int id) {
        for (Piece& p : pieces_) {
            if (p.id == id) {
                return &p;
            }
        }
        return nullptr;
    }

    void toTop(int id) {
        for (auto it = pieces_.begin(); it != pieces_.end(); ++it) {
            if (it->id == id) {
                pieces_.splice(pieces_.end(), pieces_, it);
                return;
            }
        }
    }

    Point16 objectPosition(const Piece& p) {
        if (const adventure56::Object* o = game_.scene.object(p.object)) {
            return {int(o->position[0]), int(o->position[1])};
        }
        return p.position;
    }

    Game& game_;
    int across_ = 0, down_ = 0;     // +0x1d8
    int cellW_ = 0, cellH_ = 0;     // +0x1d4
    int gapX_ = 0, gapY_ = 0;       // +0x1dc
    int originX_ = 0, originY_ = 0; // +0x1cc
    std::vector<Cell> board_;       // +0x1b4
    std::list<Piece> pieces_;       // +0x1f0, in drawing order (last on top)
    int poolSize_ = 0;              // +0x1fc
    int created_ = 0;               // +0x200: pool slots used
    int serial_ = 0;                // +0x204
    int lastPiece_ = 0;             // +0x208
    std::uint16_t snap_ = 0;        // +0x230
    Point16 penStart_;              // +0x234
    Point16 lastTouch_;             // the pen's last position (game +0x520 keeps it)
    std::uint32_t status_ = 0;      // +0x238: 1 overlap, 2 a piece off the board
    std::uint32_t flags_ = 0;       // +0x23c

    // ---- board history (+0x21c, commands 41..44): snapshots of the
    // occupied cells, newest last, with three values the script stores.
    struct Snapshot {
        struct Entry {
            int x = 0, y = 0;
            Cell cell;
        };

        std::vector<Entry> cells; // at most 0x400
        int x = 0, y = 0;         // +0xa020
        float value = 0;          // +0xa024
    };

    std::vector<Snapshot> history_;
    ArrowPad arrows_; // the base loop's commands 11000..11010

    void restore(const Snapshot& s);
};

} // namespace picarat::adventure5::fit
