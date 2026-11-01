// =============================================================================================
// StageGuide.hpp - touch hints over the course (StageObjects.hpp)
// =============================================================================================
//
// What the player sees: while the protagonist stands in a hint's area and the camera looks at the
// hint point from in front, a speech balloon with an icon (the touch screen with a finger, a
// microphone, or a button text) pops up over the point; it shrinks away when the view turns or
// the protagonist leaves.
//
// GuideAppearPoint (vtable 0x4c45fc: init 0x1a25cc, init after placement 0x1a24ac, kill 0x1a2be0,
//   control 0x1a2d8c). Placement: DegreeXZ, DegreeYZUp, DegreeYZBottom (each 0..360),
//   DistanceToCamera, isJudgeDegreeYZ, ConnectFace, IconType (0 text "Text", 1 microphone "Mic",
//   2 touch screen "DRCTouch": the icon animations of the GuideBalloon layout's Icon group), links
//   GuideAppearArea (the area group) and SwitchKillImmediate. Two presentations: the layout actor
//   "GuideBalloon" (the top screen) and the 2D actor "GUIDE_BALLOON" of the in-game 2D actor data
//   (sequences BUTTON_ / MIC_ / TOUCH_ + INTRO / IDLE / OUTRO; on the touch screen's copy of the
//   course view, not ported). The 2D actor's sequences time the nerves: two frames per tick, a
//   sequence has ended once its frame reaches its length.
//   Nerves (table from 0x4f750c):
//     Hide 0x422454: step 0 kills the layout; each step the judge (0x2b7760) -> Appear.
//     Appear 0x1a30c8: step 0 makes the layout appear with "Appear" and starts *_INTRO; -> Wait
//       when *_INTRO has ended.
//     Wait 0x1a2f20: step 0 "Wait" and *_IDLE; each step the judge; failing -> End.
//     End 0x1a2c10: step 0 "End" and *_OUTRO; -> Hide when *_OUTRO has ended.
//   Each shown step the layout's root pane goes to the point's screen position (0x2e8a9c ->
//   0x2b7fc4).
//   The judge 0x2b7760 fails while the current camera is the Start camera (0x2a6a5c "スタート"),
//   during the scene's demo / stop states (+0x74 -> +0x20: +4, +0x1c, +0x1d), while the point moved
//   in the last tick (+0xf0: this and the last position, IsConnectToCollision), and when the
//   protagonist is outside the area group (0x2f62d4); else it passes when DegreeXZ is 0 or both
//   DegreeYZ are 0, or when the camera eye lies within DegreeXZ (horizontal) of the point's facing
//   (its local Z) and, with isJudgeDegreeYZ, within DegreeYZUp above / DegreeYZBottom below the
//   point.
//   Observed (touchpuzzle-start-a, camera turned with the C-stick both ways): hidden under the Start
//   camera; Appear four frames after the first camera input, Wait 34 frames later (17 ticks,
//   TOUCH_INTRO 31 frames), End when the eye passed 100 degrees from the facing (98.1 shown,
//   101.9 / -101 hidden; the judge sees the camera of its own tick), Hide 14 frames later (7
//   ticks, TOUCH_OUTRO 11 frames).
//   Not ported: the vertical judge is a reading of the code that the runs did not test (elevations
//   26..34 degrees only); DistanceToCamera, ConnectFace and SwitchKillImmediate; the 2D actor;
//   the scale of the 2D actor by the camera zoom (control 0x1a2d8c).
// =============================================================================================
#pragma once

#include "game/StageObjects.hpp"

#include <cmath>
#include <vector>

namespace captain_toad::RecoveredStageGuide {

using namespace captain_toad::RecoveredStage;

struct GuideAppearPoint : Behaviour {
    enum class Nerve { Hide, Appear, Wait, End } nerve = Nerve::Hide;
    int step = 0;
    float degreeXZ = 0, degreeYZUp = 0, degreeYZBottom = 0;
    bool judgeYZ = false;
    int iconType = 2;
    std::vector<M34> areas;   // GuideAppearArea

    // The point (IsConnectToCollision: it rides the map part below it, as the coins do).
    M34 base{};
    CollisionConnector connector;
    V3 position{}, lastPosition{};   // +0xf0: this tick's and the last tick's

    explicit GuideAppearPoint(Object& o) {
        degreeXZ = float(o.number("DegreeXZ", 0));
        degreeYZUp = float(o.number("DegreeYZUp", 0));
        degreeYZBottom = float(o.number("DegreeYZBottom", 0));
        judgeYZ = o.number("isJudgeDegreeYZ", 0) != 0;
        iconType = int(o.number("IconType", 0));
        auto it = o.links.find("GuideAppearArea");
        if (it != o.links.end()) {
            for (const LinkTarget& t : it->second) {
                areas.push_back(t.placement);
            }
        }
        base = o.placement;
        position = lastPosition = o.translation();
        o.visible = false;   // no model: the balloon is a layout (the session draws it)
    }

