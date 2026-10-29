// The first adventure's sliding block engine, "Slide Puzzle" and "Slide
// Puzzle 2" (engines 18 and 21, one class: ARM9 0204ab48..0204af44, logic
// 0205ac40..0205c574).
//
// How it plays
// ------------
// Blocks lie on a grid with walls. In the tap mode (most puzzles) tapping a
// block sends it sliding as far as it can go, left or right for a lying
// block and up or down for a standing one, towards the side of the block
// that was tapped; the puzzle is solved once the marked cells are covered
// (a car out of the car park). In the drag mode (command 0xaa 1) the player
// drags blocks a cell at a time in any direction; the puzzle is solved
// once every block with a goal is on it and the required cells hold the
// required blocks. Some walls are gates: only the block with the gate's
// number passes. Moves are counted. "Restart" puts every block back. A
// solved puzzle needs no "Submit"; there is no wrong answer.
//
// The second adventure's sliding puzzles (puzzles/a2_slide_puzzle.*) are a
// different design (pixel collisions, free sliding); this engine matches
// its unused overlay 29 instead, so it is reconstructed here on its own.
#include "puzzles/a1_engines.hpp"

namespace picarat::adventure1 {

namespace {

constexpr int kGrid = 0x40; // the occupancy grid is 64 x 64 cells

class SlideBlocks final : public Engine {
public:
    explicit SlideBlocks(PuzzleScene& scene) : kit_(&scene.kit) {
        // 0204ab48 and 0205ac40.
        SceneKit& kit = scene.kit;
        const std::string picture = "q" + std::to_string(scene.id()) + "_bg";
        if (fileExists(kit.assets.root / "bg" / (picture + ".l2bg"))) {
            scene.setEngineBackground(picture); // 02040ddc
        }
        restart_.setSprite(kit, 6, 6, "?/restart.spr", "gfx");
        restart_.visibleIdle = true;
        restart_.sound = 10;
        counter_ = kit.load("river_count.spr", 0, 0, 1);
        grid_.fill(0);
        // Loading the blocks holds the black screen (a loading pause).
        kit.hold(kLoadFrames);
    }

    // Solved without "Submit", so the result does not wait for its sound
    // (see a1_connect.cpp; measured on puzzle 13).
    int resultLoadFrames() const override { return 38; }

    void bind(ScriptContext& context, PuzzleScene& scene) override {
        SceneKit& kit = scene.kit;
        // Virtual functions +0x138..+0x144, +0xb8, +0xcc, +0xd0, +0x1c
        // (0204ad64..0204ade0) write the logic object.
        context.bind(0xa3, [this](CommandArguments& a) { // grid origin in pixels
            originX_ = a.integer();
            originY_ = a.integer();
        });
        context.bind(0xa4, [this](CommandArguments& a) { // grid size in cells
            width_ = a.integer();
            height_ = a.integer();
        });
        context.bind(0xa5, [this](CommandArguments& a) { cell_ = a.integer(); });
        context.bind(0xaa, [this](CommandArguments& a) { drag_ = a.integer() == 1; });
        // 0xa6 (0205af30 -> 0205baf0): a block at a cell, its kind (0 lying,
        // 1 standing, 2 wall or gate, 3 marked cell, 4 free in the drag
        // mode) and its animation; at most 32.
        context.bind(0xa6, [this, &kit](CommandArguments& a) {
            const int x = a.integer(), y = a.integer(), kind = a.integer();
            const std::string animation = a.string();
            if (blocks_.size() >= 0x20) {
                return;
            }
            Block b;
            b.x = b.homeX = x;
            b.y = b.homeY = y;
            b.kind = kind;
            b.number = int(blocks_.size()) + 1;
            b.speed = drag_ ? 4 : 2;
            b.sprite = kit.load(drag_ ? "slidepuzzle2.spr" : "slidepuzzle.spr", 0, 0, 1);
            kit.select(b.sprite, animation);
            blocks_.push_back(std::move(b));
        });
        // 0x38 (0205b048): the last block's goal cell.
        context.bind(0x38, [this](CommandArguments& a) {
            const int x = a.integer(), y = a.integer();
            if (!blocks_.empty()) {
                blocks_.back().goalX = x;
                blocks_.back().goalY = y;
            }
        });
        // 0xb2 (0205b020): the last block's number (gates let it pass).
        context.bind(0xb2, [this](CommandArguments& a) {
            const int id = a.integer();
            if (!blocks_.empty()) {
                blocks_.back().id = id;
            }
        });
        // 0xb1 (0205afc8): a cell that must hold the block numbered id.
        context.bind(0xb1, [this](CommandArguments& a) {
            const int x = a.integer(), y = a.integer(), id = a.integer();
            if (requirements_.size() < 0x20) {
                requirements_.push_back({x, y, id});
            }
        });
    }

