// Stage start of the opening: the stage scene's start nerve and the stage-start "bind demo"
// (DemoObjList entry "StageStartBindDemoZoomIn", actor class 0x206204), recovered from code.bin
// and driven by the stage data. Evidence: docs/director.md, runs director-intro-a / director-heap-a.
//
// What the player sees: a white iris opens on a close-up of the protagonist. While the camera is
// zoomed in, he plays a short "arrival" animation (CourseInRove) facing the camera. Then he turns
// towards the course, the camera eases back to the normal game camera over 40 frames, and control
// passes to the player. The background music starts during the arrival animation.
//
// In engine terms, a "bind demo" takes control of ("binds") the player actor: the player answers a
// bind request, and from then on the demo commands his facing and animations until it unbinds him.
//
// Data (exported from the stage map by the importer):
//   - DemoObjList entry: UnitConfigName (demo kind), Rotate Y (the bound player faces the demo's
//     front during the demo) and StartAnimation (the bound player's start action, "CourseInRove");
//   - PlayerList Rotate Y: the player's placement front, the turn target after the start action;
//   - clip lengths of the player's actions (CourseInRove, Turn, Wait).
// Code (literals at the given addresses):
//   - scene init 0x147b3c: for a StageStartBindDemoZoomIn stage (not a ghost-player stage) the stage
//     BGM starts after 100 scene steps (0x148008; scene +0x1a4, counted by nerve 0x1483b4);
//   - demo constructor 0x2065b4: camera distance 540 and height 120 (0x2066b8 / 0x2066bc);
//   - BindStart message 0x205dd8: the player faces the demo's front; zoom camera target = player +
//     (0, 120, 0), eye = target + 540 * front (side and pitch offsets 0);
//   - nerve WaitBind 0x433338 (0x4f4a2c): bind request; the BindStart message sets CourseIn;
//   - nerve CourseIn 0x433380 (0x4f4a30): start action = StartAnimation; ends with the action;
//   - nerve Turn 0x205a38 (0x4f4a34): step 0 starts "Turn" and ends the zoom camera ticket with a
//     40-frame interpolation (0x234c04); each step rotates the player's front 0.1 degrees about +Y
//     (0x205b90, applied for every front not exactly opposed) and then at most 5 degrees towards the
//     placement front (0x2cc500); "Wait" when within 1 degree; at step 20 "Wait", unbind, End;
//   - nerve End 0x433300 (0x4f4a38): the demo ends;
//   - camera interpolation nerve 0x23e994 (0x4f7f78): t = step / (40 * 0.5), p = (3t - t^3) / 2
//     (cubic Hermite 0..1 with start slope 1.5) on eye, target and fovy towards the game camera;
//   - iris: the scene's first update opens the stage-start wipe by a message to the wipe manager
//     (0x199228 -> 0x2f8b64 with frames -1: WipeCircleWhite "End" at its natural 2 frames per tick).
// Timing (nerve keeper semantics, Nerve.hpp; s = scene step):
//   s 0        scene StageStart step 0; demo WaitBind; the iris opens and shows frame 2
//   s 1        CourseIn: "CourseInRove" (BindStart)
//   s 100      BGM
//   s 101      CourseInRove ends (195 frames, player clip clock)
//   s 102      Turn step 0
//   s 103..123 camera interpolation steps 0..20
//   s 122      Turn step 20: Wait, unbind
//   s 123      play
// Animation timing (recovered, AnimFrame.hpp): the player moves before the demo, so an action the
// demo starts keeps frame 0 for one more tick, and the player's logic reads its mirror one tick
// behind the drawn skeleton (CourseInRove ends for the logic at s 101). The iris is bound in the
// stage sequence's game frame, before that frame's layout pass; the port shows this as one tick
// before the scene's first update (iris closed, frame 0).
// Observed, not recovered: the player's pose shows the front the demo set one tick later (the player
// updates before the demo).
#pragma once

