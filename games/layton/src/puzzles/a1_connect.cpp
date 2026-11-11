// The first adventure's "Connect" engine (engine 5, ARM9 020438d4..02043e14,
// board 02060200..020621a4): joining pairs of dots with paths.
//
// How it plays
// ------------
// A grid (19 x 19 cells of 8 pixels) holds walls and four pairs of
// coloured dots. Pressing on (or next to) a dot starts a path of its
// colour, which follows the stylus one cell a frame, horizontally or
// vertically, and backs up over itself when the stylus goes back. A path
// cannot cross walls or other paths, nor run along another colour, nor
// touch itself. Lifting the stylus leaves a loose end, which can be picked
// up again; touching a path cuts it back to that cell. Reaching the other
// dot of the colour joins the pair (the dots light up). When all four
// pairs are joined the puzzle is solved. "Clear" removes every path.
//
// The second adventure carries this engine unused (overlay 35, similar
// code); its puzzles never play it, so this file is the first adventure's.
#include "puzzles/a1_engines.hpp"
#include <algorithm>
#include <cmath>

namespace picarat::adventure1 {

namespace {

// What a cell holds (+8 of the board's columns).
enum Kind { Empty = 0, Wall = 1, Dot = 2 /* 2..5: the dot of colour kind - 1 */, End = 6 };

// Where a path goes from a cell (+0x2008).
enum Way { None = 0, Up = 1, Down = 2, Left = 3, Right = 4 };

class Connect final : public Engine {
public:
    explicit Connect(PuzzleScene& scene) {
        // 020438d4 and 02060200.
        SceneKit& kit = scene.kit;
        if (fileExists(kit.assets.root / "bg" / "connect_bg.l2bg")) {
            scene.setEngineBackground("connect_bg"); // 02040ddc
        }
        pieces_ = kit.load("connect.spr", 0, 0, 1);
        end_ = kit.load("connect.spr", 0, 0, 1);
        kit.select(end_, "end");
        clear_.setSprite(kit, 0xb8, 0x78, "?/clear_btn.spr", "gfx");
        clear_.visibleIdle = true;
        clear_.sound = 10;
        // (Two plain rectangles at (0xb8, 0x48) and (0xb8, 0x60) and a
        // "decide" button at (0xb8, 0x90) are made too, but never updated
        // or drawn, so they never answer.)
        reset();
        kit.hold(kLoadFrames); // a loading pause
    }

    // The result's music load (0203e248 -> 02004cb4) first waits until no
    // sound plays. Puzzles answered with "Submit" still play its sound (0xb,
    // about 150 frames: a 135-frame wait after Match, probed); this one ends
    // without a sound left, so only the load remains (measured on puzzle 10).
    int resultLoadFrames() const override { return 38; }

    void bind(ScriptContext& context, PuzzleScene&) override {
        // 0x28 (virtual +0x70 -> 02060550): a wall rectangle x, y, w, h.
        context.bind(0x28, [this](CommandArguments& a) {
            const int x = a.integer(), y = a.integer(), w = a.integer(), h = a.integer();
            if (x < 0 || y < 0 || x + w - 1 >= kColumns || y + h - 1 >= kRows) {
                return;
            }
            for (int i = x; i < x + w; ++i) {
                for (int j = y; j < y + h; ++j) {
                    at(i, j).kind = Wall;
                }
            }
        });
        // 0x29 (virtual +0x74 -> 020604fc): a dot of colour 0..4 at x, y.
        context.bind(0x29, [this](CommandArguments& a) {
            const int x = a.integer(), y = a.integer(), colour = a.integer();
            if (x < 0 || y < 0 || x >= kColumns || y >= kRows || colour < 0 || colour > 4) {
                return;
            }
            at(x, y).kind = colour + 1;
            at(x, y).colour = colour;
        });
    }

    void draw(PuzzleScene& scene, bool interactive) override {
        // 02043c58.
        SceneKit& kit = scene.kit;
        scene.drawCommon(interactive);
        if (interactive) {
            clear_.update(kit, scene.touch);
            input(scene); // 02061838 (which draws the board once already)
            board(kit);
        } else {
            clear_.draw(kit);
        }
        board(kit); // 02061760
    }

