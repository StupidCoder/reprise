#include "game/spitter.hpp"

#include "game/projectiles.hpp"

#include <cmath>

namespace doc::game {
namespace {

constexpr float kTwoPi = 6.28318548f, kPi = 3.14159274f;

// The Spitter's settings in the tuning table (Values, defaults in code; read back from the
// running original, run lbf2_world2).
struct SpitterValues {
    int wakeMs = 1350;           // +0x23c: after the intro, before it starts
    int windUpMs = 700;          // +0x278: from seeing the mouse to the throw
    int cooldownMs = 1000;       // +0x280: before it looks for the mouse again
    int throwSpeed = 84;         // +0x1f0: units a second
    float throwLift = 55.0f;     // +0x1bc (passed on; straight throws do not use it)
    float speed = 1.0f;          // +0x58 x +0x26c
    float turnRate = 1.0f;       // +0x60
    float viewDistance = 400.0f; // +0x6c
};
constexpr SpitterValues kValues{};

constexpr float kRailSpeed = 0.75f; // "target_speed"

constexpr int kBeamTag = 123, kBeamWall = 0x8f;
constexpr int kMechSounds[] = {0xa8, 0xa4, 0xa5};
constexpr int kSoundCaught = 0x60, kSoundExplosion = 0x8f;
constexpr int kSoundEmitterOff = 0xab;
constexpr int kParticleCaught = 0x0e, kParticleExplosion = 0x2b, kParticleSmoke = 0x2d;

SoundEvent effect(SoundEvent::Kind kind, int id) {
    SoundEvent e;
    e.kind = kind;
    e.effect = id;
    return e;
}

}  // namespace

Spitter::Spitter(int objectId, int floor, Vec2 position, float radius, Vec2 size, CatRoute route, const Sheets& sheets)
    : objectId_(objectId), floor_(floor), position_(position), size_(size), radius_(radius) {
    walk_.route = std::move(route);
    // ObjControllerBoss::init @0x1a8b28 and loadAssets @0x1a9370: asleep (0x15), its timers
    // set, facing frame 4 of sixteen; the wreck loops at 200 ms.
    speed_ = kValues.speed;
    cooldownMs_ = kValues.wakeMs;
    windUpMs_ = kValues.windUpMs;
    body.setSheet(sheets.body.width, sheets.body.height, 64.0f);
    body.setAnim(4, -1, SpriteAnim::Hold, 200);
    wreck.setSheet(sheets.wreck.width, sheets.wreck.height, 64.0f);
    wreck.setAnim(0, wreck.frameCount() - 1, SpriteAnim::Loop, 200);
}

void Spitter::update(int dtMs, BossWorld& world) {
    if (state_ == 0x18) {
        // Broken (updateBrokenState @0x1abd38): a puff of smoke about it every 1.5 s; the
        // machine sounds stay off.
        wreck.update(dtMs);
        throwing_ -= dtMs;
        if (throwing_ <= 0) {
            const float x = world.random.within(-5.0f, 5.0f);
            const float y = world.random.within(-5.0f, 10.0f);
            world.particles.add(kParticleSmoke, floor_, position_ + Vec2{x, y}, {}, nullptr, 0, Vec2{-1.0f, -1.0f}, true);
            throwing_ = 1500;
        }
        for (int s : kMechSounds) world.sounds.push_back(effect(SoundEvent::Stop, s));
        return;
    }
    if (state_ == 0x16) {
        // Waking: active once its wait is over (it moves from the next tick on).
        cooldownMs_ -= dtMs;
        if (cooldownMs_ <= 0) state_ = 0x17;
        return;
    }
    if (state_ != 0x17) return;
    updateAnimations(world);
    const float speed = updateSpitter(dtMs, world);
    // The end of ObjControllerBoss::update: the turn, then Object::updatePositions.
    heading_ = turnTowards(heading_, aim_, kValues.turnRate * speed);
    stepAlongHeading(position_, velocity_, heading_, speed, dtMs, radius_, world.walls, world.viewLeft);
}

// updateAnimations @0x1a984c for the Spitter: the dome faces the mouse (or, while the mouse is
// hidden, its own heading) in sixteenths of a turn, starting 168.75 degrees before east.
void Spitter::updateAnimations(const BossWorld& world) {
    float angle = heading_;
    if (world.mouseState != 2) {
        const Vec2 d = world.mousePosition - position_;
        angle = std::atan2(d.y, d.x);
    }
    float a = std::fmod(angle + -2.94524312f, kTwoPi);
    if (a < 0.0f) a = a + kTwoPi;
    const int facing = int(a / 0.392699093f);
    if (body.frame() != facing) body.setAnim(facing, -1, SpriteAnim::Hold, 200);
}

// updateSpitter @0x1ab3a8.
float Spitter::updateSpitter(int dtMs, BossWorld& world) {
    // Pushed out of the walls first; touching the beam (kind 7) ends the fight.
    LevelGeometry::SphereOptions options;
    options.ignoreType = 0x73;
    options.viewLeft = world.viewLeft;
    if (world.walls.collideSphere(position_, radius_, options) == 7) {
        state_ = 0x18;
        GameMessage m;
        m.type = 4;
        m.sender = objectId_;
        m.targetType = kBeamEmitter;
        if (world.send) world.send(m, false);
        m.targetType = kRailTarget;
        if (world.send) world.send(m, false);
        if (world.setWalls) world.setWalls(kBeamTag, false, kBeamWall);
        if (world.fireEvent) world.fireEvent("boss_win");
        return 0.0f;
    }
    // Object::updatePath: at each corner it may turn back, if that way leads more towards the
    // mouse (not while the mouse hides, nor at a cut point).
    const float speed = walkRoute(walk_, position_, radius_ * 0.400000006f, speed_, aim_, heading_,
                                  [&](int previous, int next) {
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
        Vec2 toward = target - position_;
        toward.normalise(1.0f);
        Vec2 toPrevious = route.points[std::size_t(previous)].position - position_;
        toPrevious.normalise(1.0f);
        Vec2 toNext = route.points[std::size_t(next)].position - position_;
        toNext.normalise(1.0f);
        if (toward.dot(toPrevious) > toward.dot(toNext)) walk_.direction = -walk_.direction;
    });
    if (throwing_ != 1) return speed;
    // Every so often it looks for the mouse; seen, a throw is wound up and thrown.
    cooldownMs_ -= dtMs;
    if (cooldownMs_ < 1) {
        cooldownMs_ = 0;
        if (throwsDue_ <= 0 && canSeeMouse(world)) {
            windUpMs_ = kValues.windUpMs;
            throwsDue_ = 1;
        }
    }
    if (throwsDue_ >= 1) {
        cooldownMs_ = kValues.cooldownMs;
        if (windUpMs_ > 0) {
            windUpMs_ -= dtMs;
        } else {
            throwAtMouse(world);
            windUpMs_ = kValues.windUpMs;
            throwsDue_ -= 1;
        }
    }
    return speed;
}

// The throw: a fork, spoon or knife (rand() % 3), straight at the mouse from the dome's rim.
void Spitter::throwAtMouse(BossWorld& world) {
    static constexpr int kPictures[3] = {4, 5, 6};
    ProjectileSpec spec;
    spec.flight = 2;
    spec.hits = 5;  // the mouse and bosses
    spec.picture = kPictures[world.random.next() % 3];
    spec.layer = 8;
    spec.floor = floor_;
    Vec2 direction = world.mousePosition - position_;
    direction.normalise(1.0f);
    if (!world.projectiles) return;
    const int index = world.projectiles->create(spec);
    if (index < 0) return;
    const Vec2 at = position_ + direction * (radius_ * 1.20000005f);
    const Vec2 velocity = direction * float(kValues.throwSpeed);
    world.projectiles->fire(index, at, velocity, 0.0f, kValues.throwLift, world.sounds);
}

// Object::canSeePlayer(false, 2 pi): the mouse in play on its floor, not protected, walking,
// idle or carrying, not under a cup, on screen with it, nearer than the view distance and
// nothing in between (all round: half a turn either way).
bool Spitter::canSeeMouse(const BossWorld& world) const {
    const float onScreen = position_.x - world.viewLeft;
    if (!(onScreen >= 0.0f) || !(onScreen < kScreenWidth)) return false;
    const CatTarget& m = world.mouse;
    if (!m.active || m.floor != floor_ || m.hurtMs >= 1) return false;
    if (m.state < 0 || m.state > 8 || ((1u << m.state) & 0x111u) == 0) return false;
    if (m.carriesSomething) return false;
    Vec2 toward = m.position - position_;
    const float distance = toward.normalise(1.0f);
    if (!(distance < kValues.viewDistance)) return false;
    float off = std::fmod(heading_ - std::atan2(toward.y, toward.x), kTwoPi);
    if (off > kPi) off = off + -kTwoPi;
    if (off < -kPi) off = off + kTwoPi;
    if (!(std::fabs(off) < kTwoPi * 0.5f)) return false;
    return !world.walls.lineTouches(position_, m.position, world.viewLeft, 1);
}

void Spitter::blowUp(BossWorld& world, const std::function<void()>& throwKey) {
    world.particles.add(kParticleExplosion, floor_, position_ + Vec2{0.0f, size_.y * 0.5f}, {}, nullptr, 0,
                        Vec2{-1.0f, -1.0f}, true);
    if (throwKey) throwKey();
    state_ = 0x18;
    throwing_ = 1000;
    world.sounds.push_back(effect(SoundEvent::Play, kSoundExplosion));
}

void Spitter::touchMouse(bool mouseProtected, BossWorld& world) {
    if (mouseProtected || state_ == 0x18) return;
    for (int s : kMechSounds) world.sounds.push_back(effect(SoundEvent::Stop, s));
    if (world.killMouse) world.killMouse();
    state_ = 0x15;
    cooldownMs_ = 5000;
    world.particles.add(kParticleCaught, floor_, position_, {}, nullptr, 0, Vec2{-1.0f, -1.0f}, true);
    world.sounds.push_back(effect(SoundEvent::Play, kSoundCaught));
}

void RailTarget::update(int dtMs, bool levelOnTop, const LevelGeometry& walls, float viewLeft) {
    if (!updating) return;
    anim.update(dtMs);
    // (In a boss level, LevelState +0x14 = 2, it waits while a cutscene or popup is on top.)
    if (!levelOnTop) return;
    float speed = 0.0f;
    if (state == 0x1b) {
        speed = walkRoute(walk, position, radius, kRailSpeed, aim, heading, nullptr);
    } else if (state == 0x1c && stopMs < 60001) {
        stopMs -= dtMs;
        if (stopMs < 1) {
            stopMs = 0;
            state = 0x1b;
            anim.setAnim(0, -1, SpriteAnim::Hold, 200);
        }
    }
    heading = turnTowards(heading, aim, kValues.turnRate * speed);
    stepAlongHeading(position, velocity, heading, speed, dtMs, radius, walls, viewLeft);
}

void BeamEmitter::update(int dtMs, std::vector<SoundEvent>& sounds) {
    if (!updating) return;
    anim.update(dtMs);  // (Object::update's own, then the device's: its frames run twice as fast)
    clockMs += dtMs;
    anim.update(dtMs);
    if (state == 0x23 && step == 0) {
        timerMs -= dtMs;
        if (timerMs < 1) {
            timerMs = 0;
            state = 0x22;
            anim.setAnim(0, -1, SpriteAnim::Hold, 200);
            sounds.push_back(effect(SoundEvent::Play, kSoundEmitterOff));
        }
    } else if (step == 2) {
        timerMs -= dtMs;
        if (timerMs < 1) {
            timerMs = 0;
            updating = false;
            state = 0x22;
            step = 0;
            anim.setAnim(0, -1, SpriteAnim::Hold, 200);
        }
    }
}

}  // namespace doc::game
