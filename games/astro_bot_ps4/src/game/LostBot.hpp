// Lost bots (class lost_bot, vtable 0x16b9690, constructor 0x724b40, setup 0x726270, update
// 0x7288f0, hit 0x72f0e0). Findings: .analysis/docs/objects.md "Lost bot".
//
// A bot waits on its placement waving for help. Its clips come from the placement's WaitAnimFar /
// WaitAnimNear, or, when either is empty, help_01_far / help_01_near (+0x4880). The flag in its
// hand plays the same clip name + "_flag"; the jetpack on its back plays its own clips.
//   1 far:   help far clip; nearest hero closer than HelpMeRangeIn -> 2.
//   2 near:  help near clip, turns to face the hero (FaceToHero, or the default clips); nearest
//            hero beyond HelpMeRangeOut -> 1; closer than kickMeRangeIn (default clips or
//            KickAss) -> 7.
//   7 ready: kickassready (jetpack: kickass), turns its back to the hero; beyond kickMeRangeOut
//            -> 2.
// A punch started while a bot touches the hero's enemy sensor is a kick (0x684950 -> state 0x33);
// the kick's hit (type 1, along the hero's facing) rescues the bot (0x72b370): state 8, the
// launch velocity is KickXZVelocity x the hit direction with KickYVelocity up, and the flight
// (+0x4560, a phase machine 0x72bdc0 with three phases) takes it to the player's controller:
//   0 kicked (0x730720 / 0x7308a0): ballistic under KickGravity for KickTime, the jetpack's
//     kickass clip stretched to it; a sphere cast stops the part of the move into a wall.
//   1 fly (0x730cd0 / 0x731540): along a spline rebuilt every update (0x72d9a0) from the start
//     through the bot's path points and a highest point to rig + FlyTargetPosFromVrPlayer +
//     FlyBrakeStartOffset, at a speed that trades height for speed (FlyGravity); the last 0.6 m
//     brake to the speed that covers the brake offset in the entering clip; then 0.5 s easing
//     along the brake offset.
//   2 enter (0x72f4c0 / 0x72f9f0): a jump into the controller shrinking to 0.3 of its scale, to
//     0.1 within 0.1 s, then hidden; 0.7 s later it shows again at scale 0.091 playing
//     in_controller_landing, then in_controller_landing_out shrinking to 0.04 and sinking 2 cm;
//     then it is hidden and handed to the controller's display (0x730650; state 9).
#pragma once

#include "math/Transform.hpp"
#include "math/Vector.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace beacon::game {

// <LostBot> in multi_platformer.xml (binder 0x38d460).
struct LostBotTuning {
    float helpMeRangeIn = 1.3f, helpMeRangeOut = 1.4f;   // +0xab0 / +0xb60
    float kickMeRangeIn = 0.3f, kickMeRangeOut = 0.4f;   // +0xc10 / +0xcc0
    float kickTime = 0.55f;                              // +0x2200
    math::Vec3 flyTargetFromVrPlayer{0, 0.1f, -0.4f};    // +0x760
    float flyStartSpeedRatio = 1.02f;                    // +0x2820
    float flySpeedMin = 0.2f, flySpeedMax = 100.0f;      // +0x28d0 / +0x2980
    float flyHighestPointRatio = 0.3f;                   // +0x2ae0
    float flyHighestPointHeightToLen = 0.23f;            // +0x2b90
    float flyGravity = 5.0f;                             // +0x2c40
    math::Vec3 flyBrakeStartOffset{0, 0, -0.3f};         // +0x2c60
};
LostBotTuning loadLostBotTuning(const std::filesystem::path& root);

// The flight's spline (0x55ca90): Hermite segments between consecutive points with Catmull-Rom
// tangents (half the difference of the neighbours); phantom end points extend the first point
// backwards along the start tangent (x 1) and the last forwards along the end tangent (x 5).
// Segments are parametrised by chord length: u in [0, 1] over the whole path.
class FlyPath {
public:
    void build(const std::vector<math::Vec3>& points, const math::Vec3& startTangent, const math::Vec3& endTangent);
    math::Vec3 at(float u) const;        // 0x55d040
    math::Vec3 tangent(float u) const;   // 0x55d110 (unit)
    float length() const { return length_; }     // the chords' sum (+0x80)
    float highest() const { return highest_; }   // the points' highest y (+0x84)

private:
    struct Segment {
        float end = 0;   // cumulative chord fraction at the segment's end
        math::Vec3 c3, c2, c1, c0;
    };
    int find(float u, float& t) const;
    std::vector<Segment> segments_;
    math::Vec3 first_, last_, startTangent_, endTangent_;
    float length_ = 0, highest_ = 0;
};

// The placement's attributes the bot uses.
struct LostBotSetup {
    math::Mat4 world{};          // the placement's world transform (scale included)
    std::string waitAnimFar, waitAnimNear;
    float kickXZVelocity = 0.5f, kickYVelocity = 3.5f, kickGravity = -9.8f;
    bool faceToHero = false, kickAss = false;
    bool triggerOnGet = true;    // TriggerOnGet: "on" once in the controller, else once kicked
    int id = 0;                  // ID: picks the flight clips (id % 3 + 1)
    std::vector<math::Vec3> pathPoints;   // PathPoint1..5 (lost_bot_path_point), world
};

