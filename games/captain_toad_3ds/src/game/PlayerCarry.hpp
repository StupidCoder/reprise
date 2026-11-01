// =============================================================================================
// PlayerCarry.hpp - the protagonist carrying an object (turnip) over his head or in front
// =============================================================================================
//
// What the player sees: after pulling a turnip, the protagonist lifts it up (CarryUpStart), keeps
// it overhead while waiting and walking (CarryUp on the upper body), and on an action button
// throws it (CarryUpThrow), then goes back to normal.
//
// The carry controller (class vtable 0x4ca9fc, the player's +0x1b8; context word +8 kind, +0xc
// mode, read by the action graph through 0x3e9eac / 0x3e9e74):
//   start(kind) 0x1ee348: kind, mode 0; the upper-body clip of the kind's start column; the voice
//                "GrabHeavy" for kinds 0, 1 and 3.
//   throw 0x1ee23c: mode 2; the kind's throw clip; voice "CarryFrontThrow" (kind 0) or
//                "CarryUpThrow" (1, 3).
//   put 0x1ee1bc: mode 3; the kind's put clip (none for these kinds: then end at once).
//   end 0x1ee6f4: the upper-body clip stops; kind and mode 5 (nothing carried).
//   update 0x1ee4f8 (each tick from the player's carry update 0x1581d4): in mode 0 once the start
//                clip has ended, the loop clip (mode 1); in modes 2 and 3 once the clip has
//                ended, end.
// Clip table 0x480720 (per kind: start, loop, throw, put, storm):
//   0 front: CarryFrontStart, CarryFront, CarryFrontThrow    1 up: CarryUpStart, CarryUp, CarryUpThrow
//   3 up (no start): -, CarryUp, CarryUpThrow                (2 and 4: shout / storm, not carrying)
// The upper-body clips play in the model's partial animation slot 0 (0x181d14 -> 0x2c5a2c ->
// 0x229198) on partial group 0, the joint Spine1 and everything below it (0x29dbd8 with
// "Spine1"; InitModel PartialAnimSlotNum 1); the action clip keeps the rest. Curve clips advance
// 2 frames per tick (anim player start 0x294efc).
// =============================================================================================
#pragma once

#include <string>

namespace captain_toad::RecoveredPlayerCarry {

struct ClipRow {
    const char* start;
    const char* loop;
    const char* throwClip;
    const char* put;
};
inline constexpr ClipRow kClips[5] = {
    {"CarryFrontStart", "CarryFront", "CarryFrontThrow", nullptr},
    {"CarryUpStart", "CarryUp", "CarryUpThrow", nullptr},
    {nullptr, nullptr, nullptr, nullptr},
    {nullptr, "CarryUp", "CarryUpThrow", nullptr},
    {nullptr, nullptr, nullptr, nullptr},
};

struct Controller {
    int kind = 5;   // +8 (5: nothing carried)
    int mode = 5;   // +0xc
    // The partial slot.
    std::string upperClip;
    float upperFrame = 0;
    float upperFrames = 0;
    bool upperLoop = false;
    std::string voice;   // a voice requested this tick (PlayInfo of the player's sound entry)

    bool carrying() const { return kind != 5; }
    bool upperEnded() const { return upperClip.empty() || (!upperLoop && upperFrame >= upperFrames); }

    template <class ClipInfo>
    void play(const char* name, const ClipInfo& info) {
        upperClip = name ? name : "";
        upperFrame = 0;
        upperFrames = 0;
        upperLoop = false;
        if (name) {
            info(upperClip, upperFrames, upperLoop);
        }
    }
    template <class ClipInfo>
    void start(int k, const ClipInfo& info) {   // 0x1ee348
        kind = k;
        mode = 0;
        play(kClips[k].start, info);
        if (k == 0 || k == 1 || k == 3) {
            voice = "GrabHeavy";
        }
    }
    template <class ClipInfo>
    void throwIt(const ClipInfo& info) {   // 0x1ee23c
        if (kind == 5) {
            return;
        }
        if (!kClips[kind].throwClip) {
            end();
            return;
        }
        play(kClips[kind].throwClip, info);
        mode = 2;
        voice = kind == 0 ? "CarryFrontThrow" : "CarryUpThrow";
    }
    void end() {   // 0x1ee6f4
        upperClip.clear();
        kind = 5;
        mode = 5;
    }
    // Advance the slot by one tick and run 0x1ee4f8. The loop clip follows the start clip's end;
    // a kind without a start clip (3, the turnip taken from the pull) waits for the action clip:
    // it starts once the action clip has ended (anim vtable +0x18, 0x3e5b14) or, for kind 3,
    // once the action is no longer PullOutSuccessCarry (+0x1c, 0x3e29fc). Traced: no partial clip
    // starts between the grab at 1237 and the throw at 1261 of the turnip route, which throws
    // before PullOutSuccessCarry ends.
    template <class ClipInfo>
    void update(const ClipInfo& info, bool actionEnded, bool pullOutCarryPlaying) {
        if (!upperClip.empty()) {
            upperFrame += 2.0f;
            if (upperLoop && upperFrames > 0) {
                while (upperFrame >= upperFrames) {
                    upperFrame -= upperFrames;
                }
            }
        }
        if (kind == 5) {
            return;
        }
        if (mode == 2 || mode == 3) {
            if (upperEnded()) {
                end();
            }
            return;
        }
        const bool startDone = kClips[kind].start ? upperEnded() : actionEnded || (kind == 3 && !pullOutCarryPlaying);
        if (mode == 0 && startDone && kClips[kind].loop) {
            play(kClips[kind].loop, info);
            mode = 1;
        }
    }
};

}   // namespace captain_toad::RecoveredPlayerCarry
