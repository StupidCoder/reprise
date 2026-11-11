// The first adventure's "Scale" engine (engine 6, ARM9 02046928..02047028,
// balance 0205400c..020552f4, weights 02053d64..0205400c): find the odd
// weight with a pan balance.
//
// How it plays
// ------------
// Numbered weights lie on a shelf; one of them (picked at random) is
// heavier or lighter than the others. The player drags weights onto the
// two pans (six at most each, stacked in rows) and presses the balance's
// button to weigh: the heavier side sinks. Only a few weighings are
// allowed (the counter). A weight dropped elsewhere goes back to the
// nearest free place on the shelf. The answer is given by putting a weight
// in the tray and pressing "Submit". "Restart" shuffles the weights (which
// renumbers them, so the odd one changes) and gives the weighings back.
//
// The second adventure has no balance puzzle; this engine is the first
// adventure's own.
#include "puzzles/a1_engines.hpp"
#include "puzzles/judgment.hpp"
#include <algorithm>

namespace picarat::adventure1 {

namespace {

constexpr int kSlots = 12; // places on the shelf and on each pan
constexpr int kPanTop = 0x48;
constexpr int kTilt = 0x10; // how far a pan moves

class Scale final : public Engine {
public:
    explicit Scale(PuzzleScene& scene) : random_(scene.state().random) {
        // 02046928 and 0205400c.
        SceneKit& kit = scene.kit;
        if (fileExists(kit.assets.root / "bg" / "q11_bg.l2bg")) {
            scene.setEngineBackground("q11_bg"); // 02040ddc
        }
        submit_.setSprite(kit, 0xcf, 0xa0, "?/q11_submit.spr", "gfx");
        submit_.visibleIdle = true;
        restart_.setSprite(kit, 6, 6, "?/restart.spr", "gfx");
        restart_.visibleIdle = true;
        restart_.sound = 10;
        tray_ = kit.load("?/q11_answer.spr", 0xcd, 0x65, 1);
        balance_ = kit.load("scale.spr", 0, 0, 1);
        counter_ = kit.load("scale_count.spr", 0x7b, 0x7a, 1);
        weigh_.setSprite(kit, 99, 0x77, "scale.spr", "button");
        weigh_.sound = 0x9e;
        kit.hold(kLoadFrames); // a loading pause
    }

    void bind(ScriptContext& context, PuzzleScene& scene) override {
        SceneKit& kit = scene.kit;
        // 0x2d (virtual +0x80, 02046c28 -> 020549e0): that many weights.
        context.bind(0x2d, [this, &kit](CommandArguments& a) {
            for (int n = a.integer(); n > 0; --n) {
                add(kit);
            }
        });
        // 0x2e / 0x2f / 0x30 (virtual +0x84 / +0x88 / +0x8c -> 020542e4 /
        // 02054314 / 02054340): a weight at random is lighter, heavier, or
        // either (a coin toss).
        context.bind(0x2e, [this](CommandArguments&) { makeOdd(Weight::Light); });
        context.bind(0x2f, [this](CommandArguments&) { makeOdd(Weight::Heavy); });
        context.bind(0x30, [this](CommandArguments&) {
            makeOdd(gameRandom(random_) % 2 == 1 ? Weight::Light : Weight::Heavy);
        });
        // 0x27 (virtual +0x6c, 02046c90): the weighings allowed.
        context.bind(0x27, [this](CommandArguments& a) { allowed_ = left_ = a.integer(); });
    }

    void draw(PuzzleScene& scene, bool interactive) override {
        // 02046ca8.
        SceneKit& kit = scene.kit;
        scene.drawCommon(interactive);
        if (interactive) {
            submit_.update(kit, scene.touch);
            restart_.update(kit, scene.touch);
        } else {
            submit_.draw(kit);
            restart_.draw(kit);
        }
        kit.draw(tray_);
        balance(scene, interactive); // 020551ac
    }

