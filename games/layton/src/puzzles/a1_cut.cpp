// The first adventure's "Cut Puzzle" engine (engine 17, ARM9
// 0204af44..0204b470, logic 0205c574..0205ed9c): lines between points.
//
// How it plays
// ------------
// The picture has marked points. Depending on the puzzle the player either
// draws paths from point to point (the path follows the stylus and grows a
// point at a time) or places straight lines between two points (press on a
// point, drag to another, release). Lines drawn again, or tapped briefly,
// disappear; "clear" erases everything. "Submit" (once something is drawn)
// checks the drawing against the answer segments. Puzzle 86 also has four
// colour buttons for the lines.
//
// The rules are the second adventure's line puzzles (puzzles/line_puzzle.*,
// which describes them), one version earlier: LinePuzzle runs them with
// Rules::Adventure1 and the options this game uses. This file only adds the
// frame around it: the two buttons, the colour picker, the script commands
// and the drawing.
#include "puzzles/a1_engines.hpp"
#include "puzzles/line_puzzle.hpp"

namespace picarat::adventure1 {

namespace {

// 0200c1ac: 8-bit components to the 3D engine's 15-bit colour.
std::uint16_t rgb(int r, int g, int b) {
    return std::uint16_t((r >> 3) | (g >> 3) << 5 | (b >> 3) << 10);
}

class CutPuzzle final : public Engine {
public:
    explicit CutPuzzle(PuzzleScene& scene) : logic_(LinePuzzle::Rules::Adventure1) {
        // 0204af44.
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
        // 0205c574: the logic's sprites and the picker's four rectangles
        // (table 020bc27c).
        point_ = kit.load("line_point.spr", 0, 0, 1);
        here_ = kit.load("line_here.spr", 0, 0, 1);
        noMoreLines_ = kit.load("?/nomorelines.spr", 0, 0, 1);
        logic_.setNoMoreLinesSize(kit.frameWidth(noMoreLines_), kit.frameHeight(noMoreLines_));
        picker_ = kit.load("q86_pal.spr", 0, 0, 1);
        junction_ = kit.load("cut_out.spr", 0, 0, 1);
        static constexpr int kColourButtons[4][2] = {{200, 96}, {224, 96}, {200, 120}, {224, 120}};
        for (int i = 0; i < 4; ++i) {
            colours_[std::size_t(i)].setRect(kColourButtons[i][0], kColourButtons[i][1], 0x10,
                                             0x10);
            buttonX_[std::size_t(i)] = kColourButtons[i][0];
            buttonY_[std::size_t(i)] = kColourButtons[i][1];
        }
        // Loading the picture and the sprites holds the black screen about
        // 11 frames (measured 9 in puzzle 76 and 13 in puzzle 86; a loading
        // pause).
        kit.hold(11);
    }