    void draw(PuzzleScene& scene, bool interactive) override {
        // 0204adf0.
        SceneKit& kit = scene.kit;
        scene.drawCommon(interactive);
        if (interactive) {
            restart_.update(kit, scene.touch);
            input(scene);
        } else {
            restart_.draw(kit);
        }
        // 0205b688: the blocks from the rebuilt grid, then the count.
        rebuild();
        for (auto& b : blocks_) {
            if (b.kind == 2 || b.kind == 3) {
                continue;
            }
            b.sprite.x = b.x * cell_ + originX_ + b.offsetX;
            b.sprite.y = b.y * cell_ + originY_ + b.offsetY;
            kit.draw(b.sprite);
        }
        drawCount(kit);
    }

    void play(PuzzleScene& scene) override {
        // 0204ae28.
        SceneKit& kit = scene.kit;
        scene.intro();
        scene.frame([&] { draw(scene, false); });
        fadeIn(kit, Screens::Both); // 0200f7b4(1, 1)
        for (;;) {
            scene.frame([&] { draw(scene, true); });
            if (scene.hintClicked()) {
                scene.hintScreen();
            }
            if (scene.quitClicked() && scene.quitDialog()) {
                scene.fadeOut();
                return;
            }
            if (restart_.clicked(kit, scene.touch)) {
                restart(); // 0205ad80
                scene.stopLoopedEffect();
            }
            if (solved(scene)) { // 0205b2b0
                break;
            }
        }
        kit.hold(0x14); // 02003d64
        scene.markSolved();
        scene.fadeOut(); // 0204ac44 (destructor)
    }

private:
    static constexpr int kLoadFrames = 10; // measured 11 (puzzle 80) and 10 (13)

    struct Block {
        int x = 0, y = 0, homeX = 0, homeY = 0; // +0 / +4, +8 / +0xc: cell
        int goalX = -1, goalY = -1;             // +0x10 / +0x14
        int kind = 0;                           // +0x18
        int id = 0;                             // +0x1c
        int number = 0;                         // +0x28: its mark in the grid (1-based)
        int speed = 2;                          // +0x2c: pixels a frame the slide shows
        int offsetX = 0, offsetY = 0;           // +0x30 / +0x34: still to show of the move
        bool dragging = false, moved = false;   // +0x38 / +0x39
        int lastCellX = 0, lastCellY = 0;       // +0x3c / +0x40
        int dragX = 0, dragY = 0;               // +0x44 / +0x48
        SceneKit::SpriteObject sprite;          // +0x20
    };

    struct Requirement {
        int x, y, id;
    };

    std::uint8_t& at(int x, int y) { return grid_[std::size_t(x * kGrid + y)]; }

    // 0205bc38: the block's size in cells from its frame (at least 1).
    void footprint(SceneKit& kit, const Block& b, int& w, int& h) const {
        w = std::max(1, sdiv(kit.frameWidth(b.sprite), cell_));
        h = std::max(1, sdiv(kit.frameHeight(b.sprite), cell_));
    }

