// The first adventure's "Coin" engine (engine 4, ARM9 020434e8..020438d4,
// coins 020528e8..02053190): moving coins into a new shape.
//
// How it plays
// ------------
// Coins lie on the table in a shape. The player may move a limited number
// of them (the counter at the bottom) by dragging; a coin costs a move the
// first time it is picked up, and one let go within 16 pixels of where it
// started goes back there and gives its move back. Coins land on a 4-pixel
// grid. With no moves left, pressing a coin not moved yet shows a "no more
// moves" panel. "Submit" checks that every spot of the target shape has a
// coin (of its kind) within 16 pixels; "Restart" puts every coin back.
//
// The match puzzles (a1_match.cpp) share its counter and panel
// (a1_moves.hpp). The second adventure has no counterpart.
#include "puzzles/a1_engines.hpp"
#include "puzzles/a1_moves.hpp"
#include <algorithm>

namespace picarat::adventure1 {

namespace {

class Coin final : public Engine {
public:
    explicit Coin(PuzzleScene& scene) {
        // 020434e8 and 020528e8.
        SceneKit& kit = scene.kit;
        if (fileExists(kit.assets.root / "bg" / "coin_bg.l2bg")) {
            scene.setEngineBackground("coin_bg"); // 02040ddc
        }
        submit_.setSprite(kit, 0x16, 6, "?/mtut_off.spr", "gfx");
        submit_.visibleIdle = true;
        submit_.sound = 0xb;
        restart_.setSprite(kit, 99, 6, "?/mtut_off.spr", "gfx2");
        restart_.visibleIdle = true;
        restart_.sound = 10;
        coin_ = kit.load("coin.spr", 0, 0, 1);
        counter_.load(kit);
        kit.hold(kLoadFrames); // a loading pause
    }

    void bind(ScriptContext& context, PuzzleScene&) override {
        // Positions are rounded down to multiples of 4 (02052a4c /
        // 02052abc). 0x25 / 0x83: a coin (0x83 with its kind); 0x26 / 0x84:
        // a spot of the target shape.
        auto grid = [](int v) {
            return (v + (v < 0 ? 3 : 0)) / 4 * 4;
        };
        auto coin = [this, grid](CommandArguments& a, bool kinded) {
            Piece c;
            c.x = c.homeX = grid(a.integer());
            c.y = c.homeY = grid(a.integer());
            c.kind = kinded ? a.integer() : 0;
            coins_.push_back(c);
            moved_.push_back(false);
        };
        auto spot = [this, grid](CommandArguments& a, bool kinded) {
            Piece t;
            t.x = grid(a.integer());
            t.y = grid(a.integer());
            t.kind = kinded ? a.integer() : 0;
            spots_.push_back(t);
        };
        context.bind(0x25, [coin](CommandArguments& a) { coin(a, false); });
        context.bind(0x83, [coin](CommandArguments& a) { coin(a, true); });
        context.bind(0x26, [spot](CommandArguments& a) { spot(a, false); });
        context.bind(0x84, [spot](CommandArguments& a) { spot(a, true); });
        // 0x27 (virtual +0x6c, 0204374c): the moves allowed.
        context.bind(0x27, [this](CommandArguments& a) { allowed_ = moves_ = a.integer() & 0xff; });
    }

    void draw(PuzzleScene& scene, bool interactive) override {
        // 02043760.
        SceneKit& kit = scene.kit;
        scene.drawCommon(interactive);
        if (interactive) {
            submit_.update(kit, scene.touch);
            restart_.update(kit, scene.touch);
            input(scene); // 02052c08
        } else {
            submit_.draw(kit);
            restart_.draw(kit);
        }
        coins(kit); // 02052f48
    }

    void play(PuzzleScene& scene) override {
        // 020437ac.
        SceneKit& kit = scene.kit;
        scene.intro();
        for (;;) {
            scene.frame([&] { draw(scene, true); });
            if (scene.hintClicked()) {
                scene.hintScreen();
            }
            if (submit_.clicked(kit, scene.touch)) {
                if (judge()) { // 02052b14
                    scene.markSolved();
                } else {
                    scene.markWrong();
                }
                break;
            }
            if (restart_.clicked(kit, scene.touch)) {
                restart(); // 020529dc
            }
            if (scene.quitClicked() && scene.quitDialog()) {
                break;
            }
        }
        scene.fadeOut(); // 0200fce8(1, 1)
        NoMovesPanel::clear(kit);
    }

private:
    static constexpr int kLoadFrames = 5; // measured on puzzle 9
    static constexpr float kNear = 16.0f; // 02052c04 / 02052f44

