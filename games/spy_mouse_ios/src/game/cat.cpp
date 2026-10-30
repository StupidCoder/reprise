#include "game/cat.hpp"

#include "game/projectiles.hpp"
#include "game/objects.hpp"

#include "game/tuning.hpp"

#include <algorithm>
#include <cmath>

namespace doc::game {

namespace {

constexpr float kTwoPi = 6.28318548f;
constexpr float kPi = 3.14159274f;

// Defaults of the cat values in the tuning table (Values::init @0x143548; read from the running
// original, reference run l11_60).
constexpr float kCatSpeed = 0.5f;        // +0x58
constexpr float kCatTurnRate = 1.0f;     // +0x60
constexpr float kCatViewDistance = 400.0f;  // +0x6c
constexpr float kCatViewAngle = 145.0f;     // +0x74, degrees

// ObjControllerCat::getCatValues @0x1a1958 for the brown cat: what a chase runs on.
struct ChaseValues {
    float lookMs = 600.0f;        // +0x90: the "?" stop when it first notices
    float closeDistance = 50.0f;  // +0x70: takes aim (heart) this close
    float runSpeed = 1.15f;       // +0x5c: running after the mouse
    float checkMs = 500.0f;       // +0x8c: next awareness check while chasing
    float speedFactor = 1.0f;     // per cat kind
    float pounceFactor = 1.5f;    // "cat_pounce_multi" (at least 0)
    int pounceMs = 550;           // +0x98: length of the pounce
    int aimMs = 700;              // +0x94: aiming before the pounce
    int stunnedMs = 2500;         // +0x9c: sitting dazed after pouncing into a wall
    float lead = 0.1f;            // +0xd0: how far ahead of a running mouse it aims
};
// The mean (black) cat, 0x34: "cat_mean_speed_mult" 1.6 and "cat_mean_intel_mult" (+0x7c,
// +0xd8): it notices sooner, runs faster, aims further ahead and never stops to pounce
// (pounce distance 0). Other kinds keep the brown cat's values until they are reconstructed.
constexpr int kScaryMaxSpeedMs = 1400;  // "scary_maxspeed_time" (g_pVal +0xcc)

ChaseValues chaseValuesFor(int type) {
    ChaseValues v;
    if (type == 0x34) {
        constexpr float kMeanSpeedMult = 1.60000002f;
        constexpr float kMeanIntelMult = 8.0000124f;
        v.lead = v.lead * kMeanIntelMult;
        v.speedFactor = kMeanSpeedMult;
        v.closeDistance = 0.0f;
        v.lookMs = v.lookMs / kMeanSpeedMult;
    } else if (type == 0x38) {
        // The ninja cat: "cat_ninja_speed_mult" (+0x80) 1.45, "cat_ninja_intel_mult" (+0xdc) 1.
        v.lead = v.lead * 1.0f;
        v.speedFactor = 1.45000005f;
    }
    return v;
}
// The ninja cat's shurikens ("ninja_burst_cooldown", "ninja_throw_cooldown", "ninja_throw_speed",
// "ninja_burst_count").
constexpr int kNinjaBurstMs = 600, kNinjaThrowMs = 250, kNinjaThrowSpeed = 150, kNinjaBurst = 3;
constexpr bool kInterceptMouse = true;   // "cat_intercept"
constexpr int kWallBonkMs = 150;         // "cat_wall_collide_time"
constexpr float kInterceptMaxDot = 0.408082068f;  // chase @0x1a3d84
constexpr int kReturnPathCapacity = 32;  // Object +0x168
constexpr int kSitMs = 2000;             // g_pVal +0x68: sitting at an empty spot, half to all of it

// Particles a chasing cat shows over its head (Particles kinds, ParticleSprites table).
constexpr int kParticleCaught = 0x00;    // the fight cloud
constexpr int kParticleHeart = 0x05;     // taking aim
constexpr int kParticleQuestion = 0x06;  // noticed something
constexpr int kParticleLost = 0x07;      // reached the spot, nothing there
constexpr int kParticleStars = 0x09;     // pounced into a wall
constexpr int kParticleSleep = 0x0b;     // "zZz" while sitting at a pause
constexpr int kParticleHypnotised = 0x0d;  // the swirl over a cat drawn to a TV

// Sound effects (sound table indices).
constexpr int kSoundBrownCatAgro = 0x12;  // cat_agro_brown: spotted the mouse
constexpr int kSoundCatAim = 0x19;
constexpr int kSoundCatCharge = 0x1a;
constexpr int kSoundCatHitWall = 0x1b;
constexpr int kSoundCatDazed = 0x5a;     // cat_dizzy, after the crash
constexpr int kSoundCatLost = 0x5b;
constexpr int kSoundDeathCloud = 0x60;
constexpr int kSoundCatPurr = 0x1d;      // cat_purr_loop (looping, per cat)
constexpr int kSoundCatHypnotised = 0x21;
constexpr float kHypnotizedSpeed = 0.5f;  // "cat_hypnotized_speed" (default in play)
constexpr int kTwineIgnoreMs = 15000;     // "twine_ignore_time" (g_pVal +0x148)
constexpr int kTwineDistractMs = 15000;   // "twine_distract_time" (g_pVal +0x144)
constexpr int kCatnipMs = 8200;           // "cat_catnip_time" (g_pVal +0x150)
constexpr int kTwineRollMs = 3000;        // a played-with ball rolls again (play @0x1a2c30)
constexpr int kSoundCatnip = 0x1c, kSoundTwine = 0x20;

// Route vertex types (MapLinking::linkPath).
constexpr int kRouteStart = 0x65;
constexpr int kRouteLoopEnd = 0x67;
constexpr int kRouteReverse = 0x66;
constexpr int kRouteCut = 0x85;

float wrapPositive(float a) {
    a = std::fmod(a, kTwoPi);
    if (a < 0.0f) a = a + kTwoPi;
    return a;
}

// Vec2D::rotate @0xf2b00: by degrees, worked out in double.
Vec2 rotated(Vec2 v, float degrees) {
    const double r = double(degrees * 0.0174532924f);
    const double s = libSin(r), c = libCos(r);
    return {float(c * double(v.x) - double(v.y) * s), float(s * double(v.x) + c * double(v.y))};
}

}  // namespace

CatRoute CatRoute::link(const data::MapFile& map, int tag) {
    CatRoute route;
    const auto& lines = map.secondLines;
    const auto& verts = map.vertices;
    auto vertex = [&](int i) -> const data::MapVertex* {
        return i >= 0 && std::size_t(i) < verts.size() ? &verts[std::size_t(i)] : nullptr;
    };
    for (std::size_t li = 0; li < lines.size(); ++li) {
        int from = lines[li].a, to = lines[li].b;
        // (MapLinking::linkPath @0x13ff24: a route starts on a vertex of type 0x65, 0x82 or 0x84.)
        auto isStart = [&](const data::MapVertex* v) {
            return v && (v->type == kRouteStart || v->type == 0x82 || v->type == 0x84) && std::abs(v->d) == tag;
        };
        const data::MapVertex* start = vertex(from);
        if (!isStart(start)) {
            std::swap(from, to);
            start = vertex(from);
            if (!isStart(start)) continue;
        }
        route.points.push_back({{start->x, start->y}, start->e});
        if (start->type == 0x84) route.startMode = 2;
        else if (start->type == 0x82) route.startMode = 1;
        // Along the lines that share the current vertex, never back the same way.
        std::size_t current = li;
        int at = to;
        // (linkPath has no length limit; a route uses each line once at most. 2-10's has 65+
        // points, run l210_watch.)
        for (std::size_t guard = 0; guard <= lines.size(); ++guard) {
            const data::MapVertex* v = vertex(at);
            if (!v) break;
            route.points.push_back({{v->x, v->y}, v->e});
            if (v->type == kRouteCut) route.points.back().pauseSeconds = -1;
            if (v->type == kRouteLoopEnd) {
                route.endMode = 2;
                route.points.push_back({route.points.front().position, 0});
                break;
            }
            // MapLinking::linkPath @0x13fe68: a reversing end (type 0x66) turns back; a route
            // ending on a plain point (0x64) stops there (end mode 0; run l111_explore).
            if (v->type == kRouteReverse) route.endMode = 1;
            bool found = false;
            for (std::size_t k = 0; k < lines.size(); ++k) {
                if (k == current) continue;
                if (lines[k].a == at || lines[k].b == at) {
                    at = lines[k].a == at ? lines[k].b : lines[k].a;
                    current = k;
                    found = true;
                    break;
                }
            }
            if (!found) break;
        }
        break;
    }
    return route;
}

Cat::Cat(int objectId, int type, int floor, Vec2 position, float radius, CatRoute route, SheetSize walkSheet,
         SheetSize idleSheet)
    : objectId_(objectId), type_(type), floor_(floor), position_(position), start_(position), radius_(radius),
      route_(std::move(route)), startRoute_(route_) {
    // ObjControllerCat::init: speed = the table's cat speed × the cat kind's factor (1 for the
    // brown cat), patrolling, the first awareness check two ticks in.
    speed_ = initialSpeed();
    state_ = initialState();
    awarenessMs_ = kTickMs * 2;
    chaseStep_ = 0;
    if (type == 0x38) throwMs_ = kNinjaThrowMs;  // (init: "ninja_throw_cooldown")
    velocity_ = initialVelocity();
    initialRadius_ = radius_;
    returnPath_.reserve(kReturnPathCapacity);
    // ObjControllerCat::loadAssets: walking frames 32 wide, idle ones 50 wide; walking, the
    // idle picture looking about, the stunned one once through.
    walk.setSheet(walkSheet.width, walkSheet.height, 32.0f);
    frameHeight_ = float(walkSheet.height);
    idle.setSheet(idleSheet.width, idleSheet.height, 50.0f);
    walk.setAnim(0, 3, SpriteAnim::Loop, 200);
    startIdlePicture();
    common.setAnim(0, 3, SpriteAnim::Once, 200);
}

// Fluffy (0x3a, 0x3b) runs at "fluffy_speed_mult" (just over 1) on top of his kind's factor:
// Fluffy's is that multiplier again (his speed reads 0.50000298 in run lbf_explore2), the
// henchman's "ghostboss_speed_mult" (just under 1: 0.50000095, run lbf4_watch). He starts out
// of play (0x12) until a cutscene brings him in (ObjControllerCat::init @0x19f9a6).
// (ObjControllerCat::init: the patrol speed times the kind's speed factor, getCatValues +0x10.)
float Cat::initialSpeed() const {
    constexpr float kFluffySpeedMult = 1.00000298f;     // g_pVal +0xf0
    constexpr float kGhostBossSpeedMult = 0.999998987f; // g_pVal +0x298
    if (type_ == 0x3a) return (kCatSpeed * kFluffySpeedMult) * kFluffySpeedMult;
    if (type_ == 0x3b) return (kCatSpeed * kGhostBossSpeedMult) * kFluffySpeedMult;
    return kCatSpeed * chaseValuesFor(type_).speedFactor;
}
// (1.1.1) init @0x19fa16: the boss cat (0x35) starts facing 0 with a velocity of 0.05 both
// ways (it stands until the chase starts; run lb1_explore).
Vec2 Cat::initialVelocity() const { return type_ == 0x35 ? Vec2{0.0500000007f, 0.0500000007f} : Vec2{}; }
// (1.1.1 init @0x19fa6a: Fluffy and his henchman start in the cutscene state 0x40; 1.0.5 had
// 0x16.)
int Cat::initialState() const {
    if (ridesBalloon_) return 0x18;
    return type_ == 0x3a || type_ == 0x3b ? 0x40 : 0;
}

// (1.1.1 init @0x19fa92) Fluffy faces a direction by the profile's world: left in worlds 1
// and 3..5, up-left in world 2, down-left in world 6; postInit @0x19fbd0 shows his pose
// picture (part 2, frame 5). (1.0.5 did this in the intro's ScriptSequence::showFluffy.)
// The henchman (0x3b) has no walking pictures but in world 6 (loadAssets @0x1a01ec): he is
// not drawn while he would walk.
void Cat::initForWorld(int world) {
    world_ = world;
    if (type_ == 0x3b && world != 5) walk.setSheet(0, 0, 32.0f);
    if (type_ == 0x3b && world == 3) radius_ = initialRadius_ = 15.0f;  // (loadAssets @0x1a0234: in his balloon, run lbf4_watch)
    if (type_ != 0x3a) return;
    posePart_ = 2;
    pose.setAnim(5, 5, SpriteAnim::Loop, 125);
    Vec2 d{};
    switch (world) {
    case 0: case 2: case 3: case 4: d = {-1.0f, 0.0f}; break;
    case 1: d = {-1.0f, -1.0f}; break;
    case 5: d = {-1.0f, 1.0f}; break;
    default: break;
    }
    float a = std::fmod(d.angleDegrees() * 0.0174532924f, kTwoPi);
    if (a < 0.0f) a = a + kTwoPi;
    heading_ = targetHeading_ = a;
}

void Cat::rideBalloon(SheetSize boss) {
    ridesBalloon_ = true;
    state_ = 0x18;
    balloonX_ = position_.x;
    balloon.setSheet(boss.width, boss.height, 48.0f);
    balloon.setAnim(0, 7, SpriteAnim::Loop, 400);
}

void Cat::restore(const ObjectSnapshot& o) {
    state_ = o.state;
    chaseStep_ = o.step;
    position_ = o.position;
    heading_ = o.heading;
    routeIndex_ = o.routeIndex;
    speed_ = o.speed;
    // (The snapshot keeps the radius as a whole number, runs lb2_win, lbf_try2.)
    radius_ = float(o.radius);
    walk.setFrame(o.frame);
    // (Snapshot<Object>::rollback @0x10bc10: the route as loaded, trimmed by the points the cat
    // had dropped, Path::trim.)
    route_ = startRoute_;
    trimmed_ = o.trimmed;
    if (trimmed_ >= 1) {
        const std::size_t n = std::min(route_.points.size(), std::size_t(trimmed_));
        route_.points.erase(route_.points.begin(), route_.points.begin() + std::ptrdiff_t(n));
    }
}

// SceneGame::startGame on a reload: the cat object is reused and its controller's init,
// loadAssets and postInit run again over it (fields they do not set keep their values).
void Cat::reload() {
    // (1.1.1 run lb2_win: the aim, the calm timer, the way back and the velocity start over.)
    targetHeading_ = 0.0f;
    calmMs_ = 0;
    stoppedForGood_ = false;  // (its object's running flag comes from the snapshot, run lb4_win)
    steerOffset_ = 0.0f;      // (+0x110 and +0x1ec start over too, run l3h2_caught)
    chaseTimerMs_ = 0;
    returnPath_.clear();
    returnIndex_ = 0;
    routeDirection_ = 1;  // (a back-and-forth route starts forward again, run l3h2_win)
    velocity_ = initialVelocity();
    speed_ = initialSpeed();
    if (type_ == 0x35) {
        heading_ = 0.0f;
        velocity_ = initialVelocity();
    }
    state_ = initialState();
    awarenessMs_ = kTickMs * 2;
    chaseStep_ = 0;
    route_ = startRoute_;
    animatedMs = 0;
    facing_ = -1;
    walk.setAnim(0, 3, SpriteAnim::Loop, 200);
    startIdlePicture();
    common.setAnim(0, 3, SpriteAnim::Once, 200);
    if (world_ >= 0) initForWorld(world_);  // (init and postInit by the world again)
}

void Cat::restart(bool reload) {
    if (reload) {
        // SceneGame::startGame runs the cat's init and loadAssets again first.
        speed_ = initialSpeed();
        radius_ = initialRadius_;  // (postInit)
        route_ = startRoute_;  // (postInit links the route again: points dropped on the way are back)
        animatedMs = 0;
        facing_ = -1;
    }
    returnPath_.clear();
    returnIndex_ = 0;
    routeIndex_ = 0;
    routeDirection_ = 1;
    calmMs_ = 0;
    timer1e0_ = 0;
    heading_ = targetHeading_ = steerOffset_ = 0.0f;
    collided_ = false;
    steerUntilMs_ = 0;
    chaseTimerMs_ = 0;
    meanRampMs_ = 0;
    collisionStreak_ = 0;
    headBumps_ = sightings_ = 0;
    position_ = start_;
    velocity_ = initialVelocity();
    particle_ = -1;  // (the old particle is shrunk away; the new level's pool is fresh)
    walk.setAnim(0, 3, SpriteAnim::Loop, 200);
    startIdlePicture();
    common.setAnim(0, 3, SpriteAnim::Once, 200);
    state_ = initialState();  // (Object::reInit runs the controller's init again)
    chaseStep_ = 0;
    awarenessMs_ = kTickMs * 2;
}

// Object::loadAssets for a cat: its pictures start over.
void Cat::reloadPictures() {
    walk.setAnim(0, 3, SpriteAnim::Loop, 200);
    startIdlePicture();
    common.setAnim(0, 3, SpriteAnim::Once, 200);
}

// SceneGame::resetCats @0xf80d0 (1.1.1): Object::reInit, init and postInit, but not
// loadAssets: the cat starts over while its pictures run on as they were (run l3h2_win).
void Cat::reset() {
    const SpriteAnim keepWalk = walk, keepIdle = idle, keepCommon = common;
    restart(true);
    walk = keepWalk;
    idle = keepIdle;
    common = keepCommon;
}

void Cat::startIdlePicture() {
    if (type_ == kBlackCat) {
        setMeanCatIdleSequence("idle_0", position_ + rotated({-1.0f, 0.0f}, heading_ * 57.2957764f));
        return;
    }
    // (The ghost cat looks about on frames 2..5.)
    idle.setAnim(type_ == 0x37 ? 2 : 3, type_ == 0x37 ? 5 : 6, SpriteAnim::Loop, 250);
}

void Cat::setIdleSequences(const std::vector<data::AnimSequence>& sequences) {
    for (const data::AnimSequence& s : sequences) idle.addSequence(s);
    startIdlePicture();
}

void Cat::setMeanCatIdleSequence(const char* prefix, Vec2 target) {
    const Vec2 d = target - position_;
    float a = std::fmod(float(std::atan2(d.y, d.x)), kTwoPi);
    if (a < 0.0f) a = a + kTwoPi;
    const int dir = spriteDirection(a, 8);
    const bool mirrored = unsigned(dir - 5) < 3u;
    const int index = idle.findSequence(prefix + std::to_string(mirrored ? 8 - dir : dir));
    if (index >= 0) idle.playSequence(index, SpriteAnim::SequenceLoop, 0);
    idleMirrored_ = mirrored;
}

void Cat::setCommonSheet(SheetSize sheet) {
    common.setSheet(sheet.width, sheet.height, 32.0f);
    common.setAnim(0, 3, SpriteAnim::Once, 200);
}

void Cat::setHenchmanSheets(SheetSize cry, SheetSize splash, float splashWidth) {
    common.setSheet(cry.width, cry.height, 64.0f);
    pose.setSheet(splash.width, splash.height, splashWidth);
}

void Cat::dunkHenchman() {
    state_ = 0x40;
    common.setAnim(0, 0, SpriteAnim::Hold, 0);
    posePart_ = 0;
}

void Cat::splashHenchman() {
    pose.setAnim(0, 4, SpriteAnim::Once, 150);
    state_ = 0x17;
    common.setAnim(1, common.frameCount() - 1, SpriteAnim::Loop, 125);
}

void Cat::launchHenchman(Vec2 at, int lastFrame) {
    position_ = at;
    state_ = 0x40;
    common.setAnim(0, lastFrame, SpriteAnim::Loop, 50);
    posePart_ = 0;
    henchmanLaunched_ = true;
    heading_ = 180.0f;
}

void Cat::landHenchman() {
    pose.setAnim(0, 4, SpriteAnim::Once, 150);
    heading_ = 0.0f;
}

void Cat::shockHenchman(int frame) {
    state_ = 0x17;
    common.setAnim(frame, frame, SpriteAnim::Hold, 200);
}

void Cat::prepareFrame() {
    const Picture shown = picture();
    if (state_ == 9 || state_ == 0x11 || shown == Picture::Common || shown == Picture::None) return;
    // (renderCat @0x1a42e8: a picture without frames is not drawn.)
    if (shown == Picture::Walk && walk.frameCount() <= 0) return;
    targetHeading_ = wrapPositive(targetHeading_);
    // The facings come from the picture's set: a single picture (the boss cat's and Fluffy's
    // sitting, Fluffy's pose, the henchman's balloon) has one, so it always faces 0.
    const bool single = shown == Picture::Pose || shown == Picture::Balloon ||
                        (shown == Picture::Idle && (type_ == 0x35 || type_ == 0x3a || type_ == 0x3b));
    const int dir = spriteDirection(targetHeading_ - 0.0f, single ? 1 : 8);  // (the scene is not rotated)
    if (facing_ != dir && animatedMs > 50) {
        facing_ = dir;
        animatedMs = 0;
    }
}

Cat::Picture Cat::picture() const {
    if (type_ == 0x3a || type_ == 0x3b) {
        if (state_ == 0x40) return posePart_ == 2 ? Picture::Pose : posePart_ == 0 ? Picture::Common
                                 : posePart_ == 1 ? Picture::Idle : Picture::Walk;
        if (state_ == 0x16) return Picture::None;
        if (state_ == 0x17) return Picture::Common;
        if (state_ == 0x18 && ridesBalloon_) return Picture::Balloon;
        return Picture::Walk;
    }
    switch (state_) {
    case 0: return velocity_.x * velocity_.x + velocity_.y * velocity_.y == 0.0f ? Picture::Idle : Picture::Walk;
    case 2: case 3: case 0xd: return Picture::Common;
    case 5: case 6: return Picture::Walk;
    case 7: return Picture::Idle;  // (1.1.1: stunned, the idle sprite's daze frames)
    case 8: return lurePlayed_ ? (type_ == 0x32 || type_ == kBlackCat ? Picture::Idle : Picture::Common) : Picture::Walk;
    case 9: case 10: case 11: case 0x11: return Picture::Idle;
    default: return Picture::Walk;
    }
}

const SpriteAnim& Cat::activeSprite() const {
    switch (picture()) {
    case Picture::Idle: return idle;
    case Picture::Common: return common;
    case Picture::Pose: return pose;
    case Picture::Balloon: return balloon;
    default: return walk;
    }
}

// Object::updatePath @0x115614: heads for route point routeIndex_; within 0.4 radius of it the
// point is reached (it may be a pause, or cut the route short) and the next one taken, several
// in one tick if need be. Past an end: a route that starts and ends in mode 2 runs on from its
// last-but-one point; else by its end mode the cat stops facing along the last leg (0), turns
// back (1) or starts again at point 0 going forwards (2). The aim is set towards the last
// point looked at.
float Cat::followRoute(float speed, CatWorld& world) {
    int count = int(route_.points.size());
    if (routeIndex_ < 0 || routeIndex_ >= count) return 0.0f;
    auto point = [&](int i) { return route_.points[std::size_t(i)].position; };
    const float reach = radius_ * 0.400000006f;
    const float reachSquared = reach * reach;
    float dx = point(routeIndex_).x - position_.x;
    float dy = point(routeIndex_).y - position_.y;
    for (;;) {
        if (dx * dx + dy * dy > reachSquared) break;
        bool cut = false;
        const int pause = route_.points[std::size_t(routeIndex_)].pauseSeconds;
        auto trim = [&] {
            // Path::trim @0x11d780 (n = index + 1): the route so far is dropped, the cat goes on
            // from here; a looping route's last point becomes its new first.
            if (routeIndex_ >= count - 1) return;
            trimmed_ = routeIndex_ + 1;  // (+0x208)
            route_.points.erase(route_.points.begin(), route_.points.begin() + routeIndex_ + 1);
            routeIndex_ = 0;
            cut = true;
            if (route_.endMode == 2) route_.points.back().position = route_.points.front().position;
        };
        if (routeIndex_ + routeDirection_ == count && route_.endMode == 3) {
            if (pause == -1) trim();
        } else if (pause >= 1) {
            // A pause: the cat sits down (the common picture's frames 0-3 once) and purrs.
            state_ = 2;
            common.setAnim(0, 3, SpriteAnim::Once, 200);
            if (!purring_) world.sounds.push_back({SoundEvent::Play, kSoundCatPurr, {}, true});
            purring_ = true;
            awarenessMs_ = pause * 1000;
        } else if (pause == -1) {
            trim();
        }
        const int previous = routeIndex_;
        routeIndex_ += routeDirection_;
        count = int(route_.points.size());
        if (routeIndex_ >= 0 && routeIndex_ < count) {
            if (cut && routeIndex_ >= 1) routeIndex_ = 0;
            dx = point(routeIndex_).x - position_.x;
            dy = point(routeIndex_).y - position_.y;
            steerOffset_ = 0.0f;  // a new point: the whiskers' turn is dropped
            continue;
        }
        if (routeIndex_ < 0 && route_.startMode == 2 && route_.endMode == 2) {
            routeIndex_ = count - 2;
            break;
        }
        switch (route_.endMode) {
        case 0:
            if (routeIndex_ < 2) break;
            routeIndex_ = previous;
            heading_ = targetHeading_ = std::atan2(point(previous).y - point(previous - 1).y,
                                                   point(previous).x - point(previous - 1).x);
            // (The black cat sits looking along its heading.)
            if (type_ == kBlackCat) setMeanCatIdleSequence("idle_0", position_ + rotated({1.0f, 0.0f}, heading_ * 57.2957764f));
            return 0.0f;
        case 1:
            routeIndex_ -= 2 * routeDirection_;
            routeDirection_ = -routeDirection_;
            dx = point(routeIndex_).x - position_.x;
            dy = point(routeIndex_).y - position_.y;
            heading_ = targetHeading_ = std::atan2(dy, dx);
            break;
        case 2:
            routeDirection_ = 1;
            routeIndex_ = 0;
            break;
        default:
            break;  // (mode 3 links to another route; not on the reconstructed levels)
        }
        break;
    }
    if (routeIndex_ < count) targetHeading_ = std::atan2(dy, dx);
    return speed;
}

// Object::setReturnPos: the way back ends at `target`. While the second-to-last point still
// sees the target the last point just moves there; once a wall is in between, a new leg
// starts where the cat stands (at the point it is heading for along the way).
void Cat::setReturnPos(Vec2 target, const CatWorld& world) {
    if (returnPath_.empty()) {
        returnPath_ = {position_, target};
        return;
    }
    // (With a single point left the original reads the slot before the array; the cat's own
    // position stands in for it here. Not seen in the reference runs.)
    const Vec2 previous = returnPath_.size() >= 2 ? returnPath_[returnPath_.size() - 2] : position_;
    if (!world.walls.lineTouches(target, previous, world.viewLeft, 1)) {
        returnPath_.back() = target;
        return;
    }
    const int at = std::max(returnIndex_, 0);
    if (at >= kReturnPathCapacity - 2) return;
    returnPath_.resize(std::size_t(at) + 2);
    returnPath_[std::size_t(at)] = position_;
    returnPath_[std::size_t(at) + 1] = target;
}

// Object::updateReturnPath: walks the way back. Forwards (chasing) from point returnIndex_
// on; `backToRoute` from the last point to the first, dropping each point reached. Points
// within the cat's radius count as reached. Returns false when there is nothing left to walk.
bool Cat::followReturnPath(bool backToRoute) {
    int count = int(returnPath_.size());
    int i;
    if (backToRoute) {
        i = count - 1;
        returnIndex_ = i;
    } else {
        if (returnIndex_ < 0) returnIndex_ = 0;
        else if (returnIndex_ >= count) returnIndex_ = count - 1;
        i = returnIndex_;
    }
    if (count <= 0) return false;
    float dx = returnPath_[std::size_t(i)].x - position_.x;
    float dy = returnPath_[std::size_t(i)].y - position_.y;
    for (;;) {
        if (radius_ * radius_ < dx * dx + dy * dy) break;
        steerOffset_ = 0.0f;
        if (backToRoute) {
            --count;
            returnPath_.resize(std::size_t(count));
            i = count - 1;
            returnIndex_ = i;
            if (i < 0) return false;
        } else {
            ++i;
            ++returnIndex_;
            if (i >= count) return false;
        }
        dx = returnPath_[std::size_t(i)].x - position_.x;
        dy = returnPath_[std::size_t(i)].y - position_.y;
    }
    targetHeading_ = wrapPositive(std::atan2(dy, dx));
    return true;
}

// Object::getClosestPathPoint: the nearest route point in clear sight, -1 if none.
int Cat::closestRoutePoint(const CatWorld& world) const {
    int best = -1;
    float bestSquared = 837873.0f;
    for (std::size_t i = 0; i < route_.points.size(); ++i) {
        const Vec2 point = route_.points[i].position;
        if (world.walls.lineTouches(position_, point, world.viewLeft, 1) && type_ != 0x37) continue;
        const float d = (point - position_).lengthSquared();
        if (d < bestSquared) {
            bestSquared = d;
            best = int(i);
        }
    }
    return best;
}

// Object::canSeeTarget @0x112c00: the point it is heading for (on the way back, else on the
// route) is within its radius or in clear sight.
bool Cat::canSeeGoal(const CatWorld& world) const {
    Vec2 goal;
    if (returnIndex_ >= 0 && returnIndex_ < int(returnPath_.size())) {
        goal = returnPath_[std::size_t(returnIndex_)];
    } else if (returnIndex_ < 0) {
        if (routeIndex_ < 0 || routeIndex_ >= int(route_.points.size())) return true;
        goal = route_.points[std::size_t(routeIndex_)].position;
    } else {
        return true;  // (the original reads past the way back's end; not reached)
    }
    if ((goal - position_).lengthSquared() < radius_ * radius_ || type_ == 0x37) return true;
    return !world.walls.lineTouches(position_, goal, world.viewLeft, 1);
}

// ObjControllerCat::applySteering: two whiskers 23 ahead at ±35 degrees of the aim; one that
// crosses a wall turns the aim away by 0.07 for a while (the turn decays by 0.9 per tick).
// Not while taking aim, pouncing or dazed. A whisker counts walls of types 0, 0x6e and 0x90
// (three SceneGame::doesLineTouch calls by type, 1.1.1 @0x1a1bfa).
bool Cat::whiskerTouches(const CatWorld& world, Vec2 tip) const {
    for (int type : {0, 0x6E, 0x90})
        if (world.walls.lineTouchesType(position_, tip, type, world.viewLeft)) return true;
    return false;
}

void Cat::steer(const CatWorld& world, float speed) {
    if (speed > 0.00100000005f && type_ != 0x37 && state_ != 6 && state_ != 0x13) {
        const Vec2 left = rotated({23.0f, 0.0f}, targetHeading_ * 57.2957764f + -35.0f) + position_;
        if (whiskerTouches(world, left)) {
            steerOffset_ = steerOffset_ + 0.0700000003f;
            steerUntilMs_ = world.gameTimeMs + 100;
        }
        const Vec2 right = rotated({23.0f, 0.0f}, targetHeading_ * 57.2957764f + 35.0f) + position_;
        if (whiskerTouches(world, right)) {
            steerOffset_ = steerOffset_ + -0.0700000003f;
            steerUntilMs_ = world.gameTimeMs + 100;
        }
        targetHeading_ = wrapPositive(targetHeading_ + steerOffset_);
    }
    steerOffset_ = steerOffset_ * 0.899999976f;
    flock(world);
}

// The rest of applySteering (@0x1a1d50): while its whiskers have been quiet for 100 ms, a cat
// running to where it saw the mouse (chase step 1) keeps clear of other chasing cats within
// twice their combined radius that head the same way (aims more than 60° apart do not count):
// pushed off each by (their radii x 8 / distance) per tick, and turned partly along their aims.
void Cat::flock(const CatWorld& world) {
    if (steerUntilMs_ >= world.gameTimeMs || !world.cats) return;
    if (!(state_ == 5 && chaseStep_ == 1)) return;
    if (type_ == 0x35 || type_ == 0x36 || (type_ | 2) == 0x3a) return;
    const Vec2 aim{libCos(targetHeading_), libSin(targetHeading_)};
    std::vector<const Cat*> near;
    for (const Cat& other : *world.cats) {
        if (&other == this || other.floor_ != floor_ || other.state_ != 5) continue;
        const float reach = other.radius_ + radius_;
        if ((other.position_ - position_).lengthSquared() < (reach * reach) * 4.0f && near.size() < 16)
            near.push_back(&other);
    }
    if (near.empty()) return;
    constexpr float kFlockWeight = 1.0f;  // getCatValues +0x28 for the brown cat
    const float share = 1.0f / float(near.size());
    Vec2 away, along;
    for (const Cat* other : near) {
        const Vec2 offset = position_ - other->position_;
        const Vec2 otherAim{libCos(other->targetHeading_), libSin(other->targetHeading_)};
        const float dot = aim.dot(otherAim);
        if (!(dot > 0.5f)) continue;
        Vec2 direction = offset;
        const float distance = std::max(1.0f, direction.normalise(1.0f));
        const float push = (1.0f / distance) * ((radius_ + other->radius_) * (kFlockWeight * 8.0f));
        away += ((direction * push) * share) * (1.0f / float(kTickMs));
        along += (otherAim * share) * dot;
    }
    const float weight = kFlockWeight > 0.0f ? 2.0f / kFlockWeight : 0.0f;
    Vec2 result = aim + away * weight;
    result += along * weight;
    result *= 1.0f / weight + kFlockWeight;
    result.normalise(1.0f);
    targetHeading_ = std::atan2(result.y, result.x);
}

// Object::canSeePlayer: the mouse (walking, idle or carrying; not in the dark, not hidden)
// within the view distance, inside the view angle about the heading and in clear sight.
bool Cat::canSeeMouse(const CatWorld& world, bool wide, float halfAngle) {
    seenMouse_ = false;
    targetId_ = -1;
    if (type_ == 0x36) return false;
    if (state_ >= 0 && state_ < 24 && ((1u << state_) & 0xc01202u) != 0) return false;  // (@0x113874)
    if (calmMs_ >= 1) return false;
    const float onScreen = position_.x - world.viewLeft;
    if (!(onScreen >= 0.0f) || !(onScreen < kScreenWidth)) return false;
    float half = halfAngle;
    if (half < 0.0f) half = kCatViewAngle * 0.0174532924f;
    half = half * (wide ? 0.800000012f : 0.5f);
    const CatTarget& m = world.mouse;
    if (!m.active || m.floor != floor_) return false;
    if (m.state < 0 || m.state > 8 || ((1u << m.state) & 0x111u) == 0) return false;
    if (m.carriesSomething) return false;
    Vec2 toward = m.position - position_;
    const float distance = toward.normalise(1.0f);
    if (!(distance < kCatViewDistance)) return false;
    float off = std::fmod(heading_ - std::atan2(toward.y, toward.x), kTwoPi);
    if (off > kPi) off = off + -kTwoPi;
    if (off < -kPi) off = off + kTwoPi;
    if (!(std::fabs(off) < half)) return false;
    if (world.walls.lineTouches(position_, m.position, world.viewLeft, 1)) return false;
    seenMouse_ = true;
    targetId_ = 0;  // the player's index
    seenAt_ = m.position;
    return true;
}

LevelObject* Cat::target(CatWorld& world) const {
    if (world.objects) {
        if (targetId_ < 0 || targetId_ >= int(world.objects->size())) return nullptr;
        return &(*world.objects)[std::size_t(targetId_)];
    }
    for (LevelObject* o : world.playthings)
        if (o->id == targetId_) return o;
    return nullptr;
}

// canSeePlaything @0x1a10c4: neither the boss cat nor the blind cat, and not already on its way
// (6) or busy (10): the first TV (in object order) that plays on its floor, closer than the
// sight distance, within 0.8 x 145 degrees either side of its heading and in clear sight.
// Seeing it the first time, the cat takes over its play time as its check timer (60001 stops
// the checks for good), gets the swirl over its head (particle 0xd) and sound 0x21.
bool Cat::canSeePlaything(CatWorld& world) {
    if (type_ == 0x35 || type_ == 0x36) return false;
    if (state_ == 8 || state_ == 12) return false;
    const float range = kCatViewDistance * kCatViewDistance;
    for (LevelObject* o : world.playthings) {
        if (!o->active || o->id == objectId_ || o->floor != floor_ || o->inTrail) continue;
        if (o->type != kTv || o->state == ObjectState(kTvOff)) continue;
        const Vec2 d = o->position - position_;
        if (!(d.lengthSquared() < range)) continue;
        float a = std::fmod(heading_ - float(std::atan2(d.y, d.x)), kTwoPi);
        if (a > kPi) a = a + -kTwoPi;
        if (a < -kPi) a = a + kTwoPi;
        if (!(std::fabs(a) < kCatViewAngle * 0.0139626339f)) continue;
        if (world.walls.lineTouchesType(position_, o->position, 0, world.viewLeft)) continue;  // solid walls only
        targetId_ = o->id;
        awarenessMs_ = o->timerMs;
        if (particle_ == -1) attachParticle(world, kParticleHypnotised);
        world.sounds.push_back({SoundEvent::Play, kSoundCatHypnotised});
        return true;
    }
    return false;
}

// play @0x1a2b90 (state 6): it heads for the TV along its way back at "cat_hypnotized_speed"
// (0.5) until it is as close as the TV's first seat is to the TV; then it takes the first free
// seat (a random one when all are taken) and walks dazed to it (state 1); the TV's picture runs
// a second time that tick. It ignores balls of twine for 15 s ("twine_ignore_time", +0x1e0).
float Cat::play(int dtMs, CatWorld& world) {
    LevelObject* t = target(world);
    if (!t) return 0.0f;
    float speed = chaseValuesFor(type_).runSpeed * chaseValuesFor(type_).speedFactor;
    seenAt_ = t->position;
    setReturnPos(seenAt_, world);
    const float distance = (seenAt_ - position_).lengthSquared();
    float near = radius_ * radius_;
    if (t->type == kTwine) {
        // A brown or black cat on its "playing" sequence sets a stopped ball rolling again when
        // the sequence is back on its first two frames (run l45_lure: 40 ticks after it stopped).
        if (type_ == 0x32 || type_ == kBlackCat)
            if (idle.currentSequence() >= 0 && idle.sequenceFrame() < 2 && t->rollMs == 0) t->rollMs = kTwineRollMs;
    } else if (t->type == kCatnip) {
        const float reach = radius_ + t->radius;
        near = reach * reach;
    } else if (t->type == kTv && !t->seats.empty()) {
        const Vec2 seat = t->seats.front();
        const float dx = seat.x - t->position.x, dy = seat.y - t->position.y;
        near = dx * dx + dy * dy;
        speed = kHypnotizedSpeed;
    }
    if (distance > near) {
        followReturnPath(false);
    } else if (t->type != kTv) {
        // At a lure: it is let go for good (0x1e) and no snap target; catnip (or cheese) is
        // eaten (state 0xd, frames 10-13; catnip runs and fades over its time, sound 0x1c);
        // twine more than a whisker away pulls the cat halfway to it (the ball follows; the
        // common picture's frames 14-17, the purr and sound 0x20). Its picture runs once more.
        if (!isCheese(t->type)) {
            t->state = ObjectState(0x22);
            t->snappable = false;
        }
        if (isCheese(t->type) || t->type == kCatnip) {
            state_ = 0xf;
            walk.setAnim(10, 13, SpriteAnim::Loop, 200);
            if (t->type == kCatnip) {
                world.sounds.push_back({SoundEvent::Play, kSoundCatnip});
                t->updating = true;
                t->playMs = 2000;
                t->timerMs = kCatnipMs;
            }
        } else if (distance > 0.0100000007f) {
            position_ = (position_ + seenAt_) * 0.5f;  // (the factor is a hypothesis)
            if (t->type == kTwine) {
                common.setAnim(14, 17, SpriteAnim::Loop, 200);
                t->position = position_;
                if (!purring_) world.sounds.push_back({SoundEvent::Play, kSoundCatPurr, {}, true});
                purring_ = true;
                world.sounds.push_back({SoundEvent::Play, kSoundTwine});
            }
        }
        t->anim.update(dtMs);
        speed = 0.0f;
    } else {
        const int seats = int(t->seats.size());
        int i = 0;
        while (i < seats && t->watchers[std::size_t(i)] != 0) ++i;
        if (i == seats) i = int(world.random.next()) % seats;
        t->watchers[std::size_t(i)] = objectId_;
        seenAt_ = t->seats[std::size_t(i)];
        state_ = 1;
        setReturnPos(seenAt_, world);
        speed = 0.0f;
        t->anim.update(dtMs);
    }
    timer1e0_ = kTwineIgnoreMs;
    return speed;
}

// dazewalk @0x1a2a88 (state 1): to its seat at its own speed; there it sits hypnotised
// facing the TV (state 7; the black cat on its "idle_tv_0N" sequence) and keeps the TV's
// play time as its check timer.
float Cat::dazewalk(CatWorld& world) {
    const float speed = speed_;
    LevelObject* t = target(world);
    if ((seenAt_ - position_).lengthSquared() > radius_ * radius_) {
        followReturnPath(false);
    } else {
        state_ = 9;
        if (t) seenAt_ = t->position;
        if (type_ == kBlackCat) {
            setMeanCatIdleSequence("idle_tv_0", seenAt_);
        } else {
            idle.playOneShot(1, 3, 150);
            idle.setAnim(3, 6, SpriteAnim::Loop, 250);
        }
    }
    if (t) awarenessMs_ = t->timerMs;
    return speed;
}

// hypnotised @0x1a29b0 (state 7): the swirl stays over its head; it faces the TV.
void Cat::hypnotised(CatWorld& world) {
    if (particle_ == -1) attachParticle(world, kParticleHypnotised);
    Vec2 d = seenAt_ - position_;
    d.normalise(1.0f);
    targetHeading_ = float(std::atan2(d.y, d.x));
    if (LevelObject* t = target(world)) awarenessMs_ = t->timerMs;
}

namespace {

// The view test of getValidTrailTarget and canSeeTrail: within half the view angle (145 x
// 0.0087266 radians) either side of the heading and no solid (type 0) wall in between.
bool lureInView(float heading, Vec2 from, Vec2 to, const CatWorld& world) {
    Vec2 toward = to - from;
    toward.normalise(1.0f);
    float off = std::fmod(heading - float(std::atan2(toward.y, toward.x)), kTwoPi);
    if (off > kPi) off = off + -kTwoPi;
    if (off < -kPi) off = off + kTwoPi;
    if (!(std::fabs(off) < kCatViewAngle * 0.00872664619f)) return false;
    return !world.walls.lineTouchesType(from, to, 0, world.viewLeft);
}

bool isLure(int type) { return type == kTwine || type == kCatnip; }

}  // namespace

// getValidTrailTarget @0x1a1384: from the trail's head, the first twine or catnip if it is in
// view (nothing after it is looked at); for the orange cat (0x33) also the last cheese in view
// before it. (The distance is held against the sight distance squared: always within.)
int Cat::validTrailTarget(const CatWorld& world) const {
    int found = -1;
    for (const LevelObject* o : world.trail) {
        const bool lure = isLure(o->type);
        if (!lure && !(type_ == 0x33 && isCheese(o->type))) continue;
        if (o->id >= 0) {
            const float distance = (o->position - position_).length();
            if (distance < kCatViewDistance * kCatViewDistance && lureInView(heading_, position_, o->position, world))
                found = o->id;
        }
        if (lure) break;
    }
    return found;
}

// ObjControllerCat::canSeeTrail @0x1a1514: not the blind or boss cat, not dazed (1), sitting
// hypnotised (7) or busy (10), not calmed (+0x1e4) and carrying nothing (+0x17c): a mouse in
// play on its floor, walking (+0x130 = 0) with a trail whose lure is in view and closer than
// the sight distance makes the lure its target (+0x1d8) and where it saw it (+0x1c8).
bool Cat::canSeeTrail(const CatWorld& world) {
    seenTrail_ = false;
    targetId_ = -1;
    if (type_ == kBlindCat || type_ == kBossCat || state_ == 1 || state_ == 9 || state_ == 12 || calmMs_ >= 1)
        return false;
    const CatTarget& m = world.mouse;
    if (!m.active || m.floor != floor_ || m.hurtMs >= 1 || m.state != 0 || world.trail.empty()) return false;
    const int id = validTrailTarget(world);
    if (id == -1) return false;
    for (const LevelObject* o : world.trail) {
        if (o->id != id) continue;
        if (!o->active) return false;
        if (!((o->position - position_).length() < kCatViewDistance)) return false;
        if (!lureInView(heading_, position_, o->position, world)) return false;
        targetId_ = id;
        seenTrail_ = true;
        seenAt_ = o->position;
        return true;
    }
    return false;
}

// stealTrail @0x1a173c: unless the mouse is hidden (+0x130 = 2), the lure it is after, once
// within its own and the lure's radius, leaves the trail (Player::drop(false, id): no change
// of state; the trail closes up behind it).
LevelObject* Cat::stealTrail(CatWorld& world) {
    if (world.mouse.state == 2 || !world.dropFromTrail) return nullptr;
    const int id = validTrailTarget(world);
    if (id == -1 || id != targetId_) return nullptr;
    auto it = std::find_if(world.trail.begin(), world.trail.end(), [id](const LevelObject* o) { return o->id == id; });
    if (it == world.trail.end()) return nullptr;
    LevelObject* item = *it;
    const float reach = item->radius + radius_;
    if (!((position_ - item->position).lengthSquared() < reach * reach)) return nullptr;
    world.dropFromTrail(id);
    world.trail.erase(it);
    return item;
}

// updateSpecific @0x1a530c: a cat that took a lure from the trail plays with it (state 6, its
// check timer "twine_distract_time" for twine, "cat_catnip_time" for catnip, else 2.5 s). A
// brown or black cat sits on the ball at once, its "playing" sequence facing it. Without cheese
// left in the trail the safe holes shut. (A cheese the orange cat takes it carries, +0x17c,
// and a balloon behind the lure is let go too: neither is in the reconstructed levels.)
void Cat::takeFromTrail(CatWorld& world) {
    LevelObject* item = stealTrail(world);
    if (!item) return;
    bool cheeseLeft = false;
    for (const LevelObject* o : world.trail)
        if (isCheese(o->type)) cheeseLeft = true;
    if (!cheeseLeft) world.closeBankHoles = true;
    state_ = 8;
    chaseStep_ = 0;
    awarenessMs_ = 2500;
    if (item->type == kCatnip) {
        awarenessMs_ = kCatnipMs;
    } else if (item->type == kTwine) {
        awarenessMs_ = kTwineDistractMs;
        if (type_ == 0x32 || type_ == kBlackCat) {
            const int index = idle.findSequence("playing");
            if (index >= 0) idle.playSequence(index, SpriteAnim::SequenceLoop, 0);
            idleMirrored_ = position_.x < item->position.x;
            position_ = item->position;
        }
    }
}

// AIBehaviour::interceptTarget @0x18fcc8: where to run to meet a moving target: ahead of it
// along its movement by the distance × its speed relative to ours × `lead`, unless walls are
// in the way or it is coming towards us.
static Vec2 interceptPoint(Vec2 selfPosition, Vec2 selfVelocity, Vec2 targetPosition, Vec2 targetVelocity,
                    const LevelGeometry& walls, float lead, float maxDot) {
    if (lead > 1.0f) lead = 1.0f;
    else if (lead < 0.0f) lead = 0.0f;
    Vec2 targetDirection = targetVelocity;
    const float targetSpeed = std::fabs(targetDirection.normalise(1.0f));
    const float selfSpeed = std::fabs(selfVelocity.length());
    if (!(targetSpeed > 0.0f) || !(selfSpeed > 0.0f)) return targetPosition;
    Vec2 toward = targetPosition - selfPosition;
    const float distance = std::fabs(toward.normalise(1.0f));
    Vec2 ahead = targetDirection * distance;
    ahead *= targetSpeed / selfSpeed;
    ahead *= lead;
    Vec2 meet = targetPosition + ahead;
    walls.clipLine(targetPosition, meet, 0.0f);
    if (walls.lineTouches(selfPosition, meet, 0.0f, 0)) return targetPosition;
    if (!(toward.dot(targetDirection) > maxDot + -1.0f)) return targetPosition;
    return meet;
}

void Cat::attachParticle(CatWorld& world, int type) {
    world.particles.add(type, floor_, position_, {}, &particle_);
}

// ObjControllerCat::update: the particle over a chasing (or confused) cat's head follows it,
// 20 units up; otherwise it is shrunk away.
void Cat::moveParticle(CatWorld& world) const {
    if (particle_ < 0) return;
    Particle* p = world.particles.at(particle_);
    if (!p) return;
    // (1.1.1 @0x1a04d6) Over the cat while it chases, aims, is stunned, confused (1, 9, 10),
    // goes for a TV (8) or is surprised (0x13, 0x14); otherwise shrunk away. (A crumb's
    // points text, kind 0x34, is let go.)
    if (p->type == kTextPoints) return;
    const LevelObject* t = state_ == 8 ? target(world) : nullptr;
    const bool dazed = (state_ | 8) == 9 || (state_ == 8 && t && t->type == kTv);
    if ((state_ < 0x14 && ((1u << state_) & 0x804e0u) != 0) || state_ == 0x14 || dazed) {
        Vec2 at = position_;
        if (p->type == 0x23) at = {at.x + 22.0f, at.y + 6.0f};
        at.y = at.y + ((state_ | 8) != 9 ? 20.0f : 30.0f);
        if (p->type == kTextStun) at.y = at.y + 10.0f;
        p->position = rotated(at - position_, 0.0f) + position_;  // the scene is not rotated
    } else {
        p->scale = 0.0f;
    }
}

// The mouse in sight again after a pounce or a daze (pounce @0x1a359c, stun @0x1a30f0): seen
// close it takes aim at once (state 6), seen further off it chases (state 5 from its first
// step), unseen (after a look the other way) it walks its patrol again.
void Cat::lookAfterPounce(CatWorld& world) {
    canSeeMouse(world, true, -1.0f);
    if (!seenMouse_) {
        heading_ = heading_ + kPi;
        targetHeading_ = heading_;
    }
    canSeeMouse(world, true, -1.0f);
    walk.setAnim(0, 3, SpriteAnim::Loop, 200);
    if (!seenMouse_) {
        state_ = 0;
        chaseStep_ = 0;
    } else {
        const float close = chaseValuesFor(type_).closeDistance;
        if ((seenAt_ - position_).lengthSquared() < close * close) {
            takeAim(world);
            return;
        }
        state_ = 5;
        chaseStep_ = 0;
    }
    seenAt_ = position_;
}

// Taking aim (state 6, step 0; chase @0x1a3b4c and the others): the walk's frame 4 held half a
// second, facing the mouse, a heart over its head and the aim sound.
void Cat::takeAim(CatWorld& world) {
    state_ = 6;
    chaseStep_ = 0;
    chaseTimerMs_ = 0;
    walk.playOneShot(4, 4, 500);
    heading_ = targetHeading_ = (world.mouse.position - position_).angleDegrees() * 0.0174532924f;
    attachParticle(world, kParticleHeart);
    world.sounds.push_back({SoundEvent::Play, kSoundCatAim});
}

// ObjControllerCat::chase @0x1a3850 (1.1.1): one tick of a chase (state 5); returns the speed.
// The steps (+0x1dc): 0 noticed: stops for a moment with a "?" (and may give up after
// 20-36 s, +0x1ec); 1 runs to where it saw the mouse (ahead of it if it moves), along its
// way-back trail; close to the mouse it takes aim (state 6); at the spot with nothing there,
// 2 sits a while. Seeing the mouse again from 2 starts over at step 0.
float Cat::chase(int dtMs, CatWorld& world) {
    float result = speed_;
    const bool direct = !kInterceptMouse;
    if (chaseStep_ == 1) {
        walk.setFrameMs(125);
        if (canSeePlaything(world)) {
            // A playing TV beats the mouse: off to it, checking again next tick.
            state_ = 8;
            chaseStep_ = 0;
            awarenessMs_ = 0;
            return result;
        }
        const float close = chaseValuesFor(type_).closeDistance;
        if ((seenMouse_ || seenTrail_) && (seenAt_ - position_).lengthSquared() < close * close && type_ != 0x3b) {
            // Close: it takes aim (heading as it was aiming, not at the mouse: @0x1a3b74).
            state_ = 6;
            chaseStep_ = 0;
            walk.playOneShot(4, 4, 500);
            heading_ = targetHeading_;
            chaseTimerMs_ = 0;
            attachParticle(world, kParticleHeart);
            world.sounds.push_back({SoundEvent::Play, kSoundCatAim});
            return 0.0f;
        }
        if (type_ == 0x38 && !seenTrail_) {
            // The ninja cat (@0x1a3a92), not after the trail: in the open (nothing within 1.2
            // of its radius; 1.0.5: 1.75) it stops to throw a burst of shurikens (state 0x10,
            // frames 14..15).
            Vec2 at = position_;
            static constexpr int kIgnored[] = {0x73};
            if (world.walls.collideSphere(at, radius_ * 1.20000005f, world.viewLeft, nullptr, kIgnored) == 0) {
                state_ = 0x10;
                chaseStep_ = 0;
                awarenessMs_ = kNinjaBurstMs;
                throwMs_ = kNinjaThrowMs;
                burstLeft_ = kNinjaBurst;
                walk.setAnim(14, 15, SpriteAnim::Loop, 333);
                return result;
            }
        } else if (!seenMouse_ && type_ == 0x37) {
            // The blind cat that lost the mouse goes back from where it stands.
            seenAt_ = position_;
            setReturnPos(seenAt_, world);
        }
    } else if (chaseStep_ == 0) {
        chaseStep_ = 1;
        if (type_ != 0x3b) walk.playOneShot(4, 4, 500);
        calmMs_ = -20000 - int(world.random.next() & 0x3fff);
        lookMs_ = int(chaseValuesFor(type_).lookMs);
        attachParticle(world, type_ == 0x37 ? 0x23 : kParticleQuestion);
    }

    calmMs_ += dtMs;
    if (calmMs_ > 0) calmMs_ = 5000;
    if (!canSeeTrail(world)) canSeeMouse(world, false, -1.0f);
    if (seenMouse_ || seenTrail_) {
        if (chaseStep_ == 2) {
            walk.setAnim(0, 3, SpriteAnim::Loop, 200);
            chaseStep_ = 0;
        }
        // The lure it is after (getObjectById), else the mouse, ahead of it unless the chase
        // is direct (chase @0x1a3d5c).
        const CatTarget& m = world.mouse;
        Vec2 aimAt = m.position;
        if (seenTrail_) {
            if (const LevelObject* lure = target(world)) aimAt = lure->position;
        } else if (!direct) {
            aimAt = interceptPoint(position_, velocity_, m.position, m.velocity, world.walls, chaseValuesFor(type_).lead,
                                   kInterceptMaxDot);
        }
        seenAt_ = aimAt;
        awarenessMs_ = int(chaseValuesFor(type_).checkMs);
        setReturnPos(seenAt_, world);
    }
    if (chaseStep_ == 2) return 0.0f;
    const float d = (seenAt_ - position_).lengthSquared();
    if (d <= radius_ * radius_) {
        // At the spot, nothing there: it sits a while (half to all of g_pVal +0x68).
        if (chaseStep_ == 1) {
            if (type_ == 0x3b) walk.setAnim(0, 0, SpriteAnim::Loop, 200);
            else walk.setAnim(5, 8, SpriteAnim::Loop, 200);
            attachParticle(world, kParticleLost);
            world.sounds.push_back({type_ == 0x37 ? SoundEvent::PlayOnce : SoundEvent::Play, kSoundCatLost});
            const int half = kSitMs >> 1;
            awarenessMs_ = int(world.random.next() % unsigned(half)) + half;
            chaseStep_ = 2;
        }
        result = 0.0f;
    } else {
        if (followReturnPath(false)) {
            awarenessMs_ = int(chaseValuesFor(type_).checkMs);
            result = chaseValuesFor(type_).runSpeed;
        } else {
            heading_ = targetHeading_ + kPi;
            targetHeading_ = heading_;
            seenAt_ = position_;
        }
        result = chaseValuesFor(type_).speedFactor * result;
    }
    if (lookMs_ >= 1) {
        // The "?" moment: standing, facing where it is going.
        lookMs_ -= dtMs;
        result = 0.0f;
        heading_ = targetHeading_;
    }
    return result;
}

// ObjControllerCat::pounce @0x1a3304 (1.1.1, state 6): 0 aims for 0.7 s, then 1 pounces in a
// straight line, fast at first and slowing; head first into a wall it is stunned (state 7);
// at the end of the pounce it looks for the mouse again.
float Cat::pounce(int dtMs, CatWorld& world) {
    const ChaseValues values = chaseValuesFor(type_);
    float result = speed_;
    if (chaseStep_ == 1) {
        heading_ = targetHeading_;
        chaseTimerMs_ -= dtMs;
        bool glanced = false;
        if (collided_ && chaseTimerMs_ > kWallBonkMs && float(chaseTimerMs_) < float(values.pounceMs) * 0.5f) {
            velocity_.normalise(1.0f);
            if (pushNormal_.dot(velocity_) > 0.5f) {
                // Head first into the wall: stunned (state 7) for its time, the stars first.
                state_ = 7;
                chaseStep_ = 0;
                awarenessMs_ = values.stunnedMs;
                world.sounds.push_back({SoundEvent::Play, kSoundCatHitWall});
                idle.setAnim(7, 14, SpriteAnim::Loop, 166);
                idle.playOneShot(0, 2, 150);
                attachParticle(world, kParticleStars);
                ++headBumps_;
                return 0.0f;
            }
            glanced = true;  // (along the wall: the pounce goes on, @0x1a3714)
        }
        if (!glanced && chaseTimerMs_ < 1) lookAfterPounce(world);
        setReturnPos(seenAt_, world);
        returnIndex_ = int(returnPath_.size()) - 1;
        awarenessMs_ = int(values.checkMs);
        result = (float(chaseTimerMs_) / float(values.pounceMs / 3)) * (values.runSpeed * values.pounceFactor);
    } else if (chaseStep_ == 0) {
        if (chaseTimerMs_ == 0) {
            followReturnPath(false);
            heading_ = targetHeading_;
        } else if (chaseTimerMs_ > values.aimMs) {
            chaseTimerMs_ = values.pounceMs;
            chaseStep_ = 1;
            world.sounds.push_back({SoundEvent::Play, kSoundCatCharge});
        }
        awarenessMs_ = int(values.checkMs);
        result = 0.0f;
        chaseTimerMs_ += dtMs;
    } else {
        if (followReturnPath(false)) {
            awarenessMs_ = int(values.checkMs);
            result = values.runSpeed;
        } else {
            heading_ = targetHeading_ + kPi;
            targetHeading_ = heading_;
            seenAt_ = position_;
        }
    }
    result = values.speedFactor * result;
    if (lookMs_ >= 1) {
        lookMs_ -= dtMs;
        heading_ = targetHeading_;
        result = 0.0f;
    }
    return result;
}

// ObjControllerCat::stun @0x1a2fb4 (1.1.1, state 7): sitting dazed (the idle picture's frames
// held longer at first) until its time is up; the time waits for the stars to go
// (awarenessCheck). Then a playing TV, else the mouse as after a pounce.
float Cat::stun(int dtMs, CatWorld& world) {
    (void)dtMs;
    idle.setFrameMs(idle.frame() == 0 ? 900 : idle.frame() < 7 ? 150 : 166);
    if (awarenessMs_ > 0) return 0.0f;
    if (canSeePlaything(world)) {
        if (LevelObject* t = target(world)) seenAt_ = t->position;
        setReturnPos(seenAt_, world);
        state_ = 8;
        chaseStep_ = 0;
        awarenessMs_ = 0;
        return 0.0f;
    }
    lookAfterPounce(world);
    setReturnPos(seenAt_, world);
    returnIndex_ = int(returnPath_.size()) - 1;
    return 0.0f;
}

// ObjControllerCat::throwProj @0x1a2620: while shurikens are left in the burst its check timer
// is held at the burst's pause and it looks for the mouse (wide); every 250 ms a shuriken flies
// at where it saw the mouse last (straight, picture 0, 150 a second, spinning half a turn a
// second; it hits the mouse and balloons), and the cat aims that way. It stands meanwhile.
float Cat::throwProj(int dtMs, CatWorld& world) {
    if (burstLeft_ < 1) return 0.0f;
    awarenessMs_ = kNinjaBurstMs;
    canSeeMouse(world, true, -1.0f);
    if (throwMs_ >= 1) {
        throwMs_ -= dtMs;
        return 0.0f;
    }
    if (world.projectiles) {
        ProjectileSpec spec;
        spec.flight = 2;
        spec.playerTrigger = 2;
        spec.hits = 9;
        spec.picture = kPictureShuriken;
        spec.layer = 2;
        spec.floor = floor_;
        const int index = world.projectiles->create(spec);
        if (index >= 0) {
            Vec2 toward = seenAt_ - position_;
            toward.normalise(1.0f);
            targetHeading_ = std::atan2(toward.y, toward.x);
            world.projectiles->fire(index, position_, toward * float(kNinjaThrowSpeed), kPi, 0.0f, world.sounds);
        }
    }
    // (1.1.1 @0x1a2786: the wait before the next throw is the awareness timer just set, the
    // burst cooldown, 600 ms; 1.0.5 waited "ninja_throw_cooldown", 250. Run l41_seen.)
    throwMs_ = awarenessMs_;
    --burstLeft_;
    return 0.0f;
}

// ObjControllerCat::awarenessCheck: timers; when the awareness timer runs out, the next
// check is set by rand() and the cat looks for the mouse: seen, it chases (state 5).
// Each kind's cry on seeing the mouse or a lure (ObjControllerCat::awarenessCheck @0x1a5b5c,
// stopped first and played again); the blind cat and Fluffy have none (0x3b cries 0x9a in
// world 6 only).
int Cat::cry() const {
    switch (type_) {
    case 0x32: return kSoundBrownCatAgro;
    case 0x33: return 0x17;
    case kBlackCat: return 0x13;
    case 0x37: return 0x14;
    case 0x38: return 0x16;
    case 0x39: return 0x15;
    case 0x11d: return 0x18;
    default: return -1;
    }
}

void Cat::awareness(int dtMs, CatWorld& world) {
    if (awarenessMs_ <= 60000) {
        // (1.1.1 @0x1a54ec) A stunned cat's time waits while its stars (particle 9) show.
        const Particle* p = state_ == 7 ? world.particles.at(particle_) : nullptr;
        if (!(p && p->type == 9)) awarenessMs_ -= dtMs;
    }
    // (1.1.1 @0x1a5500) A stunned cat (7) and one in state 0x11 notice nothing.
    if (state_ == 7 || state_ == 0x11) return;
    if (timer1e0_ >= 1) timer1e0_ -= dtMs;
    if (calmMs_ >= 1) calmMs_ -= dtMs;
    collisionStreak_ = collided_ ? collisionStreak_ + 1 : collisionStreak_ >> 1;
    // Against walls for 16 ticks without sight of where it is going: it stops and looks about.
    if (collisionStreak_ > 15 && state_ != 11 && !canSeeGoal(world)) {
        state_ = 11;
        if (type_ == kBlackCat) setMeanCatIdleSequence("idle_0", position_ + rotated({1.0f, 0.0f}, heading_ * 57.2957764f));
        else idle.playOneShot(type_ != 0x37 ? 1 : 0, type_ == 0x37 ? 2 : 3, 150);
        awarenessMs_ = 1000;
    }
    // (1.1.1) Nothing more while stunned or in states 0x13/0x14, or until the timer runs out.
    if ((state_ < 0x15 && ((1u << state_) & 0x180080u) != 0) || awarenessMs_ > -1) return;
    awarenessMs_ = int(world.random.next() & 0x7f) + kTickMs;
    if (state_ == 0xf || state_ == 8) {
        // Done with its plaything (@0x1a5716): a TV makes it walk dazed (state 1, sound 0x21);
        // a teacup it had is gone (a balloon stays), both let go (0x1e); twine (or cheese) lies
        // on the floor again as a snap target (0x1d). Catnip is left to itself.
        LevelObject* t = target(world);
        if (t && t->type == kTv) {
            state_ = 1;
            walk.setAnim(0, 3, SpriteAnim::Loop, 200);
            attachParticle(world, kParticleHypnotised);
            world.sounds.push_back({SoundEvent::Play, kSoundCatHypnotised});
        } else if (t && (t->type == kTeacup || t->type == kBalloon)) {
            if (t->type != kBalloon) t->active = false;
            t->state = ObjectState(0x22);
        } else if (t && t->type != kCatnip && t->type != 0x5d) {
            t->state = ObjectState(0x21);
            t->snappable = true;
        }
    } else if (state_ == 5 && chaseStep_ > 0) {
        chaseStep_ = 0;  // (and a profile statistic)
    }
    // Calm again (1.1.1 @0x1a5742): back to its patrol; one getting up from a pause faces
    // where it is heading at once.
    if ((state_ > 24 || ((1u << state_) & 0x1de1c81u) == 0) && calmMs_ < 1) {
        if (state_ == 3) heading_ = targetHeading_;
        state_ = 0;
        walk.setAnim(0, 3, SpriteAnim::Loop, 200);
    }
    const bool sawBefore = seenMouse_;  // (+0x1d8 before this check's sight test)
    if (canSeePlaything(world)) {
        // A TV playing in sight: off to it (state 6), the walk's frame 4 held for half a second.
        if (LevelObject* t = target(world)) seenAt_ = t->position;
        setReturnPos(seenAt_, world);
        state_ = 8;
        walk.playOneShot(4, 4, 500);
        return;
    }
    if (canSeeTrail(world)) {
        // @0x1a589c: off after the lure it saw (state 5; no cry, the chase step kept); other
        // trail items only for the orange cat (cheese) and kind 0x3a.
        if (LevelObject* t = target(world)) seenAt_ = t->position;
        setReturnPos(seenAt_, world);
        const LevelObject* t = target(world);
        const int itemType = t ? t->type : -1;
        if (!isLure(itemType) && !(type_ == 0x33 && isCheese(itemType)) && type_ != 0x3a) seenTrail_ = false;
        if (seenTrail_) {
            state_ = 5;
            // (1.1.1 @0x1a5902) It cries as on seeing the mouse.
            if (const int c = cry(); c >= 0) {
                world.sounds.push_back({SoundEvent::Stop, c});
                world.sounds.push_back({SoundEvent::Play, c});
            }
        }
        return;
    }
    canSeeMouse(world, false, -1.0f);
    if (!seenMouse_ || seenTrail_) {
        if (state_ != 0) return;
        if (!returnPath_.empty()) {
            // On the way back: straight to a route point in sight once off the first point.
            const float d = (returnPath_.front() - position_).lengthSquared();
            if (radius_ * radius_ <= d) {
                const int point = closestRoutePoint(world);
                if (point >= 0) {
                    returnPath_.clear();
                    routeIndex_ = point;
                }
            } else {
                returnPath_.clear();
            }
        }
        return;
    }
    if (state_ == 0x12) return;
    if (type_ == 0x35) {
        // ObjControllerCat::awarenessCheck @0x1a5a0e: the boss cat that sees the mouse has won
        // (state 0x10); the level takes the loss (Session::catEffects).
        world.bossSawMouse = true;
        state_ = 0x12;
        world.sounds.push_back({SoundEvent::Play, 0x4d});
        world.sounds.push_back({SoundEvent::Play, 0x28});
        world.sounds.push_back({SoundEvent::Play, 0x7b});
    } else {
        // awarenessCheck @0x1a5b30 (1.1.1): after the mouse, the chase from its first step; a
        // fresh sighting (not seen the tick before) cries, and the black cat's makes the mouse
        // jump (Player::spottedByMeanCatOMG).
        state_ = 5;
        chaseStep_ = 0;
        if (!sawBefore) {
            if (const int c = cry(); c >= 0) {
                world.sounds.push_back({SoundEvent::Stop, c});
                world.sounds.push_back({SoundEvent::Play, c});
                if (type_ == kBlackCat) world.meanCatSpotted = true;
            }
        }
    }
    world.spottedMouse = true;
    ++sightings_;
}

// ObjControllerCat::runBehaviour: what the cat does in its state; returns its speed.
float Cat::behave(int dtMs, CatWorld& world) {
    // @0x1a4d20: a cat out of the screen (isGameRectOnScreen of its frame, relative to the view)
    // stops its purring (run lb2_explore: the sleeping cats far ahead in the chase).
    const float x = position_.x - world.viewLeft;
    const float w = 32.0f, h = frameHeight_;
    const bool onScreen = x + w * 0.5f >= 0.0f && x + w * -0.5f <= 480.0f && position_.y + h * 0.5f >= 0.0f &&
                          position_.y + h * -0.5f <= 320.0f;
    if (!onScreen) stopPurring(world);
    float speed = 0.0f;
    switch (state_) {
    case 0x10:
        speed = throwProj(dtMs, world);
        break;
    case 0x17:
        // A shocked henchman (@0x1a4d2c): the awareness timer held at 500, no touch circle.
        awarenessMs_ = 500;
        radius_ = 0.0f;
        break;
    case 0:
        // Patrol (ObjControllerCat::patrol @0x1a4074): back along the way it came first, then the
        // route.
        speed = speed_;
        if (type_ == kBossCat && routeIndex_ == int(route_.points.size()) - 1) {
            // (1.1.1 @0x1a408a) The boss cat heading for the last point of its route runs at 2.5
            // times its speed; within its radius of it, it stops for good: its object no longer
            // updates and the view's scroll is cleared (and then set once more from its last
            // step, ObjControllerCat::update @0x1a14a4: the view runs on to the level's end).
            // (1.0.5 walked it on sideways out of the view.)
            if ((route_.points.back().position - position_).length() < radius_) {
                stoppedForGood_ = true;
                world.scrollSpeed = 0.0f;
                speed = 0.0f;
            } else {
                speed = speed_ * 2.5f;
            }
        }
        if (!followReturnPath(true)) speed = followRoute(speed, world);
        break;
    case 2: case 3: case 4:
        followRoute(0.0f, world);
        if (state_ == 3) {
            // Getting up: (the walking picture, not shown, ping-pongs its frames 3-5.)
            if (walk.finished()) walk.setAnim(3, 5, SpriteAnim::PingPong, 200);
            walk.setFrameMs(walk.frame() == 5 ? 600 : 200);
        } else if (state_ == 2) {
            walk.setFrameMs(walk.frame() == 10 ? 2500 : 200);
            if (awarenessMs_ <= 2000) {
                // Two seconds left: it stops purring and gets up (frames 6-13 once).
                state_ = 3;
                calmMs_ = 0;
                stopPurring(world);
                common.setAnim(6, 13, SpriteAnim::Once, 166);
            } else if (calmMs_ <= 0) {
                // A "zZz" every 550 ms, timed by the rest of the pause.
                calmMs_ = 550;
                const int slot = world.particles.add(kParticleSleep, floor_, position_);
                const int i = routeIndex_ - routeDirection_;
                if (i >= 0 && i < int(route_.points.size()))
                    if (Particle* p = world.particles.at(slot)) {
                        p->life = awarenessMs_ - 2000;
                        p->aux = int(float(route_.points[std::size_t(i)].pauseSeconds * 1000) + -2000.0f);
                    }
            }
        }
        break;
    case 11:
        if (awarenessMs_ < 500) {
            awarenessMs_ = 0;
            collisionStreak_ = 0;
            if (!canSeeGoal(world)) {
                const int d = routeDirection_;
                if (d == 1 && routeIndex_ == 0) {
                    awarenessMs_ = 1000;
                    break;
                }
                if (route_.endMode != 1 && route_.endMode != 2) break;
                routeDirection_ = -d;
                routeIndex_ -= d;
            }
            state_ = 0;
        }
        break;
    case 6:
        speed = pounce(dtMs, world);
        break;
    case 7:
        speed = stun(dtMs, world);
        break;
    case 5:
        speed = chase(dtMs, world);
        if (type_ == kBlackCat && speed > 0.0f) {
            // runBehaviour @0x1a4fac: the black cat gets up to its speed over
            // "scary_maxspeed_time" (1400 ms), from the plain chase speed (run l22_tv).
            const float f = float(meanRampMs_) / float(kScaryMaxSpeedMs);
            speed = (1.0f - f) * chaseValuesFor(type_).runSpeed + f * speed;
        }
        break;
    case 8:
        speed = play(dtMs, world);
        break;
    case 1:
        speed = dazewalk(world);
        break;
    case 9:
        hypnotised(world);
        speed = 0.0f;
        break;
    case 0x12: case 0x15: case 0x18:
        // (No behaviour: it goes on at its own speed, runBehaviour @0x1a5146.)
        speed = speed_;
        break;
    default:
        break;
    }
    steer(world, speed);
    return speed;
}

// ObjControllerCat::touchPlayer: a cat catches the mouse it touches unless it is busy with
// something else (canKillPlayer @0x19c734: not in states 1, 2, 4, 7, 9, 12, 13, 15, 17, 19, 22, 23),
// confused, or dazed after a pounce.
bool Cat::touchMouse(const CatTarget& mouse, CatWorld& world, Vec2& meetingPoint) {
    if (mouse.hurtMs >= 1) return false;
    if (state_ >= 0 && state_ < 24 && ((1u << state_) & 0xcab296u) != 0) return false;  // (@0x19c734)
    if (state_ == 1 || state_ == 9) return false;
    // (@0x1a0e56: a chasing cat leaves a mouse with twine or catnip in its trail alone; one
    // on its way to or at a plaything catches only when that is of kind 3 or 0x5d.)
    if (state_ == 5 && mouse.carriesLure) return false;
    if (state_ == 8) {
        const LevelObject* t = target(world);
        if (!t || (t->type != 3 && t->type != 0x5d)) return false;
    }
    position_ = (position_ + mouse.position) * 0.5f;
    meetingPoint = position_;
    attachParticle(world, kParticleCaught);
    world.sounds.push_back({SoundEvent::Play, kSoundDeathCloud});
    chaseStep_ = 0;
    state_ = 4;
    walk.setAnim(5, 8, SpriteAnim::Loop, 200);
    calmMs_ = 0;
    awarenessMs_ = 5000;
    return true;
}

// The end of ObjControllerCat::update: the heading turns towards the aim by the turn rate ×
// speed / pi of the difference (never past it); then Object::updatePositions moves the cat
// along it and out of walls.
void Cat::turnAndMove(int dtMs, float speed, CatWorld& world) {
    const float aim = wrapPositive(targetHeading_);
    const float h = wrapPositive(heading_);
    heading_ = h;
    float d = std::fmod(aim - h, kTwoPi);
    if (d > kPi) d = d + -kTwoPi;
    if (d < -kPi) d = d + kTwoPi;
    float step = d * (kCatTurnRate * speed);
    step = step * 0.318309873f;
    const float limit = std::fabs(d);
    step = std::min(limit, std::max(-limit, step));
    heading_ = wrapPositive(h + step);
    // (1.1.1 @0x1a14a4) The boss cat of an infinite level sets the view's scroll from the
    // velocity of its last step, before this one (the first tick scrolls by init's 0.05).
    if (type_ == kBossCat && world.infinite) scrollWithView(world);

    const float move = (float(dtMs) * speed) * 0.0500000007f;
    velocity_ = {libCos(heading_) * move, libSin(heading_) * move};
    Vec2 next = position_ + velocity_;
    pushNormal_ = {};
    // (Object::updatePositions @0x115518: in states 0x20 and 0x3c, a cutscene, walls do not
    // hold the cat.)
    if (state_ != 0x24 && state_ != 0x40) {
        const unsigned touched =
            world.walls.collideSphere(next, radius_, world.viewLeft, &pushNormal_, kMovementIgnoredWalls);
        collided_ = (touched & (contact::kSolid | contact::kBouncy)) != 0;
        if (state_ == 6 && (touched & contact::k91)) {
            // (1.1.1's state 6 stops at a 0x91 wall: the cat stays, the push gets its step.)
            collided_ = true;
            pushNormal_ = velocity_;
            next = position_;
        }
    }
    position_ = next;
}

// ObjControllerCat::update @0x1a147c (the boss cat of an infinite level): route points the
// view has passed are dropped; the view scrolls by the cat's step.
void Cat::scrollWithView(CatWorld& world) {
    if (route_.points.size() >= 2 && route_.points[1].position.x < world.viewLeft) {
        route_.points.erase(route_.points.begin());
        --routeIndex_;
        trimmed_ = (trimmed_ == -1 ? 0 : trimmed_) + 1;  // (+0x208)
    }
    if (state_ != 0x15) world.scrollSpeed = velocity_.x;
}

void Cat::stopPurring(CatWorld& world) {
    if (!purring_) return;
    purring_ = false;
    world.sounds.push_back({SoundEvent::Stop, kSoundCatPurr});
}

// ScriptSequence::showFluffy @0x1b9f70 (1.0.5).
void Cat::showInCutscene(float aim) {
    pose.setAnim(5, 5, SpriteAnim::Loop, 125);
    posePart_ = 2;
    state_ = 0x40;
    float a = std::fmod(aim, kTwoPi);
    if (a < 0.0f) a = a + kTwoPi;
    heading_ = a;
    targetHeading_ = a;
}

// ScriptSequence::walkPath @0x1d418a, first call for an actor that is not the mouse.
void Cat::startScriptedWalk() {
    state_ = 0x40;
    walk.setAnim(0, 3, SpriteAnim::Loop, 200);
    posePart_ = -1;
}

// ScriptSequence::walkPath @0x1d428a: the cat's own route is set aside while it follows the
// path from point `point` at "fluffy_cutscene_mult" times its speed, facing straight at where
// it goes; it moves (updatePositions) and the route comes back. The way back is left empty.
float Cat::scriptedWalk(const CatRoute& path, int& point, CatWorld& world) {
    // ("fluffy_cutscene_mult": 2.74999905, the factor that gives the original's speed of
    // 1.3750077 in run lbf_explore2 from his 0.50000298.)
    constexpr float kFluffyCutsceneMult = 2.74999905f;
    CatRoute ownRoute = route_;
    const int ownIndex = routeIndex_;
    route_ = path;
    routeIndex_ = point;
    const float speed = followRoute(speed_ * kFluffyCutsceneMult, world);
    float a = std::fmod(targetHeading_, kTwoPi);
    if (a < 0.0f) a = a + kTwoPi;
    heading_ = a;
    targetHeading_ = a;
    turnAndMove(kTickMs, speed, world);
    point = routeIndex_;
    route_ = std::move(ownRoute);
    routeIndex_ = ownIndex;
    returnPath_.clear();
    return speed;
}

void Cat::update(int dtMs, CatWorld& world) {
    // updateAnimations @0x1a0998: the picture getActiveSprite picks; dazed, the end of the
    // crash one-shot sounds (the sprite's one-shot flag changing).
    {
        const LevelObject* t = state_ == 8 ? target(world) : nullptr;
        lurePlayed_ = t && t->state == ObjectState(0x22);
    }
    const Picture shown = picture();
    if (shown != Picture::None) {
        SpriteAnim& sprite = shown == Picture::Idle ? idle : shown == Picture::Common ? common
                           : shown == Picture::Pose ? pose : shown == Picture::Balloon ? balloon : walk;
        const bool oneShotBefore = sprite.playingOneShot();
        sprite.update(dtMs);
        animatedMs += dtMs;
        if (state_ == 7 && sprite.playingOneShot() != oneShotBefore)
            world.sounds.push_back({SoundEvent::Play, kSoundCatDazed});
    }
    // In a cutscene (state 0x3c) Object::update @0x118ea6 runs only the animations.
    if (state_ == 0x40) return;
    if (state_ == 0) stopPurring(world);  // (update: the looping sound ends on patrol)
    if (type_ == kBlackCat) {
        // updateSpecific @0x1a5168: running after the mouse (chase step 1, no "?" stop) its
        // ramp time grows up to 1400 ms; any other time it is 0.
        if (state_ == 5 && chaseStep_ == 1) {
            if (lookMs_ < 1) meanRampMs_ = std::min(meanRampMs_ + dtMs, kScaryMaxSpeedMs);
        } else {
            meanRampMs_ = 0;
        }
    }
    takeFromTrail(world);
    awareness(dtMs, world);
    const float speed = behave(dtMs, world);
    {
        const LevelObject* t = state_ == 8 ? target(world) : nullptr;
        lurePlayed_ = t && t->state == ObjectState(0x22);
    }
    moveParticle(world);
    if (ridesBalloon_) {
        // @0x1a065a: the balloon picture runs on (a second step while it is also the shown
        // one); in the balloon he sways 5 either side of his place, a turn every 2π s of game
        // time. (His walk at his own speed, after this, still moves him on by a step: the
        // sway's middle is a step to the right of where he was placed, run lbf4_clock.)
        balloon.update(dtMs);
        if (state_ == 0x18) position_.x = balloonX_ + std::sin(float(world.gameTimeMs) * 0.00100000005f) * 5.0f;
    }
    walk.pause(state_ == 0 && std::fabs(speed) < 0.00100000005f);
    if (state_ == 0x16 || state_ == 0x17) return;
    if (state_ == 0x12) {
        // Having seen the mouse (the boss cat, @0x1a0754): it faces the mouse and stands still
        // from the next tick on (its speed goes to 0, its walk pauses).
        const Vec2 d = world.mouse.position - position_;
        targetHeading_ = std::atan2(d.y, d.x);
        speed_ = 0.0f;
        walk.pause(true);
    }
    turnAndMove(dtMs, speed, world);
}

}  // namespace doc::game
