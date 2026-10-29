#include "game/vacuum.hpp"

#include <cmath>

namespace doc::game {
namespace {

constexpr float kTwoPi = 6.28318548f, kPi = 3.14159274f;

// The vacuum's settings in the tuning table (Values; defaults in docs/values-defaults.txt).
struct VacuumValues {
    int wakeMs = 1350;          // +0x23c mech_taunt_time: after the intro, before it starts
    int phaseMs = 850;          // +0x240 mech_phase_time: each suction stage
    float speedMult = 1.29999995f;  // +0x244 mech_speed_mult
    float maxPull = 1.10000002f;    // +0x248 mech_max_vacuum: units a tick at full suction
    float reach = 160.0f;       // +0x24c mech_vacuum_dist
    float patrolSpeed = 0.5f;   // +0x58 cat_patrol_speed
    float turnRate = 1.0f;      // +0x60 cat_turn_speed
    float visionArc = 145.0f;   // +0x74 cat_vision_arc (degrees)
    float viewDistance = 400.0f;  // +0x6c
};
constexpr VacuumValues kValues{};

constexpr float kHalfDegree = 0.00872664619f;  // pi / 360: the arc passed to the sight checks
constexpr int kSoundMove = 0xa4, kSoundSuck = 0xa5, kSoundSuckEnd = 0xa6, kSoundBomb = 0xa8, kSoundSwallow = 0xa9;
constexpr int kSoundCaught = 0x60;
constexpr int kSoundExplosion = 0x8f;
constexpr int kParticleCaught = 0x0e, kParticleSwallow = 0x10, kParticleExplosion = 0x2b, kParticleSmoke = 0x2d;

void soundEvent(std::vector<SoundEvent>& sounds, SoundEvent::Kind kind, int id, bool loop = false) {
    SoundEvent e;
    e.kind = kind;
    e.effect = id;
    e.loop = loop;
    sounds.push_back(e);
}

}  // namespace

Vacuum::Vacuum(int objectId, int floor, Vec2 position, Vec2 size, CatRoute route, const Sheets& sheets)
    : objectId_(objectId), floor_(floor), position_(position), size_(size) {
    walk_.route = std::move(route);
    // ObjControllerBoss::init @0x1a8b28: asleep, facing down the screen, its speed and wait.
    speed_ = kValues.patrolSpeed * kValues.speedMult;
    heading_ = aim_ = -1.57079637f;
    timerMs_ = kValues.wakeMs;
    // loadAssets @0x1a91d0: the body on frame 2, the swirl looping at 85 ms, the nozzle idle.
    body.setSheet(sheets.body.width, sheets.body.height, 64.0f);
    body.setAnim(2, -1, SpriteAnim::Hold, 200);
    effect.setSheet(sheets.effect.width, sheets.effect.height, 34.0f);
    effect.setAnim(0, effect.frameCount() - 1, SpriteAnim::Loop, 85);
    nozzle.setSheet(sheets.nozzle.width, sheets.nozzle.height, 64.0f);
    nozzle.setAnim(0, -1, SpriteAnim::Hold, 200);
    suction.setSheet(sheets.suction.width, sheets.suction.height, 64.0f);
    suction.setAnim(0, -1, SpriteAnim::Hold, 200);
}

void Vacuum::update(int dtMs, BossWorld& world, const VacuumTargets& targets) {
    if (state_ == 0x18) {
        // Broken (updateBrokenState @0x1abd38): a puff of smoke about it every 1.5 s; the
        // machine sounds stay off.
        smokeMs_ -= dtMs;
        if (smokeMs_ < 1) {
            const float x = world.random.within(-5.0f, 5.0f);
            const float y = world.random.within(-5.0f, 10.0f);
            world.particles.add(kParticleSmoke, floor_, position_ + Vec2{x, y}, {}, nullptr, 0, Vec2{-1.0f, -1.0f}, true);
            smokeMs_ = 1500;
        }
        for (int s : {kSoundBomb, kSoundMove, kSoundSuck}) soundEvent(world.sounds, SoundEvent::Stop, s);
        return;
    }
    if (state_ == 0x16) {
        // Waking: active once its wait is over (it moves from the next tick on).
        timerMs_ -= dtMs;
        if (timerMs_ <= 0) state_ = 0x17;
        return;
    }
    if (state_ != 0x17) return;
    // While it sucks the controller redraws a timer of its own (nothing reads it here).
    moodMs_ -= dtMs;
    if (moodMs_ < 1 && (stage_ == 1 || stage_ == 2)) moodMs_ = int(world.random.next() % 4000) + 8000;
    updateAnimations(dtMs);
    const float speed = updateVaccuum(dtMs, world, targets);
    // The end of ObjControllerBoss::update: the turn, then Object::updatePositions.
    heading_ = turnTowards(heading_, aim_, kValues.turnRate * speed);
    stepAlongHeading(position_, velocity_, heading_, speed, dtMs, radius_, world.walls, world.viewLeft);
}

// updateAnimations @0x1a984c for the vacuum: eight facings, starting 22.5 degrees past east and
// turning clockwise; while it sucks the suction picture shows the facing's two frames (25 ms
// each). Frame 8 (swallowing) stays.
void Vacuum::updateAnimations(int dtMs) {
    if (body.frame() == 8) return;
    float a = std::fmod((0.392699093f - heading_) + kTwoPi, kTwoPi);
    if (a < 0.0f) a = a + kTwoPi;
    const int facing = int(a * 1.27323949f);
    if (stage_ >= 1) {
        if ((pictureFlags_ & ~0x100) != facing * 2) {
            suction.setAnim(facing * 2, facing * 2 + 1, SpriteAnim::Loop, 25);
            pictureFlags_ = facing * 2 | 0x100;
        }
    } else {
        pictureFlags_ &= ~0x100;
    }
    effect.update(dtMs);
    suction.update(dtMs);
    if (body.frame() != facing) body.setAnim(facing, -1, SpriteAnim::Hold, 200);
}

// Object::updatePath for the vacuum: at each corner it turns back when the corner behind leads
// more towards the mouse (not while the mouse hides, nor at a cut point); while a bomb is about
// it leads towards the conveyor's end instead (@0x1159d6).
float Vacuum::walk(float speed, const BossWorld& world, const VacuumTargets& targets) {
    return walkRoute(walk_, position_, radius_ * 0.400000006f, speed, aim_, heading_, [&](int previous, int next) {
        const CatRoute& route = walk_.route;
        const int count = int(route.points.size());
        if (route.startMode == 2 && route.endMode == 2) {
            if (previous < 0) previous = count - 2;
            if (next < 0) next = count - 2;
        }
        if (previous < 0 || previous >= count || next < 0 || next >= count) return;
        if (route.points[std::size_t(next)].pauseSeconds == -1 || world.mouseState == 2) return;
        Vec2 target = world.mousePosition;
        if (world.mouseState == 4 || world.mouseState == 9) target = world.mouseStateEntry;
        if (targets.bomb && targets.bomb->active && targets.bombTarget) target = targets.bombTarget->position;
        Vec2 toward = target - position_;
        toward.normalise(1.0f);
        Vec2 toPrevious = route.points[std::size_t(previous)].position - position_;
        toPrevious.normalise(1.0f);
        Vec2 toNext = route.points[std::size_t(next)].position - position_;
        toNext.normalise(1.0f);
        if (toward.dot(toPrevious) > toward.dot(toNext)) walk_.direction = -walk_.direction;
    });
}

// updateVaccuum @0x1ab7b0: returns this tick's speed.
float Vacuum::updateVaccuum(int dtMs, BossWorld& world, const VacuumTargets& targets) {
    float speed = speed_;
    const int stageBefore = stage_;
    Vec2 toMouse = world.mousePosition - position_;
    seesMouse_ = canSeeMouse(world);
    bool walking = true;
    if (seesMouse_ && toMouse.lengthSquared() < kValues.reach * kValues.reach) {
        // Seen within reach: it sucks, harder each stage; at a corner of its route it stops
        // and turns to the mouse.
        const Vec2 point = walk_.route.points[std::size_t(walk_.index)].position;
        const float dx = point.x - position_.x, dy = point.y - position_.y;
        if (dx * dx + dy * dy < radius_ * radius_) {
            toMouse.normalise(1.0f);
            heading_ = aim_ = std::atan2(toMouse.y, toMouse.x);
            speed = 0.0f;
            walking = false;
        }
        timerMs_ += dtMs;
        stage_ = timerMs_ < kValues.phaseMs ? 1 : timerMs_ < kValues.phaseMs * 2 ? 2 : 3;
    } else {
        stage_ = 0;
        timerMs_ = 0;
    }
    if (walking && state_ == 0x17) speed = walk(speed, world, targets);
    if (stage_ >= 1 && world.mouseState == 0 && targets.mouse) {
        // The walking mouse is drawn towards the nozzle: a quarter, half or all of the pull.
        Vec2 pull = *targets.mouse - position_;
        const float distance = pull.normalise(-1.0f);
        if (kValues.reach > distance && seesMouse_) {
            float strength = kValues.maxPull;
            if (stage_ == 2) strength = strength * 0.5f;
            else if (stage_ == 1) strength = strength * 0.25f;
            *targets.mouse += pull * strength;
        }
    }
    LevelObject* bomb = targets.bomb;
    if (!bomb || !bomb->active) {
        soundEvent(world.sounds, SoundEvent::Stop, kSoundBomb);
    } else {
        soundEvent(world.sounds, SoundEvent::PlayOnce, kSoundBomb, true);
        Vec2 pull = bomb->position - position_;
        const float distance = pull.normalise(-1.0f);
        if (kValues.reach * 0.600000024f > distance) {
            if (canSee(bomb->position, kValues.visionArc * kHalfDegree, kValues.reach, world)) {
                if (stage_ < 3)
                    world.particles.add(kParticleSwallow, 0, position_ + Vec2{0.0f, 15.0f}, {}, nullptr, 0,
                                        Vec2{-1.0f, -1.0f}, true);
                stage_ = 3;
                bomb->position += pull * kValues.maxPull;
                speed = 0.0f;
            }
            const float reach = bomb->radius + radius_;
            if ((bomb->position - position_).lengthSquared() < reach * reach) {
                // Swallowed: on the tick after its picture turned to the swallowing frame.
                if (body.frame() == 8) {
                    bomb->active = false;
                    if (world.fireEvent) world.fireEvent("boss_win");
                    soundEvent(world.sounds, SoundEvent::PlayOnce, kSoundSwallow);
                }
                body.setAnim(8, -1, SpriteAnim::Hold, 200);
            }
        }
    }
    if (speed > 0.0f) soundEvent(world.sounds, SoundEvent::PlayOnce, kSoundMove, true);
    else soundEvent(world.sounds, SoundEvent::Stop, kSoundMove);
    if (stage_ >= 1) soundEvent(world.sounds, SoundEvent::PlayOnce, kSoundSuck, true);
    else soundEvent(world.sounds, SoundEvent::Stop, kSoundSuck);
    if (stageBefore >= 1 && stage_ == 0) soundEvent(world.sounds, SoundEvent::PlayOnce, kSoundSuckEnd);
    return speed;
}

// Object::canSeePlayer(false, 145 x pi / 360): the mouse in play on its floor, not protected,
// walking, idle or carrying, not under a cup, on screen with it, nearer than the view distance,
// within half the arc either side of its heading and nothing in between.
bool Vacuum::canSeeMouse(const BossWorld& world) const {
    const float onScreen = position_.x - world.viewLeft;
    if (!(onScreen >= 0.0f) || !(onScreen < kScreenWidth)) return false;
    const CatTarget& m = world.mouse;
    if (!m.active || m.floor != floor_ || m.hurtMs >= 1) return false;
    if (m.state < 0 || m.state > 8 || ((1u << m.state) & 0x111u) == 0) return false;
    if (m.carriesSomething) return false;
    return canSee(m.position, kValues.visionArc * kHalfDegree, kValues.viewDistance, world);
}

// The shared part of Object::canSeePlayer and canSeeObject @0x113688: nearer than `distance`,
// within half of `angle` either side of the heading, no wall in between.
bool Vacuum::canSee(Vec2 at, float angle, float distance, const BossWorld& world) const {
    Vec2 toward = at - position_;
    if (!(toward.normalise(1.0f) < distance)) return false;
    float off = std::fmod(heading_ - std::atan2(toward.y, toward.x), kTwoPi);
    if (off > kPi) off = off + -kTwoPi;
    if (off < -kPi) off = off + kTwoPi;
    if (!(std::fabs(off) < angle * 0.5f)) return false;
    return !world.walls.lineTouches(position_, at, world.viewLeft, 1);
}

void Vacuum::blowUp(BossWorld& world, const std::function<void()>& throwKey) {
    world.particles.add(kParticleExplosion, floor_, position_ + Vec2{0.0f, size_.y * 0.5f}, {}, nullptr, 0,
                        Vec2{-1.0f, -1.0f}, true);
    if (throwKey) throwKey();
    state_ = 0x18;
    smokeMs_ = 1000;
    soundEvent(world.sounds, SoundEvent::Play, kSoundExplosion);
}

void Vacuum::touchMouse(bool mouseProtected, BossWorld& world) {
    if (mouseProtected || state_ == 0x18) return;
    for (int s : {kSoundBomb, kSoundMove, kSoundSuck}) soundEvent(world.sounds, SoundEvent::Stop, s);
    if (world.killMouse) world.killMouse();
    state_ = 0x15;
    timerMs_ = 5000;
    world.particles.add(kParticleCaught, floor_, position_, {}, nullptr, 0, Vec2{-1.0f, -1.0f}, true);
    soundEvent(world.sounds, SoundEvent::Play, kSoundCaught);
}

}  // namespace doc::game