    struct Piece {
        int x = 0, y = 0;         // coins: +8 / +0x48 (top left); spots: +0x148 / +0x188
        int homeX = 0, homeY = 0; // +0xc8 / +0x108
        int kind = 0;             // +0x88 / +0x1c8
    };

    static float distance(int x0, int y0, int x1, int y1) { // 0200c15c
        const float dx = float(x0 - x1), dy = float(y0 - y1);
        return float(std::sqrt(double(dx * dx + dy * dy)));
    }

    // 020529dc.
    void restart() {
        for (auto& c : coins_) {
            c.x = c.homeX;
            c.y = c.homeY;
        }
        moves_ = allowed_;
        counter_.stop();
        std::fill(moved_.begin(), moved_.end(), false);
        refused_ = false;
        holding_ = false;
        held_ = 0;
    }

    // 02052c08.
    void input(PuzzleScene& scene) {
        SceneKit& kit = scene.kit;
        TouchState& t = scene.touch;
        const int movesBefore = moves_;
        panel_.update(kit, refused_);
        if (t.pressed) {
            for (std::size_t i = 0; i < coins_.size(); ++i) {
                const Piece& c = coins_[i];
                if (!(c.x <= t.pressX && t.pressX <= c.x + 0x10 && c.y <= t.pressY &&
                      t.pressY <= c.y + 0x10)) {
                    continue;
                }
                if (moves_ != 0 || moved_[i]) {
                    if (!moved_[i]) {
                        moved_[i] = true;
                        --moves_;
                    }
                    held_ = i;
                    holding_ = true;
                    kit.effect(0x99);
                    break;
                }
                refused_ = true; // and the search goes on
            }
        }
        if (t.released) {
            refused_ = false;
            if (holding_) {
                kit.effect(0x97);
                Piece& c = coins_[held_];
                if (distance(c.x, c.y, c.homeX, c.homeY) < kNear) {
                    c.x = c.homeX;
                    c.y = c.homeY;
                    ++moves_;
                    moved_[held_] = false;
                } else {
                    c.x = c.x / 4 * 4;
                    c.y = c.y / 4 * 4;
                }
            }
            holding_ = false;
        } else if (t.down && holding_ && t.steady()) {
            // The coin's middle follows the stylus, kept on the table.
            coins_[held_].x = std::clamp(t.x, 0xc, 0xf4) - 8;
            coins_[held_].y = std::clamp(t.y, 0x30, 0x9f) - 8;
        }
        if (movesBefore != moves_) {
            counter_.hop();
        }
    }

    // 02052f48: shadows under the coins and where moved coins started,
    // the coins, then the moves left.
    void coins(SceneKit& kit) {
        auto put = [&](int x, int y) {
            coin_.x = x;
            coin_.y = y;
            kit.draw(coin_);
        };
        for (const auto& c : coins_) {
            kit.select(coin_, "Shadow");
            put(c.x + 1, c.y + 1);
        }
        for (const auto& c : coins_) {
            kit.select(coin_, "Shadow");
            if (c.homeX != c.x || c.homeY != c.y) {
                put(c.homeX + 1, c.homeY + 1);
            }
        }
        for (const auto& c : coins_) {
            kit.select(coin_, unsigned(c.kind + 2));
            put(c.x, c.y);
        }
        counter_.draw(kit, moves_);
    }

    // 02052b14: every spot has a coin of its kind within 16 pixels.
    bool judge() const {
        std::vector<bool> covered(spots_.size(), false);
        for (const auto& c : coins_) {
            for (std::size_t j = 0; j < spots_.size(); ++j) {
                if (distance(c.x, c.y, spots_[j].x, spots_[j].y) < kNear &&
                    c.kind == spots_[j].kind) {
                    covered[j] = true;
                }
            }
        }
        return std::all_of(covered.begin(), covered.end(), [](bool b) { return b; });
    }

    std::vector<Piece> coins_, spots_; // counts +0 / +2
    std::vector<bool> moved_;          // +0x218
    int allowed_ = 0, moves_ = 0;      // +0x22a / +0x229
    std::size_t held_ = 0;             // +0x232
    bool holding_ = false;             // +0x228
    bool refused_ = false;             // +0x231
    SceneKit::SpriteObject coin_;      // +4
    MovesCounter counter_;             // +0x22c, +0x230..+0x238
    NoMovesPanel panel_;
    Button submit_, restart_; // +0x74 / +0x78
};

} // namespace

std::unique_ptr<Engine> createCoin(PuzzleScene& scene) {
    return std::make_unique<Coin>(scene);
}

} // namespace picarat::adventure1