    void play(PuzzleScene& scene) override {
        // 02046d40.
        SceneKit& kit = scene.kit;
        scene.intro();
        for (;;) {
            const int onPans = count(&Weight::onLeft) + count(&Weight::onRight);
            scene.frame([&] { draw(scene, true); });
            if (submit_.clicked(kit, scene.touch)) {
                if (answer_ != -1) {
                    kit.effect(0xb);
                    if (odd(answer_)) { // 02054374
                        scene.markSolved();
                    } else {
                        scene.markWrong();
                    }
                    break;
                }
                kit.effect(4); // nothing in the tray
            }
            if (onPans != 0 && count(&Weight::onLeft) == 0 && count(&Weight::onRight) == 0 &&
                canTip()) {
                // The pans were emptied while tilted: they level out.
                kit.effect(0x9f);
                for (int i = 0; i < 0x20; ++i) {
                    scene.frame([&] {
                        draw(scene, false);
                        level();
                        drawScale(scene, false); // the step shows on top
                    });
                }
            }
            if (restart_.clicked(kit, scene.touch)) {
                restart(); // 020541d8
            }
            if (scene.hintClicked()) {
                scene.hintScreen();
            }
            if (scene.quitClicked() && scene.quitDialog()) {
                break;
            }
            if (weigh_.clicked(kit, scene.touch) &&
                (count(&Weight::onLeft) != 0 || count(&Weight::onRight) != 0) && left_ > 0) {
                weighOnce(scene);
            }
        }
        scene.fadeOut(); // 02046a98 (destructor)
    }

private:
    static constexpr int kLoadFrames = 10; // measured on puzzle 11

    // A weight (02053d64, 0x18 bytes).
    struct Weight {
        enum Kind { Normal = 0, Heavy = 1, Light = 2 };

        int x = 0, y = 0;              // +4 / +8: top left (on a pan: before its tilt)
        Kind kind = Normal;            // +0xc
        bool onShelf = false;          // +0xd
        int slot = 0;                  // +0xe: its place on the shelf or pan
        bool onLeft = false;           // +0xf
        bool onRight = false;          // +0x10
        int number = 1;                // +0x11: its label (animation)
        bool odd = false;              // +0x12
        bool held = false;             // +0x13
        SceneKit::SpriteObject sprite; // +0
    };

    // 020549e0: a weight at the next place on the shelf.
    void add(SceneKit& kit) {
        if (weights_.size() >= kSlots) {
            return;
        }
        Weight w;
        w.sprite = kit.load("weights.spr", 0, 0, 1);
        w.slot = int(weights_.size());
        w.x = w.slot * 0x10 + 10;
        w.y = 0xa0;
        w.onShelf = true;
        w.number = w.slot + 1;
        shelf_[std::size_t(w.slot)] = true;
        weights_.push_back(std::move(w));
    }

    void makeOdd(Weight::Kind kind) {
        if (weights_.empty()) {
            return;
        }
        Weight& w = weights_[gameRandom(random_) % weights_.size()];
        w.kind = kind;
        w.odd = true;
    }

    int count(bool Weight::* where) const {
        return int(std::count_if(weights_.begin(), weights_.end(),
                                 [&](const Weight& w) { return w.*where; }));
    }

    bool odd(int number) const {
        for (const auto& w : weights_) {
            if (w.number == number && w.odd) {
                return true;
            }
        }
        return false;
    }

    bool any(bool Weight::* where, Weight::Kind kind) const {
        for (const auto& w : weights_) {
            if (w.*where && w.kind == kind) {
                return true;
            }
        }
        return false;
    }

    // 02054540 / 020545fc: which side is heavier. More weights win; with as
    // many on each side, the side with the heavy weight, or the side
    // without the light one, sinks.
    bool heavier(bool Weight::* side, bool Weight::* other) const {
        const int a = count(side), b = count(other);
        if (a != b) {
            return a > b;
        }
        return (!any(side, Weight::Light) && any(other, Weight::Light)) || any(side, Weight::Heavy);
    }

    bool leftHeavier() const { return heavier(&Weight::onLeft, &Weight::onRight); }

    bool rightHeavier() const { return heavier(&Weight::onRight, &Weight::onLeft); }

