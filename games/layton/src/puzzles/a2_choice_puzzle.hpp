#pragma once
// Multiple choice (puzzle type 2, ARM9 overlay 30).
//
// How the puzzle plays
// --------------------
// The lower screen shows a handful of answer buttons drawn by the puzzle's
// parameter script (opcode 0x14, one per button). Tapping a button answers
// the puzzle immediately: there is no separate "Submit" step, and no "Reset"
// button. The button's flag in the script says whether it is the right answer,
// which decides between the "correct" and "wrong" judgment screens.
//
// Buttons behave like the game's ordinary buttons: a click is a press and
// release on the same button, and it completes six frames later while the
// button shows its pressed image (see Button in graphics.hpp).
#include "graphics/graphics.hpp"
#include "system/script.hpp"
#include <optional>

namespace picarat {

class ChoicePuzzle {
public:
    struct Choice {
        std::string sprite;               // button sprite ("nazo/freebutton/...")
        Button button;                    // hit area, click state and click sound
        bool correct = false;             // whether this answer solves the puzzle
        int sound = 0;                    // click sound from the script
        const Surface* idle = nullptr;    // "off" image
        const Surface* pressed = nullptr; // "on" image
        int x = 0, y = 0;                 // drawing position on the lower screen
    };

    ChoicePuzzle(std::span<const std::uint8_t> script, GraphicsAssets& assets);

    // One frame of button input, in script order.
    void update(const Touch& touch);

    // The answer, once a button's click has completed: true for the correct
    // button, false for a wrong one; nothing while no click has completed.
    std::optional<bool> consume();

    const auto& choices() const { return choices_; }

private:
    std::vector<Choice> choices_;
};

} // namespace picarat
