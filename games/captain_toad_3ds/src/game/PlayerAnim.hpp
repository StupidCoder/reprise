// Player animation timing (original game): the action-animation object 0x334e47d8 (vtable
// 0x4c1ef4), its frame mirror 0x334e490c and the skeletal animation player (anim info +0x28).
//
// Every player "action" (Wait, Walk, Run, Fall, Land, ...) plays an animation clip. The game logic
// often asks "has the clip ended?" to decide what to do next (for example, Land returns to Wait
// when its clip is over), so the exact frame timing changes the game's behaviour, not only its
// looks. The general frame controller is recovered in AnimFrame.hpp (docs/director.md);
// this is its single-clip form for the player's own action machine:
//   - start(action) (0x181fc8 -> 0x295640): frame 0, rate 1, the length and loop flag of the
//     action's first clip, and the "skip" flag set (0x294ff0). The mirror (+0x90 frame, +0x94 end,
//     +0x8c loop) is reset at once (0x1d818c).
//   - setRate(r) (0x2c6880): the skeleton advances 2*r frames per 30 Hz game tick (clips are
//     authored at 60 frames per second, the game logic runs at 30).
//   - Each tick, the action update first copies the skeleton into the mirror (0x1d8094). Game logic
//     queries read the mirror: isEnd (0x3e5b14) = !loop && frame >= end, frame (vt +0x24), end (+0x28).
//   - Then the player's movement (0x2d6e90 -> 0x229444 -> 0x294780) advances the skeleton by its
//     rate unless the skip flag is set, and clears the flag at its end (0x2d7304). So the first tick
//     after a start does not advance. Looping clips subtract the length while frame >= end; one-shot
//     clips stop at their end.
// The result: the logic sees the frame one tick behind what is drawn.
// Hypothesis (not yet traced): vtable +0x14 (0x3e2988) reports a one-shot clip.
#pragma once

#include <map>
#include <stdexcept>
#include <string>

#include "game/PlayerWalk.hpp"

namespace captain_toad::RecoveredPlayerAnim {

// Length and loop flag of an action's (first) clip, from the imported action table.
struct Clip {
    float frames = 0;
    bool loop = false;
};

struct Player : captain_toad::RecoveredPlayerWalk::AnimationPort {
    std::map<std::string, Clip> actions;   // action name -> its first clip's length and loop flag
    std::string action;                    // current action (anim +0x4c name)

    // The skeleton's frame controller.
    float frame = 0;
    float end = 0;
    float rate = 2.0f;   // frames per 30 Hz tick
    bool loop = true;
    bool fresh = false;   // the skip flag: set by a start, cleared by the next movement

    // The mirror the game logic reads (0x334e490c +0x90 frame, +0x94 end, +0x8c loop).
    float mirrorFrame = 0;
    float mirrorEnd = 0;
    bool mirrorLoop = true;

    float weights[6] = {1, 0, 0, 0, 0, 0};   // Move blend weights (0x181c88), see RecoveredPlayerWalk
    std::string missing;                     // last action without a clip (reported, not fatal)

    bool isPlaying(const std::string& name) const override { return action == name; }

    // 0x181fc8: start an action's clip from frame 0.
    void start(const std::string& name) override {
        action = name;
        auto it = actions.find(name);
        Clip clip = it == actions.end() ? Clip{1.0f, true} : it->second;
        if (it == actions.end()) {
            missing = name;
        }
        frame = 0;
        end = clip.frames;
        loop = clip.loop;
        rate = 2.0f;
        fresh = true;
        mirrorFrame = 0;
        mirrorEnd = end;
        mirrorLoop = loop;
    }

    void setBlendWeights(const float w[6]) override {
        for (int i = 0; i < 6; i++) {
            weights[i] = w[i];
        }
    }

    // 0x2c6880: playback speed relative to the authored speed.
    void setRate(float r) override { rate = 2.0f * r; }

    // Queries of the game logic (they read the mirror).
    bool isEnd() const { return !mirrorLoop && mirrorFrame >= mirrorEnd; }   // vtable +0x18
    float currentFrame() const { return mirrorFrame; }                       // vtable +0x24
    float length() const { return mirrorEnd; }                               // vtable +0x28
    bool isOneShot() const { return !mirrorLoop; }                           // vtable +0x14 (hypothesis)

    // 0x1d8094: the action update copies the skeleton into the mirror.
    void copyMirror() {
        mirrorFrame = frame;
        mirrorEnd = end;
        mirrorLoop = loop;
    }

    // 0x294780, called by the movement: advance unless this is the tick right after a start.
    void advance() {
        if (fresh) {
            fresh = false;
            return;
        }
        frame = frame + rate;
        if (loop) {
            if (frame >= end) {
                frame = frame - end;
            }
        } else if (frame > end) {
            frame = end;
        }
    }
};

}   // namespace captain_toad::RecoveredPlayerAnim