    // 020546b8: whether the pans would still move to show the result.
    bool canTip() const {
        if (rightHeavier()) {
            return leftTilt_ > -kTilt;
        }
        if (leftHeavier()) {
            return rightTilt_ > -kTilt;
        }
        return leftTilt_ != 0 || rightTilt_ != 0;
    }

    // 0205491c: one step towards the result.
    void tipStep() {
        if (rightHeavier()) {
            if (leftTilt_ >= -0xf) {
                --leftTilt_;
                ++rightTilt_;
            }
        } else if (leftHeavier()) {
            if (rightTilt_ >= -0xf) {
                --rightTilt_;
                ++leftTilt_;
            }
        } else {
            level();
        }
    }

    // 0205499c: one step back to level.
    void level() {
        for (int* t : {&leftTilt_, &rightTilt_}) {
            if (*t < 0) {
                ++*t;
            } else if (*t > 0) {
                --*t;
            }
        }
    }

    // The weigh button: one weighing (the play loop's last part).
    void weighOnce(PuzzleScene& scene) {
        SceneKit& kit = scene.kit;
        --left_;
        kit.hold(10); // 02003d64
        // 02046cfc: the frame, then the scale drawn once more on top.
        auto still = [&] {
            scene.frame([&] {
                draw(scene, false);
                drawScale(scene, false);
            });
        };
        if (canTip()) {
            for (int i = 0; i < 0x20; ++i) {
                if (canTip() && i == 2) {
                    kit.effect(0x9f);
                }
                scene.frame([&] {
                    draw(scene, false);
                    tipStep();
                    drawScale(scene, false); // the step shows on top
                });
            }
        } else if (count(&Weight::onLeft) != 0 && count(&Weight::onRight) != 0) {
            // 02054744: balanced (or already tilted all the way): a wobble.
            auto step = [&](int dl) {
                leftTilt_ += dl;
                rightTilt_ -= dl;
            };
            if (leftTilt_ != 0 || rightTilt_ != 0) {
                const int d = leftTilt_ < 0 ? 1 : -1;
                for (int i = 0; i < 8; ++i) {
                    if (i == 4) {
                        kit.effect(0x9f);
                    }
                    step(d);
                    still();
                }
                for (int i = 0; i < 8; ++i) {
                    step(-d);
                    still();
                }
                return;
            }
            for (int i = 0; i < 8; ++i) {
                if (i == 4) {
                    kit.effect(0x9f);
                }
                if (leftTilt_ > -kTilt) {
                    step(-1);
                }
                still();
            }
            for (int i = 0; i < 0x10; ++i) {
                if (rightTilt_ > -kTilt) {
                    step(1);
                }
                still();
            }
            for (int i = 0; i < 8; ++i) {
                level();
                still();
            }
        }
    }

    // 020541d8: level pans, all weighings, the weights shuffled onto the
    // shelf and numbered in their new order.
    void restart() {
        leftTilt_ = rightTilt_ = 0;
        left_ = allowed_;
        shelf_.fill(false);
        leftPan_.fill(false);
        rightPan_.fill(false);
        const auto n = std::uint32_t(weights_.size());
        for (int i = 0; i < 0x32 && n != 0; ++i) {
            const auto a = gameRandom(random_) % n;
            const auto b = gameRandom(random_) % n;
            std::swap(weights_[a], weights_[b]);
        }
        for (std::size_t i = 0; i < weights_.size(); ++i) {
            Weight& w = weights_[i];
            w.onLeft = w.onRight = false;
            w.x = int(i) * 0x10 + 10;
            w.y = 0xa0;
            w.onShelf = true;
            w.slot = int(i);
            w.number = int(i) + 1;
            shelf_[i] = true;
        }
        answer_ = -1;
    }

