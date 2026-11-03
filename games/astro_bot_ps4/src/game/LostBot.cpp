#include "game/LostBot.hpp"

#include "data/Bytes.hpp"
#include "data/Xml.hpp"
#include "game/LevelDocument.hpp"
#include "game/LevelRuntime.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace beacon::game {

using math::Quat;
using math::Vec3;

namespace {

constexpr float kClipBlend = 0.2f;        // 0x123aa18 / 0x123a9fc: the wait clips' blend
constexpr float kTurnRate = 3.0f;         // 0x123aa14 / 0x123ab08: turning towards a target, 1/s
constexpr float kTurnMinDistance = 0.1f;  // 0x123aa38: closer targets do not turn the bot
constexpr float kKickBlend = 0.1f;        // 0x123aaf8: the kicked clip
constexpr float kFlyBlend = 0.2f;         // 0x123ab10: the flying clip
constexpr float kTakeoffBlend = 0.1f;     // 0x123ab0c: the jetpack's takeoff
constexpr float kEnterBlend = 0.2f;       // 0x123ab3c: the entering clip
constexpr float kPathStartScale = 1.0f;   // 0x123aaa8: phantom point before the start
constexpr float kPathEndScale = 5.0f;     // 0x123aab0: phantom point after the end
constexpr float kBrakeLength = 0.6f;      // -0x123ab18: braking over the path's last 0.6 m ...
constexpr float kBrakeMinPart = 0.9f;     // 0x123ab1c: ... but never before 90 % of it
constexpr float kLookAhead = 0.1f;        // 0x123ab34: the speed law's look-ahead along the path, m
constexpr float kEaseTime = 0.5f;         // 0x123ab28: the brake offset's easing, s
constexpr float kEnterHeight = 0.2f;      // 0x123aaf0: the jump into the controller, m
constexpr float kEnterGravity = 15.0f;    // 0x123aaf4
constexpr float kShrinkTime = 0.1f;       // 0x123aae4: shrinking after arriving, s
constexpr float kArriveScale = 0.3f;      // 0x123aaec: share of the scale on arrival ...
constexpr float kShrunkScale = 0.1f;      // 0x123aae4: ... and after shrinking
constexpr float kReappearTime = 0.7f;     // 0x123aae8
constexpr float kLandingScale = 0.091f;   // 0x1239d70 (absolute)
constexpr float kLandedScale = 0.04f;     // 0x123aacc (absolute)
constexpr float kLandingDrop = -0.02f;    // 0x123aae0
constexpr float kLandingBlend = 0.1f;     // 0x123aae4: in_controller_landing_out
constexpr float kLeaveControllerTime = 0.4167f;   // into in_controller_landing_out: 0x5f1860

Vec3 rotate(const Quat& q, const Vec3& v) {
    const Vec3 u{q.x, q.y, q.z};
    const Vec3 t = 2.0f * math::cross(u, v);
    return v + q.w * t + math::cross(u, t);
}

Quat yawRotation(float yaw) {
    return {0, std::sin(0.5f * yaw), 0, std::cos(0.5f * yaw)};
}

// Facing `dir` (unit) with +y up (0x731540 builds the basis and turns it into a rotation): the
// heading, then the pitch.
Quat lookAlong(const Vec3& dir) {
    const float yaw = std::atan2(dir.x, dir.z);
    const float pitch = -std::asin(std::clamp(dir.y, -1.0f, 1.0f));
    const float cy = std::cos(0.5f * yaw), sy = std::sin(0.5f * yaw);
    const float cp = std::cos(0.5f * pitch), sp = std::sin(0.5f * pitch);
    return {cy * sp, sy * cp, -sy * sp, cy * cp};
}

}   // namespace

