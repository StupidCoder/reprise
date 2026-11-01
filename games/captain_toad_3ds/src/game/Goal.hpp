// Goal and after-goal sequence of the opening stage, recovered from code.bin as the original's
// nerve machines and driven by the stage data. Evidence: docs/director.md, nerve traces
// evidence/runs/director-goal-a (calls from the goal checkpoint; goal tick k = (call - 3) / 2).
//
// What the player sees: when the protagonist reaches the golden star at the top of the ruins, he
// grabs it, and both characters strike a sign pose and then a pair pose with the star while the
// camera cuts to a fixed goal shot and a jingle plays. A "course clear" banner appears. Then a
// circular wipe closes to black, opens again on the after-goal cutscene (a large bird swoops in and
// snatches the star, with its own camera animation and music), and the title logo appears.
//
// Actors and their nerves (keeper semantics: Nerve.hpp):
//   GoalItem (the star, vtable 0x4d06d4): touched by the player's Head sensor (radius 55 at +125
//     along up) against its Body sensor (radius 100, 80 above the star root), evaluated after
//     movement: an overlap at the end of a tick is the "Get" on the next (goal-star-a).
//     Get 0x3ce6a8: action "GoalPoseSign"; the item is killed when its clip ends; its hit reaction
//     (named "get" in the data) plays the HrGot sound.
//   Goal object (initialiser 0x1ec824): goal demo parameters (pair poses for a goal with a PlayerNpc
//     link, camera literals 0x1ecc8c..0x1ecc94, demo steps 96 / 160, start thresholds 0 / 0, jingle
//     delay 0) and, with the placement's IsClearAfterDemo, the clear-after demo. It starts the goal
//     demo on the tick after the Get.
//   Goal demo (0x1e0868):
//     Start 0x1e0618     step 0 stops the bound actors; from step 1 requests the bind of the players
//                        (0x2b1128) and continues when all are bound (0x2a6b28) -> Pose.
//     Pose 0x1df6a8      step 0 starts the sign pose on the bound actors; when all actions have ended:
//                        pair poses, actors placed at the goal -> GoalPose.
//     GoalPose 0x1df400  step 0: the demo star's pose, the goal camera ticket, the clear jingle;
//                        step 96: HeadCourseClear appears; step 160: the demo ends (the goal object
//                        starts the clear-after demo on the next tick).
//   HeadCourseClear (layout actor, the banner): Appear 0x4336f4 until the animation ends, then
//     Wait 0x4336b8.
//   Clear-after demo (0x21337c, "DemoOpeningSeason1" for this stage):
//     WipeClose 0x212d58  step 0 closes WipeCircle in 60 frames (0x2f8a5c); continues when the wipe
//                         actor is in Wait (0x2f8bd8) -> Reset.
//     Reset 0x4359c4      step 0 rebinds the demo actors (0x2c0448); continues when bound -> Wait.
//     Wait 0x435a20       at step 30 (0x1e) opens the wipe in 60 frames (0x2f8b64) -> Main.
//     Main 0x212ff8       step 0: DemoOpeningSeason1 on the demo actors, the DemoCamera animation and
//                         the "DemoOp01" stream; at step 905 (0x389; 790 for season type 3) the title
//                         logo appears; ends when the title is done.
//   WipeCircle (layout actor, 0x419944.. family): Appear until its animation ends (duration-fitted to
//     60 frames), Wait; End plays the end animation fitted the same way, then the actor dies.
//   TitleLogoDemo (layout actor): Appear 0x41aa6c until the animation ends; Wait 0x41a9d4 ends at
//     step 10 for game mode 1 (45 otherwise; the mode 0x2f942c read 1 in the traced first play);
//     End 0x41a950 plays "End" until step (frames - 1) / 2.
//
// Recovered timing rules that matter here (AnimFrame.hpp): the characters move before the goal
// object and the demos, so a pose the demo starts keeps frame 0 for one more tick; the star starts
// its own pose and advances on the next tick; the player's logic reads its mirror, one tick behind
// its skeleton; layout animations bound by a nerve are drawn at frame 0 on the next two frames
// because the layout pass runs before the actors.
// Observed, not recovered (documented in docs/director.md):
//   - bind latency: the bound player still moves on the tick of the bind request and only animates
//     on the next one; the companion keeps her rail step on the tick her pose starts; the player's
//     pose shows a placement one tick after the demo set it (as in the stage start);
//   - sound requests through the BGM keeper (0x2ea5b0) and the star's hit reaction reach the sound
//     API one tick after the request (director-goal-a against sound-trace-a);
//   - the HeadCourseClear layout is hidden when the wipe has closed.
#pragma once

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