#include <cmath>
#include <map>
#include <string>

#include "game/AnimFrame.hpp"
#include "game/Math.hpp"
#include "game/Nerve.hpp"
#include "game/VecLib.hpp"

namespace captain_toad::RecoveredStageStart {

using captain_toad::RecoveredVec::V3;
namespace RV = RecoveredVec;
namespace RM = RecoveredMath;

// 0x2cc500: turn `from` towards `to` by at most maxDeg degrees (a rotation about from x to).
// Returns whether the limit exceeded the remaining angle.
inline bool turnVecToVecDegree(V3& out, const V3& from, const V3& to, float maxDeg) {
    const float maxRad = maxDeg * RM::fbits(0x3c8efa35);   // degrees -> radians
    V3 axis = RV::cross(from, to);
    float l = axis.x * axis.x;
    l = l + axis.y * axis.y;
    l = l + axis.z * axis.z;
    RM::Quat q{0, 0, 0, 1};
    bool reached = false;
    if (l >= RM::fbits(0x358637be)) {
        RV::normalize(axis);
        float d = RV::dot(from, to);
        if (d < -1.0f) {
            d = -1.0f;
        } else if (d > 1.0f) {
            d = 1.0f;
        }
        const float angle = RM::acosf(d);
        float magnitude = angle;
        if (magnitude <= 0.0f) {
            magnitude = -magnitude;
        }
        if (magnitude >= RM::fbits(0x3a83126f)) {
            reached = maxRad > angle;
            float k = maxRad / angle;
            if (k < 0.0f) {
                k = 0.0f;
            } else if (k > 1.0f) {
                k = 1.0f;
            }
            q = RM::fromAxisAngle(axis, angle * k);
        }
    }
    // Inline q * from * q^-1 (0x2cc664).
    float tx = q.y * from.z;
    tx = tx - q.z * from.y;
    tx = tx + q.w * from.x;
    float ty = q.z * from.x;
    ty = ty - q.x * from.z;
    ty = ty + q.w * from.y;
    float tz = q.x * from.y;
    tz = tz - q.y * from.x;
    tz = tz + q.w * from.z;
    float tw = -(q.x * from.x);
    tw = tw - q.y * from.y;
    tw = tw - q.z * from.z;
    float ox = tx * q.w;
    ox = ox - ty * q.z;
    ox = ox + tz * q.y;
    ox = ox - tw * q.x;
    float oy = ty * q.w;
    oy = oy + tx * q.z;
    oy = oy - tz * q.x;
    oy = oy - tw * q.y;
    float oz = ty * q.x;
    oz = oz - tx * q.y;
    oz = oz + tz * q.w;
    oz = oz - tw * q.z;
    out = {ox, oy, oz};
    RV::normalize(out);
    return reached;
}

// 0x205b90..0x205c40: front rotated by quaternion q (no normalisation; the original interleaves
// the three output components, kept here).
inline V3 rotateFront(const RM::Quat& q, const V3& f) {
    float ty = q.z * f.x;
    ty = ty - q.x * f.z;
    ty = ty + q.w * f.y;
    float tw = -(q.x * f.x);
    tw = tw - q.y * f.y;
    tw = tw - q.z * f.z;
    float tz = q.x * f.y;
    tz = tz - q.y * f.x;
    tz = tz + q.w * f.z;
    float tx = q.y * f.z;
    tx = tx - q.z * f.y;
    tx = tx + q.w * f.x;
    float z = ty * q.x;
    float y = ty * q.w;
    float x = tx * q.w;
    z = z - tx * q.y;
    y = y + tx * q.z;
    x = x - ty * q.z;
    z = z + tz * q.w;
    y = y - tz * q.x;
    x = x + tz * q.y;
    z = z - tw * q.z;
    y = y - tw * q.y;
    x = x - tw * q.x;
    return {x, y, z};
}

// The bound player's action as the demo drives it (AnimFrame.hpp): the skeleton that is drawn, and
// the mirror the game logic reads (isActionEnd 0x3e5b14, the action keepers).
struct ActionClock {
    std::string name = "Wait";   // the player's own Wait until the demo starts an action
    int age = -1;
    captain_toad::RecoveredAnimFrame::Skeletal skel;
    captain_toad::RecoveredAnimFrame::Mirror mirror;

