// Pancake-towers engine. See a2_tower_puzzle.hpp for the rules and the layout.
#include "puzzles/a2_tower_puzzle.hpp"
#include "puzzles/judgment.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace picarat {

namespace {
constexpr int kBottomY = 147;     // sprite y of a pancake at level 0
constexpr int kLevelHeight = 8;   // vertical distance between stacked pancakes
constexpr int kPlateSpacing = 64; // horizontal distance between plates
constexpr int kFirstPlateX = 4;   // sprite x on the left plate
} // namespace

TowerPuzzle::TowerPuzzle(std::span<const std::uint8_t> bytes, std::uint32_t* randomState)
    : random_(randomState ? randomState : &ownedRandom_) {
    ScriptContext context;
    // Command 0x40: overlay 2 (02079d84) passes it to the puzzle object
    // (overlay 41, virtual function +15c, 020f8558).
    context.bind(0x40, [&](CommandArguments& arguments) {
        // PancakeCount(n)
        const int n = arguments.integer();
        if (n < 1) {
            return;
        }
        if (n > 7) {
            throw std::runtime_error("pancake count exceeds original seven-piece storage");
        }
        count_ = std::int16_t(n);
        reset();
    });
    ScriptInterpreter(bytes).run(context);
    if (!count_) {
        throw std::runtime_error("missing pancake count");
    }
}

void TowerPuzzle::target(Piece& p) {
    // 020f78a0. (The disassembler splits this function in two at 020f78bc;
    // the code at 020f78b8 loads the plate and falls through into it.)
    p.x = p.plate == 0 ? 4 : p.plate == 1 ? 68 : p.plate == 2 ? 132 : 0;
    p.y = std::int16_t(kBottomY - kLevelHeight * p.level);
}

void TowerPuzzle::fall(Piece& p) {
    // 020f7900. Gravity adds 1 pixel/frame to the vertical speed every frame.
    // The pancake needs time t to fall the distance dy with initial speed vy:
    //   dy = vy·t + t²/2   =>   t = −vy + √(vy² + 2·dy)
    // and its horizontal speed is chosen to cover the remaining dx in that time.
    // If the target is above the pancake, it just slides there horizontally.
    p.vx = float(p.x) - p.displayX;
    const float dy = float(p.y) - p.displayY;
    if (dy < 0) {
        return;
    }
    const float discriminant = 2.f * dy + p.vy * p.vy;
    if (discriminant < 0) {
        return;
    }
    p.vx = (float(p.x) - p.displayX) / (-p.vy + std::sqrt(discriminant));
}

void TowerPuzzle::bounceBack(Piece& p) {
    // 020f79b8 with a height of −32: a hop of √256 = 16 frames back to the
    // plate the pancake came from, rising about 32 pixels on the way.
    const float duration = std::sqrt(256.f);
    p.vx = (float(p.x) - p.displayX) / duration;
    p.vy = (-128.f + (float(p.y) - p.displayY)) / duration;
}

void TowerPuzzle::reset() {
    // 020f7bd4 -> 020f8558: empty plates and idle pancakes...
    state_ = kIdle;
    touchX_ = touchY_ = -1;
    selected_ = -1;
    counter_ = moves_ = 0;
    for (auto& stack : stacks_) {
        stack = Stack{};
    }
    for (auto& p : pieces_) {
        p.state = kResting;
        p.plate = p.level = 0;
        p.priorPlate = p.priorLevel = -1;
        target(p);
    }
    // ...then build the tower on the left plate, largest pancake first. Each
    // pancake starts above the top of the screen and is "dropped" into place,
    // so the stack falls in when the puzzle opens. Higher pancakes start with
    // a random upward kick, which staggers their arrival.
    for (int i = count_ - 1; i >= 0; --i) {
        auto& p = pieces_[i];
        p.plate = 0;
        p.level = std::int16_t(count_ - i - 1);
        target(p);
        p.displayX = float(p.x);
        p.displayY = float(-22 - kLevelHeight * p.level);
        selected_ = std::int16_t(i);
        drop(0, false);
        p.vy = float(p.level) * (-2.f * (1.f + float(gameRandom(*random_)) / 32767.f));
    }
    // The set-up reuses drop(), but the original entrance plays no sound.
    sounds.take();
}

