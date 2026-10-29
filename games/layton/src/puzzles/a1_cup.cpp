// The first adventure's "Cup" engine (engine 9, ARM9 02047830..02047cd4,
// logic 020575c8..02057c58, jugs 02057cb0..02058968): pouring water.
//
// How it plays
// ------------
// Two or three jugs of different sizes stand on a shelf; the first is full.
// Tapping a jug lifts it (it hovers, bobbing); tapping another pours from
// the lifted jug into it until one is empty or the other full, with a short
// animation (the jug rises, moves over, tilts, pours, tilts back and
// returns). Tapping the lifted jug again puts it down. Pours are counted.
// The puzzle is solved, without "Submit", once every jug holds its target
// amount. "Restart" fills the first jug again and empties the others.
//
// The second adventure has no pouring puzzle; this engine is the first
// adventure's own.
#include "puzzles/a1_engines.hpp"
#include <cmath>

namespace picarat::adventure1 {

namespace {

constexpr float kPi = 3.14159012f; // 020584d0 (0x40490fd0)

// 0200c1ac: 8-bit components to the 3D engine's 15-bit colour.
std::uint16_t rgb(int r, int g, int b) {
    return std::uint16_t((r >> 3) | (g >> 3) << 5 | (b >> 3) << 10);
}

class Cup final : public Engine {
public:
    explicit Cup(PuzzleScene& scene) {
        // 02047830 and 020575c8.
        SceneKit& kit = scene.kit;
        const std::string picture = "q" + std::to_string(scene.id()) + "_bg";
        if (fileExists(kit.assets.root / "bg" / (picture + ".l2bg"))) {
            scene.setEngineBackground(picture); // 02040ddc
        }
        restart_.setSprite(kit, 6, 6, "?/restart.spr", "gfx");
        restart_.visibleIdle = true;
        restart_.sound = 10;
        prompt_ = kit.load("?/cup_gfx.spr", 0, 0, 1);
        kit.select(prompt_, "touch");
        prompt_.x = 0x43;
        prompt_.y = 0xa5;
        numbers_ = kit.load("cup_numbers.spr", 0, 0, 1);
        kit.hold(kLoadFrames); // a loading pause
    }

    // Solved without "Submit", so the result does not wait for its sound
    // (see a1_connect.cpp; measured on puzzle 14).
    int resultLoadFrames() const override { return 38; }

    void bind(ScriptContext& context, PuzzleScene& scene) override {
        SceneKit& kit = scene.kit;
        // 0xbf (virtual +0xd8, 02057684): the water's colour (default 0x94).
        context.bind(0xbf, [this](CommandArguments& a) {
            const int r = a.integer(), g = a.integer(), b = a.integer();
            water_ = rgb(r, g, b);
        });
        // 0x3a (virtual +0xd4, 02057b2c -> 02057cb0): a jug's sprite, x,
        // bottom y, animation (its size), capacity and target amount (in
        // decilitres); at most 4. The first starts full.
        context.bind(0x3a, [this, &kit](CommandArguments& a) {
            const std::string sprite = a.string();
            const int x = a.integer(), y = a.integer(), animation = a.integer();
            const int capacity = a.integer(), target = a.integer();
            if (jugs_.size() >= 4) {
                return;
            }
            Jug j;
            j.sprite = kit.load(sprite, x, y, unsigned(animation));
            const int h = kit.frameHeight(j.sprite);
            j.x = j.homeX = x;
            j.y = j.homeY = y - (h >> 1) - (h % 2 == 1 ? 1 : 0);
            j.capacity = capacity;
            j.target = target;
            j.amount = j.goal = jugs_.empty() ? capacity : 0;
            jugs_.push_back(std::move(j));
        });
    }

