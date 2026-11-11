// The first adventure's "Place Target" engine (engine 13, ARM9
// 02048f6c..02049618): putting markers on the picture.
//
// How it plays
// ------------
// Each tap on the picture puts the next of the puzzle's markers (in turn)
// where the stylus was lifted; a tap outside the picture takes the next
// marker away. "Submit" answers: right when every target spot has a marker
// within its radius.
//
// The second adventure has no counterpart; this engine is the first
// adventure's own.
#include "puzzles/a1_engines.hpp"
#include <algorithm>
#include <cmath>

namespace picarat::adventure1 {

namespace {

class PlaceTarget final : public Engine {
public:
    explicit PlaceTarget(PuzzleScene& scene) {
        // 02048f6c.
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
        // 0x5e (virtual +0x108, 020492e0): a marker: its target spot, its
        // sprite, the spot's radius and a sound (replaced by 0xb, or 0xdc
        // in puzzle 27); at most 8.
        context.bind(0x5e, [this, &kit, &scene](CommandArguments& a) {
            Marker m;
            m.targetX = a.integer();
            m.targetY = a.integer();
            const std::string sprite = a.string();
            m.radius = a.real();
            a.integer();
            if (markers_.size() >= 8) {
                return;
            }
            m.sprite = kit.load(sprite, 0, 0, 1);
            m.sound = scene.id() == 0x1b ? 0xdc : 0xb;
            markers_.push_back(std::move(m));
        });
    }

    void draw(PuzzleScene& scene, bool interactive) override {
        // 020493b4: the markers placed, centred on their spots.
        SceneKit& kit = scene.kit;
        scene.drawCommon(interactive);
        if (interactive) {
            submit_.update(kit, scene.touch);
        } else {
            submit_.draw(kit);
        }
        for (auto& m : markers_) {
            if (m.x > 0 && m.y > 0) {
                m.sprite.x = m.x;
                m.sprite.y = m.y;
                kit.drawRotated(m.sprite, 0);
            }
        }
    }

    void play(PuzzleScene& scene) override {
        // 02049440.
        SceneKit& kit = scene.kit;
        TouchState& t = scene.touch;
        scene.intro();
        for (;;) {
            scene.frame([&] { draw(scene, true); });
            if (scene.hintClicked()) {
                scene.hintScreen();
            }
            if (scene.quitClicked() && scene.quitDialog()) {
                break;
            }
            if (submit_.clicked(kit, t)) {
                if (judge()) { // 020491f4
                    scene.markSolved();
                } else {
                    scene.markWrong();
                }
                break;
            }
            // Where the stylus is (not while it holds "Submit").
            if (t.down && t.steady() && !submit_.held(t)) {
                lastX_ = t.x;
                lastY_ = t.y;
            }
            if (t.released && !markers_.empty()) {
                Marker& m = markers_[next_];
                if (lastX_ < 0xba && lastX_ > 2 && lastY_ < 0xb8 && lastY_ > 0xc) {
                    m.x = lastX_ - 2;
                    m.y = lastY_ - 2;
                    kit.effect(m.sound);
                    next_ = (next_ + 1) % markers_.size();
                } else {
                    m.x = m.y = -100; // taken away
                }
            }
        }
        scene.fadeOut(); // 0204907c (destructor)
    }

private:
    static constexpr int kLoadFrames = 12; // measured on puzzle 27

    struct Marker {
        int targetX = 0, targetY = 0;  // +0x98 / +0xb8
        float radius = 0;              // +0x140
        int x = -100, y = -100;        // +0xd8 / +0xf8
        int sound = 0xb;               // +0x120
        SceneKit::SpriteObject sprite; // +0x78
    };

    // 020491f4: every target spot has a marker within its radius (each
    // marker counts for the first spot it fits).
    bool judge() const {
        std::vector<bool> covered(markers_.size(), false);
        for (const auto& m : markers_) {
            for (std::size_t j = 0; j < markers_.size(); ++j) {
                const float dx = float(m.x - markers_[j].targetX);
                const float dy = float(m.y - markers_[j].targetY);
                if (float(std::sqrt(double(dx * dx + dy * dy))) <= markers_[j].radius) {
                    covered[j] = true;
                    break;
                }
            }
        }
        return std::all_of(covered.begin(), covered.end(), [](bool b) { return b; });
    }

    std::vector<Marker> markers_; // count +0x118
    std::size_t next_ = 0;        // +0x11c
    int lastX_ = 0, lastY_ = 0;   // the play loop's stylus position
    Button submit_;               // +0x74
};

} // namespace

std::unique_ptr<Engine> createPlaceTarget(PuzzleScene& scene) {
    return std::make_unique<PlaceTarget>(scene);
}

} // namespace picarat::adventure1