void TowerPuzzle::pick(int plate) {
    // 020f8698: lift the top pancake of the touched plate (unless one is
    // already held, or the plate is invalid or empty).
    if (std::uint16_t(selected_) < 7 || unsigned(plate) >= 3) {
        return;
    }
    auto& stack = stacks_[plate];
    if (stack.count <= 0) {
        return;
    }
    selected_ = stack.pieces[--stack.count];
    stack.pieces[stack.count] = -1;
    auto& p = pieces_[selected_];
    p.state = kHeld;
    p.priorPlate = p.plate;
    p.priorLevel = p.level;
    p.plate = p.level = -1;
    p.bounces = 0;
    state_ = kHolding;
    sounds.play(0x105);
}

bool TowerPuzzle::drop(int plate, bool countMove) {
    // 020f8778: put the held pancake on top of `plate`.
    if (std::uint16_t(selected_) >= 7 || unsigned(plate) >= 3) {
        return false;
    }
    auto& stack = stacks_[plate];
    if (std::uint16_t(stack.count) >= 7) {
        return false;
    }
    // The rule of the puzzle: never on a smaller pancake (lower index).
    if (stack.count > 0 && stack.pieces[stack.count - 1] < selected_) {
        return false;
    }
    auto& p = pieces_[selected_];
    // Dropping back onto the plate it came from is not a move.
    if (countMove && plate != p.priorPlate) {
        moves_ = std::int16_t(std::min(int(moves_) + 1, 9999));
    }
    p.state = kResting;
    p.plate = std::int16_t(plate);
    p.level = stack.count;
    p.priorPlate = p.priorLevel = -1;
    p.vx = p.vy = 0;
    p.bounces = 0;
    target(p);
    // A pancake released more than 4 pixels above its place falls there;
    // otherwise it lands at once.
    if (!(p.displayY > float(p.y) - 4.f)) {
        p.state = kFalling;
        fall(p);
    } else {
        sounds.play(0x106);
    }
    stack.pieces[stack.count++] = selected_;
    selected_ = -1;
    state_ = kIdle;
    return true;
}

void TowerPuzzle::update(const Touch& touch) {
    // 020f7d2c.
    switch (state_) {
    case kIdle:
        // A press in the plate area (lower half of the screen, left of x = 193)
        // lifts the top pancake of the plate below the stylus.
        if (touch.pressed) {
            touchX_ = std::int16_t(touch.x);
            touchY_ = std::int16_t(touch.y);
            pick(touchY_ < 96 || touchX_ > 192 ? -1 : plateAt(touchX_));
        }
        counter_ = 0;
        break;

    case kHolding:
        // The held pancake follows the stylus, kept inside the play area. Its
        // sprite is offset so the stylus holds it near its centre.
        if (touch.held && selected_ >= 0) {
            touchX_ = std::int16_t(touch.x);
            touchY_ = std::int16_t(touch.y);
            auto& p = pieces_[selected_];
            p.x = std::int16_t(std::clamp(touch.x, 24, 168) - 28);
            p.y = std::int16_t(std::clamp(touch.y, 25, 183) - 11);
        }
        if (!touch.held) {
            // Released: drop onto the plate under the last stylus position.
            const int destination = plateAt(touchX_);
            auto& p = pieces_[selected_];
            if (!drop(destination, true)) {
                // Illegal: the pancake goes back on its own plate (logically
                // at once), but visibly first moves over the attempted
                // plate's stack and then hops back.
                drop(p.priorPlate, false);
                p.x = std::int16_t(kFirstPlateX + kPlateSpacing * destination);
                p.y = std::int16_t(kBottomY - kLevelHeight * stacks_[destination].count);
                if (p.displayY > float(p.y) - 4.f) {
                    p.state = kRejectedSlide;
                } else {
                    p.state = kRejectedFall;
                    fall(p);
                }
            }
            touchX_ = touchY_ = -1;
        }
        counter_ = 0;
        break;

    case kComplete:
        // Count frames towards the automatic judgment. The state is set again
        // below every frame while the tower stays complete.
        state_ = kIdle;
        counter_ = std::bit_cast<std::int16_t>(std::uint16_t(std::uint16_t(counter_) + 1));
        break;
    }
    // Solved when every pancake is on the right plate.
    if (count_ > 0 && stacks_[2].count == count_) {
        state_ = kComplete;
    }
}

