// Cycling-pictures engine. See a2_color_puzzle.hpp for how the puzzle plays.
#include "puzzles/a2_color_puzzle.hpp"
#include <stdexcept>

namespace picarat {

ColorPuzzle::ColorPuzzle(std::span<const std::uint8_t> bytes, GraphicsAssets& assets,
                         unsigned internalId)
    : internalId_(internalId) {
    ScriptContext context;
    context.bind(0x2e, [&](CommandArguments& arguments) {
        // CyclingSprite(x, y, sprite, startVariant, requiredVariant)
        Item item{};
        item.x = arguments.integer();
        item.y = arguments.integer();
        auto path = "nazo/touch/" + arguments.string();
        item.initial = arguments.integer();
        item.expected = arguments.integer();
        if (path.ends_with(".spr")) {
            path.replace(path.size() - 4, 4, ".arc");
        }
        item.sprite = &assets.sprite(path);
        item.current = item.initial;
        // The original (020f7904) stores up to 32 pictures without checking
        // the bound; a native build rejects such data instead of corrupting
        // memory. The start variant must be a real variant (not animation 0).
        if (items_.size() >= 32) {
            throw std::runtime_error("cycling sprite capacity");
        }
        if (item.initial < 1 || unsigned(item.initial) >= item.sprite->animations.size()) {
            throw std::runtime_error("invalid cycling sprite initial animation");
        }
        item.animation.select(item.initial, 0, item.sprite->animations);
        items_.push_back(item);
    });
    ScriptInterpreter(bytes).run(context);
    if (items_.empty()) {
        throw std::runtime_error("empty cycling puzzle");
    }
}

Rect ColorPuzzle::bounds(const Item& item) const {
    const auto& frames = item.sprite->animations.at(item.animation.animation);
    const auto& image = item.sprite->images.at(frames.at(item.animation.step).image);
    // Unlike ordinary sprite buttons (which shift the hit area by -2 pixels),
    // this overlay tests the image rectangle exactly where it is drawn.
    return {item.x, item.y, int(image.width), int(image.height)};
}

void ColorPuzzle::update(const Touch& touch, std::uint32_t tick) {
    // Overlay 38, 020f7a30.
    if (touch.held) {
        lastX_ = touch.x;
        lastY_ = touch.y;
    }
    // A press selects the first picture (in script order) under the stylus.
    if (touch.pressed) {
        for (unsigned i = 0; i < items_.size(); ++i) {
            if (bounds(items_[i]).contains(touch.x, touch.y)) {
                selected_ = i;
                armed_ = true;
                break;
            }
        }
    }
    if (touch.released) {
        // The release is tested at the last held position (the original keeps
        // the final contact coordinates in its touch state at +20/+24).
        if (armed_) {
            auto& item = items_.at(selected_);
            if (bounds(item).contains(lastX_, lastY_)) {
                // Next variant, wrapping from the last one back to variant 1.
                ++item.current;
                if (unsigned(item.current) >= item.sprite->animations.size()) {
                    item.current = 1;
                }
                item.animation.select(item.current, tick, item.sprite->animations);
                // Some puzzles have their own tap sound, chosen by internal ID.
                const unsigned id = internalId_;
                int sound = 3; // the generic tap
                if (id == 0x22) {
                    sound = 0xf7;
                } else if (id == 0x26) {
                    sound = 0x10e;
                } else if (id == 0x43) {
                    sound = 0x113;
                } else if (id == 0x50 || id == 0xbf) {
                    sound = 0x114;
                } else if (id == 0x56 || id == 0xbd) {
                    sound = 0x111;
                }
                sounds.play(sound);
            }
        }
        armed_ = false;
    }
}

void ColorPuzzle::reset(std::uint32_t tick) {
    // 020f79a4 restores the start variants. Like the original it leaves the
    // current selection alone.
    for (auto& item : items_) {
        item.current = item.initial;
        item.animation.select(item.current, tick, item.sprite->animations);
    }
}

bool ColorPuzzle::solved() const {
    // 020f79e8: every picture must show its required variant; -1 accepts any.
    for (const auto& item : items_) {
        if (item.expected != -1 && item.current != item.expected) {
            return false;
        }
    }
    return true;
}

void ColorPuzzle::animate(std::uint32_t tick) {
    for (auto& item : items_) {
        item.animation.advance(tick, item.sprite->animations);
    }
}

void ColorPuzzle::render(Surface& target, int yOffset) const {
    // 020f7cb8 draws in script order. In the original, the shared sprite
    // service advances the animations after drawing.
    for (const auto& item : items_) {
        const auto& frames = item.sprite->animations.at(item.animation.animation);
        blit(target, item.sprite->images.at(frames.at(item.animation.step).image), item.x,
             yOffset + item.y);
    }
}

} // namespace picarat