#include "game/AnimFrame.hpp"
#include "game/Nerve.hpp"
#include "game/VecLib.hpp"

namespace captain_toad::RecoveredGoal {

using captain_toad::RecoveredVec::V3;

// Stage data and code literals of the goal.
struct Params {
    V3 position{550, 50, -150};       // goal object translate
    V3 starRoot{550, 71.35f, -150};   // GoalItem translate + DisplayTranslate
    float starBodyOffset = 80;
    float starBodyRadius = 100;
    float headOffset = 125;
    float headRadius = 55;
    // Goal camera: eye above and in front of the goal object, looking at it.
    float eyeUp = 100;
    float eyeFront = 550;
    float targetUp = 130;
    float fovy = 35;
    // Goal object parameters (0x1ec824): pose names, start thresholds (+4, +8), demo steps.
    std::string pose = "GoalPosePair";
    std::string signPose = "GoalPosePairSign";
    std::string starSign = "GoalPoseSign";   // the GoalItem's own clip at the Get (0x3ce6a8)
    std::string miniPose = "MiniGoalPose";   // the demo star's pose for a Mini player (0x1df400)
    bool pair = true;                        // a PlayerNpc link: the companion poses too
    float rotateY = 0;                       // the goal object's Y rotation (degrees): its front
    int startStep = 0;
    int bindStep = 0;
    int courseClearStep = 96;
    int endStep = 160;
    bool clearAfterDemo = true;   // placement IsClearAfterDemo
    // Clear-after demo (0x21337c family) and title logo literals.
    int wipeFrames = 0x3c;
    int blackSteps = 0x1e;
    int titleStep = 0x389;
    int titleWaitSteps = 10;
};

// Do the player's Head sensor and the star's Body sensor overlap? (distance below the radius sum)
inline bool headTouchesStar(const Params& p, const V3& playerPos, const V3& playerUp) {
    const V3 head =
        captain_toad::RecoveredVec::add(playerPos, captain_toad::RecoveredVec::scale(playerUp, p.headOffset));
    const V3 body{p.starRoot.x, p.starRoot.y + p.starBodyOffset, p.starRoot.z};
    const V3 d = captain_toad::RecoveredVec::sub(head, body);
    const float r = p.headRadius + p.starBodyRadius;
    return captain_toad::RecoveredVec::dot(d, d) < r * r;
}

// A character's skeletal clip as the director drives it (AnimFrame.hpp): the drawn skeleton frame,
// and for the player the mirror his game logic reads.
struct ClipClock {
    std::string name;
    float end = 0;
    int age = -1;
    captain_toad::RecoveredAnimFrame::Skeletal skel;
    captain_toad::RecoveredAnimFrame::Mirror mirror;
    bool usesMirror = false;

    void start(const std::string& clipName, float length) {
        name = clipName;
        end = length;
        age = 0;
        skel.start(clipName, length);
        mirror.reset();
    }

    void actionUpdate() {   // player: 0x1d8094 before the movement
        if (usesMirror) {
            mirror.copy(skel);
        }
    }
    void movement() {   // 0x2d6e90 step 1
        skel.update();
        age = skel.age;
    }
    void endMovement() {   // 0x2d7304
        skel.endMovement();
    }

    bool running() const { return age >= 0; }
    float frame() const { return skel.frame; }                                    // drawn
    float logicFrame() const { return usesMirror ? mirror.frame : skel.frame; }   // isActionEnd, keepers
    float raw() const { return skel.elapsed; }                                    // material animations loop on this
    bool ended() const { return running() && logicFrame() >= end; }
};

// A layout actor's animation (captain_toad::RecoveredAnimFrame::Layout): `frames` is the animation's frame count,
// `step` the advance per 60 Hz frame (1 at natural speed; a duration-fitted play uses animation
// frames / requested frames, 0x2f8b34).
struct LayoutClock {
    std::string name;
    float length = 0;
    int age = -1;
    captain_toad::RecoveredAnimFrame::Layout a;

    void start(const std::string& animationName, float frames, float step) {
        name = animationName;
        length = frames;
        age = 0;
        a.bind(animationName, frames, step);
    }