void TowerPuzzle::animate() {
    // 020f7fa4 moves the largest pancake first, so pancakes above it can
    // follow its position in the same frame.
    for (int i = count_ - 1; i >= 0; --i) {
        auto& p = pieces_[i];
        if (p.state == kFalling || p.state == kRejectedFall) {
            // A falling pancake lands on whatever is below it right now: the
            // plate, or 8 pixels above the (possibly still falling) pancake
            // underneath.
            if (p.state == kFalling && std::uint16_t(p.plate) < 3) {
                p.y = p.level == 0
                          ? kBottomY
                          : std::int16_t(pieces_[stacks_[p.plate].pieces[p.level - 1]].displayY -
                                         8.f);
            }
            p.displayX += p.vx;
            p.vy += 1.f; // gravity
            if (p.vy > 0 && p.displayY + p.vy >= float(p.y)) {
                // Touching down this frame.
                ++p.bounces;
                if (p.state == kRejectedFall || p.vy > 1.f) {
                    // A real impact: bounce up at a fifth of the speed, with
                    // almost no sideways movement, towards the target.
                    sounds.play(p.state == kRejectedFall ? 0x104 : 0x106);
                    p.vx *= 0.15f;
                    if (p.vx < 0 ? float(p.x) > p.displayX : float(p.x) < p.displayX) {
                        p.vx *= -1.f;
                    }
                    p.vy *= -0.2f;
                    p.displayY = float(p.y) + p.vy;
                    if (p.state == kRejectedFall) {
                        // After the rejected landing, hop back home.
                        p.state = kFalling;
                        p.bounces = 0;
                        target(p);
                        bounceBack(p);
                    }
                } else if (p.y >= kBottomY - kLevelHeight * p.level) {
                    // Slow enough to settle (unless resting on a pancake that
                    // is still above its own final place).
                    p.state = kResting;
                    p.displayY = float(p.y);
                }
            } else {
                p.displayY += p.vy;
            }
        } else {
            // Resting, held or sliding: cover 75 % of the remaining distance
            // each frame.
            p.vx = 0.75f * (float(p.x) - p.displayX);
            p.vy = 0.75f * (float(p.y) - p.displayY);
            p.displayX += p.vx;
            p.displayY += p.vy;
            if (p.state == kRejectedSlide && std::abs(float(p.y) - p.displayY) < 2.f) {
                // Arrived over the plate it was not allowed on: hop back home.
                p.state = kFalling;
                p.displayY = float(p.y);
                p.bounces = 0;
                target(p);
                bounceBack(p);
                sounds.play(0x104);
            }
        }
    }
}

std::vector<int> TowerPuzzle::drawOrder() const {
    // 020f83fc: resting pancakes at the back, then falling, rejected ones, and
    // the held pancake in front; within each group the largest first.
    std::vector<int> order;
    for (int state : {kResting, kFalling, kRejectedSlide, kRejectedFall, kHeld}) {
        for (int i = count_ - 1; i >= 0; --i) {
            if (pieces_[i].state == state) {
                order.push_back(i);
            }
        }
    }
    return order;
}

} // namespace picarat
