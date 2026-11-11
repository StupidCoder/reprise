// The first adventure's "Queen" engine (engine 10, ARM9 02047cd4..02048220,
// board 02058cbc..02059698, pieces 02058968..02058cbc): queens on a chess
// board.
//
// How it plays
// ------------
// Queens wait in a tray beside the board (some puzzles fix a few on the
// board). The player drags them onto squares; a queen dropped off the board
// or on a taken square returns to the tray. "Submit" checks the board: every
// queen must stand on it with none attacking another, and in the second
// kind of puzzle every square must also be attacked or taken. If the board
// is not right, the attacked queens (and, in the second kind, the attacked
// squares) are shown until the stylus touches the screen again; nothing
// counts as a wrong answer. "Restart" returns the queens to the tray.
#include "puzzles/a1_engines.hpp"
#include <algorithm>

namespace picarat::adventure1 {

namespace {

constexpr int kSquare = 0x14; // pixels a square

class Queen final : public Engine {
public:
    explicit Queen(PuzzleScene& scene) {
        // 02047cd4 and 02058cbc.
        SceneKit& kit = scene.kit;
        const std::string picture = "q" + std::to_string(scene.id()) + "_bg";
        if (fileExists(kit.assets.root / "bg" / (picture + ".l2bg"))) {
            scene.setEngineBackground(picture); // 02040ddc
        }
        restart_.setSprite(kit, 6, 6, "?/restart.spr", "gfx");
        restart_.visibleIdle = true;
        restart_.sound = 10;
        submit_.setSprite(kit, 0xba, 0x9d, "?/submit.spr", "gfx");
        submit_.visibleIdle = true;
        submit_.sound = 0xb;
        tray_ = kit.load("queen_gfx.spr", 0, 0, 1);
        // Loading the sprites holds the black screen (a loading pause).
        kit.hold(kLoadFrames);
    }

    void bind(ScriptContext& context, PuzzleScene& scene) override {
        SceneKit& kit = scene.kit;
        // 0x3b (virtual +0xdc, 02047f78): the board's corner and size; its
        // picture is chess_<size>_bg.
        context.bind(0x3b, [this, &scene](CommandArguments& a) {
            originX_ = a.integer();
            originY_ = a.integer();
            size_ = std::clamp(a.integer(), 0, 8);
            const std::string picture = "chess_" + std::to_string(size_) + "_bg";
            if (fileExists(scene.kit.assets.root / "bg" / (picture + ".l2bg"))) {
                scene.setEngineBackground(picture);
            }
        });
        // 0x3e (virtual +0xe0, 02047fb0): 0 = queens only, 1 = also every
        // square attacked.
        context.bind(0x3e, [this](CommandArguments& a) { coverAll_ = a.integer() == 1; });
        // 0x3c (virtual +0xe4, 02047fbc): that many queens in the tray (at
        // most 8 in all).
        context.bind(0x3c, [this, &kit](CommandArguments& a) {
            for (int n = a.integer(); n > 0; --n) {
                add(kit, 0, 0, Piece::Tray, true);
            }
        });
        // 0x3d (virtual +0xe8, 02058e70): a queen fixed on a square.
        context.bind(0x3d, [this, &kit](CommandArguments& a) {
            const int x = a.integer(), y = a.integer();
            if (add(kit, x * kSquare + originX_, y * kSquare + originY_, Piece::Placed, false)) {
                pieces_.back().column = x;
                pieces_.back().row = y;
                occupied(x, y) = true;
            }
        });
    }

    void draw(PuzzleScene& scene, bool interactive) override {
        // 0204800c.
        SceneKit& kit = scene.kit;
        scene.drawCommon(interactive);
        if (interactive) {
            restart_.update(kit, scene.touch);
        } else {
            restart_.draw(kit);
        }
        board(scene, interactive);
        if (!showing_) {
            submit_.update(kit, scene.touch);
        } else {
            submit_.draw(kit);
        }
    }