    // The layout pass of the game frame.
    void tick() {
        a.calc();
        if (age >= 0) {
            ++age;
        }
    }

    bool running() const { return age >= 0; }
    float frame() const { return a.shown; }
    bool ended() const { return running() && a.end; }

    // What the panes show this frame: the animation and frame of the last layout pass (before
    // the first pass, the bound animation at frame 0), clamped to that animation's count.
    const std::string& drawnName() const { return a.shownName.empty() ? name : a.shownName; }
    float drawnFrame() const { return a.shownName.empty() ? 0.0f : std::min(a.shown, a.shownCount); }
};

// The whole goal sequence: the goal demo, the clear-after demo and the three layout actors, each a
// nerve keeper over this object's member functions.
struct Sequence {
    Params p;
    bool contact = false;   // the sensors overlapped at the end of the previous tick
    int k = -1;             // ticks since the Get (-1: not reached); diagnostics only

    // The characters' clips.
    ClipClock player, companion, star, demoStar, bossBird;

    // What is shown.
    bool starVisible = true;
    bool demoStarVisible = false;
    bool companionPlaced = false;
    bool playerPlaced = false;
    bool fixedCamera = false;
    bool courseClear = false;
    bool demoEnded = false;
    bool bossBirdVisible = false;
    bool demoCamera = false;
    bool courseClearVisible = false;
    bool wipeVisible = false;
    bool titleVisible = false;

    std::map<std::string, float>
        clipLength;   // GoalPosePairSign, GoalPosePair, GoalPoseSign (star), DemoOpeningSeason1
    std::map<std::string, float>
        layoutLength;   // CourseClear Appear, Wipe Appear/End, Title Appear/End (animation frames)
    LayoutClock courseClearAnim, wipeAnim, titleAnim;

    // Events of this tick for the sound and effect services.
    bool getSound = false;
    bool jingleRequest = false;
    bool wipeSound = false;
    bool demoBgmRequest = false;
    bool pairPosesStarted = false;                       // the Pose nerve placed the actors (headlamp off)
    bool goalPoseStarted = false;                        // GoalPose step 0 has run (headlamp back on)
    int demoCameraStart = -1;                            // tick k of the demo camera's frame 0
    std::vector<std::pair<int, const char*>> nerveLog;   // (k, nerve) at each nerve's step 0 (validation)

    captain_toad::RecoveredNerve::Keeper<Sequence> demo, clear, courseClearActor, wipeActor, titleActor;

    bool playerMini = false;   // the player's power state at the goal (session)

    bool active() const { return k >= 0; }
    float demoCameraFrame() const { return 2.0f * float(k - demoCameraStart); }

    // Who controls the characters this tick: the normal game logic, or the demo.
    bool playerMoves() const { return !playerBound; }
    bool playerAnimates() const { return playerBound && !player.running(); }
    bool companionMoves() const { return !companionBound; }

    // Start of a 30 Hz tick: the Get when the previous tick ended in contact, then the actors in the
    // original update order (characters, then the goal object and demos, then the layout actors).
    void beginTick() {
        if (k < 0) {
            if (!contact) {
                return;
            }
            k = 0;
        } else {
            ++k;
        }
        getSound = jingleRequest = wipeSound = demoBgmRequest = pairPosesStarted = false;

        // Layout pass (0x37f328), early in the game frame.
        courseClearAnim.tick();
        wipeAnim.tick();
        titleAnim.tick();

        // Character movements in the actor list order (player, companion, GoalItem, demo actors;
        // actor-order-a), before the goal object and the demos.
        player.usesMirror = true;
        player.actionUpdate();
        for (ClipClock* c : {&player, &companion, &star, &demoStar, &bossBird}) {
            c->movement();
        }
        if (k == 0) {
            star.start(p.starSign, clipLength.at(p.starSign));   // GoalItem Get 0x3ce6a8, its own nerve
        }
        if (star.ended()) {
            starVisible = false;   // killed at the clip end
        }
        for (ClipClock* c : {&player, &companion, &star, &demoStar, &bossBird}) {
            c->endMovement();
        }

        // Effects requested on the previous tick.
        if (bindPending) {
            bindPending = false;
            playerBound = true;
        }
        if (companionBindPending) {
            companionBindPending = false;
            companionBound = true;
        }
        if (getSoundPending) {
            getSoundPending = false;
            getSound = true;
        }
        if (jinglePending) {
            jinglePending = false;
            jingleRequest = true;
        }
        if (demoBgmPending) {
            demoBgmPending = false;
            demoBgmRequest = true;
        }
        if (wipeSoundPending) {
            wipeSoundPending = false;
            wipeSound = true;
        }

        // The goal object: the Get on k == 0, the goal demo on the tick after it.
        if (k == 0) {
            getSoundPending = true;
            demoStartPending = true;
        } else if (demoStartPending) {
            demoStartPending = false;
            demo.set(&Sequence::demoStart);
        }
        if (clearStartPending) {
            clearStartPending = false;
            if (p.clearAfterDemo) {
                clear.set(&Sequence::wipeClose);
            }
        }

        demo.update(*this);
        clear.update(*this);
        courseClearActor.update(*this);
        wipeActor.update(*this);
        titleActor.update(*this);
    }