    void start(const std::string& clipName, float length) {
        name = clipName;
        age = 0;
        skel.start(clipName, length);
        mirror.reset();
    }

    // The player's tick: the action update (mirror copy 0x1d8094), then its movement (0x2d6e90).
    void playerTick() {
        mirror.copy(skel);
        skel.update();
        skel.endMovement();
        age = skel.age;
    }

    float frame() const { return skel.frame; }                            // drawn
    float logicFrame() const { return mirror.frame; }                     // seen by the game logic
    bool ended() const { return age >= 0 && mirror.frame >= skel.end; }   // isActionEnd
};

// Stage data that drives the demo.
struct Params {
    std::string demoKind = "StageStartBindDemoZoomIn";   // DemoObjList UnitConfigName
    std::string startAnimation = "CourseInRove";         // DemoObjList StartAnimation
    float demoYaw = -45;                                 // DemoObjList Rotate Y
    float playerYaw = 90;                                // PlayerList Rotate Y
    std::map<std::string, float> clipLength;             // player clips (frames)
    bool ghostStage = false;                             // GhostPlayer* stages (BGM delay 109)
};

// Placement front of a Y rotation: Rotate Y (degrees, about +Y) applied to +Z.
inline V3 frontOfYaw(float deg) {
    const float r = deg * RM::fbits(0x3c8efa35);
    return {RM::sinf(r), 0.0f, RM::cosf(r)};
}

// The stage-start bind demo actor (0x206204), together with the stage scene's start nerve
// (the 0x1483b4 part that counts the BGM delay).
struct Demo {
    Params p;

    // Constructor literals 0x2066b8 / 0x2066bc and the zoom ticket's fovy.
    static constexpr float kDistance = 540;
    static constexpr float kHeight = 120;
    static constexpr float kFovy = 35;
    static constexpr int kTurnSteps = 20;
    static constexpr int kCameraEndFrames = 40;

    captain_toad::RecoveredNerve::Keeper<Demo> nerve;

    // The bound player as the demo commands him.
    ActionClock action;
    V3 playerFront{};
    V3 playerPos{};
    bool bound = false;

    // Zoom camera ticket (BindStart) and its end interpolation (0x23e994).
    V3 zoomEye{};
    V3 zoomTarget{};
    bool zoomActive = false;
    bool cameraInterp = false;
    int cameraStep = 0;
    int cameraTotal = 0;

    // Scene (0x1483b4): step counter and BGM delay (0x147b3c).
    int sceneStep = -1;
    int bgmDelay = 0;
    int bgmCount = 0;
    bool bgmStarted = false;
    bool bgmRequest = false;   // true on the tick the BGM should start
    // The demo is turning (its vtable +0x98, polled by the scene's StageStart nerve 0x1483b4, which
    // then asks the title card to open, 0x33822c): from Turn's step 0 (card-end-a: the card's End2
    // frames match from this tick; the trace logs the request in the game frame before).
    bool turnStarted = false;
    bool finished = true;   // until begin()

    // Iris: WipeCircleWhite "End" (captain_toad::RecoveredAnimFrame::Layout), bound in the sequence's frame before
    // its layout pass: frame 2 (s + 1) at scene step s; the layout actor dies after the end.
    captain_toad::RecoveredAnimFrame::Layout iris;
    float wipeFrame = 0;
    float wipeLength = 211;
    bool wipeVisible = true;
    int tick = -1;   // displayed intro ticks (scene step = tick - 1)