    void play(PuzzleScene& scene) override {
        // 02043c94.
        SceneKit& kit = scene.kit;
        scene.intro();
        for (;;) {
            scene.frame([&] { draw(scene, true); });
            if (scene.hintClicked()) {
                scene.hintScreen();
            }
            if (clear_.clicked(kit, scene.touch)) {
                reset(); // 020605d0
            }
            if (std::all_of(joined_.begin() + 1, joined_.end(), [](bool b) { return b; })) {
                kit.hold(0x14); // 02003d64
                scene.markSolved();
                break;
            }
            if (scene.quitClicked() && scene.quitDialog()) {
                break;
            }
        }
        scene.fadeOut(); // 0200fce8(1, 1)
    }

private:
    static constexpr int kLoadFrames = 10;              // measured on puzzle 10
    static constexpr int kColumns = 0x13, kRows = 0x13; // +0x5010 / +0x5014
    static constexpr int kCell = 8;                     // +0x501c
    static constexpr int kLeft = 0x10, kTop = 0x10;     // +0x5008 / +0x500c

    struct Cell {
        int kind = Empty;   // +8
        int colour = 0;     // +0x1008
        int way = None;     // +0x2008: to the next cell (the head: how it came)
        int shape = 0;      // +0x3008: the piece drawn (02062108)
        bool start = false; // +0x4008: the dot the path began at
    };

    // The original keeps 32 rows a column; rows past the grid read as empty.
    Cell& at(int x, int y) { return cells_[std::size_t(x * 32 + y)]; }

    const Cell& at(int x, int y) const { return cells_[std::size_t(x * 32 + y)]; }

    // 020604b4: a cell's colour, -1 outside (one row past the bottom still
    // reads, as empty).
    int colourAt(int x, int y) const {
        if (x < 0 || x >= kColumns || y < 0 || y > kRows) {
            return -1;
        }
        return at(x, y).colour;
    }

    static bool isDot(int kind) { return kind >= 2 && kind <= 5; }

    // 020605d0: every path and loose end removed.
    void reset() {
        for (int x = 0; x < kColumns; ++x) {
            for (int y = 0; y < kRows; ++y) {
                Cell& c = at(x, y);
                if (c.kind < 1 || c.kind > 5) {
                    c.kind = Empty;
                    c.colour = 0;
                }
            }
        }
        for (auto& c : cells_) {
            c.way = None;
            c.shape = 0;
            c.start = false;
        }
        joined_.fill(false);
        drawing_ = false;
        colour_ = 0;
    }

    // 02060a04: the cell under the stylus (a steady sample), kept on the
    // grid.
    bool touchedCell(TouchState& t, int& x, int& y) const {
        if (!t.steady()) {
            return false;
        }
        x = (std::max(t.x, kLeft) - kLeft - 4) / kCell;
        y = (std::max(t.y, kTop) - kTop - 4) / kCell;
        x = std::clamp(x, 0, kColumns - 1);
        y = std::clamp(y, 0, kRows - 1);
        return true;
    }

    // 02060b1c / 02060ce0: a dot (or a loose end) in the 3 x 3 cells around
    // the stylus. The original measures from the stylus's cell number to
    // each cell's pixel position, so the cell nearest the grid's corner
    // wins among several.
    bool nearby(const TouchState& t, bool loose, int& x, int& y) const {
        if (t.x == -16 || t.y == -16) {
            return false;
        }
        const int px = (t.x - kLeft - 4) >> 3, py = (t.y - kTop - 4) >> 3;
        float best = 1000.0f;
        bool found = false;
        for (int dx = -1; dx < 2; ++dx) {
            for (int dy = -1; dy < 2; ++dy) {
                const int cx = px + dx, cy = py + dy;
                if (cx < 0 || cx > kColumns || cy < 0 || cy > kRows) {
                    continue;
                }
                const int kind = at(cx, cy).kind;
                if (loose ? kind != End : !isDot(kind)) {
                    continue;
                }
                const float fx = float(px - cx * 8), fy = float(py - cy * 8);
                const float distance = float(std::sqrt(double(fx * fx + fy * fy)));
                if (distance < best) {
                    best = distance;
                    x = cx;
                    y = cy;
                    found = true;
                }
            }
        }
        return found;
    }