    // End of a tick (after all actors moved): the sensor overlap that triggers the Get next tick.
    void endTick(const V3& playerPos, const V3& playerUp) {
        if (k < 0) {
            contact = headTouchesStar(p, playerPos, playerUp);
        }
    }

    // The goal object's front (its Y rotation; the opening's goal faces +Z).
    V3 front() const {
        const float r = p.rotateY * 0.0174532924f;
        return {std::sin(r), 0, std::cos(r)};
    }
    // The fixed goal camera: above and in front of the goal object, looking at it.
    void cameraPose(V3& eye, V3& target) const {
        const V3 f = front();
        eye = {p.position.x + f.x * p.eyeFront, p.position.y + p.eyeUp, p.position.z + f.z * p.eyeFront};
        target = {p.position.x, p.position.y + p.targetUp, p.position.z};
    }

private:
    bool demoStartPending = false;
    bool clearStartPending = false;
    bool getSoundPending = false;
    bool jinglePending = false;
    bool demoBgmPending = false;
    bool wipeSoundPending = false;
    bool bindPending = false;
    bool companionBindPending = false;
    bool playerBound = false;
    bool companionBound = false;

    void log(int step, const char* nerveName) {
        if (step == 0) {
            nerveLog.push_back({k, nerveName});
        }
    }

    // ---- Goal demo (0x1e0868) ----------------------------------------------------------------------

    // Start 0x1e0618: bind the players.
    void demoStart(int step) {
        log(step, "GoalDemo.Start");
        if (step <= p.startStep) {
            return;
        }
        // 0x2b1128: bind request; the player answers in his update this tick (bound on the next).
        if (!playerBound) {
            bindPending = true;
            return;
        }
        if (step > p.bindStep) {
            demo.set(&Sequence::pose);
        }
    }

    // Pose 0x1df6a8: sign pose, then pair pose.
    void pose(int step) {
        log(step, "GoalDemo.Pose");
        if (step == 0) {
            player.start(p.signPose, clipLength.at(p.signPose));
            if (p.pair) {
                companion.start(p.signPose, clipLength.at(p.signPose));
                companionBindPending = true;
            }
        }
        if (!(player.ended() && (!p.pair || companion.ended()))) {
            return;
        }
        player.start(p.pose, clipLength.at(p.pose));
        if (p.pair) {
            companion.start(p.pose, clipLength.at(p.pose));
            companionPlaced = true;
        }
        pairPosesStarted = true;
        demo.set(&Sequence::goalPose);
    }

    // GoalPose 0x1df400: the goal shot, the banner, the end of the demo.
    void goalPose(int step) {
        log(step, "GoalDemo.GoalPose");
        if (step == 0) {
            playerPlaced = true;
            fixedCamera = true;
            goalPoseStarted = true;
            demoStarVisible = true;
            // The demo star's pose: the goal's pose name, MiniGoalPose for a Mini player (0x2d6dac).
            const std::string& starPose = playerMini && p.pose == "GoalPose" ? p.miniPose : p.pose;
            demoStar.start(starPose, clipLength.at(starPose));
            jinglePending = true;   // the clear jingle, with delay +0x14 = 0
        }
        if (step == p.courseClearStep) {
            courseClear = courseClearVisible = true;
            courseClearActor.set(&Sequence::courseClearAppear);
            courseClearAnim.start("Appear", layoutLength.at("CourseClear.Appear"), 1);
        }
        if (step == p.endStep) {
            demoEnded = true;
            clearStartPending = true;
            demo.set(&Sequence::idle);
        }
    }