    void draw(PuzzleScene& scene, bool interactive) override {
        // 02047b1c.
        SceneKit& kit = scene.kit;
        scene.drawCommon(interactive);
        angle_ = angle_ + 0.1f;
        const int alpha = int(15.0f * std::sin(angle_));
        restart_.update(kit, scene.touch);
        // 02057c58: the jugs' input, their drawing, the labels, the count.
        if (interactive) {
            for (std::size_t i = 0; i < jugs_.size(); ++i) {
                update(scene, i);
            }
        }
        for (auto& j : jugs_) {
            drawJug(kit, j);
        }
        drawLabels(kit);
        drawCount(kit);
        // The prompt passes alpha int(15 sin a) + 16, but the constructor's
        // blend setting (0209adac on 0x04000050) leaves the 3D layer
        // unblended over the background: it shows opaque (measured); only
        // its disappearance at the bottom of the pulse remains.
        if (allIdle() && !solved()) {
            kit.select(prompt_, "touch");
            if (alpha + 0x10 >= 2) {
                kit.draw(prompt_);
            }
        } else if (oneLifted()) {
            kit.select(prompt_, "float");
            if (alpha + 0x10 >= 2) {
                kit.draw(prompt_);
            }
        }
    }

    void play(PuzzleScene& scene) override {
        // 02047bf0.
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
            if (solved()) { // 02057be0
                kit.hold(0x1e);
                scene.markSolved();
                break;
            }
            if (restart_.clicked(kit, scene.touch)) {
                restart(); // 020576a4
            }
        }
        scene.fadeOut(); // 02047984 (destructor)
    }

private:
    static constexpr int kLoadFrames = 11;

    enum State { Idle = 0, Lifting = 1, Hovering = 2, Lowering = 3, Receiving = 4, Pouring = 5 };

    struct Jug {
        SceneKit::SpriteObject sprite; // +0
        int x = 0, y = 0;              // +4 / +8: centre
        int homeX = 0, homeY = 0;      // +0xc / +0x10
        float tilt = 0;                // +0x14: degrees drawn this frame
        int capacity = 0;              // +0x18
        int state = Idle;              // +0x1c
        int bob = 0;                   // +0x20
        int step = 0;                  // +0x24: of the pour
        int goal = 0;                  // +0x28: amount the pour runs to
        float bobAngle = 0;            // +0x2c
        float pourTilt = 0;            // +0x30
        int partner = -1;              // +0x38
        int amount = 0;                // +0x3c
        int target = 0;                // +0x40
    };

    bool allIdle() const { // 020579e4
        for (const auto& j : jugs_) {
            if (j.state != Idle) {
                return false;
            }
        }
        return true;
    }

    bool oneLifted() const { // 02057a1c: two idle and one hovering
        int idle = 0;
        bool hover = false;
        for (const auto& j : jugs_) {
            idle += j.state == Idle ? 1 : 0;
            hover = hover || j.state == Hovering;
        }
        return idle == 2 && hover;
    }

    bool solved() const { // 02057be0 / 02057ddc
        for (const auto& j : jugs_) {
            if (j.state != Idle || j.amount != j.target) {
                return false;
            }
        }
        return true;
    }

    // 02057f78: released after a press in the jug's frame.
    bool tapped(SceneKit& kit, const TouchState& t, const Jug& j) const {
        const int w = kit.frameWidth(j.sprite), h = kit.frameHeight(j.sprite);
        const int left = j.x - (w >> 1), top = j.y - (h >> 1);
        return t.released && left <= t.pressX && t.pressX <= left + w && top <= t.pressY &&
               t.pressY <= top + h;
    }

    // 02057f1c.
    void enter(Jug& j, int state) {
        if (j.state == Hovering) {
            j.bobAngle = 0;
        }
        j.step = 0;
        if (j.state == Pouring) {
            j.y += int(8.0f * std::sin(j.bobAngle));
        }
        j.state = state;
    }

    // 02057a80: pour from the hovering jug into `to`.
    void pour(std::size_t to) {
        Jug& dest = jugs_[to];
        for (std::size_t i = 0; i < jugs_.size(); ++i) {
            if (jugs_[i].state != Hovering) {
                continue;
            }
            Jug& src = jugs_[i];
            src.partner = int(to);
            const int moved = std::min(dest.capacity - dest.amount, src.amount);
            src.goal = src.amount - moved;
            dest.goal = dest.amount + moved;
            enter(dest, Receiving);
            enter(src, Pouring);
            pours_ = std::min(pours_ + 1, 999);
            return;
        }
    }

    // 020585f0: the amount runs towards the pour's goal, 2 a frame.
    static void run(Jug& j) {
        if (j.amount < j.goal) {
            j.amount = std::min(j.amount + 2, j.goal);
        } else if (j.amount > j.goal) {
            j.amount = std::max(j.amount - 2, j.goal);
        }
    }