    // 02060e9c: a press on a dot starts its colour again; on a loose end,
    // the path goes on from there.
    bool begin(TouchState& t) {
        int x, y;
        if (!touchedCell(t, x, y)) {
            return false;
        }
        if (nearby(t, false, x, y)) {
            cursorX_ = x;
            cursorY_ = y;
            colour_ = at(x, y).kind - 1;
            clearColour(colour_); // 020606d8
            at(x, y).start = true;
            return true;
        }
        if (nearby(t, true, x, y)) {
            at(x, y).kind = Empty;
            cursorX_ = x;
            cursorY_ = y;
            colour_ = at(x, y).colour;
            return true;
        }
        return false;
    }

    // 020606d8: a colour's path removed (its dots stay; only the cursor's
    // dot keeps the start mark).
    void clearColour(int colour) {
        joined_[std::size_t(colour)] = false;
        for (int y = 0; y < kRows; ++y) {
            for (int x = 0; x < kColumns; ++x) {
                Cell& c = at(x, y);
                if (c.colour == colour && c.kind != colour + 1) {
                    c = Cell{};
                }
                if (c.kind == colour + 1 && (x != cursorX_ || y != cursorY_)) {
                    c.start = false;
                }
            }
        }
    }

    // 020607b4: touching a path (of a pair not joined) cuts it back to the
    // cell touched and draws on from there.
    void cut(TouchState& t) {
        int x, y;
        if (touchedCell(t, x, y) && at(x, y).colour > 0) {
            if (joined_[std::size_t(at(x, y).colour)]) {
                return;
            }
            cursorX_ = x;
            cursorY_ = y;
            colour_ = at(x, y).colour;
            step(at(x, y).way, x, y);
            while (x >= 0 && y >= 0 && x < kColumns && y < kRows && at(x, y).colour == colour_) {
                const int way = at(x, y).way;
                at(x, y).way = None;
                at(x, y).colour = 0;
                at(x, y).kind = Empty;
                step(way, x, y);
            }
            drawing_ = true;
        }
        // Loose ends of the colour go, except in the cursor's row and
        // column (the original tests both coordinates for a difference).
        for (int i = 0; i < kColumns; ++i) {
            for (int j = 0; j < kRows; ++j) {
                Cell& c = at(i, j);
                if (c.kind == End && c.colour == colour_ && i != cursorX_ && j != cursorY_) {
                    c.kind = Empty;
                }
            }
        }
    }

    static void step(int way, int& x, int& y) {
        x += way == Left ? -1 : way == Right ? 1 : 0;
        y += way == Up ? -1 : way == Down ? 1 : 0;
    }

    // 02061028: whether the path may go on into (x, y).
    bool open(int x, int y) const {
        if (x < 0 || y < 0 || x >= kColumns || y >= kRows) {
            return false;
        }
        const Cell& c = at(x, y);
        if (c.kind == Wall || (c.colour != 0 && c.colour != colour_) ||
            (isDot(c.kind) && colour_ != c.kind - 1)) {
            return false;
        }
        const std::pair<int, int> around[4] = {{x, y - 1}, {x, y + 1}, {x - 1, y}, {x + 1, y}};
        int own = 0;
        bool goal = false; // the pair's other dot is next to it
        for (const auto& [nx, ny] : around) {
            if (colourAt(nx, ny) == colour_) {
                ++own;
                const Cell& n = at(nx, ny);
                if (n.kind == colour_ + 1 && !n.start) {
                    goal = true;
                }
            }
        }
        if (own > 1 && colourAt(x, y) == 0 && !goal) {
            return false; // it would touch its own path
        }
        for (const auto& [nx, ny] : around) {
            const int other = colourAt(nx, ny);
            if (other > 0 && other != colour_) {
                return false; // never along another colour
            }
        }
        return true;
    }

