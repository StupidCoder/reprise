#pragma once
// Tile placement and rotating tiles (puzzle types 26 and 18), ARM9 overlay 48.
//
// How the puzzles play
// --------------------
// Both types show picture pieces lying in slots (fixed positions on the lower
// screen). The player picks a piece up with the stylus and drops it; it
// snaps to the nearest free slot (or, if the puzzle allows swapping, to the
// nearest slot, exchanging places with the piece there). When the player
// presses "Submit", the answer is right if every piece listed in the answer
// lies in its slot (and, for rotating tiles, in its orientation).
//
//  * Type 26, tile placement: pieces only move.
//  * Type 18, rotating tiles: pieces also turn in quarter turns. Pressing
//    inside a piece's "drag area" moves it; pressing elsewhere on the piece
//    turns it, following the stylus around the piece's centre and snapping to
//    the nearest quarter turn.
//
// Two puzzles use special modes of the same engine (chosen by internal ID):
//
//  * Internal puzzle 2 ("key" mode): a key piece has to be pushed into a
//    keyhole on the right (around x = 155, y = 67). The engine recognises
//    the push from the key's movement, plays an insertion animation and
//    accepts the answer by itself when the key is right; a wrong key slides
//    back out to a free slot.
//  * Internal puzzle 203 ("sun" mode): in stages, two pieces are turned
//    upright, then the sun piece is dragged up to its slot, and then the
//    player must blow into the microphone long enough. The screen brightens
//    and the puzzle is accepted automatically.
//
// The parameter script supplies sprites (command 0x54), slots (0x55), pieces
// (0x57, or 0x58 for rotating pieces), their hit and drag areas (0x59,
// 0x6d), answers (0x5a, or 0x5b with an orientation) and the swap option (0x60).
#include "system/effect_queue.hpp"
#include "system/interaction.hpp"
#include "system/script.hpp"
#include <string>
#include <utility>
#include <vector>

namespace picarat {

class TilePuzzle {
public:
    struct Slot {
        std::int16_t x = 0, y = 0;
        std::int16_t occupant = -1; // piece in the slot, -1 = empty
    };

    struct Piece {
        std::int16_t initialSlot = -1, slot = -1; // slot -1: not in a slot
        std::int16_t anchorX = 0, anchorY = 0;    // grab point inside the sprite
        std::int16_t x = 0, y = 0;                // logical position
        float displayX = 0, displayY = 0;         // drawn position (eased)
        float displayAngle = 0, targetAngle = 0;  // degrees, eased towards the target
        int animationState = 0;                   // key insertion animation (key mode)
        std::int16_t animationTick = 0;
        // Where the piece was when it was picked up.
        std::int16_t priorSlot = -1, priorX = -1, priorY = -1;
        int priorRotation = 0;
        int sprite = 0;                        // index into sprites()
        std::string idle, held, released;      // animation names
        std::vector<Rect> hit;                 // where a press picks the piece up (at most 8)
        std::vector<Rect> dragHit;             // rotating tiles: where a press moves it
        int initialRotation = 0, rotation = 0; // quarter turns, 0..3
        // Bounding box of the hit areas, relative to the piece position.
        int minX = 0, minY = 0, maxX = 0, maxY = 0;
    };

    // Piece `piece` must be in slot `slot` (and in `rotation`, unless -1).
    struct Answer {
        unsigned piece;
        int slot, rotation = -1;
    };

    // `internalId` selects the special modes and alternative sounds.
    explicit TilePuzzle(std::span<const std::uint8_t> script, unsigned internalId = 0);

    // Every piece back to its starting slot and orientation (020f8614).
    void reset();

    // One frame of input (020f968c). `microphoneRange` is the microphone's
    // loudness for the sun puzzle.
    void update(const Touch& touch, unsigned microphoneRange = 0);

    // One frame of easing and animation (020fa344 -> 020f7dbc).
    void animate();

    // The answer check (020f8a48).
    bool solved() const;

    const std::vector<Slot>& slots() const { return slots_; }

    const std::vector<Piece>& pieces() const { return pieces_; }

    // Piece indices back to front; the piece picked up last is drawn on top.
    const std::vector<int>& drawOrder() const { return order_; }

    const std::vector<std::string>& sprites() const { return sprites_; }

    // For research and validation.
    const std::vector<Answer>& answers() const { return answers_; }

    int selected() const { return selected_; }

    bool dragging() const { return state_ != kIdle; }

    bool rotating() const { return rotationParameters_; }

    // The special modes judge themselves, without "Submit".
    bool automatic() const { return specialMode_ != kNormal; }

    int state() const { return state_; }

    int stage() const { return stage_; }

    int counter() const { return counter_; }

    int secondaryCounter() const { return secondaryCounter_; }

    int specialMode() const { return specialMode_; }

    // Sun mode: screen brightening (0..16).
    int brightness() const { return brightness_; }

    // Phase of the keyhole's pulsing highlight.
    float pulseAngle() const { return pulseAngle_; }

    // Sound effects, played by the puzzle scene.
    EffectQueue sounds;

private:
    // Special modes.
    static constexpr int kNormal = 0;
    static constexpr int kKey = 1; // internal puzzle 2
    static constexpr int kSun = 2; // internal puzzle 203

    // Engine states.
    static constexpr int kIdle = 0;
    static constexpr int kDrag = 1;
    static constexpr int kRotate = 2;
    static constexpr int kKeyholePress = 3; // kept from the original; not entered here
    static constexpr int kKeyRejected = 4;  // a wrong key slides back out
    static constexpr int kAccepted = 5;     // waiting before the automatic judgment

    std::vector<Slot> slots_;     // at most 64
    std::vector<Piece> pieces_;   // at most 32
    std::vector<Answer> answers_; // at most 32
    std::vector<std::string> sprites_;
    std::vector<int> order_;
    bool swap_ = false;               // dropping on an occupied slot swaps (command 0x60)
    bool rotationParameters_ = false; // rotating tiles (type 18)
    bool alternateSounds_ = false;    // internal puzzles 0x3e and 0x73
    // Phase of the pulse. The original keeps one game-wide phase (+82b0);
    // here it starts from zero when the puzzle opens.
    float pulseAngle_ = 0;
    int specialMode_ = kNormal;
    int state_ = kIdle;
    int stage_ = 0; // progress through the sun puzzle
    int brightness_ = 0;
    std::int16_t counter_ = 0, secondaryCounter_ = 0;
    std::int16_t touchX_ = -1, touchY_ = -1; // where the stylus was pressed
    // Seed for the sun puzzle's visual shaking. The original uses the
    // game-wide random generator; only the range and easing matter visually.
    std::uint32_t jitterSeed_ = 1;
    int selected_ = -1;

    bool matchesAnswers() const;
    bool recognizesKey(bool released) const;
    void insertKey();
    int hit(int x, int y) const;
    int nearest(int x, int y, bool hadSlot) const;
    void pick(int index);
    void drop(int index);
    void clamp(Piece& piece);
};

} // namespace picarat
