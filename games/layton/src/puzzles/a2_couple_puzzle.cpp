// Moving-pairs engine (ARM9 overlay 51). See a2_couple_puzzle.hpp for the rules.
#include "puzzles/a2_couple_puzzle.hpp"
#include "system/script.hpp"
#include <cmath>
#include <stdexcept>

namespace picarat {

namespace {
// Stores a value the way the original stores it into a 16-bit field.
std::int16_t s16(int v) {
    return std::int16_t(std::uint16_t(unsigned(v)));
}

// Float to int as the game's helper (0203aed0) does for these small values.
int f2i(float v) {
    return std::isnan(v) ? 0 : int(v);
}

constexpr int kRowY = 0x50;      // y of the row of slots
constexpr int kSlotSpacing = 16; // pixels between slots
} // namespace

CouplePuzzle::CouplePuzzle(std::span<const std::uint8_t> script) {
    ScriptContext context;
    // PairsRow(items, slots, x): virtual function +1e4 (020f7b4c).
    context.bind(0x66, [&](CommandArguments& arguments) {
        count_ = s16(arguments.integer());
        slotCount_ = s16(arguments.integer());
        originX_ = s16(arguments.integer());
    });
    ScriptInterpreter(script).run(context);
    if (slotCount_ < 2 || slotCount_ > 16) {
        throw std::runtime_error("couple slot count outside reviewed range");
    }
    reset();
}

void CouplePuzzle::ease(float& v, int target) {
    v = float(target) + 0.5f * (v - float(target));
}

void CouplePuzzle::reset() {
    state_ = kIdle;
    timer_ = 0;
    touchX_ = touchY_ = picked_ = -1;
    pointX_ = pointY_ = 0;
    moves_ = 0;
    for (auto& held : held_) {
        held.kind = kEmpty;
    }
    // The first `count_` slots alternate bin, can, bin, can, ...; the rest are empty.
    for (int i = 0; i < 16; ++i) {
        auto& slot = slots_[std::size_t(i)];
        slot.kind = i < count_ ? ((i & 1) ? kCan : kBin) : kEmpty;
        slot.targetX = s16(originX_ + kSlotSpacing * i);
        slot.targetY = kRowY;
        slot.x = float(slot.targetX);
        slot.y = float(slot.targetY);
    }
}

void CouplePuzzle::update(const TouchFilter& t) {
    // The pair whose grab marker lies under pixel x: the marker of slots
    // (i, i + 1) is centred 8 pixels right of slot i. The +0xf8 offset
    // (16 slots minus half a slot) rounds and keeps the division positive.
    auto pairAt = [&](int x) {
        return (x - originX_ + 0xf8) / 16 - 16;
    };

    if (state_ == kIdle) {
        // A new press near the row (0x30 <= y <= 0x70) on two neighbouring
        // items picks them up (position read on the press frame, 0206e288).
        if (t.state == 1 && t.y > 0x2f && t.y < 0x71) {
            const int i = pairAt(t.x);
            if (i >= 0 && i < slotCount_ - 1 && slots_[std::size_t(i)].kind != kEmpty &&
                slots_[std::size_t(i + 1)].kind != kEmpty) {
                state_ = kHolding;
                picked_ = s16(i);
                touchX_ = s16(t.x);
                touchY_ = s16(t.y);
                pointX_ = float(originX_ + i * kSlotSpacing + 8);
                pointY_ = 80.0f;
                held_[0] = slots_[std::size_t(i)];
                held_[1] = slots_[std::size_t(i + 1)];
                slots_[std::size_t(i)].kind = kEmpty;
                slots_[std::size_t(i + 1)].kind = kEmpty;
                sounds.play(0x115);
            }
        }
    } else if (state_ == kHolding) {
        if (t.state == 1 || t.state == 2) {
            // The pair hangs 24 pixels above the stylus, side by side.
            touchX_ = s16(t.x);
            touchY_ = s16(t.y);
            held_[0].targetX = s16(touchX_ - 0x10);
            held_[0].targetY = s16(touchY_ - 0x18);
            held_[1].targetX = touchX_;
            held_[1].targetY = s16(touchY_ - 0x18);
        } else {
            // Released: put the pair on the two slots under the stylus (kept
            // inside the row) if both are empty; otherwise it goes back where
            // it came from. Only a real move to a new place is counted.
            int i = pairAt(touchX_);
            if (i < 0) {
                i = 0;
            }
            if (slotCount_ - 1 <= i) {
                i = slotCount_ - 2;
            }
            int destination = picked_;
            if (i != picked_ && slots_[std::size_t(i)].kind == kEmpty &&
                slots_[std::size_t(i + 1)].kind == kEmpty && moves_ < 9999) {
                destination = i;
                moves_ = s16(moves_ + 1);
            }
            // The items keep their drawn position and glide into the slots.
            for (int k = 0; k < 2; ++k) {
                auto& slot = slots_[std::size_t(destination + k)];
                slot.kind = held_[std::size_t(k)].kind;
                slot.x = held_[std::size_t(k)].x;
                slot.y = held_[std::size_t(k)].y;
            }
            state_ = kIdle;
            picked_ = touchX_ = touchY_ = -1;
            pointX_ = pointY_ = 0;
            held_[0].kind = held_[1].kind = kEmpty;
            sounds.play(0x116);

            // Sorted when, reading the row from left to right, bins start
            // exactly one run and cans exactly one run (empty slots may be
            // anywhere, also between the two groups).
            std::array<int, 3> runs{}; // runs started, per kind
            int previous = kEmpty;
            for (int k = 0; k < slotCount_; ++k) {
                const int kind = slots_[std::size_t(k)].kind;
                if (kind != previous) {
                    ++runs[std::size_t(kind)];
                    previous = kind;
                }
            }
            if (runs[kBin] == 1 && runs[kCan] == 1) {
                state_ = kSorted;
                timer_ = 0;
            }
        }
    } else if (state_ == kSorted) {
        timer_ = s16(timer_ + 1);
    }

    // Movement: everything covers half its remaining distance per frame.
    for (int k = 0; k < slotCount_; ++k) {
        auto& slot = slots_[std::size_t(k)];
        ease(slot.x, slot.targetX);
        ease(slot.y, slot.targetY);
    }
    if (state_ != kHolding) {
        return;
    }
    ease(pointX_, touchX_ - 8);
    ease(pointY_, touchY_ - 0x18);
    for (auto& held : held_) {
        ease(held.x, held.targetX);
        ease(held.y, held.targetY);
    }
}

std::vector<CouplePuzzle::Draw> CouplePuzzle::draws() const {
    std::vector<Draw> out;
    // Items in the row...
    for (int k = 0; k < slotCount_; ++k) {
        const auto& slot = slots_[std::size_t(k)];
        if (slot.kind == kBin) {
            out.push_back({"bin", f2i(0.5f + slot.x), f2i(0.5f + slot.y)});
        } else if (slot.kind == kCan) {
            out.push_back({"can", f2i(0.5f + slot.x), f2i(0.5f + slot.y)});
        }
    }
    // ...a grab marker between every two neighbouring items...
    for (int k = 0; k < slotCount_ - 1; ++k) {
        if (slots_[std::size_t(k)].kind != kEmpty && slots_[std::size_t(k + 1)].kind != kEmpty) {
            out.push_back({"touchpoint", originX_ + kSlotSpacing * k + 8, kRowY});
        }
    }
    // ...and the held pair with its highlighted versions and marker on top.
    if (held_[0].kind == kEmpty || held_[1].kind == kEmpty) {
        return out;
    }
    for (const auto& held : held_) {
        if (held.kind == kBin) {
            out.push_back({"bin on", f2i(0.5f + held.x), f2i(0.5f + held.y)});
        } else if (held.kind == kCan) {
            out.push_back({"can on", f2i(0.5f + held.x), f2i(0.5f + held.y)});
        }
    }
    out.push_back({"touchpoint on", f2i(0.5f + pointX_), f2i(0.5f + pointY_)});
    return out;
}

} // namespace picarat