    void bind(ScriptContext& context, PuzzleScene& scene) override {
        // Virtual functions +0x120..+0x140 (0204b384..0204b460) pass the
        // commands to the logic object.
        context.bind(0x9f, [this](CommandArguments& a) { // 0205dc90
            const int x0 = a.integer(), y0 = a.integer(), x1 = a.integer(), y1 = a.integer();
            logic_.setCells(x0, y0, x1, y1, a.integer());
        });
        context.bind(0xa0, [this](CommandArguments& a) { // 0205cb80
            const int x = a.integer();
            logic_.addPoint(x, a.integer());
        });
        context.bind(0xa1, [this](CommandArguments& a) { // 0205ca04
            LinePuzzle::Segment s;
            s.x0 = a.integer();
            s.y0 = a.integer();
            s.x1 = a.integer();
            s.y1 = a.integer();
            logic_.addAnswer(s);
        });
        context.bind(0xa2, [this](CommandArguments&) { logic_.allowAnyDirection(); });
        context.bind(0xa3, [this](CommandArguments& a) {
            const int x = a.integer();
            logic_.setOrigin(x, a.integer());
        });
        // 0xa4 (0204b3f0): the logic object is made here with the grid's
        // size, cells 2x2.
        context.bind(0xa4, [this](CommandArguments& a) {
            const int w = a.integer();
            logic_.setGridSize(w, a.integer());
            logic_.setCellSize(2);
        });
        context.bind(0xa5, [this](CommandArguments& a) { logic_.setCellSize(a.integer()); });
        context.bind(0xac, [this](CommandArguments& a) {
            const int r = a.integer(), g = a.integer(), b = a.integer();
            logic_.setLineColour(rgb(r, g, b));
        });
        context.bind(0xad, [this](CommandArguments& a) {
            const int r = a.integer(), g = a.integer(), b = a.integer();
            logic_.setPreviewColour(rgb(r, g, b));
        });
        context.bind(0x27, [this](CommandArguments& a) { logic_.setMaxLines(a.integer()); });
        // 0xaa (0204b314): the mode. 0 paths, 1 lines; 2 lines with the
        // colour picker and thin lines, 3 lines that stop at points, 4 thin
        // lines.
        context.bind(0xaa, [this](CommandArguments& a) {
            const int mode = a.integer();
            lines_ = mode != 0;
            pickerOn_ = mode == 2;
            logic_.options.junctions = mode == 2;
            logic_.options.thinLines = mode == 2 || mode == 4;
            logic_.options.stopAtPoints = mode == 3;
        });
        (void)scene;
    }

    void draw(PuzzleScene& scene, bool interactive) override {
        // 0204b180.
        SceneKit& kit = scene.kit;
        if (interactive) {
            input(scene);
            for (const auto& item : logic_.drawList()) {
                show(kit, item);
            }
            // 0205db78 updates the picker's rectangles after its drawing.
            for (auto& b : colours_) {
                b.update(kit, scene.touch);
            }
            submit_.update(kit, scene.touch);
            clear_.update(kit, scene.touch);
        } else {
            submit_.draw(kit);
            clear_.draw(kit);
        }
        // 0205d9d0: the placed lines.
        for (const auto& item : logic_.lineList()) {
            show(kit, item);
        }
        scene.drawCommon(interactive);
    }

    void play(PuzzleScene& scene) override {
        // 0204b1d4.
        SceneKit& kit = scene.kit;
        logic_.palette = {rgb(0xff, 0, 0), rgb(0, 0xff, 0), rgb(0xa0, 0xfa, 0xf5), rgb(0, 0, 0),
                          std::uint16_t(scene.inkColour() & 0x7fff)};
        logic_.start(lines_, scene.id()); // 0205cbbc
        scene.intro();
        for (;;) {
            scene.frame([&] { draw(scene, true); });
            if (scene.hintClicked()) {
                scene.hintScreen();
            }
            if (!logic_.empty() && submit_.clicked(kit, scene.touch)) {
                if (logic_.solved()) { // 0205d124
                    scene.markSolved();
                } else {
                    scene.markWrong();
                }
                break;
            }
            if (clear_.clicked(kit, scene.touch)) {
                logic_.reset(); // 0205cca0 (it also clears the text layer)
                kit.clearText(SceneKit::Lower);
            }
            if (scene.quitClicked() && scene.quitDialog()) {
                break;
            }
        }
        scene.fadeOut(); // 0200fce8(1, 1)
    }

private:
    // 0205d1bc: the stylus for the logic (its press, hold and release, with
    // the steadiness tests asked in the original's order), then the colour
    // picker, then the logic's drawing.
    void input(PuzzleScene& scene) {
        TouchState& t = scene.touch;
        TouchFilter f;
        if (t.pressed && t.steady()) {
            f.state = 1;
        } else if (t.down && t.steady()) {
            f.state = 2;
        } else if (t.released) {
            f.state = 3;
        }
        f.pressX = t.pressX;
        f.pressY = t.pressY;
        f.x = t.x;
        f.y = t.y;
        f.releaseX = t.previousX;
        f.releaseY = t.previousY;
        // The picker is asked after the stylus logic and before its drawing;
        // the two never act in the same frame, so asking it first is the
        // same (LinePuzzle::input also draws).
        if (pickerOn_) {
            for (int i = 0; i < 4; ++i) {
                if (colours_[std::size_t(i)].clicked(scene.kit, t)) {
                    scene.kit.effect(0xe3);
                    logic_.pickerX = buttonX_[std::size_t(i)];
                    logic_.pickerY = buttonY_[std::size_t(i)] - 1;
                    logic_.setCurrentColour(i);
                }
            }
        }
        logic_.input(f);
        // The drag sound 0xbe is the looped effect (0200552c / 02005594).
        for (const auto& r : logic_.sounds.take()) {
            if (r.id == 0xbe) {
                if (r.stop) {
                    scene.stopLoopedEffect();
                } else {
                    scene.startLoopedEffect(r.id);
                }
            } else if (!r.stop) {
                scene.kit.effect(r.id);
            }
        }
    }

