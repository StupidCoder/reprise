// Selection engine. See a2_selection_puzzle.hpp for how the puzzle plays.
#include "puzzles/a2_selection_puzzle.hpp"
#include <algorithm>
#include <stdexcept>

namespace picarat {

SelectionPuzzle::SelectionPuzzle(std::span<const std::uint8_t> bytes, GraphicsAssets& assets,
                                 unsigned internalId) {
    alternate_ = internalId == 0x86;
    ScriptContext context;
    // Script command 0x14 adds one item; overlay 2 (02078880) forwards it to
    // the puzzle object (overlay 31, virtual function +ec, 020f7d58).
    context.bind(0x14, [&](CommandArguments& arguments) {
        // SelectionChoice(x, y, sprite, expected, clickSound)
        Choice choice;
        choice.bounds.x = arguments.integer();
        choice.bounds.y = arguments.integer();
        choice.sprite = "nazo/onoff/" + arguments.string();
        choice.expected = arguments.truth();
        choice.sound = arguments.integer();
        // The original object holds at most 24 items.
        if (choices_.size() >= 24) {
            return;
        }
        if (choice.sprite.ends_with(".spr")) {
            choice.sprite.replace(choice.sprite.size() - 4, 4, ".arc");
        }
        const auto& sprite = assets.sprite(choice.sprite);
        // Every marker sprite used by these puzzles has two animations, the
        // second being a single still image: the marker shown when selected.
        if (sprite.animations.size() != 2 || sprite.animations[1].size() != 1) {
            throw std::runtime_error("unreviewed selection sprite animation");
        }
        choice.image = &sprite.images.at(sprite.animations[1][0].image);
        choice.bounds.width = int(choice.image->width);
        choice.bounds.height = int(choice.image->height);
        choices_.push_back(std::move(choice));
    });
    ScriptInterpreter(bytes).run(context);
    if (choices_.empty()) {
        throw std::runtime_error("empty selection puzzle");
    }
}

void SelectionPuzzle::update(const Touch& touch) {
    // Overlay 31 (020f7cd8) runs each item as a toggle button (overlay 2,
    // 02080664 / 02080300, toggle mode 1).
    for (auto& choice : choices_) {
        // Hit test (02075f44 -> 0207430c): the image size at a position
        // shifted 2 pixels up and left, inclusive right and bottom edges. None
        // of the nine selection puzzles uses a per-pixel hit mask.
        const Rect hit{choice.bounds.x - 2, choice.bounds.y - 2, choice.bounds.width,
                       choice.bounds.height};
        const bool inside = touch.held && hit.contains(touch.x, touch.y);

        // A press on the item arms it.
        if (touch.pressed && choice.state != kClicked && inside) {
            choice.state = kArmed;
        }
        if (touch.held && choice.state == kArmed) {
            // Dragging off the item and back is allowed; only the release
            // position matters.
            choice.inside = inside;
        } else if (!touch.held && choice.state == kArmed) {
            // Released: a click if the stylus was still over the item.
            choice.state = choice.inside ? kClicked : kIdle;
            choice.inside = false;
            if (choice.state == kClicked) {
                choice.age = 0;
                if (choice.sound >= 0) {
                    sounds.play(choice.sound);
                }
            }
        }
        // A click normally toggles at once in consume(). This fallback, from
        // the shared button code, toggles a click that is still pending after
        // five frames (it runs before the age is increased).
        if (choice.state == kClicked && choice.age > 5) {
            choice.state = kIdle;
            choice.selected = !choice.selected;
        }
        if (choice.state == kClicked) {
            ++choice.age;
        }
    }
}

void SelectionPuzzle::consume() {
    // Overlay 31 (020f7b38), called after the common puzzle buttons: only the
    // first clicked item toggles this frame. Unlike ordinary buttons, which
    // wait six frames, the click is taken immediately (020806c4).
    for (auto& choice : choices_) {
        if (choice.state == kClicked) {
            choice.state = kIdle;
            choice.selected = !choice.selected;
            const int on = alternate_ ? 0x127 : 0xf7;
            const int off = alternate_ ? 0x128 : 0xf8;
            sounds.play(choice.selected ? on : off);
            break;
        }
    }
}

bool SelectionPuzzle::solved() const {
    // 020f7abc: every item must match, including items that must stay off.
    return std::all_of(choices_.begin(), choices_.end(),
                       [](const Choice& c) { return c.selected == c.expected; });
}

} // namespace picarat
