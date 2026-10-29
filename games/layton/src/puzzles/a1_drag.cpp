// The first adventure's "Drag Puzzle" engine (engine 19, ARM9
// 0204b470..0204b7a8, logic 020621a4..02062608): putting things in place.
//
// How it plays
// ------------
// A few objects (coins, eggs, a ring) lie on the picture; the player drags
// them anywhere. "Submit" answers: right when every target spot of the
// puzzle has an object closer than the puzzle's radius (12 pixels unless
// the script sets one).
//
// The second adventure carries a drag engine unused (overlay 32, partly
// similar code); this file is the first adventure's.
#include "puzzles/a1_engines.hpp"
#include <cmath>

namespace picarat::adventure1 {

namespace {

class DragPuzzle final : public Engine {
public:
    explicit DragPuzzle(PuzzleScene& scene) {
        // 0204b470 and 020621a4.
        SceneKit& kit = scene.kit;
        const std::string picture = "q" + std::to_string(scene.id()) + "_bg";
        if (fileExists(kit.assets.root / "bg" / (picture + ".l2bg"))) {
            scene.setEngineBackground(picture); // 02040ddc
        }
        submit_.setSprite(kit, 0xba, 0x9d, "?/submit.spr", "gfx");
        submit_.visibleIdle = true;
        submit_.sound = 0xb;
        kit.hold(kLoadFrames); // a loading pause
    }

    void bind(ScriptContext& context, PuzzleScene& scene) override {
        SceneKit& kit = scene.kit;
        // 0x6c (virtual +0x1cc -> 02062214): an object at x, y with its
        // sprite and animation.
        context.bind(0x6c, [this, &kit](CommandArguments& a) {
            Object o;
            o.x = a.integer();
            o.y = a.integer();
            const std::string sprite = a.string();
            o.sprite = kit.load(sprite, 0, 0, 1);
            kit.select(o.sprite, a.string());
            objects_.push_back(std::move(o));
        });
        // 0x38 (virtual +0xb8): a target spot.
        context.bind(0x38, [this](CommandArguments& a) {
            const int x = a.integer();
            targets_.push_back({x, a.integer()});
        });
        // 0xf2 (virtual +0xbc, 0204b628): the radius around a target.
        context.bind(0xf2, [this](CommandArguments& a) { radius_ = a.real(); });
    }

    void draw(PuzzleScene& scene, bool interactive) override {
        // 0204b65c.
        SceneKit& kit = scene.kit;
        scene.drawCommon(interactive);
        if (interactive) {
            submit_.update(kit, scene.touch);
            input(scene); // 02062398
        } else {
            submit_.draw(kit);
        }
        // 020625b0: every object centred on its position.
        for (auto& o : objects_) {
            o.sprite.x = o.x;
            o.sprite.y = o.y;
            kit.drawRotated(o.sprite, 0);
        }
    }

    void play(PuzzleScene& scene) override {
        // 0204b698.
        SceneKit& kit = scene.kit;
        scene.intro();
        for (;;) {
            scene.frame([&] { draw(scene, true); });
            if (scene.hintClicked()) {
                scene.hintScreen();
            }
            if (submit_.clicked(kit, scene.touch)) {
                if (judge()) { // 020622c0
                    scene.markSolved();
                } else {
                    scene.markWrong();
                }
                break;
            }
            if (scene.quitClicked() && scene.quitDialog()) {
                break;
            }
        }
        scene.fadeOut(); // 0200fce8(1, 1)
    }

private:
    static constexpr int kLoadFrames = 14; // measured on puzzle 111

    struct Object {
        int x = 0, y = 0;              // +0x44 / +0x84: its middle
        SceneKit::SpriteObject sprite; // +4
    };

    void input(PuzzleScene& scene) {
        SceneKit& kit = scene.kit;
        TouchState& t = scene.touch;
        // Picking and putting down sound different in some puzzles.
        const unsigned id = scene.id();
        const bool soft =
            id == 0x47 || id == 0x7f || id == 0x84 || id == 0xa2 || id == 0x6f || id == 0x66;
        if (t.pressed) {
            // The press began on an object's frame (the first one listed).
            for (std::size_t i = 0; i < objects_.size(); ++i) {
                const Object& o = objects_[i];
                const int w = kit.frameWidth(o.sprite), h = kit.frameHeight(o.sprite);
                const int left = o.x - (w >> 1), top = o.y - (h >> 1);
                if (left <= t.pressX && t.pressX <= left + w && top <= t.pressY &&
                    t.pressY <= top + h) {
                    held_ = i;
                    holding_ = true;
                    kit.effect(soft ? 0xaf : 0x99);
                    break;
                }
            }
        }
        if (t.released) {
            if (holding_) {
                kit.effect(soft ? 0xb0 : 0x97);
            }
            holding_ = false;
            return;
        }
        if (t.down && holding_ && t.steady()) {
            // The object's middle sits 4 pixels up and left of the stylus.
            objects_[held_].x = t.x - 4;
            objects_[held_].y = t.y - 4;
        }
    }

    // 020622c0: every target has an object closer than the radius.
    bool judge() const {
        for (const auto& [tx, ty] : targets_) {
            bool near = false;
            for (const auto& o : objects_) {
                const float dx = float(o.x - tx), dy = float(o.y - ty);
                near = near || float(std::sqrt(double(dx * dx + dy * dy))) < radius_;
            }
            if (!near) {
                return false;
            }
        }
        return true;
    }

    std::vector<Object> objects_;              // count +0
    std::vector<std::pair<int, int>> targets_; // count +2, +0xc4 / +0x104
    float radius_ = 12.0f;                     // +0x158
    std::size_t held_ = 0;                     // +0x15c
    bool holding_ = false;                     // +0x154
    Button submit_;                            // +0x74
};

} // namespace

std::unique_ptr<Engine> createDragPuzzle(PuzzleScene& scene) {
    return std::make_unique<DragPuzzle>(scene);
}

} // namespace picarat::adventure1
