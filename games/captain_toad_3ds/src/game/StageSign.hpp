// =============================================================================================
// StageSign.hpp - the wooden direction sign and its snow cover (StageObjects.hpp)
// =============================================================================================
//
// What the player sees: a wooden sign with one painted arrow pointing the way, in snowy courses
// with a cap of snow. Tapping it, hitting it with a thrown turnip or the pickaxe, or an explosion
// breaks it: it bursts into pieces with a crack, the snow drops off, and a stump stays behind.
//
// SignBoard (creator 0x454714, vtable 0x4d1578, init 0x3dd4c8; model SignBoardWood; sensor Body
// MapObj r160 y170): the model carries all eight arrows (SignYUpper, SignYUnder, SignXLeft,
// SignXRight and the diagonals); its visibility animation "SignAim" shows one of them per frame.
// The init reads the placement's ModelName (0x2eec30) and tests it for the direction names of the
// table 0x4f60c8 from the last entry down (0x30b13c, a substring test, so "RightUp" is found
// before "Up"): Up 0, Down 1, Left 2, Right 3, RightUp 4, RightDown 5, LeftUp 6, LeftDown 7,
// none -1. 0x2ab384 starts "SignAim" at that frame and holds it (rate 0). PullUpStage's sign is
// SignBoardWoodLeft: frame 2, SignXLeft.
//   Only the model SignBoardWood breaks (+0x8f). The init makes the snow cover with IsSnowCover
//   (0x2c9590, +0x94) and, unless the sign is scaled up (+0xa0; pose +0x1c > 1.0), finds its
//   sub-actors (StageSubActors.hpp) 木製看板壊れモデル (BreakModel SignBoardWoodBreak, +0x98) and
//   木製看板残留モデル (SignBoardWoodTrace, +0x9c).
//   receiveMsg 0x3dd200 (not in the break nerve): a kicked shell (0x2cea54, reaction
//   キック甲羅ヒット; no shell in this port) or one of 17 attacks (0x2cecac, 0x2eabc4, 0x2fc6d0 the
//   pickaxe, 0x2f2c28, 0x2fb418, 0x2d3820, 0x2f2984 a thrown object, 0x2f2a74, 0x2d0d90,
//   0x2cec34, 0x2f0a88 the explosion of 0x2ec0d4, 0x2ced8c, 0x2ceacc, 0x263d68, 0x2c8430,
//   0x2f02d4, 0x2ceb44) -> the message's reaction (0x2f4408; none in the sign's table), the score
//   壊れ (0x2fb36c; not ported) and the break nerve. The player's touch is not among them.
//   touch 0x3dd47c (vtable +0x68): the screen touch -> the break nerve.
//   break nerve 0x411230 (one step): reaction 破壊 (effect Break, HrBreak) or, scaled up, 拡大破壊
//   (BreakScale); the snow cover breaks (0x2ca27c); the break model appears (vtable +0x10); the trace
//   starts "Appear" (0x2fd7b0) and appears; the sign is killed (0x2f7b04).
// SignBoardSnowCover (vtable 0x4d1630, init 0x3dd830; model SignBoardSnowCover, its own
// EffectYaml / SeUser SnowCover; sensors Body1 / Body2 MapObj r70): at the sign's placement.
//   Wait 0x41130c (nothing). The sign's break (0x2ca27c, in Wait and unless +0x78 [0]) -> Break
//   0x411310: action "Break" (effect and sound only: no clip of that name), killed at its end.
//   receiveMsg 0x3dd7dc in Wait: the message 0x2f2c28 (tag 0x4fe068, sent only by RideDragon's
//   attackSensor 0x144aa0 -> 0x326964) -> Thaw 0x4112b8: action "Thaw", killed at its end. Not
//   ported: no RideDragon in the ported courses.
// =============================================================================================
#pragma once

#include "game/StageObjects.hpp"
#include "game/StageSubActors.hpp"