    void play(PuzzleScene& scene) override {
        // 02048064.
        SceneKit& kit = scene.kit;
        scene.intro();
        for (;;) {
            scene.frame([&] { draw(scene, true); });
            if (scene.hintClicked()) {
                scene.hintScreen();
            }
            if (scene.quitClicked() && scene.quitDialog()) {
                break;
            }
            if (restart_.clicked(kit, scene.touch)) {
                restart(); // 02058da0
            }
            if (submit_.clicked(kit, scene.touch) && !showing_) {
                kit.hold(0x1e); // 02003d64
                if (check()) {  // 020590ac
                    kit.effect(0xd7);
                    scene.frame([&] { draw(scene, false); });
                    kit.hold(0x1e);
                    scene.markSolved();
                    break;
                }
                scene.frame([&] { draw(scene, false); });
                bool placed = false, attacked = false;
                for (const auto& p : pieces_) {
                    placed = placed || p.state == Piece::Placed;
                    attacked = attacked || isAttacked(p);
                }
                if (placed) {
                    kit.effect(attacked ? 0xd8 : 0xd7);
                }
                kit.hold(10);
                submit_.cancel(); // 02050ee8
            }
        }
        scene.fadeOut(); // 02047e20 (destructor)
    }

private:
    static constexpr int kLoadFrames = 12; // measured on puzzle 16

    struct Piece {
        enum State { Tray, Placed, Dragged };

        int x = 0, y = 0;              // +4 / +8
        State state = Tray;            // +0xc
        int column = 0, row = 0;       // +0x10 / +0x14
        bool movable = true;           // +0x1c
        SceneKit::SpriteObject sprite; // +0x18
    };

    bool& occupied(int x, int y) { return occupied_[std::size_t(x * 8 + y)]; }

    bool& attacked(int x, int y) { return attacked_[std::size_t(x * 8 + y)]; }

    bool add(SceneKit& kit, int x, int y, Piece::State state, bool movable) {
        // 02058e10 / 02058e70: at most 8 queens.
        if (pieces_.size() >= 8) {
            return false;
        }
        Piece p;
        p.x = x;
        p.y = y;
        p.state = state;
        p.movable = movable;
        p.sprite = kit.load("queen_gfx.spr", 0, 0, 1);
        pieces_.push_back(std::move(p));
        return true;
    }

    // 020589ec: a placed queen on an attacked square.
    bool isAttacked(const Piece& p) {
        return p.state == Piece::Placed && attacked(p.column, p.row);
    }

    // 020595e0 with 02059488: the tray and the marks, then the queens.
    void board(PuzzleScene& scene, bool interactive) {
        SceneKit& kit = scene.kit;
        kit.select(tray_, 1u);
        int y = 0x3e;
        for (int i = 0; i < int(pieces_.size()); ++i) {
            const bool odd = i % 2 == 1;
            if (pieces_[std::size_t(i)].state == Piece::Tray) {
                const int x = 0xc5 + (odd ? 0x16 : 0);
                tray_.x = x;
                tray_.y = y;
                pieces_[std::size_t(i)].x = x;
                pieces_[std::size_t(i)].y = y;
                kit.draw(tray_);
            }
            if (i != 0 && odd) {
                y += 0x16;
            }
        }
        if (showing_ && coverAll_) {
            kit.select(tray_, "red");
            for (int c = 0; c < size_; ++c) {
                for (int r = 0; r < size_; ++r) {
                    if (attacked(c, r)) {
                        tray_.x = originX_ + c * kSquare;
                        tray_.y = originY_ + r * kSquare;
                        kit.draw(tray_);
                    }
                }
            }
        }
        if (showing_ && scene.touch.down && interactive) {
            showing_ = false;
            attacked_.fill(false);
        }
        for (auto& p : pieces_) {
            if (interactive && !showing_) {
                update(scene, p);
            }
            drawPiece(kit, p);
        }
    }

    // 02058b80: pick up (sound 0xaf), drop (0xb0), follow the stylus.
    void update(PuzzleScene& scene, Piece& p) {
        const TouchState& t = scene.touch;
        bool anyDragged = false;
        for (const auto& q : pieces_) {
            anyDragged = anyDragged || q.state == Piece::Dragged;
        }
        // 02058a38: the press in the queen's square (its frame's width both
        // ways).
        const int w = scene.kit.frameWidth(p.sprite);
        if (p.movable && !anyDragged && t.pressed && p.x <= t.pressX && t.pressX <= p.x + w &&
            p.y <= t.pressY && t.pressY <= p.y + w) {
            if (p.state == Piece::Placed) {
                occupied(p.column, p.row) = false;
            }
            p.state = Piece::Dragged;
            scene.kit.effect(0xaf);
        }
        if (t.released && p.state == Piece::Dragged) {
            scene.kit.effect(0xb0);
            drop(scene.kit, p); // 020591bc
        }
        if (p.state == Piece::Dragged && t.down) {
            p.x = t.x - 2 - (scene.kit.frameWidth(p.sprite) >> 1);
            p.y = t.y - 2 - (scene.kit.frameHeight(p.sprite) >> 1);
        }
    }

