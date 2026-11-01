// Frame controllers of the original's skeletal and layout animations, and their place in the game
// tick, recovered from code.bin. They replace the per-actor "hold" constants of earlier ports.
// Evidence: docs/director.md; runs anim-frame-writes-a, anim-skip-writes-a, anim-pose-a,
// actor-order-a, layout-writes-a/-b, layout-exec-a/-b.
//
// Why this file exists: the opening's choreography (the goal pose, the camera cuts, the title)
// is timed by "wait until this animation has ended". Whether an animation advances in the tick it
// starts, and whether the drawing shows the frame before or after the advance, decides on which
// tick each event happens. These rules reproduce the original's order exactly.
//
// Skeletal animation (actor +0x4c -> anim info +0x28 -> player object, slots +0x1c):
//   - start 0x295640 (via 0x294efc, called for each blend slot by 0x295a.. and 0x2984..): frame 0,
//     playing (flag bit 1), not ended. The player object's skip flag (+0x10) is set (0x294ff0) and
//     the pose is computed at once (tail call 0x295b80 -> 0x294aec calc).
//   - update 0x229444 (from 0x14edf0, the first step of the generic actor movement 0x2d6e90): with
//     the skip flag set it returns at once (no advance, no pose). Otherwise every slot advances
//     (0x294780: frame += rate; at the end a one-shot clip stops at its end, a looping clip
//     subtracts the length while frame >= end) and the pose is computed (0x294aec).
//   - The movement then runs the actor's nerve (its state machine) and control, and finally clears
//     the skip flag (0x2d7304 at its end; the player's tick clears it at 0x15c974 the same way).
//   Consequence: a start from the actor's own nerve is advanced on the next tick, while a start
//   from an actor that moves later in the tick (the demos move after the characters, actor-order-a)
//   keeps its frame and pose for one more tick. The drawn pose is the skeleton at its last
//   computation.
//   Rate: 1 per 60 Hz frame, so 2 per 30 Hz tick at action rate 1 (frames in 60 Hz units).
//
// Player mirror (0x334e490c): the player's action update copies the skeleton frame (0x1d8094)
//   before the movement advances the skeleton, and a start resets it to 0 (0x1d818c). The game
//   logic (isActionEnd 0x3e5b14, the action keepers) reads the mirror, one tick behind the drawing.
//
// Layout animation (2D screens such as the course-clear banner and the title logo; nw lyt frame
// controller: +0x10 frame, +0x24 step, +0x28 play type):
//   - bind 0x382b00: frame 0.
//   - Each layout's calc 0x37f328 first applies the current frames to the panes (0x35cd3c) and then
//     advances them by delta 2.0 (0x35caec -> 0x35f330: f = frame + step * 2). When f reaches the
//     animation's frame count, a one-time play stops: step 0, frame = count, end flag; the layout
//     actor's isAnimEnd (0x2fa6c4) is true from then on.
//   - The layout pass runs early in the game frame: after the scene and sequence code, before the
//     actors and demos (layout-exec-a/-b). At the stage start one emulator call holds the
//     sequence's frame, which opens the iris before its layout pass, and the scene's first frame
//     (layout-exec-b).
//   Consequence: an animation bound by an actor or demo nerve is drawn at frame 0 on the next two
//   frames; one bound by the scene/sequence code before the pass is drawn at frame 0 in the same
//   frame.
//   Duration-fitted plays (0x2f8a5c / 0x2f8b64 with a frame count): step = animation frames /
//   requested frames (0x2f8b34).
#pragma once

#include <string>

namespace captain_toad::RecoveredAnimFrame {

// One skeletal animation player (single slot) with the movement's skip flag.
struct Skeletal {
    std::string name;
    float frame = 0;
    float end = 0;
    float rate = 2.0f;   // frames per 30 Hz tick
    bool loop = false;
    bool playing = false;
    bool skip = false;   // set by start(), cleared at the end of the actor's movement
    int age = -1;        // ticks since the start (0 on the start tick); bookkeeping only
    float elapsed = 0;   // unclamped advance (material animations loop on it)

    // 0x295640 / 0x294ff0: start a clip.
    void start(const std::string& clipName, float length, bool looping = false) {
        name = clipName;
        end = length;
        loop = looping;
        frame = 0;
        elapsed = 0;
        playing = true;
        skip = true;
        age = 0;
    }

    // Movement step 1 (0x229444 -> 0x294780).
    void update() {
        if (age >= 0) {
            ++age;
        }
        if (skip || age < 0) {
            return;
        }
        elapsed = elapsed + rate;   // the actor's other animation players (materials) keep going
        if (!playing) {
            return;
        }
        frame = frame + rate;
        if (frame < end) {
            return;
        }
        if (loop && end > 0.0f) {
            do {
                frame = frame - end;
            } while (frame >= end);
        } else {
            frame = end;
            playing = false;
        }
    }

    // 0x2d7304: the end of the actor's movement clears the skip flag.
    void endMovement() { skip = false; }

    bool running() const { return age >= 0; }
    bool ended() const { return running() && !loop && frame >= end; }
};

// The player's frame mirror read by the game logic (0x1d8094 copy, 0x1d818c reset).
struct Mirror {
    float frame = 0;
    void copy(const Skeletal& s) { frame = s.frame; }
    void reset() { frame = 0; }
};

// One layout animation (frame controller of an animation transform, one-time play type 0).
struct Layout {
    std::string name;
    float frame = 0;
    float step = 1.0f;   // per 60 Hz frame
    float count = 0;     // the animation's frame count
    float shown = 0;     // frame applied to the panes in this game frame
    // The animation applied in the last calc: after a bind the panes keep it until the next layout
    // pass applies the new one (an animation switched by a nerve shows on the next frame).
    std::string shownName;
    float shownCount = 0;
    bool bound = false;
    bool end = false;   // the advance reached the count (+0x2c bit 0)

    // 0x382b00: bind an animation and start it at frame 0.
    void bind(const std::string& animationName, float frameCount, float stepPerFrame = 1.0f) {
        name = animationName;
        count = frameCount;
        step = stepPerFrame;
        frame = 0;
        bound = true;
        end = false;
    }

    // Layout calc 0x37f328: apply the frame (0x35cd3c), then UpdateAnimFrame(2.0) (0x35caec -> 0x35f330).
    void calc() {
        if (!bound) {
            return;
        }
        shown = frame;
        shownName = name;
        shownCount = count;
        if (step == 0.0f) {
            return;
        }
        float f = frame;
        f = f + step * 2.0f;
        if (count > f) {
            frame = f;
            return;
        }
        // One-time play: stop at the count.
        step = 0.0f;
        frame = count;
        end = true;
    }
};

}   // namespace captain_toad::RecoveredAnimFrame