#include <cmath>

namespace captain_toad::RecoveredStageSign {

using namespace captain_toad::RecoveredStage;

struct SignBoardSnowCover : Behaviour {
    enum class Nerve { Wait, Break } nerve = Nerve::Wait;
    int step = 0;
    explicit SignBoardSnowCover(Object& o) { captain_toad::RecoveredStageSubActors::startHidden(o); }
    void hostBroken(Object& o) {   // 0x2ca27c
        if (nerve == Nerve::Wait && o.alive) {
            nerve = Nerve::Break;
            step = 0;
        }
    }
    void control(World&, Object& o) override {
        if (nerve != Nerve::Break) {
            return;
        }
        if (step == 0) {   // 0x411310
            o.startAction("Break");
        }
        step++;
        if (o.isActionEnd()) {
            o.alive = false;
            o.visible = false;
        }
    }
};

struct SignBoard : Behaviour {
    bool breakable = false;
    bool scaledUp = false;
    bool broken = false;

    explicit SignBoard(Object& o) {
        static const char* const kDirections[] = {"Up",      "Down",      "Left",   "Right",
                                                  "RightUp", "RightDown", "LeftUp", "LeftDown"};
        const std::string name = o.string("ModelName", "");
        int aim = -1;
        for (int k = 7; k >= 0; k--) {
            if (name.find(kDirections[k]) != std::string::npos) {
                aim = k;
                break;
            }
        }
        o.setVisibilityFrame("SignAim", float(aim));
        breakable = o.model == "SignBoardWood";
        // Pose +0x1c is the X scale: the pose (vtable 0x4d4730) holds translation +4, rotation +0x10,
        // scale +0x1c (PullUpStage's sign in memory: 450,-450,-750 / 0,0,0 / 1,1,1).
        const auto& m = o.placement.m;
        scaledUp = std::sqrt(m[0] * m[0] + m[4] * m[4] + m[8] * m[8]) > 1.0f;
    }

    bool breakApart(World& w, Object& o) {   // the break nerve 0x411230
        if (!breakable || broken) {
            return false;
        }
        broken = true;
        w.reaction(o, scaledUp ? "拡大破壊" : "破壊");
        using namespace captain_toad::RecoveredStageSubActors;
        if (const int snow = spawnedBy(w, o, "SignBoardSnowCover"); snow >= 0) {
            if (auto* b = dynamic_cast<SignBoardSnowCover*>(w.behaviours[size_t(snow)].get())) {
                b->hostBroken(w.objects[size_t(snow)]);
            }
        }
        if (!scaledUp) {
            if (const int pieces = spawnedBy(w, o, "BreakModel"); pieces >= 0) {
                if (auto* b = dynamic_cast<BreakModel*>(w.behaviours[size_t(pieces)].get())) {
                    b->appear(w.objects[size_t(pieces)], o);
                }
            }
            if (const int trace = spawnedBy(w, o, "SubActorModel"); trace >= 0) {
                if (auto* b = dynamic_cast<SubActorModel*>(w.behaviours[size_t(trace)].get())) {
                    b->appear(w.objects[size_t(trace)], "Appear");
                }
            }
        }
        o.alive = false;   // 0x2f7b04: neither drawn nor collided with
        o.visible = false;
        return true;
    }
    bool screenTouch(World& w, Object& o) override { return breakApart(w, o); }   // 0x3dd47c
    bool thrownHit(World& w, Object& o, const V3&, bool) override { return breakApart(w, o); }
    bool pickaxeHit(World& w, Object& o, const Sphere&, bool) override { return breakApart(w, o); }
    bool explosion(World& w, Object& o) override { return breakApart(w, o); }
};

inline const Register<SignBoard> kSignBoard{"SignBoard"};
inline const Register<SignBoardSnowCover> kSignBoardSnowCover{"SignBoardSnowCover"};

}   // namespace captain_toad::RecoveredStageSign
