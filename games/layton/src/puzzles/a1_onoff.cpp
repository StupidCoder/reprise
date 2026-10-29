// The first adventure's "On Off" engine (engine 12, ARM9 02048a44..02048f6c):
// switches.
//
// How it plays
// ------------
// The picture has switches (doors, valves, checkboxes); tapping one turns it
// on or off, and only switches that are on show their picture. "Submit"
// answers: right when exactly the required switches are on.
//
// The second adventure's selection puzzles (puzzles/a2_selection_puzzle.*)
// work alike but differ in everything around the rule (its buttons, limits
// and sounds); this engine stays on the first adventure's own button class
// (a1_ui.hpp), whose toggle mode is the whole mechanism.
#include "puzzles/a1_engines.hpp"

namespace picarat::adventure1 {

namespace {

class OnOff final : public Engine {
public:
    explicit OnOff(PuzzleScene& scene) {
        // 02048a44.
        SceneKit& kit = scene.kit;
        const std::string picture = "q" + std::to_string(scene.id()) + "_bg";
        if (fileExists(kit.assets.root / "bg" / (picture + ".l2bg"))) {
            scene.setEngineBackground(picture); // 02040ddc
        }
        submit_.setSprite(kit, 0xba, 0x9d, "?/submit.spr", "gfx");
        submit_.visibleIdle = true;
        submit_.sound = 0xb;
        kit.hold(kLoadFrames); // loading the switches (a loading pause)
    }

    void bind(ScriptContext& context, PuzzleScene& scene) override {
        // 0x5d (02013388 -> virtual +0x100, 02048ca4): a switch at x, y with
        // its sprite, whether it must be on, and a value this engine
        // ignores; at most 24.
        context.bind(0x5d, [this, &scene](CommandArguments& a) {
            const int x = a.integer(), y = a.integer();
            const std::string sprite = a.string();
            const bool on = a.integer() != 0;
            a.integer();
            if (switches_.size() >= 0x18) {
                return;
            }
            Switch s;
            s.button.setSprite(scene.kit, x, y, sprite);
            s.button.toggle = true;
            s.required = on;
            switches_.push_back(std::move(s));
        });
    }

    void draw(PuzzleScene& scene, bool interactive) override {
        // 02048d90.
        SceneKit& kit = scene.kit;
        if (interactive) {
            for (auto& s : switches_) {
                s.button.update(kit, scene.touch);
            }
            submit_.update(kit, scene.touch);
        } else {
            submit_.draw(kit);
        }
        scene.drawCommon(interactive);
    }

    void play(PuzzleScene& scene) override {
        // 02048df0.
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
            if (submit_.clicked(kit, scene.touch)) {
                bool right = true; // 02048d4c
                for (const auto& s : switches_) {
                    right = right && s.button.on == s.required;
                }
                if (right) {
                    scene.markSolved();
                } else {
                    scene.markWrong();
                }
                break;
            }
            for (auto& s : switches_) {
                if (s.button.clicked(kit, scene.touch)) {
                    // Puzzle 26 (internal 0x1a) has valve sounds.
                    const bool valves = scene.id() == 0x1a;
                    kit.effect(s.button.on ? (valves ? 0xe1 : 0xcd) : (valves ? 0xe2 : 0xce));
                }
            }
        }
        scene.fadeOut(); // 02048b2c (destructor)
    }

private:
    static constexpr int kLoadFrames = 9;

    struct Switch {
        Button button;         // +0x78: a toggle button (+0x20), on = +0x21
        bool required = false; // +0xd8
    };

    std::vector<Switch> switches_; // count +0xf0
    Button submit_;                // +0x74
};

} // namespace

std::unique_ptr<Engine> createOnOff(PuzzleScene& scene) {
    return std::make_unique<OnOff>(scene);
}

} // namespace picarat::adventure1