    // 02061dfc / 02061eb0 / 02061ca4 / 02061d50: one cell on; going back
    // over its own path removes the cell left.
    void move(int way) {
        int x = cursorX_, y = cursorY_;
        step(way, x, y);
        Cell& from = at(cursorX_, cursorY_);
        if (at(x, y).colour == colour_) {
            from.way = None;
            from.colour = 0;
        } else {
            from.way = way;
        }
        cursorX_ = x;
        cursorY_ = y;
        at(x, y).way = way;
        at(x, y).colour = colour_;
    }

    // 020619f0: one step toward the stylus, along the longer distance
    // first (the original's distances go wrong when negative: -(a + b)).
    void follow(TouchState& t) {
        int x, y;
        if (touchedCell(t, x, y) && (x != cursorX_ || y != cursorY_)) {
            int dx = x - cursorX_, dy = y - cursorY_;
            if (dx < 0) {
                dx = -(x + cursorX_);
            }
            if (dy < 0) {
                dy = -(y + cursorY_);
            }
            if (dx > dy) {
                if (!across(t)) {
                    upDown(t);
                }
            } else if (!upDown(t)) {
                across(t);
            }
        }
        for (int i = 0; i < kColumns; ++i) {
            for (int j = 0; j < kRows; ++j) {
                Cell& c = at(i, j);
                if (c.kind == End && c.colour == colour_ && i != cursorX_ && j != cursorY_) {
                    c.kind = Empty;
                }
                if (c.kind == End && c.colour == 0) {
                    c.kind = Empty;
                    c.way = None;
                }
            }
        }
    }

    // 02061b4c / 02061bf8.
    bool across(TouchState& t) {
        int x, y;
        if (!touchedCell(t, x, y)) {
            return false;
        }
        if (x < cursorX_ && open(cursorX_ - 1, cursorY_)) {
            move(Left);
            return true;
        }
        if (x > cursorX_ && open(cursorX_ + 1, cursorY_)) {
            move(Right);
            return true;
        }
        return false;
    }

    bool upDown(TouchState& t) {
        int x, y;
        if (!touchedCell(t, x, y)) {
            return false;
        }
        if (y < cursorY_ && open(cursorX_, cursorY_ - 1)) {
            move(Up);
            return true;
        }
        if (y > cursorY_ && open(cursorX_, cursorY_ + 1)) {
            move(Down);
            return true;
        }
        return false;
    }

    // 02060330 / 020603ac: the path from the colour's start dot reaches the
    // other dot (a chime the first time).
    bool joined(SceneKit& kit) {
        for (int x = 0; x < kColumns; ++x) {
            for (int y = 0; y < kRows; ++y) {
                if (at(x, y).kind != colour_ + 1 || !at(x, y).start) {
                    continue;
                }
                int way = at(x, y).way, cx = x, cy = y;
                for (int n = 0; n < 0x6e; ++n) {
                    if (way < Up || way > Right) {
                        return false;
                    }
                    step(way, cx, cy);
                    if (cx < 0 || cy < 0 || cx >= kColumns || cy >= kRows) {
                        return false;
                    }
                    const Cell& c = at(cx, cy);
                    if (c.kind == colour_ + 1 && !c.start) {
                        if (!joined_[std::size_t(colour_)]) {
                            kit.effect(0xa2);
                        }
                        return true;
                    }
                    if (c.colour != colour_) {
                        return false;
                    }
                    way = c.way;
                }
                return false;
            }
        }
        return false;
    }

    // 02061838: idle (02061874) or drawing (02061914).
    void input(PuzzleScene& scene) {
        SceneKit& kit = scene.kit;
        TouchState& t = scene.touch;
        if (!drawing_) {
            if (t.pressed && begin(t)) {
                at(cursorX_, cursorY_).colour = colour_; // 02060ff0
                drawing_ = true;
                return;
            }
            if (t.pressed && kLeft <= t.pressX && t.pressX <= kLeft + kColumns * kCell &&
                kTop <= t.pressY && t.pressY <= kTop + kRows * kCell) {
                cut(t);
            }
            return;
        }
        if (t.released) {
            drawing_ = false;
            Cell& c = at(cursorX_, cursorY_);
            if (c.kind == Empty) {
                c.kind = End; // a loose end
            }
            return;
        }
        int x, y;
        if (!touchedCell(t, x, y)) {
            return;
        }
        follow(t);
        if (joined(kit)) {
            // Joined: the end dot becomes part of the path.
            at(cursorX_, cursorY_).kind = Empty;
            joined_[std::size_t(colour_)] = true;
            drawing_ = false;
        } else {
            joined_[std::size_t(colour_)] = false;
        }
    }

