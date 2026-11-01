// =============================================================================================
// StageAssist.hpp - map parts the player moves with the touch screen (StageObjects.hpp)
// =============================================================================================
//
// What the player sees: some blocks glow with a touch symbol; holding a finger on one makes it
// slide out along one axis to its other position, and holding it again slides it back.
//
// AssistSlideMapParts (creator 0x453714, constructor 0x1ce2bc, vtable 0x4c80cc; init 0x1cd8a0,
//   receiveMsg 0x1cd54c, control 0x1cdfc4; units such as TouchPuzzleMiniTouchPartsA/B, whose
//   collision material code "TouchPoint" makes them touch targets).
//   Placement: MoveAxis (local axis 0 / 1 / 2), MoveDistance, MoveSpeed (x 2, literal 0x1cdc8c),
//   MoveTime and OnWaitTime (ticks x 0.5 when not negative, 0x1cdc90), MoveType (0: returns by
//   itself after OnWaitTime; 1: stays until touched again; 2: no return), GuideBalloonType.
//   Named nerves (table 0x500b90, descriptors 0x4729ec..0x472a50), each also the action of its
//   name (sounds On / Off / OnWait, effects MoveOn / MoveOff):
//     OffWait 0x428d94: progress 0 (the balloon logic of GuideBalloonType is not ported).
//     On 0x1cdd50: progress = step / frames clamped to [0, 1]; frames = MoveTime, or
//       int(|MoveDistance / MoveSpeed|) (0 below a speed of 1); the sliding loop (keeper
//       "SwitchSlidingOn") runs; at step >= frames -> OnWait.
//     OnWait 0x428ca0: progress 1; with MoveType 0, after OnWaitTime steps -> OffSign.
//     OffSign 0x428d38: after 59 steps -> Off.
//     Off 0x1cde7c: progress = 1 - step / frames; at the end the hit reaction "OffEnd" ->
//       OffWait.
//     Stop 0x428c90: nothing (set by the SwitchStopForceOn listener 0x1ce2ac).
//   The touch (0x2abab0, from the hold message 0x31af10 or the tap message 0x31af40): in
//   OffWait -> On; in OnWait or OffSign after their first step, unless MoveType is 2: the hit
//   reaction "TouchOff" -> Off. A part in a group (+0x90) passes the touch to every member
//   (0x2005c0). The hold message counts for two ticks (+0x110) and starts one move per hold
//   (+0x114).
//   control 0x1cdfc4: translation = the placement's + its rotation's MoveAxis column (0x285348) x
//   MoveDistance x progress (0x2ab930); the rotation stays the placement's.
//   Observed (run touchpuzzle-start-a, a held touch on the part from frame 10): the hold message
//   at frame 18, On frames 18..38 (10 ticks: 400 / 40), OnWait from frame 40.
// =============================================================================================
#pragma once

#include "game/StageObjects.hpp"

#include <algorithm>
#include <cmath>

namespace captain_toad::RecoveredStageAssist {

using namespace captain_toad::RecoveredStage;

struct AssistSlideMapParts : Behaviour {
    enum class Nerve { OffWait, On, OnWait, OffSign, Off, Stop } nerve = Nerve::OffWait;
    int step = 0;
    int axis = 0, moveType = 0, moveTime = -1, onWaitTime = -1;
    float distance = 0, speed = 0, progress = 0;
    int holdTicks = 0;       // +0x110: the hold message counts for two ticks
    bool holdUsed = false;   // +0x114: one move per hold
    V3 base{}, direction{};

    explicit AssistSlideMapParts(Object& o) {
        o.nerveDriven = true;   // named nerves (init 0x1cd8a0 -> 0x2cdb24)
        axis = int(o.number("MoveAxis", 0));
        distance = float(o.number("MoveDistance", 0));
        speed = float(o.number("MoveSpeed", 0)) * 2.0f;
        moveTime = int(o.number("MoveTime", -1));
        onWaitTime = int(o.number("OnWaitTime", -1));
        moveType = int(o.number("MoveType", 0));
        if (moveTime >= 0) {
            moveTime = int(float(moveTime) * 0.5f);
        }
        if (onWaitTime >= 0) {
            onWaitTime = int(float(onWaitTime) * 0.5f);
        }
        base = o.translation();
        const auto& m = o.placement.m;   // the rotation's column `axis` (0x285348), unit length
        direction = {m[axis], m[4 + axis], m[8 + axis]};
        const float n = std::sqrt(captain_toad::RecoveredVec::dot(direction, direction));
        if (n > 0) {
            direction = captain_toad::RecoveredVec::scale(direction, 1.0f / n);
        }
        enter(o, Nerve::OffWait);
    }

