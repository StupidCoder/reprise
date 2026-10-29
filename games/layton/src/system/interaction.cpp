// Input, buttons, name entry and sprite animation. See interaction.hpp.
#include "system/interaction.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace picarat {
bool Rect::contains(int px, int py) const {
    return width != 0 && height != 0 && px >= x && px <= x + width && py >= y && py <= y + height;
}

// Filter states: 0 idle, 1 pressed (first frame), 2 held, 3 released (one
// frame), then back to 0.
Touch TouchFilter::update(bool touching, int sx, int sy) {
    auto near = [&] {
        // 0206b0c4: signed differences, binary32 multiply/add and sqrt.
        const float dx = float(rawX - sx), dy = float(rawY - sy);
        return !(std::sqrt(dx * dx + dy * dy) > threshold);
    };
    const bool held = state == 1 || state == 2;
    if (!held && touching) {
        // A first sample only remembers the position; the press starts when
        // the next sample is close to it.
        if (rawX == -1 && rawY == -1) {
            rawX = sx;
            rawY = sy;
        } else {
            if (near()) {
                state = 1;
                x = pressX = sx;
                y = pressY = sy;
            }
            rawX = sx;
            rawY = sy;
        }
    } else if (held && touching) {
        state = 2;
        if (near()) {
            x = sx;
            y = sy;
        }
        rawX = sx;
        rawY = sy;
    } else if (held) {
        state = 3;
        releaseX = x;
        releaseY = y;
        x = y = rawX = rawY = -1;
    } else if (state == 3) {
        state = 0;
        pressX = pressY = releaseX = releaseY = -1;
    }
    return {state == 1 || state == 2, state == 1, state == 3, x, y};
}

// Overlay 2, 02080300, the ordinary (non-toggle) behaviour.
void Button::update(const Touch& touch) {
    if (touch.pressed && state != State::Clicked && bounds.contains(touch.x, touch.y)) {
        state = State::Armed;
    }
    if (touch.held && state == State::Armed) {
        inside = bounds.contains(touch.x, touch.y);
    } else if (!touch.held && state == State::Armed) {
        state = inside ? State::Clicked : State::Idle;
        inside = false;
        if (state == State::Clicked) {
            age = 0;
            if (sound >= 0 && clickSound) {
                clickSound(sound);
            }
        }
    }
    if (state == State::Clicked) {
        if (age > 5) {
            state = State::Idle;
        } else {
            ++age;
        }
    }
}

// Overlay 2, 02080690: a click is seen once, six updates after the release.
bool Button::consumeClick() {
    if (state == State::Clicked && age > 5) {
        state = State::Idle;
        return true;
    }
    return false;
}

void NameEditor::insert(std::uint8_t character) {
    characters[cursor] = character;
    if (cursor < 9) {
        ++cursor;
    }
}

// Backspace: clears the slot under the cursor, or, if it is empty, moves
// back and clears the previous one.
void NameEditor::erase() {
    if (characters[cursor] == 0) {
        if (cursor > 0) {
            --cursor;
        }
        characters[cursor] = 0;
    } else {
        characters[cursor] = 0;
        if (cursor > 0 && cursor < 9) {
            --cursor;
        }
    }
}

void NameEditor::clear() {
    cursor = 0;
    characters.fill(0);
}

std::string NameEditor::value() const {
    auto end = std::find(characters.begin(), characters.end(), 0);
    return std::string(characters.begin(), end);
}

bool NameEditor::reserved(const std::array<std::string, 2>& names) const {
    return value() == names[0] || value() == names[1];
}

Rect NameEditor::keyBounds(unsigned index, int layout) {
    if (layout == 3) {
        if (index >= 52) {
            throw std::runtime_error("special key index");
        }
        return {5 + int(index % 13) * 19, 49 + int(index / 13) * 19, 19, 19};
    }
    if (index >= 45) {
        throw std::runtime_error("normal key index");
    }
    if (index < 12) {
        return {15 + int(index) * 19, 49, 19, 19};
    }
    if (index < 24) {
        return {24 + int(index - 12) * 19, 68, 19, 19};
    }
    if (index < 35) {
        return {34 + int(index - 24) * 19, 87, 19, 19};
    }
    return {43 + int(index - 35) * 19, 106, 19, 19};
}

void AnimationPlayer::select(unsigned index, std::uint32_t tick,
                             const std::vector<std::vector<AnimationStep>>& animations) {
    if (index < 1 || index >= animations.size() || index == animation) {
        return;
    }
    if (animations[index].empty()) {
        throw std::runtime_error("empty animation selected");
    }
    animation = index;
    step = 0;
    lastTick = tick;
    duration = animations[index][0].duration;
}

// Overlay 2, 02075e50: move to the next step once the current one has lasted
// its duration. The unsigned subtraction wraps like the original's frame
// counter. A frozen animation keeps its step.
void AnimationPlayer::advance(std::uint32_t tick,
                              const std::vector<std::vector<AnimationStep>>& animations,
                              bool frozen) {
    if (animation == 0) {
        return;
    }
    if (frozen) {
        lastTick = tick;
        return;
    }
    if (duration < std::uint32_t(tick - lastTick)) {
        step = animations.at(animation).at(step).next;
        duration = animations.at(animation).at(step).duration;
        lastTick = tick;
    }
}
} // namespace picarat