    // The layout's icon animation and the 2D actor's sequence prefix by IconType.
    const char* iconAnimation() const { return iconType == 0 ? "Text" : iconType == 1 ? "Mic" : "DRCTouch"; }
    const char* sequencePrefix() const { return iconType == 0 ? "BUTTON_" : iconType == 1 ? "MIC_" : "TOUCH_"; }
    // True while the layout is shown (it appears in Appear and dies in Hide).
    bool balloonShown() const { return nerve != Nerve::Hide; }

    void enter(Nerve n) {
        nerve = n;
        step = 0;
    }
    // The 2D actor's sequence has ended: two frames per tick, from 0 at the nerve's first step.
    bool sequenceEnded(const World& w, const char* suffix) const {
        auto it = w.actor2dFrames.find(std::string(sequencePrefix()) + suffix);
        const int frames = it == w.actor2dFrames.end() ? 0 : it->second;
        return 2 * step >= frames;
    }

    static bool inBox(const M34& m, const V3& p) {   // the area box (as Area::contains)
        const V3 d{p.x - m.m[3], p.y - m.m[7], p.z - m.m[11]};
        float l[3];
        for (int k = 0; k < 3; k++) {
            const V3 axis{m.m[k], m.m[4 + k], m.m[8 + k]};
            l[k] = captain_toad::RecoveredVec::dot(d, axis) / captain_toad::RecoveredVec::dot(axis, axis);
        }
        return std::fabs(l[0]) <= 500 && l[1] >= 0 && l[1] <= 1000 && std::fabs(l[2]) <= 500;
    }

    // 0x2b7760.
    bool judge(const World& w, const Object& o) const {
        if (w.startCameraView || w.guidesBlocked) {
            return false;
        }
        const V3 moved = captain_toad::RecoveredVec::sub(position, lastPosition);
        if (std::sqrt(captain_toad::RecoveredVec::dot(moved, moved)) >= 0.001f) {
            return false;
        }
        if (!areas.empty()) {
            bool inside = false;
            for (const M34& a : areas) {
                inside = inside || inBox(a, w.player.pos);
            }
            if (!inside) {
                return false;
            }
        }
        if (std::fabs(degreeXZ) < 0.001f || (std::fabs(degreeYZUp) < 0.001f && std::fabs(degreeYZBottom) < 0.001f)) {
            return true;
        }
        const V3 d = captain_toad::RecoveredVec::sub(w.cameraEye, position);
        const M34& m = o.world;
        const V3 front{m.m[2], m.m[6], m.m[10]};
        const float fl = std::sqrt(front.x * front.x + front.z * front.z);
        const float dl = std::sqrt(d.x * d.x + d.z * d.z);
        if (fl > 0 && dl > 0) {
            const float c = (front.x * d.x + front.z * d.z) / (fl * dl);
            const float angle = std::acos(std::fmin(std::fmax(c, -1.0f), 1.0f)) * 57.29578f;
            if (angle > degreeXZ) {
                return false;
            }
        }
        if (judgeYZ && dl > 0) {
            const float elevation = std::atan2(d.y, dl) * 57.29578f;
            if (elevation >= 0 ? elevation > degreeYZUp : -elevation > degreeYZBottom) {
                return false;
            }
        }
        return true;
    }

    void control(World& w, Object& o) override {
        if (connector.connectBelow(w, o, base)) {
            base = connector.follow(w, base);
        }
        o.world = base;
        switch (nerve) {
        case Nerve::Hide:
            if (judge(w, o)) {
                enter(Nerve::Appear);
                break;
            }
            step++;
            break;
        case Nerve::Appear:
            if (sequenceEnded(w, "INTRO")) {
                enter(Nerve::Wait);
                break;
            }
            step++;
            break;
        case Nerve::Wait:
            if (!judge(w, o)) {
                enter(Nerve::End);
                break;
            }
            step++;
            break;
        case Nerve::End:
            if (sequenceEnded(w, "OUTRO")) {
                enter(Nerve::Hide);
                break;
            }
            step++;
            break;
        }
        // control 0x1a2d8c: the point's position history (+0xf0).
        lastPosition = position;
        position = {base.m[3], base.m[7], base.m[11]};
    }
};

inline const Register<GuideAppearPoint> kGuideAppearPoint{"GuideAppearPoint"};

}   // namespace captain_toad::RecoveredStageGuide
