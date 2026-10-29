// The first adventure's "Trace Button" engine (engine 22, ARM9
// 0204e2f8..0204ee94): circle the answer in the picture.
//
// How it plays
// ------------
// The player draws a loop around one spot of the picture. When the stylus
// is lifted the loop's bounding box is checked against the puzzle's marked
// spots: it must enclose exactly one of them and be at least 10 pixels
// across (its diagonal); then a marker appears on that spot. Otherwise a
// "try again" sign shows until the player draws anew or wipes the picture
// with the clear button. "Submit" answers with the marked spot (right if
// it is the spot flagged correct); without a marked spot it only wipes the
// ink.
//
// The second adventure's circle puzzles (circle_input.*) judge the stroke's
// shape against regions; this engine only takes the bounding box, so only
// the ink drawing (CircleInk::stamp, ARM9 02010378 / 0200fff4) is shared.
#include "puzzles/a1_engines.hpp"
#include "puzzles/circle_ink.hpp"

namespace picarat::adventure1 {

namespace {

constexpr int kNone = -16; // 0xfff0: no position

class TraceButton final : public Engine {
public:
    explicit TraceButton(PuzzleScene& scene) {
        // 0204e2f8.
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
        retry_ = kit.load("?/retry_trace.spr", 0, 0, 1);
        retry_.x = (0x100 - kit.frameWidth(retry_)) >> 1;
        retry_.y = (0xc0 - kit.frameHeight(retry_)) >> 1;
        marker_ = kit.load("point_trace.spr", 0, 0, 1);
        // Loading the picture and the sprites holds the black screen 9
        // frames (measured, puzzle 115; a loading pause).
        kit.hold(9);
    }

    void bind(ScriptContext& context, PuzzleScene&) override {
        // 0xd4 (02016470 -> virtual +0xfc, 0204e674): a spot x, y, a radius
        // (stored, never used) and whether it is the answer; at most 16.
        context.bind(0xd4, [this](CommandArguments& a) {
            Spot spot;
            spot.x = a.integer();
            spot.y = a.integer();
            a.real();
            spot.correct = a.truth();
            if (spots_.size() < 16) {
                spots_.push_back(spot);
            }
        });
        // 0x3f (02012790 -> virtual +0xf4): empty in this engine.
        context.bind(0x3f, [](CommandArguments& a) {
            a.integer();
            a.integer();
        });
        // 0xd0 (020161d8 -> virtual +0xf8): a rectangle, empty in this
        // engine too (puzzles 44 and 134).
        context.bind(0xd0, [](CommandArguments& a) {
            for (int i = 0; i < 4; ++i) {
                a.integer();
            }
        });
    }

    void draw(PuzzleScene& scene, bool interactive) override {
        // 0204e944 (virtual +0x274).
        SceneKit& kit = scene.kit;
        TouchState& touch = scene.touch;
        submit_.update(kit, touch);
        clear_.update(kit, touch);
        scene.drawCommon(interactive);
        if (markerX_ > 2 && markerY_ >= 0 && !failed_) {
            marker_.x = markerX_;
            marker_.y = markerY_;
            kit.draw(marker_);
        }
        if (failed_) {
            kit.draw(retry_);
        }
        if (interactive && !submit_.held(touch) && !clear_.held(touch) &&
            !scene.hintButton().held(touch) && !scene.quitButton().held(touch)) {
            write(scene);
        }
        if (!touch.down && !touch.released) {
            startX_ = startY_ = previousX_ = previousY_ = kNone;
            restart_ = true;
        }
    }

    void play(PuzzleScene& scene) override {
        // 0204ecdc.
        SceneKit& kit = scene.kit;
        scene.intro();
        for (;;) {
            scene.frame([&] {
                draw(scene, true);
                // Nothing stays drawn over the two buttons.
                kit.clearTextRect(submit_.x - 4, submit_.y - 4, submit_.width + 8,
                                  submit_.height + 8);
                kit.clearTextRect(clear_.x, clear_.y, clear_.width, clear_.height);
            });
            if (clear_.clicked(kit, scene.touch)) {
                reset(kit);
            }
            if (scene.hintClicked()) {
                scene.hintScreen();
            }
            if (scene.quitClicked() && scene.quitDialog()) {
                return;
            }
            if (!failed_ && markerX_ > 2 && markerY_ >= 0 && submit_.clicked(kit, scene.touch)) {
                scene.fadeOut(); // 0200fce8(1, 1)
                if (correct_) {
                    scene.markSolved();
                } else {
                    scene.markWrong();
                }
                return;
            }
            if (!failed_ && submit_.clicked(kit, scene.touch, false)) {
                reset(kit);
            }
        }
    }

private:
    struct Spot {
        int x = 0, y = 0;     // +0x7c / +0x80
        bool correct = false; // +0x13c
    };

