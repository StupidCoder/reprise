// =============================================================================================
// StageNpc.hpp - the brigade members standing about a course (StageObjects.hpp)
// =============================================================================================
//
// What the player sees: small explorers stand around, turn to face the protagonist when he walks
// past, and one of them holds a gem: when the protagonist comes up to him in front, he takes it
// out and it pops up for the protagonist to grab.
//
// KinopioMemberNpc (creator 0x45336c, vtable 0x4c480c, init 0x1a4060; archive "KinopioMemberNPC"):
//   KinopioColor picks a controller (0: 0x1e10b0 -> 0x2a6308, vtable 0x4ccde0); its nerves:
//   Wait 0x20e410     "Wait1"; with the player within 3000 and in sight (0x2a5c40): a facing
//                     error of 35 degrees or more and IsEnableTurn -> Turn; the take-out check
//                     0x29c138 -> TakeOut.
//   Turn 0x434368     "Turn"; turns toward the player (0x2ad428); when facing him: ByeBye if the
//                     NPC's +0x98 flag is set (cleared by the init for KinopioColor 2; set
//                     otherwise: hypothesis, the setter was not found), else Wait.
//   ByeBye 0x43444c   "ByeBye" (a wave); the take-out check -> TakeOut, at the action's end -> Wait
//                     (run sfxA: SeNvKinopioMemberNpcByeBye after the turns).
//   Reaction 0x4343f8 "Reaction" (a touch / attack, receiveMsg 0x20e19c), then Wait.
//   TakeOut 0x20df48  "TakeOut"; at step 16 (once) the item comes out: a CollectItem with the
//                     "PopUpNPC" clip at the NPC + its rotation * (0, 0, 85) (0x501fa4, read from
//                     memory); at the action's end -> Wait.
//   Take-out check 0x29c138: the NPC still holds its item, the player within 30 degrees of its
//   front and inside its KinopioMemberNpcTakeOutItemArea link (else within 500).
//   KinopioColor 1 (controller 0x1f3754, vtable 0x4cb334): its only nerve 0x430a30 plays "Sleep"
//   with IsSleep, else "Wait1"; KinopioColor 2 (0x1ed290, vtable 0x4ca86c): nerve 0x42d0fc plays
//   "WaitMap" (reading a map; the color-0 machine 0x2a6308 it also builds is not ported). Both
//   react to a touch with the hit reaction タッチ and a 15-tick wobble (not ported). Observed in run
//   loadB (course load from the book): Wait1, WaitMap and Sleep started at frame 1149.
//   The colour itself is the material animation "Color" held at frame KinopioColor (init 0x1a4060,
//   0x2fb538) and IsPutGlass shows the glasses (visibility animation "Glasses" held at frame 1, else 0; 0x2de2bc).
//   The turn rate and the sight test follow the companion's recovered Wait / Turn (3.5 degrees a
//   tick, done within 0.1; the line-of-sight ray is not ported).
// =============================================================================================
#pragma once

#include "game/StageItems.hpp"
#include "game/StageObjects.hpp"

#include <cmath>

namespace captain_toad::RecoveredStageNpc {

using namespace captain_toad::RecoveredStage;

struct KinopioMemberNpc : Behaviour {
    enum class Nerve { Wait, Turn, ByeBye, Reaction, TakeOut } nerve = Nerve::Wait;
    int step = 0;
    float yaw = 0;   // degrees about +Y
    V3 pos;
    bool enableTurn = true;
    bool hasItem = false;
    int item = -2;   // the spawned item's object (-2: not looked up)
    const LinkTarget* area = nullptr;
    V3 areaScale{1, 1, 1};
    int color = 0;           // KinopioColor: which controller runs
    bool sleeping = false;   // IsSleep (color 1)