    static int sdiv(int a, int b) { return b == 0 ? a : a / b; }

    // 0205b074 / 0205b140: mark or clear the block's cells (walls and
    // marked cells take one cell).
    void stamp(SceneKit& kit, const Block& b, std::uint8_t value) {
        int w = 1, h = 1;
        if (b.kind != 2 && b.kind != 3) {
            footprint(kit, b, w, h);
        }
        for (int y = b.y; y < b.y + h && y < kGrid; ++y) {
            for (int x = b.x; x < b.x + w && x < kGrid; ++x) {
                if (x >= 0 && x < width_ && y >= 0 && y < height_ && b.number > 0) {
                    at(x, y) = value;
                }
            }
        }
    }

    // 0205b204: the grid from every block but the marked cells.
    void rebuild() {
        grid_.fill(0);
        for (const auto& b : blocks_) {
            if (b.kind != 3) {
                stamp(*kit_, b, std::uint8_t(b.number));
            }
        }
    }

    // 0205ae2c: cells x..x+w, y..y+h free (inside the grid), or holding a
    // gate with the moving block's number.
    bool free(int x, int y, int w, int h, int id) {
        if (x < 0 || y < 0) {
            return false;
        }
        for (int cy = y; cy < y + h && cy < height_; ++cy) {
            for (int cx = x; cx < x + w && cx < width_; ++cx) {
                const int mark = at(cx, cy);
                if (mark == 0) {
                    continue;
                }
                const Block& other = blocks_[std::size_t(mark - 1)];
                if (id == 0 || other.kind != 2 || other.id != id) {
                    return false;
                }
            }
        }
        return x + w <= width_ && y + h <= height_;
    }

    bool insideBoard(int px, int py) const {
        return originX_ <= px && px <= originX_ + width_ * cell_ && originY_ <= py &&
               py <= originY_ + height_ * cell_;
    }

    // 0205b7cc (interactive part).
    void input(PuzzleScene& scene) {
        TouchState& t = scene.touch;
        bool moving = false;
        for (const auto& b : blocks_) {
            moving = moving || b.offsetX != 0 || b.offsetY != 0;
        }
        if (!drag_) {
            // A tap (judged at the release, at the press position) on a
            // block while nothing moves.
            if (t.released && insideBoard(t.pressX, t.pressY) && !moving) {
                const int cx = sdiv(t.pressX - originX_, cell_),
                          cy = sdiv(t.pressY - originY_, cell_);
                if (cx >= 0 && cy >= 0 && cx < width_ && cy < height_) {
                    rebuild();
                    const int mark = at(cx, cy);
                    if (mark != 0 && mark <= int(blocks_.size())) {
                        act(scene, blocks_[std::size_t(mark - 1)]);
                    }
                }
            }
        } else {
            bool none = true;
            for (const auto& b : blocks_) {
                none = none && !b.dragging;
            }
            if (t.down && t.steady() && insideBoard(t.pressX, t.pressY) && none) {
                const int cx = sdiv(t.x - originX_, cell_), cy = sdiv(t.y - originY_, cell_);
                if (cx >= 0 && cy >= 0 && cx < width_ && cy < height_) {
                    rebuild();
                    const int mark = at(cx, cy);
                    if (mark != 0 && mark <= int(blocks_.size())) {
                        Block& b = blocks_[std::size_t(mark - 1)];
                        b.dragging = true;
                        touchCell(t, b.lastCellX, b.lastCellY);
                        b.dragX = t.x;
                        b.dragY = t.y;
                    }
                }
            }
        }
        for (auto& b : blocks_) {
            update(scene, b);
        }
    }