LostBotTuning loadLostBotTuning(const std::filesystem::path& root) {
    LostBotTuning t;
    data::Bytes bytes;
    if (!data::loadFile(root / "data" / "multi_platformer" / "multi_platformer.xml", bytes)) {
        return t;
    }
    auto xml = data::parseXml(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    if (!xml) {
        return t;
    }
    const data::XmlNode* node = xml->child("LostBot");
    if (!node) {
        return t;
    }
    const data::XmlNode* path = node->child("FlyPath");
    auto read = [](const data::XmlNode* n, const char* name, float& value) {
        if (n) {
            if (auto text = n->childText(name)) {
                data::parseFloatList(*text, &value, 1);
            }
        }
    };
    auto readVec = [](const data::XmlNode* n, const char* name, Vec3& value) {
        if (n) {
            if (auto text = n->childText(name)) {
                float v[3];
                if (data::parseFloatList(*text, v, 3)) {
                    value = {v[0], v[1], v[2]};
                }
            }
        }
    };
    read(node, "HelpMeRangeIn", t.helpMeRangeIn);
    read(node, "HelpMeRangeOut", t.helpMeRangeOut);
    read(node, "kickMeRangeIn", t.kickMeRangeIn);
    read(node, "kickMeRangeOut", t.kickMeRangeOut);
    read(node, "KickTime", t.kickTime);
    readVec(node, "FlyTargetPosFromVrPlayer", t.flyTargetFromVrPlayer);
    read(path, "FlyStartSpeedRatio", t.flyStartSpeedRatio);
    read(path, "FlySpeedMin", t.flySpeedMin);
    read(path, "FlySpeedMax", t.flySpeedMax);
    read(path, "FlyHighestPointRatio", t.flyHighestPointRatio);
    read(path, "FlyHighestPointHeightToLen", t.flyHighestPointHeightToLen);
    read(path, "FlyGravity", t.flyGravity);
    readVec(path, "FlyBrakeStartOffset", t.flyBrakeStartOffset);
    return t;
}

LostBotSetup lostBotSetup(const Level& level, const LevelRuntime& runtime, std::size_t index) {
    const LevelObject& o = level.objects[index];
    LostBotSetup s;
    s.world = runtime.world(index);
    s.waitAnimFar = o.text("WaitAnimFar");
    s.waitAnimNear = o.text("WaitAnimNear");
    s.kickXZVelocity = o.number("KickXZVelocity").value_or(s.kickXZVelocity);
    s.kickYVelocity = o.number("KickYVelocity").value_or(s.kickYVelocity);
    s.kickGravity = o.number("KickGravity").value_or(s.kickGravity);
    s.faceToHero = o.flag("FaceToHero").value_or(false);
    s.kickAss = o.flag("KickAss").value_or(false);
    s.triggerOnGet = o.flag("TriggerOnGet").value_or(true);
    s.id = int(o.number("ID").value_or(0));
    for (int k = 1; k <= 5; ++k) {
        for (const LinkTarget& t : o.links("PathPoint" + std::to_string(k))) {
            if (const LevelObject* p = level.find(t.uuid)) {
                const math::Mat4& w = runtime.world(std::size_t(p - level.objects.data()));
                s.pathPoints.push_back({w[12], w[13], w[14]});
                break;
            }
        }
    }
    return s;
}

// ---- FlyPath ----------------------------------------------------------------------------------

void FlyPath::build(const std::vector<Vec3>& points, const Vec3& startTangent, const Vec3& endTangent) {
    segments_.clear();
    length_ = 0;
    if (points.empty()) {
        return;
    }
    std::vector<Vec3> p;
    p.reserve(points.size() + 2);
    p.push_back(points.front() - kPathStartScale * startTangent);
    p.insert(p.end(), points.begin(), points.end());
    p.push_back(points.back() + kPathEndScale * endTangent);
    first_ = points.front();
    last_ = points.back();
    startTangent_ = startTangent;
    endTangent_ = endTangent;
    highest_ = points.front().y;
    for (const Vec3& q : points) {
        highest_ = std::max(highest_, q.y);
    }
    std::vector<float> chords;
    for (std::size_t i = 0; i + 3 < p.size(); ++i) {
        const float chord = math::length(p[i + 1] - p[i + 2]);
        chords.push_back(chord);
        length_ += chord;
    }
    float done = 0;
    for (std::size_t i = 0; i < chords.size(); ++i) {
        const Vec3 &p0 = p[i], &p1 = p[i + 1], &p2 = p[i + 2], &p3 = p[i + 3];
        const Vec3 m1 = 0.5f * (p2 - p0), m2 = 0.5f * (p3 - p1);
        done += chords[i];
        Segment s;
        s.end = length_ > 0 ? done / length_ : 1;
        s.c3 = m1 + (2.0f * p1 - 2.0f * p2) + m2;
        s.c2 = (-3.0f * p1 + 3.0f * p2) - 2.0f * m1 - m2;
        s.c1 = m1;
        s.c0 = p1;
        segments_.push_back(s);
    }
}

int FlyPath::find(float u, float& t) const {
    float begin = 0;
    std::size_t i = 0;
    for (; i < segments_.size(); ++i) {
        if (u < segments_[i].end) {
            break;
        }
        begin = segments_[i].end;
    }
    if (i == segments_.size()) {
        i = segments_.size() - 1;   // u at the very end
        begin = i > 0 ? segments_[i - 1].end : 0;
    }
    const float span = segments_[i].end - begin;
    t = span > 0 ? (u - begin) / span : 1;
    return int(i);
}

Vec3 FlyPath::at(float u) const {
    if (segments_.empty() || u < 0) {
        return first_;
    }
    if (u > 1) {
        return last_;
    }
    float t = 0;
    const Segment& s = segments_[std::size_t(find(u, t))];
    return t * s.c1 + (t * t) * s.c2 + (t * t * t) * s.c3 + s.c0;
}

Vec3 FlyPath::tangent(float u) const {
    if (segments_.empty() || u < 0) {
        return startTangent_;
    }
    if (u > 1) {
        return endTangent_;
    }
    float t = 0;
    const Segment& s = segments_[std::size_t(find(u, t))];
    return math::normalize((3 * t * t) * s.c3 + (2 * t) * s.c2 + s.c1, startTangent_);
}

// ---- LostBot ----------------------------------------------------------------------------------

LostBot::LostBot(const LostBotSetup& setup, const LostBotTuning& tuning) : t_(tuning), setup_(setup) {
    const math::Mat4& m = setup.world;
    position_ = {m[12], m[13], m[14]};
    scale_ = std::sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
    scaleNow_ = scale_;
    const Vec3 forward = math::normalize({m[8], 0, m[10]}, {0, 0, 1});
    rotation_ = yawRotation(std::atan2(forward.x, forward.z));
    // The default clips unless both wait clips are named (0x726270).
    defaultClips_ = setup.waitAnimFar.empty() || setup.waitAnimNear.empty();
    // SpawnType Default: straight to waiting far (0x7275e9); the jetpack idles (0x72673c).
    play(defaultClips_ ? "help_01_far" : setup.waitAnimFar, kClipBlend);
    playJetpack("inactive_idle", 0);
}

std::string LostBot::flightClip(const char* prefix) const {
    char n[8];
    std::snprintf(n, sizeof n, "0%d", setup_.id % 3 + 1);   // 0x730cd0: "%02d" of ID % 3 + 1
    return std::string(prefix) + n;
}

// 0x727ad0: the clip, and the flag's clip of the same name + "_flag" (the view hides the flag
// when it has no such clip).
void LostBot::play(const std::string& name, float blend, float duration) {
    clip_ = {name, blend, duration, clip_.serial + 1};
    flagClip_ = {name + "_flag", blend, duration, flagClip_.serial + 1};
}

void LostBot::playJetpack(const std::string& name, float blend) {
    jetpackClip_ = {name, blend, 0, jetpackClip_.serial + 1};
    jetpackTime_ = 0;
}

// 0x72a600: towards the target's heading by 1 - (1 - rate dt)^(frame time x 60) of the way
// (frames of 1/60 s: rate x dt).
void LostBot::turnTowards(const Vec3& target, float dt) {
    const Vec3 d{target.x - position_.x, 0, target.z - position_.z};
    if (math::length(d) < kTurnMinDistance) {
        return;
    }
    const Quat goal = yawRotation(std::atan2(d.x, d.z));
    rotation_ = math::slerp(rotation_, goal, kTurnRate * dt);
}

void LostBot::update(const World& w) {
    arrived_ = false;
    leftController_ = false;
    const float dt = w.dt;
    const float d = w.hasHero ? math::length(w.hero - position_) : 10000.0f;
    const std::string far = defaultClips_ ? "help_01_far" : setup_.waitAnimFar;
    const std::string near = defaultClips_ ? "help_01_near" : setup_.waitAnimNear;
    switch (state_) {
    case State::Far:
        if (d < t_.helpMeRangeIn) {   // 0x72a260 (In outside state 2)
            play(near, kClipBlend);
            state_ = State::Near;
        }
        break;
    case State::Near:
        if (w.hasHero && (defaultClips_ || setup_.faceToHero)) {
            turnTowards(w.hero, dt);
        }
        if (!(d < t_.helpMeRangeOut)) {
            play(far, kClipBlend);
            state_ = State::Far;
        } else if (d < t_.kickMeRangeIn && (defaultClips_ || setup_.kickAss)) {
            // 0x72a310(7): kickassready, the jetpack's kickass (layer state 2).
            play("kickassready", kClipBlend);
            playJetpack("kickass", kClipBlend);
            jetpackLayer_ = 2;
            state_ = State::Ready;
        }
        break;
    case State::Ready:
        if (w.hasHero) {
            turnTowards(2.0f * position_ - w.hero, dt);   // its back to the hero
        }
        if (t_.kickMeRangeOut < d) {
            play(near, kClipBlend);
            state_ = State::Near;
        }
        break;
    case State::Flying:
        phaseTime_ += dt;
        if (phase_ == 0) {
            flyKicked(w);
        } else if (phase_ == 1) {
            flyPath(w);
        } else if (phase_ == 2) {
            flyEnter(w);
        }
        break;
    case State::Rescued:
        break;
    }
    // The jetpack (0x72d790): back to idle when the bot leaves the ready state unkicked; its
    // takeoff loops once takeoff_in has played.
    jetpackTime_ += dt;
    if (jetpackLayer_ == 2 && state_ != State::Ready && state_ != State::Flying) {
        playJetpack("inactive_idle", 0.2f);   // 0x123aaa0
        jetpackLayer_ = 1;
    } else if (jetpackLayer_ == 3 && w.takeoffClipLength <= jetpackTime_) {
        playJetpack("takeoff_loop", 0.1f);    // 0x123aaa4
        jetpackLayer_ = 4;
    }
}

void LostBot::kick(const Vec3& direction) {
    if (!kickable()) {
        return;
    }
    // 0x72b370: the launch velocity (+0x4870) along the hit, KickYVelocity up.
    const Vec3 h = math::normalize(direction, {0, 0, 1});
    velocity_ = {h.x * setup_.kickXZVelocity, setup_.kickYVelocity, h.z * setup_.kickXZVelocity};
    state_ = State::Flying;
    kicked_ = true;   // the controller is told (0x5f1820: its incoming count +1)
    World none;
    startPhase(0, none);
}

void LostBot::startPhase(int phase, const World& w) {
    phase_ = phase;
    phaseTime_ = 0;
    if (phase == 0) {
        // 0x730720: the bot's kickass clip, stretched to last KickTime.
        play("kickass", kKickBlend, t_.kickTime);
    } else if (phase == 1) {
        // 0x730cd0: the jetpack takes off; the flying clip; the path from here.
        if (jetpackLayer_ == 1 || jetpackLayer_ == 2) {
            playJetpack("takeoff_in", kTakeoffBlend);
            jetpackLayer_ = 3;
        }
        play(flightClip("backpack_flying_"), kFlyBlend);
        pathStart_ = position_;
        u_ = 0;
        sub_ = 0;
        subTime_ = 0;
        buildPath(w);
        speed_ = std::min(t_.flySpeedMax, std::sqrt(std::max(0.0f, 2 * t_.flyGravity * (path_.highest() - pathStart_.y))) *
                                              t_.flyStartSpeedRatio);
    } else if (phase == 2) {
        // 0x72f4c0: entering_controller_0N_in, from the brake offset's end into the controller.
        play(flightClip("entering_controller_") + "_in", kEnterBlend);
        endRotation_ = rotation_;
        sub_ = 0;
        subTime_ = 0;
    }
}

// 0x72d9a0: start, path points, the highest point, the target in front of the VR player.
void LostBot::buildPath(const World& w) {
    std::vector<Vec3> points;
    points.push_back(pathStart_);
    points.insert(points.end(), setup_.pathPoints.begin(), setup_.pathPoints.end());
    const Vec3 target = w.rig + t_.flyTargetFromVrPlayer + t_.flyBrakeStartOffset;
    const Vec3 last = points.back();
    const Vec3 flat{target.x - last.x, 0, target.z - last.z};
    Vec3 high = t_.flyHighestPointRatio * target + (1 - t_.flyHighestPointRatio) * last;
    high.y = std::max(last.y, target.y) + math::length(flat) * t_.flyHighestPointHeightToLen;
    points.push_back(high);
    points.push_back(target);
    const Vec3 start = math::normalize(points[1] - points[0], {0, 1, 0});
    const Vec3 end = -math::normalize(t_.flyBrakeStartOffset, {0, 0, -1});
    path_.build(points, start, end);
}

// Phase 0 (0x7308a0): ballistic; facing the path's start.
void LostBot::flyKicked(const World& w) {
    const float dt = w.dt;
    velocity_.y += setup_.kickGravity * dt;
    position_ += dt * velocity_;   // the cast against walls (radius 0.04 at +0.11) is not ported
    if (t_.kickTime < phaseTime_) {
        startPhase(1, w);   // the turn below still runs this update
    }
    pathStart_ = position_;
    buildPath(w);
    turnTowards(position_ + path_.tangent(0), dt);
}

// Phase 1 (0x731540).
void LostBot::flyPath(const World& w) {
    const float dt = w.dt;
    buildPath(w);   // the target follows the VR player
    const float len = std::max(path_.length(), 1e-4f);
    const Vec3 here = path_.at(u_);
    if (sub_ == 1) {
        subTime_ += dt;
        // Easing along the brake offset over the entering clip: s = |b| (2 tau - tau^2).
        const float b = math::length(t_.flyBrakeStartOffset);
        const float tau = subTime_ / std::max(w.enteringClipLength, 1e-3f);
        const float s = b * (2 * tau - tau * tau);
        position_ = path_.at(1) + s * -math::normalize(t_.flyBrakeStartOffset, {0, 0, -1});
        // Turning to face +z by (1 - cos(min(1, 2t) pi)) / 2 (fitted to run roof_bot5 within 0.01 deg).
        const float k = 0.5f * (1 - std::cos(std::min(1.0f, 2 * subTime_) * 3.14159265f));
        rotation_ = math::slerp(endRotation_, Quat{}, k);
        if (kEaseTime < subTime_) {
            startPhase(2, w);
        }
        return;
    }
    const float du = kLookAhead / len;
    const Vec3 ahead = path_.at(u_ + du) - here;
    const float step = std::max(math::length(ahead), 1e-6f);
    const float brakeFrom = std::max((len - kBrakeLength) / len, kBrakeMinPart);
    const float oldSpeed = speed_;   // the step below moves at the speed the update started with
    if (u_ <= brakeFrom) {
        // Height for speed: v^2 - 2 g dy per update (dy along the look-ahead, per metre moved).
        const float v2 = speed_ * speed_ - 2 * t_.flyGravity * (ahead.y * speed_ * dt / step);
        float v = t_.flySpeedMin;
        if (v2 > 0) {
            v = std::min(std::max(t_.flySpeedMin, std::sqrt(v2)), t_.flySpeedMax);
        }
        speed_ = v;
        rotation_ = lookAlong(path_.tangent(u_));
    } else {
        // Brake to the speed that covers the brake offset within the entering clip.
        const float endSpeed = 2 * math::length(t_.flyBrakeStartOffset) / std::max(w.enteringClipLength, 1e-3f);
        const float left = std::max(math::length(here - path_.at(1)), 1e-6f);
        speed_ = std::max(endSpeed, speed_ - (speed_ * speed_ - endSpeed * endSpeed) * 0.5f / left * dt);
        rotation_ = math::slerp(rotation_, Quat{}, 0.1f);   // a tenth of the way to facing +z (the player)
    }
    u_ += oldSpeed * dt / step * du;
    position_ = here;
    if (1 < u_) {
        sub_ = 1;
        subTime_ = 0;
        position_ = path_.at(1);
        endRotation_ = rotation_;
        play(flightClip("entering_controller_"), kEnterBlend);
    }
}

// Phase 2 (0x72f9f0). Sub-state 0: a jump (0xa5a940 with height 0.2 and gravity 15: up from rig
// + FlyTargetPosFromVrPlayer to 0.2 above it, down into the controller, straight across in time;
// fitted to run roof_bot5 within 0.1 mm, the routine itself not decoded) while the bot shrinks to
// 0.3 of its scale. Then: 0.3 -> 0.1 in 0.1 s and hidden (with the jetpack); after 0.7 s shown at
// scale 0.091 playing in_controller_landing; when it has played, in_controller_landing_out while
// the scale goes 0.091 -> 0.04 and the bot sinks 2 cm (cosine ease) with the clip's progress;
// at its end hidden for good (the controller shows the rescued bots, 0x730650; state 9).
void LostBot::flyEnter(const World& w) {
    const float dt = w.dt;
    subTime_ += dt;
    const Vec3& to = w.controller;
    switch (sub_) {
    case 0: {
        const Vec3 from = w.rig + t_.flyTargetFromVrPlayer;   // +0xc0, every update
        const float apex = std::max(from.y, to.y) + kEnterHeight;
        const float up = std::sqrt(2 * kEnterGravity * (apex - from.y));
        const float total = up / kEnterGravity + std::sqrt(2 * (apex - to.y) / kEnterGravity);
        float k = total > 0 ? subTime_ / total : 1;
        if (1 < k) {
            k = 1;
            position_ = to;
            arrived_ = true;
            reached_ = true;
            if (jetpackLayer_ != 0) {
                playJetpack("takeoff_out", 0);
                jetpackLayer_ = 5;
            }
            sub_ = 1;
            subTime_ = 0;
        } else {
            position_ = math::lerp(from, to, k);
            position_.y = from.y + up * subTime_ - 0.5f * kEnterGravity * subTime_ * subTime_;
        }
        rotation_ = math::slerp(endRotation_, Quat{}, k);   // to the controller's (upright here)
        scaleNow_ = scale_ * ((1 - k) + k * kArriveScale);
        break;
    }
    case 1: {
        const float k = subTime_ / kShrinkTime;
        scaleNow_ = scale_ * ((1 - k) * kArriveScale + k * kShrunkScale);
        position_ = to;
        if (kShrinkTime < subTime_) {
            hidden_ = true;
            jetpackHidden_ = true;
            sub_ = 2;
            subTime_ = 0;
            position_ = to - Vec3{0, 0.01f, 0};   // where it shows again (run roof_bot5)
        }
        break;
    }
    case 2:
        if (kReappearTime < subTime_) {
            hidden_ = false;
            jetpackHidden_ = false;
            scaleNow_ = kLandingScale;
            play("in_controller_landing", 0);
            sub_ = 3;
            landingTime_ = 0;
        }
        break;
    case 3:
        landingTime_ += dt;
        if (w.landingClipLength <= landingTime_) {
            play("in_controller_landing_out", kLandingBlend);
            sub_ = 4;
            subTime_ = 0;
        }
        break;
    case 4: {
        if (!countedOut_ && kLeaveControllerTime <= subTime_) {
            countedOut_ = true;
            leftController_ = true;   // 0x5f1860: the controller's incoming count -1
        }
        if (w.landingOutClipLength <= subTime_) {
            hidden_ = true;
            jetpackHidden_ = true;
            state_ = State::Rescued;
            phase_ = 3;
            break;
        }
        const float f = subTime_ / std::max(w.landingOutClipLength, 1e-3f);
        scaleNow_ = f * kLandedScale + (1 - f) * kLandingScale;
        drop_ = kLandingDrop * 0.5f * (1 - std::cos(f * 3.14159265f));
        break;
    }
    }
}

Vec3 LostBot::bodyCentre() const {
    constexpr float kRootHeight = 0.388f;   // AR_Root above the model's origin (bind pose)
    return position_ + rotate(rotation_, {0, kRootHeight * scale_, 0});
}

Vec3 LostBot::bodyHalf() const {
    return Vec3{0.25f, 0.35f, 0.35f} * scale_;   // mup_arbot_col: box rbd_hit
}

math::Mat4 LostBot::pose() const {
    return math::compose(rotation_, position_ + Vec3{0, drop_, 0}, {scaleNow_, scaleNow_, scaleNow_});
}

}   // namespace game