    explicit KinopioMemberNpc(Object& o) {
        pos = o.translation();
        yaw = std::atan2(o.placement.m[2], o.placement.m[0]) * float(180.0 / M_PI);
        enableTurn = o.number("IsEnableTurn", 1) != 0;
        hasItem = o.string("KinopioMemberNpcAppearItem", "") == "CollectItem";
        area = o.link("KinopioMemberNpcTakeOutItemArea");
        color = int(o.number("KinopioColor", 0));
        if (color < 0 || color > 2) {
            color = 0;
        }
        sleeping = o.number("IsSleep", 0) != 0;
    }
    void setNerve(Nerve n) {
        nerve = n;
        step = 0;
    }
    V3 front() const {
        const float r = yaw * float(M_PI / 180.0);
        return {std::sin(r), 0, std::cos(r)};
    }
    float facingError(const V3& p) const {   // degrees between the front and the player
        V3 d = captain_toad::RecoveredVec::sub(p, pos);
        d.y = 0;
        const float target = std::atan2(d.x, d.z) * float(180.0 / M_PI);
        return std::remainder(target - yaw, 360.0f);
    }
    bool inArea(const V3& p) const {
        if (!area) {
            return captain_toad::RecoveredVec::dot(captain_toad::RecoveredVec::sub(p, pos),
                                                   captain_toad::RecoveredVec::sub(p, pos)) <= 500.0f * 500.0f;
        }
        const M34& m = area->placement;
        const float sx = std::sqrt(m.m[0] * m.m[0] + m.m[4] * m.m[4] + m.m[8] * m.m[8]);
        const float sy = std::sqrt(m.m[1] * m.m[1] + m.m[5] * m.m[5] + m.m[9] * m.m[9]);
        const float sz = std::sqrt(m.m[2] * m.m[2] + m.m[6] * m.m[6] + m.m[10] * m.m[10]);
        const V3 t = area->translation();
        return std::fabs(p.x - t.x) <= 500 * sx && p.y >= t.y && p.y <= t.y + 1000 * sy &&
               std::fabs(p.z - t.z) <= 500 * sz;
    }
    void place(Object& o) const {
        const float r = yaw * float(M_PI / 180.0), c = std::cos(r), s = std::sin(r);
        const float m[12] = {c, 0, s, pos.x, 0, 1, 0, pos.y, -s, 0, c, pos.z};
        for (int k = 0; k < 12; k++) {
            o.world.m[k] = m[k];
        }
    }
    bool pushesPlayer(const Object& o) const override { return o.alive; }
    bool colored = false;
    void control(World& w, Object& o) override {
        if (!colored) {
            o.setColorFrame("Color", float(color));
            o.setVisibilityFrame("Glasses", o.number("IsPutGlass", 1) != 0 ? 1.0f : 0.0f);
            colored = true;
        }
        if (color != 0) {   // the idle-only controllers (0x430a30 / 0x42d0fc)
            if (step++ == 0) {
                o.startAction(color == 2 ? "WaitMap" : sleeping ? "Sleep" : "Wait1");
            }
            return;
        }
        if (item == -2) {
            item = -1;
            for (size_t i = 0; i < w.objects.size(); i++) {
                if (w.objects[i].string("SpawnedBy", "") == o.id) {
                    item = int(i);
                }
            }
        }
        const V3 p = w.player.pos;
        const V3 d = captain_toad::RecoveredVec::sub(p, pos);
        const bool near = captain_toad::RecoveredVec::dot(d, d) <= 3000.0f * 3000.0f;
        switch (nerve) {
        case Nerve::Wait: {
            if (step == 0) {
                o.startAction("Wait1");
            }
            const float err = facingError(p);
            if (near && enableTurn && std::fabs(err) >= 35.0f) {
                setNerve(Nerve::Turn);
                return;
            }
            if (near && hasItem && std::fabs(err) <= 30.0f && inArea(p)) {
                setNerve(Nerve::TakeOut);
                return;
            }
            break;
        }
        case Nerve::Turn: {
            if (step == 0) {
                o.startAction("Turn");
            }
            const float err = facingError(p);
            if (std::fabs(err) <= 0.1f) {
                setNerve(Nerve::ByeBye);
                return;
            }
            yaw += std::max(-3.5f, std::min(3.5f, err));
            place(o);
            break;
        }
        case Nerve::ByeBye:   // 0x43444c
            if (step == 0) {
                o.startAction("ByeBye");
            }
            if (hasItem && std::fabs(facingError(p)) <= 30.0f && inArea(p)) {
                setNerve(Nerve::TakeOut);
                return;
            }
            if (step > 0 && o.isActionEnd()) {
                setNerve(Nerve::Wait);
                return;
            }
            break;
        case Nerve::Reaction:
            if (step == 0) {
                o.startAction("Reaction");
            } else if (o.isActionEnd()) {
                setNerve(Nerve::Wait);
                return;
            }
            break;
        case Nerve::TakeOut:
            if (step == 0) {
                o.startAction("TakeOut");
            }
            if (hasItem && step > 15) {
                hasItem = false;
                if (item >= 0) {
                    auto* gem =
                        dynamic_cast<captain_toad::RecoveredStageItems::CollectItem*>(w.behaviours[size_t(item)].get());
                    if (gem) {
                        gem->popUp(
                            w.objects[size_t(item)],
                            captain_toad::RecoveredVec::add(pos, captain_toad::RecoveredVec::scale(front(), 85.0f)),
                            "PopUpNPC");
                    }
                }
            }
            if (step > 0 && o.isActionEnd()) {
                setNerve(Nerve::Wait);
                return;
            }
            break;
        }
        step++;
    }
};

inline const Register<KinopioMemberNpc> kKinopioMemberNpc{"KinopioMemberNpc"};

}   // namespace captain_toad::RecoveredStageNpc