    static const char* nameOf(Nerve n) {
        switch (n) {
        case Nerve::OffWait:
            return "OffWait";
        case Nerve::On:
            return "On";
        case Nerve::OnWait:
            return "OnWait";
        case Nerve::OffSign:
            return "OffSign";
        case Nerve::Off:
            return "Off";
        default:
            return "Stop";
        }
    }
    // A named nerve (0x2fd600) is also the action of that name.
    void enter(Object& o, Nerve n) {
        nerve = n;
        step = 0;
        o.startAction(nameOf(n));
        if (getenv("DIORAMA_DEBUG")) {
            fprintf(stderr, "assist %s -> %s\n", o.id.c_str(), nameOf(n));
        }
    }
    int frames() const {
        if (moveTime >= 0) {
            return moveTime;
        }
        if (speed < 1.0f) {
            return 0;
        }
        return int(std::fabs(distance / speed));
    }
    static float rate(int step, int frames) {
        if (frames <= 0) {
            return 1.0f;
        }
        return std::min(std::max(float(step) / float(frames), 0.0f), 1.0f);
    }

    bool touchTarget() const override { return true; }
    // 0x2abab0: the touch toggles the part.
    bool assistTouch(World& w, Object& o) override {
        if (nerve == Nerve::OffWait) {
            enter(o, Nerve::On);
            return true;
        }
        if (moveType != 2 && (nerve == Nerve::OnWait || nerve == Nerve::OffSign) && step > 0) {
            w.reaction(o, "TouchOff");
            enter(o, Nerve::Off);
            return true;
        }
        return false;
    }
    // receiveMsg 0x1cd54c: the hold message (0x313ae8) counts for two ticks and moves the part once.
    bool assistHold(World& w, Object& o) override {
        holdTicks = 2;
        if (holdUsed) {
            return false;
        }
        holdUsed = true;
        return assistTouch(w, o);
    }

    void control(World& w, Object& o) override {
        if (nerve != Nerve::Stop && w.switchOn(o, "SwitchStopForceOn")) {   // listener 0x1ce2ac
            enter(o, Nerve::Stop);
        }
        run(w, o);
        o.nerveStep = step;
        // 0x1cdfc4 / 0x2ab930: the part along its axis.
        o.world = o.placement;
        const float d = distance * progress;
        o.world.m[3] = base.x + direction.x * d;
        o.world.m[7] = base.y + direction.y * d;
        o.world.m[11] = base.z + direction.z * d;
        if (getenv("DIORAMA_DEBUG") && (nerve == Nerve::On || nerve == Nerve::Off)) {
            fprintf(stderr, "assist %s at (%.1f, %.1f, %.1f) tick %ld\n", o.id.c_str(), o.world.m[3], o.world.m[7],
                    o.world.m[11], w.tickCount);
        }
        if (--holdTicks < 0) {
            holdUsed = false;
        }
    }
    void run(World& w, Object& o) {
        switch (nerve) {
        case Nerve::OffWait:   // 0x428d94
            progress = 0.0f;
            break;
        case Nerve::On:   // 0x1cdd50
            progress = rate(step, frames());
            if (frames() <= step) {
                enter(o, Nerve::OnWait);
                progress = 1.0f;
                return;
            }
            break;
        case Nerve::OnWait:   // 0x428ca0
            progress = 1.0f;
            if (moveType == 0 && onWaitTime <= step) {
                enter(o, Nerve::OffSign);
                return;
            }
            break;
        case Nerve::OffSign:   // 0x428d38
            if (step > 59) {
                enter(o, Nerve::Off);
                return;
            }
            break;
        case Nerve::Off:   // 0x1cde7c
            progress = 1.0f - rate(step, frames());
            if (frames() <= step) {
                w.reaction(o, "OffEnd");
                enter(o, Nerve::OffWait);
                progress = 0.0f;
                return;
            }
            break;
        case Nerve::Stop:
            break;
        }
        step++;
    }
};

inline const Register<AssistSlideMapParts> kAssistSlideMapParts{"AssistSlideMapParts"};

}   // namespace captain_toad::RecoveredStageAssist