    // 02054b3c / 02054c50: the weights on a pan in rows of six, five, ...,
    // each row centred over the one below.
    void stack(SceneKit& kit, bool right) {
        auto& pan = right ? rightPan_ : leftPan_;
        bool Weight::* on = right ? &Weight::onRight : &Weight::onLeft;
        int column = 0, shift = 0, rowX = right ? 0xa8 : 0x30, rowY = kPanTop, width = 6;
        Weight* w = nullptr;
        for (int slot = 0; slot < kSlots; ++slot) {
            if (!pan[std::size_t(slot)]) {
                continue;
            }
            if (column == 0) {
                // The row's weights shift it left by 8 pixels each (the
                // shift adds up over the rows).
                int counted = 0;
                for (std::size_t i = 0; i < weights_.size() && counted < width; ++i) {
                    if (weights_[i].*on) {
                        shift += 8;
                        ++counted;
                    }
                }
            }
            for (auto& candidate : weights_) {
                w = &candidate;
                if (candidate.slot == slot && candidate.*on) {
                    break;
                }
            }
            w->x = rowX + column * 0x10 - shift;
            w->y = rowY - kit.frameHeight(w->sprite);
            if (++column == width) {
                --width;
                column = 0;
                rowY -= kit.frameHeight(w->sprite);
                rowX += kit.frameWidth(w->sprite) >> 1;
            }
        }
    }

    static int firstFree(const std::array<bool, kSlots>& places) {
        for (int i = 0; i < kSlots; ++i) {
            if (!places[std::size_t(i)]) {
                return i;
            }
        }
        return -1;
    }

    // 02054ec0: the free shelf place nearest to x.
    int nearestShelf(int x) const {
        const int at = std::clamp((x - 10) / 0x10, 0, kSlots - 1);
        if (!shelf_[std::size_t(at)]) {
            return at;
        }
        if (at > 0 && !shelf_[std::size_t(at - 1)]) {
            return at - 1;
        }
        if (at < 10 && !shelf_[std::size_t(at + 1)]) {
            return at + 1;
        }
        for (int i = at; i < kSlots; ++i) {
            if (!shelf_[std::size_t(i)]) {
                return i;
            }
        }
        for (int i = at; i >= 0; --i) {
            if (!shelf_[std::size_t(i)]) {
                return i;
            }
        }
        return firstFree(shelf_);
    }

    // 02054d64: a weight let go lands on a pan, in the tray or on the shelf.
    void drop(SceneKit& kit, Weight& w) {
        kit.effect(0x9d);
        auto toPan = [&](bool right) {
            auto& pan = right ? rightPan_ : leftPan_;
            const int slot = firstFree(pan);
            w.x = slot * 0x10 + (right ? 0x80 : 8);
            w.y = kPanTop - kit.frameHeight(w.sprite);
            pan[std::size_t(slot)] = true;
            (right ? w.onRight : w.onLeft) = true;
            w.slot = slot;
            stack(kit, right);
        };
        if (w.x >= 0 && w.x <= 0x60 && w.y >= 0 && w.y <= leftTilt_ + kPanTop &&
            count(&Weight::onLeft) < 6) {
            toPan(false);
        } else if (w.x >= 0x70 && w.x <= 0xe0 && w.y >= 0 && w.y <= rightTilt_ + kPanTop &&
                   count(&Weight::onRight) < 6) {
            toPan(true);
        } else if (w.x > 0xc9 && w.x <= 0x10e && w.y > 0x79 && w.y < 0xbe && answer_ == -1) {
            w.x = 0xdc;
            w.y = 0x84;
            answer_ = w.number;
        } else {
            const int slot = nearestShelf(w.x);
            w.x = slot * 0x10 + 10;
            w.y = 0xa0;
            shelf_[std::size_t(slot)] = true;
            w.onShelf = true;
            w.slot = slot;
        }
    }

    int tilt(const Weight& w) const { return w.onLeft ? leftTilt_ : w.onRight ? rightTilt_ : 0; }

