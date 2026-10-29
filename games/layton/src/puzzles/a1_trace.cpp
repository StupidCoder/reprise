// The first adventure's "Trace" engine (engine 11, ARM9 02048220..02048a44,
// logic 02059698..02059920): draw around an area.
//
// How it plays
// ------------
// The player draws one line around part of the picture; lifting the stylus
// closes it (invisibly) from its end back to its start. A new line replaces
// the old one, and "clear" wipes it. "Submit" (once a line is drawn) fills
// the area around a seed point inside the line: the puzzle's inside points
// must be on the line or in the area, its outside points neither.
//
// The checks are the second adventure's enclosed-area tracing
// (puzzles/trace_puzzle.*, type 34), whose mask, fill and points this
// engine uses through TracePuzzle; the stylus handling around it is this
// game's own (the same as its Trace Button engine's).
#include "puzzles/a1_engines.hpp"
#include "puzzles/circle_ink.hpp"
#include "puzzles/trace_puzzle.hpp"

namespace picarat::adventure1 {

namespace {

constexpr int kNone = -16;

class Trace final : public Engine {
public:
    explicit Trace(PuzzleScene& scene) {
        // 02048220.
        SceneKit& kit = scene.kit;
        const std::string picture = "q" + std::to_string(scene.id()) + "_bg";
        if (fileExists(kit.assets.root / "bg" / (picture + ".l2bg"))) {
            scene.setEngineBackground(picture); // 02040ddc
        }
        submit_.setSprite(kit, 0xba, 0x9d, "?/submit.spr", "gfx");
        submit_.visibleIdle = true;
        submit_.sound = 0xb;
        clear_.setSprite(kit, 0xc0, 0x2f, "?/clear_btn.spr", "gfx");
        clear_.visibleIdle = true;
        clear_.sound = 10;
        kit.hold(kLoadFrames); // a loading pause
    }

    void bind(ScriptContext& context, PuzzleScene&) override {
        // 0x3f (virtual +0xf4, 02048a20): the fill's seed; 0x40 / 0x41
        // (virtual +0xec / +0xf0 -> 020596d8 / 02059710): inside and
        // outside points.
        context.bind(0x3f, [this](CommandArguments& a) {
            const int x = a.integer();
            area_.setSeed(x, a.integer());
        });
        context.bind(0x40, [this](CommandArguments& a) {
            const int x = a.integer();
            area_.addInside(x, a.integer());
        });
        context.bind(0x41, [this](CommandArguments& a) {
            const int x = a.integer();
            area_.addOutside(x, a.integer());
        });
    }

    void draw(PuzzleScene& scene, bool interactive) override {
        // 0204850c.
        SceneKit& kit = scene.kit;
        submit_.update(kit, scene.touch);
        clear_.update(kit, scene.touch);
        if (interactive && !submit_.held(scene.touch)) {
            write(scene);
        }
        ink(scene);
        scene.drawCommon(interactive);
    }

    void play(PuzzleScene& scene) override {
        // 02048874.
        SceneKit& kit = scene.kit;
        scene.intro();
        for (;;) {
            scene.frame([&] {
                draw(scene, true);
                kit.clearTextRect(submit_.x - 4, submit_.y - 4, submit_.width + 8,
                                  submit_.height + 8);
                kit.clearTextRect(clear_.x, clear_.y, clear_.width, clear_.height);
            });
            if (clear_.clicked(kit, scene.touch)) {
                kit.clearText(SceneKit::Lower);
                area_.clearMask();
                valid_ = true;
                drawn_ = false;
            }
            if (scene.hintClicked()) {
                scene.hintScreen();
                drawn_ = false;
            }
            if (scene.quitClicked()) {
                drawn_ = false;
                if (scene.quitDialog()) {
                    return;
                }
            }
            if (drawn_ && submit_.clicked(kit, scene.touch)) {
                break;
            }
        }
        scene.fadeOut();                     // 0200fce8(1, 1)
        if (valid_ && area_.areaCorrect()) { // 02059880 + 02059760
            scene.markSolved();
        } else {
            scene.markWrong();
        }
    }

private:
    static constexpr int kLoadFrames = 11; // measured on puzzle 21

    // The stylus (the ink part of 0204850c): the pen in the text layer's
    // colour 0xfd, recorded in the mask.
    void write(PuzzleScene& scene) {
        SceneKit& kit = scene.kit;
        TouchState& t = scene.touch;
        int x = t.x;
        const int y = t.y;
        if (t.released && !submit_.clicked(kit, t, false) && !clear_.clicked(kit, t, false) &&
            !scene.quitButton().clicked(kit, t, false) &&
            !scene.hintButton().clicked(kit, t, false)) {
            // Lifted: the line closes from its end to its start, unseen.
            area_.line(previousX_, previousY_, startX_, startY_, true);
            drawn_ = true;
            startX_ = startY_ = previousX_ = previousY_ = kNone;
        }
        x = std::min(x, 0xf0);
        const bool valid = x != kNone && y != kNone;
        auto onScreen = [](int px, int py) { // the rectangle (0, 0, 0x100, 0xc0)
            return px >= 0 && px <= 0x100 && py >= 0 && py <= 0xc0;
        };
        if (!t.pressed) {
            if (t.down && t.steady() && valid && previousX_ != kNone && previousY_ != kNone) {
                if (!onScreen(x, y)) {
                    valid_ = false;
                }
                if (startX_ != kNone || startY_ != kNone) {
                    area_.line(previousX_, previousY_, x, y, false);
                } else {
                    startX_ = x;
                    startY_ = y;
                    kit.clearText(SceneKit::Lower);
                    area_.clearMask();
                    area_.dot(x, y);
                }
                drawn_ = true;
            }
        } else {
            // A new press: a new line.
            previousX_ = previousY_ = kNone;
            kit.clearText(SceneKit::Lower);
            area_.clearMask();
            valid_ = true;
            if (valid) {
                startX_ = x;
                startY_ = y;
                area_.dot(x, y);
                drawn_ = true;
                if (!onScreen(x, y)) {
                    valid_ = false;
                }
            } else {
                startX_ = startY_ = kNone;
            }
        }
        if (valid) {
            previousX_ = x;
            previousY_ = y;
        }
        if (!t.down) {
            startX_ = startY_ = previousX_ = previousY_ = kNone;
        }
    }

    void ink(PuzzleScene& scene) {
        const Font& brush = scene.kit.assets.font("fontq.NFTR");
        for (const auto& c : area_.takeInk()) {
            CircleInk::stamp(scene.kit.textLayer(SceneKit::Lower), c, brush, scene.inkColour());
        }
    }

    TracePuzzle area_;                          // +0x74
    bool valid_ = true;                         // +0x90
    bool drawn_ = false;                        // +0x91
    int startX_ = kNone, startY_ = kNone;       // 020daac0 +0xc / +0 (shared)
    int previousX_ = kNone, previousY_ = kNone; // +8 / +4
    Button submit_, clear_;                     // +0x94 / +0x98
};

} // namespace

std::unique_ptr<Engine> createTrace(PuzzleScene& scene) {
    return std::make_unique<Trace>(scene);
}

} // namespace picarat::adventure1
