// Multiple-choice engine. See a2_choice_puzzle.hpp for how the puzzle plays.
#include "puzzles/a2_choice_puzzle.hpp"
#include <stdexcept>

namespace picarat {

ChoicePuzzle::ChoicePuzzle(std::span<const std::uint8_t> bytes, GraphicsAssets& assets) {
    ScriptContext context;
    // Script command 0x14 adds one answer button. The shared interpreter in
    // overlay 2 (02078880) forwards it to the puzzle object (overlay 30,
    // virtual function +ec, 020f7aec).
    context.bind(0x14, [&](CommandArguments& arguments) {
        // ChoiceButton(x, y, sprite, correct, clickSound)
        Choice choice;
        choice.x = arguments.integer();
        choice.y = arguments.integer();
        choice.sprite = "nazo/freebutton/" + arguments.string();
        choice.correct = arguments.truth();
        choice.sound = arguments.integer();
        // The original object has room for 24 buttons and ignores the rest.
        // Published puzzles use at most eight.
        if (choices_.size() >= 24) {
            return;
        }
        // Scripts name the sprite ".spr"; the file in the ROM is ".arc".
        if (choice.sprite.ends_with(".spr")) {
            choice.sprite.replace(choice.sprite.size() - 4, 4, ".arc");
        }
        const auto& sprite = assets.sprite(choice.sprite);
        // Each button has a still "off" and a still "on" image of equal size.
        // Anything else would need reviewing against the original first.
        auto stillImage = [&](const char* animation) -> const Surface* {
            const auto& frames = sprite.animations.at(sprite.animationNames.at(animation));
            if (frames.size() != 1) {
                throw std::runtime_error("unreviewed animated choice button");
            }
            return &sprite.images.at(frames[0].image);
        };
        choice.idle = stillImage("off");
        choice.pressed = stillImage("on");
        if (choice.idle->width != choice.pressed->width ||
            choice.idle->height != choice.pressed->height) {
            throw std::runtime_error("unreviewed variable choice bounds");
        }
        // The sprite hit test (02075f44 / 0207430c) uses the image size at a
        // position shifted 2 pixels up and left, with inclusive right and
        // bottom edges. Both button sprites used by all 25 puzzles have no
        // per-pixel hit mask, so the rectangle is the whole test.
        choice.button =
            Button({choice.x - 2, choice.y - 2, int(choice.idle->width), int(choice.idle->height)},
                   choice.sound); // click sound (0208003c) = script value
        choices_.push_back(std::move(choice));
    });
    ScriptInterpreter(bytes).run(context);
    if (choices_.empty()) {
        throw std::runtime_error("empty multiple-choice puzzle");
    }
}

void ChoicePuzzle::update(const Touch& touch) {
    // Overlay 30 (020f79e0) updates every button with the ordinary button
    // logic (overlay 2, 02080664), in script order.
    for (auto& choice : choices_) {
        choice.button.update(touch);
    }
}

std::optional<bool> ChoicePuzzle::consume() {
    // 020f7a60 -> 02080690: the first button whose click has completed
    // answers the puzzle; its "correct" flag selects the judgment directly.
    for (auto& choice : choices_) {
        if (choice.button.consumeClick()) {
            return choice.correct;
        }
    }
    // The original click/submit sound effects are not played here yet
    // (recorded gap G14).
    return std::nullopt;
}

} // namespace picarat
