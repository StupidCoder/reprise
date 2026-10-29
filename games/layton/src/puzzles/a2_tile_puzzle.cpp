// Tile placement and rotating tiles engine (ARM9 overlay 48). See
// a2_tile_puzzle.hpp for how the puzzles play and the two special modes.
#include "puzzles/a2_tile_puzzle.hpp"
#include <algorithm>
#include <bit>
#include <climits>
#include <cmath>
#include <stdexcept>

namespace picarat {
namespace {
// Quarter turns (0..3) to degrees; a quarter turn clockwise is +90 degrees.
float degrees(int rotation) {
    constexpr float angle[]{0, 90, 180, -90};
    return angle[rotation & 3];
}

// Brings an angle into -180..180 (one correction step, as in the original).
float normalized(float angle) {
    if (angle > 180) {
        angle -= 360;
    }
    if (angle < -180) {
        angle += 360;
    }
    return angle;
}

// The nearest quarter turn to an angle in degrees.
int quantized(float angle) {
    angle += 45;
    if (angle > 360) {
        angle -= 360;
    }
    if (angle < 0) {
        angle += 360;
    }
    return int(angle / 90) & 3;
}

// A point turned by r quarter turns (screen y points down).
std::array<int, 2> rotatePoint(int x, int y, int r) {
    switch (r & 3) {
    case 0:
        return {x, y};
    case 1:
        return {-y, x};
    case 2:
        return {-x, -y};
    default:
        return {y, -x};
    }
}

// A rectangle {left, top, right, bottom} turned by `rotation` quarter turns.
std::array<int, 4> rotatedBounds(int l, int t, int r, int b, int rotation) {
    switch (rotation & 3) {
    case 0:
        return {l, t, r, b};
    case 1:
        return {-b, l, -t, r};
    case 2:
        return {-r, -b, -l, -t};
    default:
        return {t, -r, b, -l};
    }
}

// Whether (x, y) lies in one of a piece's areas (inclusive edges), turned
// with the piece. The drawn position is rounded to whole pixels first
// (020f7aa0). Rotating pieces are positioned by their anchor, so the anchor
// offset is added for them.
bool contains(const TilePuzzle::Piece& p, const std::vector<Rect>& rects, int x, int y,
              bool anchor) {
    if (anchor) {
        auto a = rotatePoint(p.anchorX, p.anchorY, p.rotation);
        x += a[0];
        y += a[1];
    }
    x -= int(p.displayX + 0.5f);
    y -= int(p.displayY + 0.5f);
    for (const auto& r : rects) {
        auto b = rotatedBounds(r.x, r.y, r.x + r.width - 1, r.y + r.height - 1, p.rotation);
        if (x >= b[0] && x <= b[2] && y >= b[1] && y <= b[3]) {
            return true;
        }
    }
    return false;
}

// Adds one to a 16-bit counter, wrapping like the original field.
std::int16_t increment(std::int16_t value) {
    return std::bit_cast<std::int16_t>(std::uint16_t(std::uint16_t(value) + 1));
}
} // namespace

TilePuzzle::TilePuzzle(std::span<const std::uint8_t> bytes, unsigned internalId) {
    specialMode_ = internalId == 2 ? kKey : internalId == 203 ? kSun : kNormal;
    // Two puzzles use other pick-up and drop sounds (020f900c / 020f9188
    // test the internal ID at +7e88).
    alternateSounds_ = internalId == 0x3e || internalId == 0x73;
    ScriptContext context;
    // TileSprite(name): overlay 2 0207a734 -> 020f8c38. The '?' in the name
    // is replaced by the language folder ("ge" for the German release).
    context.bind(0x54, [&](CommandArguments& a) {
        sprites_.push_back(resolveLanguagePath("nazo/tile/" + a.string(), "ge"));
        if (sprites_.back().ends_with(".spr")) {
            sprites_.back().replace(sprites_.back().size() - 4, 4, ".arc");
        }
    });
    // TileSlot(x, y): 0207a77c -> 020f8cc0 -> 020f78c4.
    context.bind(0x55, [&](CommandArguments& a) {
        int x = a.integer(), y = a.integer();
        if (slots_.size() < 64) {
            slots_.push_back({std::int16_t(x), std::int16_t(y), -1});
        }
    });
    // TilePiece(sprite, anchorX, anchorY, idle, held, released, slot):
    // 0207a87c -> 020faa1c, which passes orientation 0 to 020f8d00.
    context.bind(0x57, [&](CommandArguments& a) {
        Piece p;
        p.sprite = a.integer();
        p.anchorX = std::int16_t(a.integer());
        p.anchorY = std::int16_t(a.integer());
        p.idle = a.string();
        p.held = a.string();
        p.released = a.string();
        p.initialSlot = std::int16_t(a.integer());
        if (pieces_.size() < 32) {
            pieces_.push_back(std::move(p));
        }
    });
    // RotatingTilePiece(sprite, anchorX, anchorY, idle, held, released, slot,
    // orientation): 0207a968 -> 020f8d00 / 020f798c. Any such piece makes
    // this a rotating-tiles puzzle.
    context.bind(0x58, [&](CommandArguments& a) {
        Piece p;
        p.sprite = std::int16_t(a.integer());
        p.anchorX = std::int16_t(a.integer());
        p.anchorY = std::int16_t(a.integer());
        p.idle = a.string();
        p.held = a.string();
        p.released = a.string();
        p.initialSlot = std::int16_t(a.integer());
        p.initialRotation = a.integer() & 3;
        rotationParameters_ = true;
        if (pieces_.size() < 32) {
            pieces_.push_back(std::move(p));
        }
    });
    // TileDragRect(x, y, width, height) for the last piece: 020f8db0 / 020f7a48.
    context.bind(0x6d, [&](CommandArguments& a) {
        int x = std::int16_t(a.integer()), y = std::int16_t(a.integer()),
            w = std::int16_t(a.integer()), h = std::int16_t(a.integer());
        if (!pieces_.empty() && pieces_.back().dragHit.size() < 8) {
            pieces_.back().dragHit.push_back({x, y, w, h});
        }
    });
    // RotatingTileAnswer(piece, slot, orientation): 020f8de4 / 020f7900.
    context.bind(0x5b, [&](CommandArguments& a) {
        auto piece = unsigned(std::uint16_t(a.integer()));
        int slot = std::int16_t(a.integer()), rotation = a.integer();
        if (answers_.size() < 32) {
            answers_.push_back({piece, slot, rotation});
        }
    });
    // TileHitRect(x, y, width, height) for the last piece: 0207aa64 ->
    // 020f8d7c -> 020f79f0.
    context.bind(0x59, [&](CommandArguments& a) {
        int x = a.integer(), y = a.integer(), w = a.integer(), h = a.integer();
        if (!pieces_.empty() && pieces_.back().hit.size() < 8) {
            pieces_.back().hit.push_back({x, y, w, h});
        }
    });
    // TileAnswer(piece, slot): 0207aae4 -> 020faa04, orientation -1 (any).
    context.bind(0x5a, [&](CommandArguments& a) {
        auto p = unsigned(a.integer());
        auto slot = a.integer();
        if (answers_.size() < 32) {
            answers_.push_back({p, slot, -1});
        }
    });
    // TileSwap(): dropping on an occupied slot swaps (0207ad00 -> 020f8e1c, +225e = 1).
    context.bind(0x60, [&](CommandArguments&) { swap_ = true; });
    ScriptInterpreter script(bytes);
    script.run(context);
    for (const auto& p : pieces_) {
        if (p.initialSlot < 0 || unsigned(p.initialSlot) >= slots_.size() || p.sprite < 0 ||
            unsigned(p.sprite) >= sprites_.size()) {
            throw std::runtime_error("tile resource reference");
        }
    }
    // Rotating tiles keep their sprites in another folder.
    if (rotationParameters_) {
        for (auto& s : sprites_) {
            s.replace(0, 10, "nazo/tilerotate/");
        }
    }
    reset();
}

void TilePuzzle::reset() {
    // 020f8614: empty slots, every piece back in its starting slot and
    // orientation (020f7d10), and the drawing order by piece index.
    for (auto& s : slots_) {
        s.occupant = -1;
    }
    order_.clear();
    for (unsigned i = 0; i < pieces_.size(); ++i) {
        auto& p = pieces_[i];
        p.slot = p.initialSlot;
        p.rotation = p.initialRotation;
        p.displayAngle = p.targetAngle = degrees(p.rotation);
        p.animationState = 0;
        const auto& s = slots_.at(p.slot);
        p.x = s.x;
        p.y = s.y;
        p.displayX = float(p.x);
        p.displayY = float(p.y);
        p.minX = p.minY = p.maxX = p.maxY = 0;
        for (const auto& r : p.hit) {
            p.minX = std::min(p.minX, r.x);
            p.minY = std::min(p.minY, r.y);
            p.maxX = std::max(p.maxX, r.x + r.width - 1);
            p.maxY = std::max(p.maxY, r.y + r.height - 1);
        }
        slots_[p.slot].occupant = std::int16_t(i);
        order_.push_back(int(i));
    }
    selected_ = -1;
    state_ = stage_ = counter_ = secondaryCounter_ = brightness_ = 0;
    touchX_ = touchY_ = -1;
}

int TilePuzzle::hit(int x, int y) const {
    // 020f8e58: the front-most piece under the stylus.
    for (auto i = order_.rbegin(); i != order_.rend(); ++i) {
        if (contains(pieces_[*i], pieces_[*i].hit, x, y, rotationParameters_)) {
            return *i;
        }
    }
    return -1;
}

void TilePuzzle::pick(int index) {
    // 020f900c: remember where the piece was, free its slot and bring it to
    // the front.
    auto& p = pieces_.at(index);
    p.priorSlot = p.slot;
    p.priorRotation = p.rotation;
    p.priorX = p.x;
    p.priorY = p.y;
    if (p.slot >= 0 && slots_.at(p.slot).occupant == index) {
        slots_[p.slot].occupant = -1;
    }
    std::erase(order_, index);
    order_.push_back(index);
    selected_ = index;
    sounds.play(alternateSounds_ ? 0x111 : 0xf5);
}

void TilePuzzle::clamp(Piece& p) {
    // 020f968c / 020f9188: keep the piece's turned bounding box inside the
    // play area (x and y from 0 to 192 inclusive). The sun puzzle does not.
    if (specialMode_ == kSun) {
        return;
    }
    auto b = rotatedBounds(p.minX, p.minY, p.maxX, p.maxY, p.rotation);
    if (p.x + b[0] < 0) {
        p.x = std::int16_t(-b[0]);
    }
    if (p.y + b[1] < 0) {
        p.y = std::int16_t(-b[1]);
    }
    if (p.x + b[2] > 192) {
        p.x = std::int16_t(192 - b[2]);
    }
    if (p.y + b[3] > 192) {
        p.y = std::int16_t(192 - b[3]);
    }
}

int TilePuzzle::nearest(int x, int y, bool hadSlot) const {
    // 020f8f78: the nearest slot by squared distance, however far away. On a
    // tie the first slot wins. Occupied slots only count when swapping is on
    // and the piece came from a slot.
    int best = -1, distance = INT_MAX;
    for (unsigned i = 0; i < slots_.size(); ++i) {
        const auto& s = slots_[i];
        if ((hadSlot && swap_) || s.occupant < 0) {
            int dx = x - s.x, dy = y - s.y, d = dx * dx + dy * dy;
            if (d < distance) {
                distance = d;
                best = int(i);
            }
        }
    }
    return best;
}

void TilePuzzle::drop(int index) {
    // 020f9188: snap to the nearest slot. A piece already there moves to the
    // dropped piece's old slot. In key mode a dropped key is simply free.
    auto& p = pieces_.at(index);
    clamp(p);
    const int placed = alternateSounds_ ? 0x112 : 0xf6;
    if (specialMode_ == kKey) {
        p.slot = -1;
        sounds.play(placed);
        return;
    }
    int target = nearest(p.x, p.y, p.slot >= 0);
    if (target < 0) {
        return;
    }
    auto& s = slots_[target];
    if (s.occupant >= 0) {
        auto other = s.occupant;
        auto& q = pieces_.at(other);
        q.priorSlot = q.slot;
        q.priorRotation = q.rotation;
        q.slot = p.slot;
        auto& old = slots_.at(p.slot);
        old.occupant = other;
        q.x = old.x;
        q.y = old.y;
    }
    s.occupant = std::int16_t(index);
    p.slot = std::int16_t(target);
    p.x = s.x;
    p.y = s.y;
    sounds.play(placed);
}

bool TilePuzzle::recognizesKey(bool released) const {
    // 020f94d4: key mode. The key counts as pushed into the keyhole (the
    // first answer's slot) when it moved more than 3 pixels since it was
    // picked up and got closer to the keyhole, and then either it was
    // released within 24 pixels of the keyhole, or it is inside the approach
    // area in front of it (x >= 155 − w, y within ±h of 67, where the area
    // is wider when the key lies horizontally).
    if (specialMode_ != kKey || answers_.empty() || selected_ < 0) {
        return false;
    }
    const auto& p = pieces_[selected_];
    const auto& s = slots_.at(answers_[0].slot);
    auto squared = [](int x, int y) {
        return x * x + y * y;
    };
    if (squared(p.x - p.priorX, p.y - p.priorY) <= 9) {
        return false;
    }
    int distance = squared(s.x - p.x, s.y - p.y);
    if (squared(s.x - p.priorX, s.y - p.priorY) <= distance + 2) {
        return false;
    }
    if (released && distance < 576) {
        return true;
    }
    bool horizontal = p.rotation == 0 || p.rotation == 2;
    int w = horizontal ? 32 : 16, h = horizontal ? 16 : 32;
    return p.y >= 67 - h && p.y <= 67 + h && p.x >= 155 - w;
}

void TilePuzzle::insertKey() {
    // 020f95fc: put the key in the keyhole and play the insertion animation.
    // A wrong key does not lead to the "wrong" screen: it slides back out
    // (state kKeyRejected).
    auto& p = pieces_.at(selected_);
    p.slot = std::int16_t(answers_.at(0).slot);
    slots_.at(p.slot).occupant = std::int16_t(selected_);
    bool correct = matchesAnswers();
    state_ = correct ? kAccepted : kKeyRejected;
    p.animationState = correct ? 6 : 1;
    p.animationTick = -15;
    counter_ = 0;
}

void TilePuzzle::update(const Touch& touch, unsigned microphoneRange) {
    // 020f968c. The touch and microphone readings come from the host.
    pulseAngle_ += 0.1f; // the game-wide phase +82b0 advances every update
    switch (state_) {
    case kIdle:
        // Sun puzzle, microphone stage: after a 30-frame pause (with a sound
        // at frame 18), the player must keep blowing (loudness above 192)
        // for 31 frames in a row. Meanwhile the pieces shake.
        if (specialMode_ == kSun && stage_ == 2) {
            if (counter_ < 30) {
                if (counter_ == 0x12) {
                    sounds.play(0xf3);
                }
                counter_ = increment(counter_);
                return;
            }
            if (microphoneRange > 192) {
                secondaryCounter_ = increment(secondaryCounter_);
                // Shake by -3..2 pixels per frame (see jitterSeed_).
                auto jitter = [&]() {
                    jitterSeed_ = jitterSeed_ * 1664525u + 1013904223u;
                    return int((jitterSeed_ >> 16) % 6) - 3;
                };
                for (auto& p : pieces_) {
                    p.displayX += float(jitter());
                    p.displayY += float(jitter());
                }
            } else {
                secondaryCounter_ = 0;
            }
            if (secondaryCounter_ >= 31) {
                state_ = kAccepted;
                counter_ = secondaryCounter_ = 0;
                stage_ = 3;
                sounds.play(0xf4);
            }
            return;
        }
        // A press on a piece picks it up. Rotating pieces move when pressed
        // in their drag area and turn otherwise; the sun puzzle only allows
        // turning in stage 0 and moving in stage 1.
        if (!touch.pressed) {
            return;
        }
        if (int index = hit(touch.x, touch.y); index >= 0) {
            pick(index);
            auto& p = pieces_[index];
            if (!rotationParameters_) {
                state_ = kDrag;
            } else if (contains(p, p.dragHit, touch.x, touch.y, true)) {
                if (specialMode_ != kSun || stage_ == 1) {
                    state_ = kDrag;
                }
            } else if (specialMode_ != kSun || stage_ == 0) {
                state_ = kRotate;
            }
            if (state_ != kIdle) {
                sounds.play(0xf1); // drag or rotate mode set (+2250)
            }
            touchX_ = std::int16_t(touch.x);
            touchY_ = std::int16_t(touch.y);
        }
        return;
    case kDrag:
        if (!touch.held) {
            // Released: snap into a slot; a key may now be in the keyhole.
            if (selected_ >= 0) {
                drop(selected_);
                state_ = kIdle;
                touchX_ = touchY_ = -1;
                if (recognizesKey(true)) {
                    insertKey();
                }
                if (specialMode_ == kSun) {
                    // The sun dropped on slot 3 starts the microphone stage:
                    // sound 0xf2, and the music fades to 50/127 (the float
                    // 0.394) over 0.5 s.
                    if (pieces_[selected_].slot == 3) {
                        stage_ = 2;
                        counter_ = secondaryCounter_ = 0;
                        sounds.play(0xf2);
                        sounds.bgmVolume(std::bit_cast<float>(0x3ec99326u), 0.5f);
                    } else {
                        sounds.play(0xf0);
                    }
                }
            }
            selected_ = -1;
            return;
        }
        // Held: the piece follows the stylus (by its anchor for plain
        // tiles), at most to x = 192. The sun only moves vertically between
        // slots 3 and 2.
        if (selected_ >= 0) {
            auto& p = pieces_[selected_];
            auto anchor = rotatePoint(p.anchorX, p.anchorY, p.rotation);
            auto oldX = p.x;
            p.x = std::int16_t(std::min(touch.x, 192) - (rotationParameters_ ? 0 : anchor[0]));
            p.y = std::int16_t(touch.y - (rotationParameters_ ? 0 : anchor[1]));
            if (specialMode_ == kSun) {
                p.x = oldX;
                p.y = std::clamp(p.y, slots_.at(3).y, slots_.at(2).y);
            } else {
                clamp(p);
            }
            if (recognizesKey(false)) {
                insertKey();
                selected_ = -1;
            }
        }
        return;
    case kRotate:
        // The piece turns by the angle the stylus has swept around the
        // piece's position since the press, snapped to quarter turns.
        if (selected_ < 0) {
            return;
        }
        if (!touch.held) {
            drop(selected_);
            state_ = kIdle;
            touchX_ = touchY_ = -1;
            auto& p = pieces_[selected_];
            p.rotation = quantized(p.targetAngle);
            p.targetAngle = degrees(p.rotation);
            if (p.priorRotation != p.rotation && recognizesKey(false)) {
                insertKey();
            }
            // Sun puzzle: with both pieces upright, the sun can be moved.
            if (specialMode_ == kSun && pieces_[0].rotation == 0 && pieces_[1].rotation == 0) {
                stage_ = 1;
                sounds.play(0xf2);
            }
            selected_ = -1;
            return;
        } else {
            auto& p = pieces_[selected_];
            float initial = float(std::atan2(double(touchY_ - p.y), double(touchX_ - p.x)));
            float current = float(std::atan2(double(touch.y - p.y), double(touch.x - p.x)));
            // Radians to degrees; 0x40490fdb is the float value of pi.
            float delta =
                normalized(((current - initial) * 180.0f) / std::bit_cast<float>(0x40490fdbu));
            const int before = p.rotation;
            p.rotation = quantized(delta + degrees(p.priorRotation));
            p.targetAngle = normalized(degrees(p.rotation));
            if (specialMode_ == kSun && p.rotation != before) {
                sounds.play(0xf0);
            }
        }
        return;
    case kKeyholePress:
        // An old keyhole-button state kept from the original (nothing here
        // enters it).
        if (!touch.held) {
            sounds.play(0x6f);
            state_ = kIdle;
            touchX_ = touchY_ = -1;
            if (recognizesKey(true)) {
                insertKey();
            }
            selected_ = -1;
            return;
        }
        if (touch.x > 91 && touch.x < 140 && touch.y > 54 && touch.y < 79) {
            touchX_ = std::int16_t(touch.x);
            touchY_ = std::int16_t(touch.y);
            return;
        }
        selected_ = -1;
        state_ = kIdle;
        touchX_ = touchY_ = -1;
        return;
    case kKeyRejected: {
        // Wait for the wrong key's animation to finish, then put it back
        // where it was, in the first free slot from slot 3 on.
        auto& s = slots_.at(answers_.at(0).slot);
        if (s.occupant < 0) {
            return;
        }
        auto& p = pieces_.at(s.occupant);
        if (p.animationState) {
            return;
        }
        state_ = kIdle;
        s.occupant = -1;
        int free = 3;
        while (free < 6 && slots_[free].occupant >= 0) {
            ++free;
        }
        p.slot = std::int16_t(free);
        p.x = p.priorX;
        p.y = p.priorY;
        return;
    }
    case kAccepted:
        // Count frames; solved() waits for the animation to finish.
        counter_ = increment(counter_);
        secondaryCounter_ = increment(secondaryCounter_);
        return;
    default:
        return;
    }
}

void TilePuzzle::animate() {
    // 020fa344 -> 020f7dbc. Positions move 60 % of the way to the target per
    // frame (snapping within half a pixel), angles 40 % (snapping within a
    // degree).
    auto step = [](float value, float target) {
        return std::abs(target - value) > 0.5f ? target + (value - target) * 0.4f : target;
    };
    for (auto& p : pieces_) {
        float x = float(p.x), y = float(p.y);
        // Key insertion animation: the key moves to the keyhole (123, 67),
        // and is pushed 12 pixels further in during states 2..4.
        if (p.animationState && p.animationTick >= 0) {
            x = 123;
            y = 67;
        }
        if (p.animationState >= 2 && p.animationState <= 4) {
            x += 12;
        }
        p.displayX = step(p.displayX, x);
        p.displayY = step(p.displayY, y);
        float delta = normalized(p.targetAngle - p.displayAngle);
        p.displayAngle =
            std::abs(delta) < 1.0f ? p.targetAngle : normalized(p.displayAngle + delta * 0.4f);
        p.animationTick = increment(p.animationTick);
        // The animation's states, their durations in frames and successors:
        // a wrong key runs 1 -> 2 -> 3 -> 4 -> 5 -> 0 and ends back outside,
        // the right key runs 6 -> 7 and stays in.
        const int next[]{0, 2, 3, 4, 5, 0, 7, 7};
        const int duration[]{0, 16, 16, 22, 13, 16, 16, 0};
        if (p.animationState == 7 && p.animationTick == 0x19) {
            sounds.play(0x102); // 020f7dbc
        }
        if (p.animationState > 0 && p.animationState < 7 &&
            p.animationTick >= duration[p.animationState]) {
            if (p.animationState == 2) {
                sounds.play(0x101);
            } else if (p.animationState == 4) {
                sounds.play(0x103);
            }
            p.animationState = next[p.animationState];
            p.animationTick = 0;
        }
    }
    // Sun puzzle after the microphone stage: brighten the screen for about
    // 50 frames (stage 3), then return to normal (stage 4).
    if (specialMode_ == kSun) {
        if (stage_ == 3) {
            brightness_ = secondaryCounter_ / 3;
            if (brightness_ > 16) {
                stage_ = 4;
                secondaryCounter_ = 0;
                brightness_ = 16;
            }
        } else if (stage_ == 4) {
            brightness_ = 16 - secondaryCounter_ / 3;
            if (brightness_ < 0) {
                stage_ = 5;
                secondaryCounter_ = 0;
                brightness_ = 0;
            }
        }
    }
}

bool TilePuzzle::solved() const {
    // 020f8a48. The special modes are accepted by the engine itself, 90 or
    // 120 frames after the moment of success.
    if (specialMode_ == kKey) {
        return state_ == kAccepted && counter_ > 90;
    }
    if (specialMode_ == kSun) {
        return state_ == kAccepted && counter_ > 120;
    }
    return matchesAnswers();
}

bool TilePuzzle::matchesAnswers() const {
    // 020f8928: every answer must hold. (Answers naming a piece that does not
    // exist are skipped, as the original's unsigned comparison does.)
    for (const auto& a : answers_) {
        if (a.piece < pieces_.size() &&
            (pieces_[a.piece].slot != a.slot ||
             (a.rotation != -1 && a.rotation != pieces_[a.piece].rotation))) {
            return false;
        }
    }
    return true;
}
} // namespace picarat