    // 020591bc: onto the square under the queen's centre if it is free,
    // else back to the tray.
    void drop(SceneKit& kit, Piece& p) {
        const int cx = p.x + (kit.frameWidth(p.sprite) >> 1);
        const int cy = p.y + (kit.frameHeight(p.sprite) >> 1);
        const int extent = size_ * kSquare;
        if (!(originX_ <= cx && cx <= originX_ + extent && originY_ <= cy &&
              cy <= originY_ + extent)) {
            p.state = Piece::Tray;
            return;
        }
        int c = (cx - originX_) / kSquare, r = (cy - originY_) / kSquare;
        if (c >= size_) {
            --c;
        }
        if (r >= size_) {
            --r;
        }
        if (occupied(c, r)) {
            p.state = Piece::Tray;
            return;
        }
        occupied(c, r) = true;
        p.state = Piece::Placed;
        p.x = c * kSquare + originX_;
        p.y = r * kSquare + originY_;
        p.column = c;
        p.row = r;
    }

    // 02058aac: animation 2 attacked, 4 fixed or safe while shown, else 1;
    // queens in the tray are drawn with the tray.
    void drawPiece(SceneKit& kit, Piece& p) {
        if (p.state == Piece::Tray) {
            return;
        }
        unsigned animation = 1;
        if (p.state == Piece::Placed && attacked(p.column, p.row)) {
            animation = 2;
        } else if ((p.state == Piece::Placed && showing_) || !p.movable) {
            animation = 4;
        }
        kit.select(p.sprite, animation);
        p.sprite.x = p.x;
        p.sprite.y = p.y;
        kit.draw(p.sprite);
    }

    // 020590ac -> 020590cc / 02058f28 and 0205937c.
    bool check() {
        showing_ = true;
        attacked_.fill(false);
        for (int c = 0; c < size_; ++c) {
            for (int r = 0; r < size_; ++r) {
                if (occupied(c, r)) {
                    attack(c, r);
                }
            }
        }
        int safe = 0;
        for (int c = 0; c < size_; ++c) {
            for (int r = 0; r < size_; ++r) {
                if (occupied(c, r) && !attacked(c, r)) {
                    ++safe;
                } else if (coverAll_ && !attacked(c, r)) {
                    return false;
                }
            }
        }
        return safe == int(pieces_.size());
    }

    // 02058f28: the squares a queen at (c, r) attacks.
    void attack(int c, int r) {
        for (int i = 0; i < size_; ++i) {
            if (i != c) {
                attacked(i, r) = true;
            }
            if (i != r) {
                attacked(c, i) = true;
            }
        }
        for (int d = 1; d < size_; ++d) {
            const int steps[4][2] = {{d, d}, {-d, -d}, {d, -d}, {-d, d}};
            for (const auto& s : steps) {
                const int x = c + s[0], y = r + s[1];
                if (x >= 0 && y >= 0 && x < size_ && y < size_) {
                    attacked(x, y) = true;
                }
            }
        }
    }

    void restart() {
        attacked_.fill(false);
        occupied_.fill(false);
        showing_ = false;
        for (auto& p : pieces_) {
            if (p.movable) {
                p.state = Piece::Tray;
            } else {
                occupied(p.column, p.row) = true;
            }
        }
    }

    int size_ = 0, originX_ = 0, originY_ = 0; // +0 / +4 / +8
    bool coverAll_ = true;                     // +0xc (1 unless the script says 0)
    std::array<bool, 64> attacked_{};          // +0x10
    std::array<bool, 64> occupied_{};          // +0x50
    std::vector<Piece> pieces_;                // +0x90, count +0xb0
    bool showing_ = false;                     // +0xb8
    SceneKit::SpriteObject tray_;              // +0xbc
    Button restart_, submit_;                  // +0x74 / +0x78
};

} // namespace

std::unique_ptr<Engine> createQueen(PuzzleScene& scene) {
    return std::make_unique<Queen>(scene);
}

} // namespace picarat::adventure1