    // 02058634.
    void update(PuzzleScene& scene, std::size_t index) {
        SceneKit& kit = scene.kit;
        Jug& j = jugs_[index];
        const bool tap = tapped(kit, scene.touch, j);
        if (j.amount != 0 && tap && j.state == Idle && allIdle()) {
            kit.effect(0xa5);
            enter(j, Lifting);
        } else if (tap && j.state == Idle && !allIdle() && j.amount != j.capacity && oneLifted()) {
            kit.effect(3);
            pour(index);
        }
        if (j.state == Lifting) {
            if (j.homeY - j.y < 0x20) {
                j.y -= 4;
            } else {
                enter(j, Hovering);
            }
        }
        if (tapped(kit, scene.touch, j) && j.state == Hovering) {
            j.y += int(8.0f * std::sin(j.bobAngle));
            enter(j, Lowering);
        }
        if (j.state == Lowering) {
            if (j.y < j.homeY) {
                j.y = std::min(j.y + 8, j.homeY);
            } else {
                kit.effect(0xa6);
                enter(j, Idle);
            }
        }
        // (A rule lowering a jug in state 6 once its amount is reached
        // follows here in the original; no code sets state 6.)
        if (j.state != Pouring) {
            return;
        }
        Jug& other = jugs_[std::size_t(j.partner)];
        switch (j.step) {
        case 0: // up to row 0x3f
            j.y -= 4;
            if (j.y <= 0x3e) {
                j.y = 0x3f;
                ++j.step;
                j.pourTilt = 0;
            }
            break;
        case 1: { // over the other jug (02057e04)
            const int d = int(float(kit.frameHeight(j.sprite)) * 0.75f);
            bool there = false;
            if (j.x + d < other.x) {
                j.x += 4;
                if (other.x <= j.x + d) {
                    j.x = other.x - d;
                    there = true;
                }
            }
            if (!there && other.x < j.x - d) {
                j.x -= 4;
                if (j.x - d <= other.x) {
                    j.x = other.x + d;
                    there = true;
                }
            }
            if (there) {
                ++j.step;
                kit.effect(0xa7);
            }
            break;
        }
        case 2: // tilt (on reaching 90 degrees: 020b8374 is >=)
            j.pourTilt = j.pourTilt + 10.0f;
            if (j.pourTilt >= 90.0f) {
                j.pourTilt = 90.0f;
                ++j.step;
            }
            run(j);
            run(other);
            break;
        case 3: // pour
            run(j);
            run(other);
            if (j.goal == j.amount) {
                ++j.step;
            }
            break;
        case 4: // tilt back
            j.pourTilt = j.pourTilt - 10.0f;
            if (!(j.pourTilt > 0.0f)) {
                j.pourTilt = 0;
                ++j.step;
            }
            break;
        case 5: // home (02057eb0)
            if (j.x < j.homeX) {
                j.x = std::min(j.x + 4, j.homeX);
                if (j.x == j.homeX) {
                    ++j.step;
                }
            } else if (j.homeX < j.x) {
                j.x = std::max(j.x - 4, j.homeX);
                if (j.x == j.homeX) {
                    ++j.step;
                }
            }
            break;
        case 6:
            enter(j, Lowering);
            enter(other, Idle);
            break;
        }
    }

    // 020584e4 with 02057ff4: the water, then the jug, turned while it
    // pours (the hovering jug bobs).
    void drawJug(SceneKit& kit, Jug& j) {
        if (j.state == Hovering) {
            j.bobAngle = j.bobAngle + 0.05f;
            j.bob = int(8.0f * std::sin(j.bobAngle));
            j.pourTilt = 0;
        } else {
            j.bob = 0;
        }
        if (j.state == Pouring) {
            j.tilt =
                j.x < jugs_[std::size_t(j.partner)].x ? 0.0f + j.pourTilt : 360.0f - j.pourTilt;
        }
        j.y += j.bob;
        drawWater(kit, j);
        j.sprite.x = j.x;
        j.sprite.y = j.y;
        kit.drawRotated(j.sprite, unsigned(int(j.tilt * 65536.0f / 360.0f)) & 0xffff);
        j.y -= j.bob;
        j.tilt = 0;
    }