    void idle(int) {}

    // ---- HeadCourseClear layout actor ------------------------------------------------------------

    void courseClearAppear(int step) {
        log(step, "HeadCourseClear.Appear");
        if (courseClearAnim.ended()) {
            courseClearAnim.start("Wait", 0, 1);
            courseClearActor.set(&Sequence::idle);
        }
    }

    // ---- Clear-after demo (0x21337c) --------------------------------------------------------------

    // WipeClose 0x212d58: close the wipe, continue when it is closed.
    void wipeClose(int step) {
        log(step, "ClearAfter.WipeClose");
        if (step == 0) {
            // 0x2f8a5c: WipeCircle "Appear" fitted to 60 frames; its keeper runs it this tick.
            wipeVisible = true;
            const float n = layoutLength.at("Wipe.Appear");
            wipeAnim.start("Appear", n, n / float(p.wipeFrames));
            wipeActor.set(&Sequence::wipeAppear);
            wipeSoundPending = true;   // the wipe's "Appear" sound entry "Close" (StartFrame 0)
            return;
        }
        if (wipeActor.is(&Sequence::wipeWait)) {
            courseClearVisible = false;
            clear.set(&Sequence::reset);
        }
    }

    // Reset 0x4359c4: rebind the demo actors (0x2c0448), continue when bound (0x262a80).
    void reset(int step) {
        log(step, "ClearAfter.Reset");
        if (step >= 1) {
            clear.set(&Sequence::black);
        }
    }

    // Wait 0x435a20: hold the black screen, then open the wipe.
    void black(int step) {
        log(step, "ClearAfter.Wait");
        if (step < p.blackSteps) {
            return;
        }
        const float n = layoutLength.at("Wipe.End");   // 0x2f8b64: "End" fitted to 60 frames
        wipeAnim.start("End", n, n / float(p.wipeFrames));
        wipeActor.set(&Sequence::wipeEnd);
        clear.set(&Sequence::main);
    }

    // Main 0x212ff8: the cutscene and the title.
    void main(int step) {
        log(step, "ClearAfter.Main");
        if (step == 0) {
            const float n = clipLength.at("DemoOpeningSeason1");
            player.start("DemoOpeningSeason1", n);
            companion.start("DemoOpeningSeason1", n);
            demoStar.start("DemoOpeningSeason1", n);
            bossBird.start("DemoOpeningSeason1", n);
            bossBirdVisible = true;
            demoCameraStart = k + 1;   // the camera director picks the animation up next tick
            demoBgmPending = true;
        }
        if (k == demoCameraStart) {
            demoCamera = true;
        }
        if (step == p.titleStep) {
            titleVisible = true;
            titleActor.set(&Sequence::titleAppear);
            titleAnim.start("Appear", layoutLength.at("Title.Appear"), 1);
        }
    }

    // ---- WipeCircle layout actor ------------------------------------------------------------------

    void wipeAppear(int step) {   // 0x419a28
        log(step, "WipeCircle.Appear");
        if (step >= 1 && wipeAnim.ended()) {
            wipeActor.set(&Sequence::wipeWait);
        }
    }

    void wipeWait(int step) { log(step, "WipeCircle.Wait"); }

    void wipeEnd(int step) {   // 0x419944
        log(step, "WipeCircle.End");
        if (wipeAnim.ended()) {
            wipeVisible = false;
            wipeActor.set(&Sequence::idle);
        }
    }

    // ---- TitleLogoDemo layout actor -----------------------------------------------------------------

    void titleAppear(int step) {   // 0x41aa6c
        log(step, "TitleLogo.Appear");
        if (titleAnim.ended()) {
            titleAnim.start("Wait", 0, 1);
            titleActor.set(&Sequence::titleWait);
        }
    }

    void titleWait(int step) {   // 0x41a9d4
        log(step, "TitleLogo.Wait");
        if (step >= p.titleWaitSteps) {
            titleActor.set(&Sequence::titleEnd);
        }
    }

    void titleEnd(int step) {   // 0x41a950
        log(step, "TitleLogo.End");
        if (step == 0) {
            titleAnim.start("End", layoutLength.at("Title.End"), 1);
        }
    }
};

}   // namespace captain_toad::RecoveredGoal