    // Outputs of this tick.
    V3 eye{};
    V3 target{};
    float fovy = kFovy;
    V3 shownFront{};   // the player's front as posed this tick

    void begin(const Params& params, const V3& spawn) {
        finished = false;
        ended = false;
        p = params;
        playerPos = spawn;
        playerFront = shownFront = frontOfYaw(p.playerYaw);
        // Zoom ticket pose (created by the constructor; set again at BindStart from the same values).
        zoomTarget = {playerPos.x, playerPos.y + kHeight, playerPos.z};
        zoomEye = RV::add(RV::scale(frontOfYaw(p.demoYaw), kDistance), zoomTarget);
        // 0x147f68..0x148018: ZoomIn demo 100 (109 on the ghost-player stages), other kinds 0.
        bgmDelay = p.demoKind == "StageStartBindDemoZoomIn" ? (p.ghostStage ? 0x6d : 0x64) : 0;
        nerve.set(&Demo::waitBind);
    }

    bool active() const { return !finished; }

    // One 30 Hz tick in the original order: the camera director first (it sees the ticket state of
    // the previous tick), then the actors (the player before the demo, so the player's pose shows the
    // front the demo set on the previous tick). gameEye/gameTarget/gameFovy: the game camera's
    // (Start ticket) pose. Returns false on the tick after the End nerve ran (handover to play).
    bool update(const V3& gameEye, const V3& gameTarget, float gameFovy) {
        if (finished) {
            return false;
        }
        if (ended) {
            finished = true;
            return false;
        }
        ++tick;
        if (tick == 0) {
            iris.bind("End", wipeLength);   // 0x199228 -> 0x2f8b64(-1): natural speed
        }
        if (iris.end) {
            wipeVisible = false;   // End nerve 0x419944: the wipe dies at the animation end
        }
        iris.calc();
        wipeFrame = iris.shown;
        camera(gameEye, gameTarget, gameFovy, eye, target, fovy);
        shownFront = playerFront;
        if (tick == 0) {
            return true;   // displayed before the scene's first update (iris closed)
        }
        ++sceneStep;
        sceneUpdate();
        action.playerTick();   // the player moves before the demo (actor list order)
        nerve.update(*this);
        if (bindStartPending) {
            bindStartPending = false;
            bindStart();
        }
        return true;
    }

    // Scene StageStart nerve: the BGM starts after bgmDelay steps (0x148510..0x148540).
    void sceneUpdate() {
        bgmRequest = false;
        if (bgmStarted) {
            return;
        }
        if (bgmCount < bgmDelay) {
            ++bgmCount;
        } else {
            bgmStarted = true;
            bgmRequest = true;
        }
    }

    // The zoom ticket, or its end interpolation towards the game camera pose; fovy in degrees.
    void camera(const V3& gameEye, const V3& gameTarget, float gameFovy, V3& eyeOut, V3& targetOut, float& fovyOut) {
        if (!cameraInterp) {
            eyeOut = zoomEye;
            targetOut = zoomTarget;
            fovyOut = kFovy;
            return;
        }
        if (cameraStep > cameraTotal) {
            eyeOut = gameEye;
            targetOut = gameTarget;
            fovyOut = gameFovy;
            return;
        }
        float t = cameraTotal > 0 ? float(cameraStep) / float(cameraTotal) : 1.0f;
        if (t < 0.0f) {
            t = 0.0f;
        } else if (t > 1.0f) {
            t = 1.0f;
        }
        // 0x23e9f8: Hermite h00 * 0 + h10 * 1.5 + h01 * 1 + h11 * 0 = (3t - t^3) / 2, evaluated with
        // the original's zero terms included (they still round).
        float s0 = t * t - t;
        float s1 = s0 * t;
        float s3 = t * t;
        s3 = s3 - s1 * 2.0f;
        const float s6 = s1 - s0;
        float s = 0.0f;
        s = s - s3 * 0.0f;
        s = s + s3 * 1.0f;
        s = s + s6 * 1.5f;
        s = s + s1 * 0.0f;
        auto lerp = [&](float a, float b) {
            float d = b - a;
            return a + d * s;
        };
        eyeOut = {lerp(zoomEye.x, gameEye.x), lerp(zoomEye.y, gameEye.y), lerp(zoomEye.z, gameEye.z)};
        targetOut = {lerp(zoomTarget.x, gameTarget.x), lerp(zoomTarget.y, gameTarget.y),
                     lerp(zoomTarget.z, gameTarget.z)};
        fovyOut = lerp(kFovy, gameFovy);
        ++cameraStep;
    }

private:
    bool bindStartPending = false;
    bool ended = false;