    // 020551ac: picking a weight up (interactive frames), then the scale
    // (whose weights follow the stylus in every frame).
    void balance(PuzzleScene& scene, bool interactive) {
        SceneKit& kit = scene.kit;
        TouchState& t = scene.touch;
        if (interactive) {
            weigh_.update(kit, t);
            if (t.pressed) {
                for (auto& w : weights_) {
                    const int width = kit.frameWidth(w.sprite), height = kit.frameHeight(w.sprite);
                    const int top = w.y + tilt(w);
                    if (!(w.x <= t.pressX && t.pressX <= w.x + width && top <= t.pressY &&
                          t.pressY <= top + height)) {
                        continue;
                    }
                    kit.effect(0x9c);
                    w.held = true;
                    if (w.onShelf) {
                        shelf_[std::size_t(w.slot)] = false;
                        w.onShelf = false;
                    } else if (w.onLeft) {
                        leftPan_[std::size_t(w.slot)] = false;
                        w.onLeft = false;
                        stack(kit, false);
                    } else if (w.onRight) {
                        rightPan_[std::size_t(w.slot)] = false;
                        w.onRight = false;
                        stack(kit, true);
                    } else if (answer_ == w.number) {
                        answer_ = -1;
                    }
                    w.slot = 0;
                    break;
                }
            }
        }
        drawScale(scene, true);
    }

    // 02055004: the beam turned by the left pan's tilt, the pans, their
    // stands, the weighings left and the weights (which, with `follow`,
    // move with the stylus).
    void drawScale(PuzzleScene& scene, bool follow) {
        SceneKit& kit = scene.kit;
        TouchState& t = scene.touch;
        const float beam = leftTilt_ < 0    ? 0.0f - float(leftTilt_)
                           : leftTilt_ == 0 ? 0.0f
                                            : 360.0f - float(leftTilt_);
        kit.select(balance_, "middle");
        balance_.x = 0x6c;
        balance_.y = 0x5f;
        kit.drawRotated(balance_, unsigned(int(65536.0f * beam / 360.0f)) & 0xffff);
        kit.select(balance_, "Plate");
        for (const auto& [x, t2] : {std::pair{4, leftTilt_}, std::pair{0x7c, rightTilt_}}) {
            balance_.x = x;
            balance_.y = t2 + kPanTop;
            kit.draw(balance_);
        }
        kit.select(balance_, "Block");
        for (const int x : {0x28, 0xa0}) {
            balance_.x = x;
            balance_.y = 0x71;
            kit.draw(balance_);
        }
        kit.select(counter_, unsigned(left_ + 1));
        kit.draw(counter_);
        for (auto& w : weights_) {
            // 02053e68: let go, or follow the stylus (2 pixels up-left,
            // kept on the screen), then 02053f78: drawn with its label.
            if (follow && t.released && w.held) {
                w.held = false;
                drop(kit, w);
            }
            if (follow && t.down && w.held && t.steady()) {
                const int x = std::clamp(t.x - 2, 10, 0xf0);
                const int y = std::clamp(t.y - 2, 10, 0xb4);
                w.y = y - (kit.frameHeight(w.sprite) >> 1);
                w.x = x - (kit.frameWidth(w.sprite) >> 1);
            }
            kit.select(w.sprite, unsigned(w.number));
            w.sprite.x = w.x;
            w.sprite.y = w.y + tilt(w);
            kit.draw(w.sprite);
        }
    }

    std::uint32_t& random_;                                     // 020de964
    std::vector<Weight> weights_;                               // +0, count +0x30
    std::array<bool, kSlots> shelf_{}, leftPan_{}, rightPan_{}; // +0x38 / +0x44 / +0x50
    int leftTilt_ = 0, rightTilt_ = 0;                          // +0x68 / +0x6c
    int allowed_ = 2, left_ = 2;                                // +0x74 / +0x70
    int answer_ = -1;                                           // +0x7c: the number in the tray
    SceneKit::SpriteObject balance_, counter_;                  // +0x5c / +0x60
    Button weigh_;                                              // +0x80
    Button submit_, restart_;                                   // +0x78 / +0x7c (engine)
    SceneKit::SpriteObject tray_;                               // +0x80 (engine)
};

} // namespace

std::unique_ptr<Engine> createScale(PuzzleScene& scene) {
    return std::make_unique<Scale>(scene);
}

} // namespace picarat::adventure1