    // 02061f64: the piece for a path cell from its coloured neighbours (of
    // any colour).
    int shapeOf(int x, int y) {
        Cell& c = at(x, y);
        const bool up = y > 0 && at(x, y - 1).colour != 0;
        const bool left = x > 0 && at(x - 1, y).colour != 0;
        const bool down = y < kRows - 1 && at(x, y + 1).colour != 0;
        const bool right = x < kColumns - 1 && at(x + 1, y).colour != 0;
        const int n = int(up) + int(down) + int(left) + int(right);
        if (n >= 2 && c.kind == End) {
            c.kind = Empty;
        } else if (n == 1 && c.kind == End && c.colour != 0) {
            return c.shape;
        }
        if (up && down) {
            return 1; // vertical
        }
        if (left && right) {
            return 2; // horizontal
        }
        if (right && down) {
            return 4;
        }
        if (left && down) {
            return 3;
        }
        if (right && up) {
            return 6;
        }
        if (left && up) {
            return 5;
        }
        return c.shape;
    }

    // 02061760: the pieces (02062108), every cell, then the dots on top.
    void board(SceneKit& kit) {
        for (auto& c : cells_) {
            c.shape = 0;
        }
        for (int x = 0; x < kColumns; ++x) {
            for (int y = 0; y < kRows; ++y) {
                if (at(x, y).way != None) {
                    at(x, y).shape = shapeOf(x, y);
                }
            }
        }
        for (int y = 0; y < kRows; ++y) {
            for (int x = 0; x < kColumns; ++x) {
                cell(kit, x, y);
            }
        }
        for (int y = 0; y < kRows; ++y) {
            for (int x = 0; x < kColumns; ++x) {
                if (isDot(at(x, y).kind)) {
                    cell(kit, x, y);
                }
            }
        }
    }

    // 02061390.
    void cell(SceneKit& kit, int x, int y) {
        const Cell& c = at(x, y);
        const int px = x * kCell + kLeft, py = y * kCell + kTop;
        auto put = [&](SceneKit::SpriteObject& s, int sx, int sy) {
            s.x = sx;
            s.y = sy;
            kit.draw(s);
        };
        if (isDot(c.kind)) {
            static const char* const lit[] = {"a", "b", "c", "d"};
            static const char* const unlit[] = {"ablock", "bblock", "cblock", "dblock"};
            const int colour = c.kind - 1;
            kit.select(pieces_, joined_[std::size_t(colour)] ? lit[colour - 1] : unlit[colour - 1]);
            put(pieces_, px - 4, py - 5);
            return;
        }
        if (c.kind == Wall) {
            return;
        }
        if (c.kind == End && c.shape == 0) {
            put(end_, px, py);
            return;
        }
        static const char* const pieces[] = {nullptr,   "vertical", "horizontal", "upleft",
                                             "upright", "downleft", "downright"};
        if (c.shape >= 1 && c.shape <= 6) {
            kit.select(pieces_, pieces[c.shape]);
            put(pieces_, px, py);
            return;
        }
        if (x == cursorX_ && y == cursorY_ && c.colour != 0) {
            put(end_, px, py); // the pen's tip
        }
    }

    std::array<Cell, 32 * 32> cells_{};   // +8 .. (columns of 32 rows)
    std::array<bool, 5> joined_{};        // +1..+4 by colour
    bool drawing_ = false;                // +0x5028
    int cursorX_ = 0, cursorY_ = 0;       // +0x502c / +0x5030
    int colour_ = 0;                      // +0x5034: the path being drawn
    SceneKit::SpriteObject pieces_, end_; // +0x5020 / +0x5024
    Button clear_;                        // +0x74
};

} // namespace

std::unique_ptr<Engine> createConnect(PuzzleScene& scene) {
    return std::make_unique<Connect>(scene);
}

} // namespace picarat::adventure1