    void show(SceneKit& kit, const LinePuzzle::Item& item) {
        if (item.countDepth) {
            kit.countDepth();
            return;
        }
        if (item.sprite) {
            SceneKit::SpriteObject* s = nullptr;
            switch (item.draw.sprite) {
            case LinePuzzle::Sprite::Point:
                s = &point_;
                break;
            case LinePuzzle::Sprite::Here:
                s = &here_;
                break;
            case LinePuzzle::Sprite::NoMoreLines:
                s = &noMoreLines_;
                break;
            case LinePuzzle::Sprite::Junction:
                s = &junction_;
                break;
            case LinePuzzle::Sprite::Picker:
                s = &picker_;
                break;
            }
            s->x = item.draw.x;
            s->y = item.draw.y;
            kit.draw(*s);
            return;
        }
        const auto& p = item.primitive;
        if (!p.thin) {
            // 0200f520 / 0200f47c: at the fixed depth, uncounted.
            kit.rectangle(p.x0, p.y0, p.x1 - p.x0, p.y1 - p.y0, p.color, 31,
                          SceneKit::Depth::Fixed);
            return;
        }
        // 020103c8 with width 1: a band along the line, (1 + 1/16) pixels
        // to each side (a vertical line at x covers columns x - 1 and x;
        // measured), drawn here pixel by pixel: the pixels whose centres
        // lie in the band, from a pixel before the start to a pixel past the
        // end (the ends are hidden under the point markers; hypothesis).
        const float dx = float(p.x1 - p.x0), dy = float(p.y1 - p.y0);
        const float length = std::sqrt(dx * dx + dy * dy);
        const float ux = length > 0 ? dx / length : 1.0f, uy = length > 0 ? dy / length : 0.0f;
        constexpr float kHalf = 1.0625f;
        const int left = std::min(p.x0, p.x1) - 3, right = std::max(p.x0, p.x1) + 3;
        const int top = std::min(p.y0, p.y1) - 3, bottom = std::max(p.y0, p.y1) + 3;
        for (int y = top; y <= bottom; ++y) {
            for (int x = left; x <= right; ++x) {
                const float cx = float(x) + 0.5f - float(p.x0), cy = float(y) + 0.5f - float(p.y0);
                const float along = cx * ux + cy * uy, across = cy * ux - cx * uy;
                if (along >= -1.0f && along < length + 1.0f && across >= -kHalf && across < kHalf) {
                    kit.rectangle(x, y, 1, 1, p.color, 31, SceneKit::Depth::Fixed);
                }
            }
        }
    }

    LinePuzzle logic_;              // +0x7c
    bool lines_ = false;            // the logic's +0x10
    bool pickerOn_ = false;         // +0x4fc
    Button submit_, clear_;         // +0x74 / +0x78
    std::array<Button, 4> colours_; // logic +0x4e4
    std::array<int, 4> buttonX_{}, buttonY_{};
    SceneKit::SpriteObject point_, here_, noMoreLines_, picker_, junction_;
};

} // namespace

std::unique_ptr<Engine> createCutPuzzle(PuzzleScene& scene) {
    return std::make_unique<CutPuzzle>(scene);
}

} // namespace picarat::adventure1