    // Nerve WaitBind 0x433338: bind request; the player answers with BindStart after his update.
    void waitBind(int step) {
        if (step == 0) {
            bindStartPending = true;
        }
    }

    // 0x205dd8 (BindStart while in WaitBind): face the demo's front, set the zoom ticket, then CourseIn.
    void bindStart() {
        bound = true;
        playerFront = frontOfYaw(p.demoYaw);
        zoomTarget = {playerPos.x, playerPos.y + kHeight, playerPos.z};
        zoomEye = RV::add(RV::scale(playerFront, kDistance), zoomTarget);
        zoomActive = true;
        if (nerve.is(&Demo::waitBind)) {
            nerve.set(&Demo::courseIn);
        }
    }

    // Nerve CourseIn 0x433380: the arrival animation; continue when it ends.
    void courseIn(int step) {
        if (step == 0) {
            action.start(p.startAnimation, p.clipLength.at(p.startAnimation));
        }
        if (action.ended()) {
            nerve.set(&Demo::turn);
        }
    }

    // Nerve Turn 0x205a38: turn towards the course while the camera eases back.
    void turn(int step) {
        if (step == 0) {
            turnStarted = true;
            action.start("Turn", p.clipLength.at("Turn"));
            zoomActive = false;
            cameraInterp = true;
            cameraStep = 0;
            cameraTotal = int(float(kCameraEndFrames) * 0.5f);   // interpolator start 0x23f5d8
        }
        const V3 placementFront = frontOfYaw(p.playerYaw);   // +0x98: the player's placement front
        V3 front = playerFront;
        float lt = RV::dot(placementFront, placementFront);
        float lf = RV::dot(front, front);
        if (lt >= RM::fbits(0x358637be) && lf >= RM::fbits(0x358637be)) {
            V3 a = placementFront;
            V3 b = front;
            RV::normalize(a);
            RV::normalize(b);
            const float d = RV::dot(a, b);
            // Unless the fronts are exactly opposed, nudge by 0.1 degrees about +Y first.
            if (!(d < RM::cosf(RM::fbits(0x40490fdb)))) {
                front = rotateFront(RM::fromAxisAngle({0, 1, 0}, RM::fbits(0x3ae4c388)), front);
            }
        }
        V3 out;
        turnVecToVecDegree(out, front, placementFront, 5.0f);
        playerFront = out;

        // Within 1 degree while turning: switch to Wait.
        V3 c = RV::cross(out, placementFront);
        float cl = c.x * c.x;
        cl = cl + c.y * c.y;
        cl = cl + c.z * c.z;
        const float degrees = RM::atan2f(std::sqrt(cl), RV::dot(out, placementFront)) * RM::fbits(0x42652ee0);
        if (RM::bitsf(degrees) <= 0x3f800000u && action.name == "Turn") {
            action.start("Wait", p.clipLength.at("Wait"));
        }
        if (step >= kTurnSteps) {
            action.start("Wait", p.clipLength.at("Wait"));
            bound = false;
            nerve.set(&Demo::end);
        }
    }

    // Nerve End 0x433300.
    void end(int) { ended = true; }
};

}   // namespace captain_toad::RecoveredStageStart