    // The stylus on the picture (the ink part of 0204e944). The pen draws in
    // the text layer's colour 0xfd (command 0x42).
    void write(PuzzleScene& scene) {
        SceneKit& kit = scene.kit;
        TouchState& touch = scene.touch;
        int x = touch.x;
        const int y = touch.y;
        if (touch.released && !submit_.clicked(kit, touch, false) &&
            !clear_.clicked(kit, touch, false) && !scene.quitButton().clicked(kit, touch, false) &&
            !scene.hintButton().clicked(kit, touch, false)) {
            startX_ = startY_ = previousX_ = previousY_ = kNone;
            judge(kit);
        }
        x = std::min(x, 0xfa);
        const bool valid = x != kNone && y != kNone;
        if (!touch.pressed) {
            // (The original also asks 0200eeb4 here; the buttons asked it
            // first this frame, so it always agrees.)
            if (touch.down && valid && previousX_ != kNone && previousY_ != kNone) {
                if (startX_ != kNone || startY_ != kNone) {
                    restartIfPending(kit);
                    ink(scene, true, x, y);
                } else {
                    startX_ = x;
                    startY_ = y;
                    kit.clearText(SceneKit::Lower);
                    restartIfPending(kit);
                    ink(scene, false, x, y);
                }
                addPoint(x, y);
            }
        } else {
            // A new press: a new loop.
            points_.clear();
            failed_ = false;
            markerX_ = markerY_ = -1;
            previousX_ = previousY_ = kNone;
            kit.clearText(SceneKit::Lower);
            if (valid) {
                startX_ = x;
                startY_ = y;
                ink(scene, false, x, y);
                addPoint(x, y);
            } else {
                startX_ = startY_ = kNone;
            }
        }
        if (!touch.down) {
            restart_ = true;
        }
        if (valid) {
            previousX_ = x;
            previousY_ = y;
        }
    }

    void ink(PuzzleScene& scene, bool line, int x, int y) {
        CircleInkCommand c;
        c.kind = line ? CircleInkCommand::Kind::Line : CircleInkCommand::Kind::Dot;
        c.x0 = line ? previousX_ : x;
        c.y0 = line ? previousY_ : y;
        c.x1 = x;
        c.y1 = y;
        CircleInk::stamp(scene.kit.textLayer(SceneKit::Lower), c,
                         scene.kit.assets.font("fontq.NFTR"), scene.inkColour());
    }

    // 0204e918, run before the first ink after the stylus was up.
    void restartIfPending(SceneKit& kit) {
        if (!restart_) {
            return;
        }
        restart_ = false;
        points_.clear();
        failed_ = false;
        markerX_ = markerY_ = -1;
        kit.clearText(SceneKit::Lower);
    }

    // 0204e6c0: a point of the loop (non-negative, not seen yet, at most 256).
    void addPoint(int x, int y) {
        if (points_.size() > 0xff || x < 0 || y < 0) {
            return;
        }
        for (const auto& [px, py] : points_) {
            if (px == x && py == y) {
                return;
            }
        }
        points_.push_back({x, y});
    }

    // 0204e730: the loop's bounding box must hold exactly one spot and be
    // at least 10 pixels across.
    void judge(SceneKit& kit) {
        correct_ = false;
        if (points_.empty()) {
            markerX_ = markerY_ = 0;
            kit.clearText(SceneKit::Lower);
            failed_ = false;
            return;
        }
        int left = 1000, top = 1000, right = 0, bottom = 0;
        for (const auto& [x, y] : points_) {
            left = std::min(left, x);
            right = std::max(right, x);
            top = std::min(top, y);
            bottom = std::max(bottom, y);
        }
        auto fail = [&] {
            correct_ = false;
            failed_ = true;
            markerX_ = markerY_ = -1;
        };
        if (left < 0 || top < 0) {
            fail();
            return;
        }
        points_.clear();
        if (spots_.empty()) {
            return;
        }
        int inside = 0;
        for (const Spot& s : spots_) {
            if (left <= s.x && top <= s.y && s.x <= right && s.y <= bottom) {
                ++inside;
                if (s.correct) {
                    correct_ = true;
                }
                if (inside == 1) {
                    markerX_ = s.x;
                    markerY_ = s.y - kit.frameHeight(marker_);
                } else {
                    fail();
                }
            }
        }
        if (inside != 1) {
            fail();
        }
        const float dx = float(right - left), dy = float(bottom - top);
        if (std::sqrt(dx * dx + dy * dy) < 10.0f) { // 0200c15c, 020b8494
            fail();
        }
    }

    // The clear button (and "submit" without a marked spot).
    void reset(SceneKit& kit) {
        failed_ = correct_ = false;
        markerX_ = markerY_ = -1;
        points_.clear();
        kit.clearText(SceneKit::Lower);
    }

    std::vector<Spot> spots_;                   // +0x78 count
    std::vector<std::pair<int, int>> points_;   // +0x14c count, +0x150
    bool correct_ = false, failed_ = false;     // +0x950 / +0x951
    int markerX_ = -1, markerY_ = -1;           // +0x954 / +0x958
    bool restart_ = false;                      // +0x74
    int startX_ = kNone, startY_ = kNone;       // 020daac0 +0xc / +0
    int previousX_ = kNone, previousY_ = kNone; // 020daac0 +4 / +8
    Button submit_, clear_;                     // +0x95c / +0x960
    SceneKit::SpriteObject marker_, retry_;     // +0x964 / +0x968
};

} // namespace

std::unique_ptr<Engine> createTraceButton(PuzzleScene& scene) {
    return std::make_unique<TraceButton>(scene);
}

} // namespace picarat::adventure1