    // 02057ff4: the water as two triangles, the jug's rectangle from its
    // bottom up to amount / capacity of its height, turned with the jug.
    void drawWater(SceneKit& kit, const Jug& j) {
        if (j.amount <= 1) {
            return;
        }
        float a = j.tilt;
        while (a >= 360.0f) {
            a -= 360.0f;
        }
        while (a < 0.0f) {
            a += 360.0f;
        }
        const float rad = a * kPi / 180.0f;
        const int w = kit.frameWidth(j.sprite), h = kit.frameHeight(j.sprite);
        const int left = j.x - (w >> 1), bottom = j.y + (h >> 1);
        const int top = bottom - int(float(j.amount) / float(j.capacity) * float(h)) + 2;
        const float c = std::cos(rad), s = std::sin(rad);
        // The corners turn about the jug's centre as whole-pixel offsets
        // (truncated towards zero), which 0200f250 stores as 4.12 fractions
        // of the screen (x / 256, y / 192, truncated) for the 3D pipeline.
        auto turn = [&](int px, int py) {
            const float dx = float(px - j.x), dy = float(py - j.y);
            const int tx = int(dx * c - dy * s);
            const int ty = int(dx * s + dy * c);
            return std::array<int, 2>{int(float(tx) / 256.0f * 4096.0f),
                                      int(float(ty) / 192.0f * 4096.0f)};
        };
        const auto p0 = turn(left, bottom), p1 = turn(left + w, bottom);
        const auto p2 = turn(left, top), p3 = turn(left + w, top);
        kit.triangle(j.x, j.y, {p0, p1, p2}, water_, SceneKit::Depth::Current);
        kit.triangle(j.x, j.y, {p1, p3, p2}, water_, SceneKit::Depth::Current);
        kit.countDepth(); // the two triangles share one depth step
    }

    // 020576f0: each jug's amount in litres on it, its capacity below.
    void drawLabels(SceneKit& kit) {
        static constexpr int kCapacityX[3] = {33, 105, 177}; // table 020db860
        for (std::size_t i = 0; i < jugs_.size() && i < 3; ++i) {
            const Jug& j = jugs_[i];
            const int litres = j.amount / 10, y = j.y + j.bob;
            if (litres > 9) {
                digit(kit, litres / 10, j.x - 7, y, true);
            }
            digit(kit, litres % 10, j.x, y, true);
        }
        for (std::size_t i = 0; i < jugs_.size() && i < 3; ++i) {
            const int litres = jugs_[i].capacity / 10;
            if (litres > 9) {
                digit(kit, litres / 10, kCapacityX[i], 0x91, false);
            }
            digit(kit, litres % 10, kCapacityX[i] + 7, 0x91, false);
        }
    }

    void digit(SceneKit& kit, int value, int x, int y, bool centred) {
        kit.select(numbers_, unsigned(value + 1));
        numbers_.x = x;
        numbers_.y = y;
        if (centred) {
            kit.drawRotated(numbers_, 0); // 0200cb3c: centred
        } else {
            kit.draw(numbers_);
        }
    }

    // 020578b8: the pours at (0x82, 10), right-aligned, 7 pixels a digit.
    void drawCount(SceneKit& kit) {
        if (pours_ > 99) {
            digit(kit, pours_ % 1000 / 100, 0x74, 10, false);
        }
        if (pours_ > 9) {
            digit(kit, pours_ % 100 / 10, 0x7b, 10, false);
        }
        digit(kit, pours_ % 10, 0x82, 10, false);
    }

    void restart() {
        // 020576a4 -> 02057dac: home, full (the others empty), the count 0.
        for (std::size_t i = 0; i < jugs_.size(); ++i) {
            Jug& j = jugs_[i];
            j.bob = 0;
            j.x = j.homeX;
            j.y = j.homeY;
            j.tilt = 0;
            j.amount = i == 0 ? j.capacity : 0;
            enter(j, Idle);
        }
        pours_ = 0;
    }

    std::vector<Jug> jugs_;          // count +0x10
    int pours_ = 0;                  // +0x18
    std::uint16_t water_ = 0x94;     // +0x1c
    SceneKit::SpriteObject numbers_; // +0x14
    SceneKit::SpriteObject prompt_;  // engine +0x7c
    float angle_ = 0;                // engine +0x80
    Button restart_;                 // engine +0x78
};

} // namespace

std::unique_ptr<Engine> createCup(PuzzleScene& scene) {
    return std::make_unique<Cup>(scene);
}

} // namespace picarat::adventure1