    // 0205b6d4: the cell under the stylus while it is down after a press on
    // the board (the grid is rebuilt as a side effect).
    bool touchCell(const TouchState& t, int& cx, int& cy) {
        if (!t.down || !insideBoard(t.pressX, t.pressY)) {
            return false;
        }
        const int x = sdiv(t.x - originX_, cell_), y = sdiv(t.y - originY_, cell_);
        if (x < 0 || y < 0 || x >= width_ || y >= height_) {
            return false;
        }
        rebuild();
        cx = x;
        cy = y;
        return true;
    }

    // 0205c2e8: what a block does when acted on, by kind.
    void act(PuzzleScene& scene, Block& b) {
        if (b.offsetX != 0 || b.offsetY != 0) {
            return;
        }
        if (scene.touch.pressed) {
            b.moved = false;
        }
        if (b.kind == 0) {
            slide(scene, b, true);
        } else if (b.kind == 1) {
            slide(scene, b, false);
        } else if (b.kind == 4) {
            step(scene, b);
        }
    }

    // 0205bc9c / 0205beb8: slide as far as possible towards the tapped side
    // (the block moves at once; the offset shows the slide).
    void slide(PuzzleScene& scene, Block& b, bool across) {
        SceneKit& kit = scene.kit;
        int w = 1, h = 1;
        footprint(kit, b, w, h);
        stamp(kit, b, 0);
        const int px = b.x * cell_ + originX_, py = b.y * cell_ + originY_;
        const bool back =
            across ? scene.touch.pressX < b.offsetX + px + (kit.frameWidth(b.sprite) >> 1)
                   : scene.touch.pressY < b.offsetY + py + (kit.frameHeight(b.sprite) >> 1);
        const int dx = across ? (back ? -1 : 1) : 0, dy = across ? 0 : (back ? -1 : 1);
        if (free(b.x + dx, b.y + dy, w, h, b.id)) {
            b.moved = true;
            scene.startLoopedEffect(0xb4); // 0200552c
            do {
                b.x += dx;
                b.y += dy;
                b.offsetX -= dx * cell_;
                b.offsetY -= dy * cell_;
            } while (free(b.x + dx, b.y + dy, w, h, b.id));
            return;
        }
        stamp(kit, b, std::uint8_t(b.number));
    }

    // 0205c0d4: a dragged block moves a cell towards the cell under the
    // stylus (columns first) once the stylus has moved a pixel.
    void step(PuzzleScene& scene, Block& b) {
        SceneKit& kit = scene.kit;
        const TouchState& t = scene.touch;
        if (b.offsetX != 0 || b.offsetY != 0) {
            return;
        }
        int w = 1, h = 1;
        footprint(kit, b, w, h);
        int cx = 0, cy = 0;
        if (touchCell(t, cx, cy)) {
            const float dx = float(t.x - b.dragX), dy = float(t.y - b.dragY);
            if (std::sqrt(dx * dx + dy * dy) >= 1.0f) {
                stamp(kit, b, 0);
                if (b.lastCellX == cx && b.lastCellY == cy) {
                    stamp(kit, b, std::uint8_t(b.number));
                    return;
                }
                int sx = 0, sy = 0;
                if (cx < b.lastCellX) {
                    sx = -1;
                } else if (b.lastCellX < cx) {
                    sx = 1;
                } else if (cy < b.lastCellY) {
                    sy = -1;
                } else if (b.lastCellY < cy) {
                    sy = 1;
                }
                b.dragX = t.x;
                b.dragY = t.y;
                if (free(b.x + sx, b.y + sy, w, h, b.id)) {
                    b.moved = true;
                    scene.startLoopedEffect(scene.id() == 0x7c ? 0xbc : 0xb9);
                    b.x += sx;
                    b.y += sy;
                    b.offsetX = -sx * cell_;
                    b.offsetY = -sy * cell_;
                    b.lastCellX = cx;
                    b.lastCellY = cy;
                }
            }
        }
        stamp(kit, b, std::uint8_t(b.number));
    }