// A clip asked for: by name, blended in over `blend` seconds, restarted when `serial` changes;
// `duration` > 0 stretches it to last that long (0x730720 sets the kicked clip's speed).
struct LostBotClip {
    std::string name;
    float blend = 0;
    float duration = 0;
    std::uint32_t serial = 0;
};

struct Level;
class LevelRuntime;
// The setup of the lost_bot placement `index` (attributes; path points resolved to their world
// positions through the runtime).
LostBotSetup lostBotSetup(const Level& level, const LevelRuntime& runtime, std::size_t index);

class LostBot {
public:
    enum class State : std::uint8_t { Far = 1, Near = 2, Ready = 7, Flying = 8, Rescued = 9 };

    LostBot(const LostBotSetup& setup, const LostBotTuning& tuning);

    struct World {
        float dt = 1.0f / 60;
        bool hasHero = false;
        math::Vec3 hero;          // the nearest hero's feet
        math::Vec3 rig;           // the VR player's position (main+0x48 -> +0x30)
        math::Vec3 controller;    // where the controller takes the bot in (its touch pad)
        // Lengths of clips the logic times by: the bot's entering_controller_0N and the
        // jetpack's takeoff_in.
        float enteringClipLength = 1;
        float takeoffClipLength = 0.5f;
        float landingClipLength = 0.7f, landingOutClipLength = 0.5f;   // in_controller_landing(_out)
    };
    void update(const World& w);

    // The kick's hit (0x72f0e0, types 1 / 2): starts the rescue flight along `direction`.
    void kick(const math::Vec3& direction);
    // Whether a punch near the bot is a kick (0x678330 finds lost bots among the sensor's contacts;
    // a rescued bot's body is off).
    bool kickable() const { return state_ == State::Far || state_ == State::Near || state_ == State::Ready; }
    // The bot's body (pfb mup_arbot_col: one box on AR_Root): centre and half extents, world.
    math::Vec3 bodyCentre() const;
    math::Vec3 bodyHalf() const;
    math::Quat rotation() const { return rotation_; }

    State state() const { return state_; }
    bool visible() const { return !hidden_; }
    bool jetpackVisible() const { return !hidden_ && !jetpackHidden_; }
    const math::Vec3& position() const { return position_; }
    math::Mat4 pose() const;   // world, with the placement's scale
    float scale() const { return scaleNow_; }
    const LostBotClip& clip() const { return clip_; }
    const LostBotClip& flagClip() const { return flagClip_; }
    const LostBotClip& jetpackClip() const { return jetpackClip_; }
    std::string flightClip(const char* prefix) const;   // prefix + "0N"
    int flightPhase() const { return phase_; }
    float pathParameter() const { return u_; }   // phase 1 (+0xe4)
    float flySpeed() const { return speed_; }    // phase 1 (+0xe8)
    bool arrived() const { return arrived_; }   // reached the controller this update (0x5f29e0)
    // Kicked since the last takeKicked() (the controller's incoming count +1, 0x5f1820).
    bool takeKicked() { return std::exchange(kicked_, false); }
    // Done with the controller this update (its incoming count -1, 0x5f1860: 0.4167 s into
    // in_controller_landing_out).
    bool leftController() const { return leftController_; }
    // The bot as a trigger (0x733820): with TriggerOnGet, from reaching the controller (+0x4919);
    // otherwise from being kicked (+0x2180). Links such as a bridge's StartTrigger read it.
    bool on() const { return setup_.triggerOnGet ? reached_ : state_ == State::Flying || state_ == State::Rescued; }

private:
    void play(const std::string& name, float blend, float duration = 0);
    void playJetpack(const std::string& name, float blend);
    void turnTowards(const math::Vec3& target, float dt);   // 0x72a600
    void flyKicked(const World& w);
    void flyPath(const World& w);
    void flyEnter(const World& w);
    void buildPath(const World& w);
    void startPhase(int phase, const World& w);

    LostBotTuning t_;
    LostBotSetup setup_;
    State state_ = State::Far;
    bool defaultClips_ = true;   // +0x4880
    math::Vec3 position_;
    math::Quat rotation_;
    float scale_ = 1, scaleNow_ = 1;   // the placement's scale; the drawn one (the controller shrinks it)
    float drop_ = 0;                   // sinking into the controller (+0x120)
    bool hidden_ = false, jetpackHidden_ = false, arrived_ = false, reached_ = false;
    bool kicked_ = false, leftController_ = false, countedOut_ = false;
    LostBotClip clip_, flagClip_, jetpackClip_;
    // The jetpack's layer state (+0x4848): 1 idle, 2 kickass, 3 takeoff_in, 4 takeoff_loop,
    // 5 takeoff_out; time in it.
    int jetpackLayer_ = 1;
    float jetpackTime_ = 0;
    // Flight (+0x4560): phase, time in phase, kick velocity (+0x4870), path start (+0xc0),
    // parameter (+0xe4), speed (+0xe8), sub-state (+0xb8), sub-state time (+0xec), rotation at
    // the path's end (+0xd0).
    int phase_ = -1;
    float phaseTime_ = 0;
    math::Vec3 velocity_;
    math::Vec3 pathStart_;
    float u_ = 0, speed_ = 0;
    int sub_ = 0;
    float subTime_ = 0, landingTime_ = 0;
    math::Quat endRotation_;
    FlyPath path_;
};

}   // namespace game
