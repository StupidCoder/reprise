// =============================================================================================
// StageDecor.hpp - decorations that only play their idle animation (StageObjects.hpp)
// =============================================================================================
//
// What the player sees: a poster on a wall, a pixel-art figure and the course star play their
// idle animation; touched, they would react (not ported yet). The double cherry and the paired
// floor switches of DoubleCherryRuinsStage only wait, too, until their course is ported.
//
// Poster (creator 0x4541c8, vtable 0x4cf780, init 0x3ba558): Wait 0x40be30 starts "Wait" (and the
//   same action on its sub-model). Touch (+0x68 0x3ba4f8) "Touch": not ported.
// DotKinopio (creator 0x452774, vtable 0x4bc768, init 0x138d7c): Wait 0x412bf0 starts "Wait"; the
//   rest of that nerve plays the "VoiceHere" sound by distance. Touch (+0x68 0x138ba8, the jumps
//   and walks of its archive): not ported. The init kills it when a count (0x2b8bcc) is positive
//   and a flag (0x2f8cd8) is clear; in run loadB (the first visit) it starts no action at all, so it
//   is dead there: the port leaves it out (when it appears is not decoded).
// =============================================================================================
#pragma once

#include "game/StageObjects.hpp"

namespace captain_toad::RecoveredStageDecor {

using namespace captain_toad::RecoveredStage;

// A class whose only ported nerve starts one idle action ("Wait" unless named).
struct IdleAction : Behaviour {
    const char* action;
    bool started = false;
    explicit IdleAction(Object&, const char* name = "Wait") : action(name) {}
    void control(World&, Object& o) override {
        if (!started) {
            o.startAction(action);
            started = true;
        }
    }
};

inline const Register<IdleAction> kPoster{"Poster"};
// The course star (GoalItem, 0x4d06d4): "Wait" from its start (run loadB, frame 1149, caller
// 0x2ed584). Touching it is the goal (session, game/Goal.hpp).
inline const Register<IdleAction> kGoalItem{"GoalItem"};
// The goal's demo star (goal object init 0x1ec824: a second GoalItem model; the exporter's
// "<star>/demo" at the goal object): hidden until GoalPose step 0 shows it with the pose
// (session courseGoalMirror, game/Goal.hpp).
struct GoalDemoStar : Behaviour {
    explicit GoalDemoStar(Object& o) {
        o.alive = false;
        o.visible = false;
    }
};
inline const Register<GoalDemoStar> kGoalDemoStar{"GoalDemoStar"};
struct DotKinopio : IdleAction {
    explicit DotKinopio(Object& o) : IdleAction(o) { o.alive = false; }
};
inline const Register<DotKinopio> kDotKinopio{"DotKinopio"};

// The double cherry ("DoubleMario"; creator 0x452ab0 -> ctor 0x2f19dc, vtable 0x4bdd8c, init
//   0x152418, archive "DoubleItem"): its first nerve 0x415d10 plays "Wait" and stops. Taking it
//   (receiveMsg 0x152014: a player sensor, 0x2e2b14) makes the second protagonist; that and its
//   other nerves (Land 0x415ca8, ...) are open, so the port shows it waiting only.
inline const Register<IdleAction> kDoubleCherry{"DoubleMario"};

// TrampleSwitchTogether (協力踏みスイッチ, ctor 0x1efe18, vtable 0x4cacc8, init 0x1efc74): the
//   switches of a pair, created by their TrampleSwitchTogetherWatcher (init 0x21a834, one per
//   TrampleSwitchTogetherList link). Nerves (0x4f6258..): the first, 0x42d4d0, plays "OffWait" and
//   counts the ticks the switch is stepped on (+0x94 / +0x98); On "On" (hit stop), OnWait, Off,
//   Success. Only the first nerve's look is ported: the pressing, the watcher's both-at-once rule
//   and its SwitchTrampleAllOn are open (they need the double cherry's second protagonist).
struct TrampleSwitchTogether : IdleAction {
    explicit TrampleSwitchTogether(Object& o) : IdleAction(o, "OffWait") {}
};
inline const Register<TrampleSwitchTogether> kTrampleSwitchTogether{"TrampleSwitchTogether"};

}   // namespace captain_toad::RecoveredStageDecor