    // 0205c3c8: the release counts a move; the slide's offset runs out.
    void update(PuzzleScene& scene, Block& b) {
        if (scene.touch.released) {
            if (b.moved && moves_ < 9999) {
                ++moves_; // 0205ad60
            }
            b.moved = false;
            b.dragging = false;
        }
        if (b.dragging) {
            act(scene, b);
        }
        auto run = [&](int& offset) {
            if (offset < 0) {
                offset = std::min(0, offset + b.speed);
            } else if (offset > 0) {
                offset = std::max(0, offset - b.speed);
            } else {
                return;
            }
            if (b.dragging) {
                act(scene, b);
            }
            // The block has arrived: its looped sound stops.
            if (b.offsetX == 0 && b.offsetY == 0) {
                scene.stopLoopedEffect();
            }
        };
        run(b.offsetX);
        run(b.offsetY);
    }

    // 0205b4d0: the count with river_count.spr, digits right-aligned at
    // (0x89, 7), 8 pixels apart.
    void drawCount(SceneKit& kit) {
        auto digit = [&](int value, int x) {
            kit.select(counter_, unsigned(value + 1));
            counter_.x = x;
            counter_.y = 7;
            kit.draw(counter_);
        };
        if (moves_ > 999) {
            digit(moves_ % 10000 / 1000, 0x71);
        }
        if (moves_ > 99) {
            digit(moves_ % 1000 / 100, 0x79);
        }
        if (moves_ > 9) {
            digit(moves_ % 100 / 10, 0x81);
        }
        digit(moves_ % 10, 0x89);
    }

    // 0205b2b0: solved once nothing moves and the stylus is up.
    bool solved(PuzzleScene& scene) {
        for (const auto& b : blocks_) {
            if (b.offsetX != 0 || b.offsetY != 0) {
                return false;
            }
        }
        if (scene.touch.down) {
            return false;
        }
        rebuild();
        if (!drag_) {
            // Every marked cell covered (each plays 0xb5).
            for (const auto& b : blocks_) {
                if (b.kind == 3 && b.x >= 0 && b.x < width_ && b.y >= 0 && b.y < height_) {
                    if (at(b.x, b.y) == 0) {
                        return false;
                    }
                    scene.kit.effect(0xb5);
                }
            }
            return true;
        }
        for (const auto& b : blocks_) {
            if ((b.goalX != -1 || b.goalY != -1) && (b.x != b.goalX || b.y != b.goalY)) {
                return false;
            }
        }
        for (const auto& r : requirements_) {
            const int mark = at(r.x, r.y) - 1;
            if (mark < 0 || r.id != blocks_[std::size_t(mark)].id) {
                return false;
            }
        }
        const unsigned n = scene.id();
        scene.kit.effect(n == 0x88 || n == 0x8e || n == 0x8f ? 0xba : 0xbb);
        return true;
    }

    // 0205ad80: every block home, the count back to 0.
    void restart() {
        grid_.fill(0);
        for (auto& b : blocks_) {
            b.x = b.homeX;
            b.y = b.homeY;
            b.offsetX = b.offsetY = 0;
            b.dragging = false;
        }
        moves_ = 0;
    }

    SceneKit* kit_;
    std::array<std::uint8_t, kGrid * kGrid> grid_{}; // +0x1000
    int originX_ = 0, originY_ = 0;                  // +0x2000 / +0x2004
    int cell_ = 1;                                   // +0x2008
    int width_ = 0, height_ = 0;                     // +0x200c / +0x2010
    bool drag_ = false;                              // +0x2098
    std::vector<Block> blocks_;                      // +0x2014, count +0x2094
    std::vector<Requirement> requirements_;          // +0x20a0.., count +0x209c
    int moves_ = 0;                                  // +0x2220
    SceneKit::SpriteObject counter_;                 // +0x2224
    Button restart_;                                 // +0x78
};

} // namespace

std::unique_ptr<Engine> createSlideBlocks(PuzzleScene& scene) {
    return std::make_unique<SlideBlocks>(scene);
}

} // namespace picarat::adventure1
