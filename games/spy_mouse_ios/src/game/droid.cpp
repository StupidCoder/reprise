#include "game/droid.hpp"

#include <algorithm>
#include <cmath>

namespace doc::game {

namespace {

constexpr float kPi = 3.14159274f;
constexpr float kTwoPi = 6.28318548f;
constexpr float kDegrees = 0.0174532924f;

// The droid's settings in the tuning table (Values::init @0x1449d0.., defaults in code).
struct AccelaValues {
    int rageBuildMs = 500;           // +0x250 "accelrobo_rage_build_time"
    int searchMs = 2000;             // +0x254 "accelrobo_search_time"
    int sensedDistance = 60;         // +0x258 "accelrobo_sensed_distance"
    float maxSpeed = 3.00007606f;    // +0x25c "accelrobo_max_speed"
    float returnSpeed = 0.600000024f;  // +0x260 "accelrobo_return_speed"
    float acceleration = 0.200000003f; // +0x264 "accelrobo_acceleration"
    float bounceBack = 0.350001007f;   // +0x268 "accelrobo_bounceback_speed"
    float turnRate = 1.0f;           // +0x60, the cats' turn rate
};
constexpr AccelaValues kValues{};

// What it is doing (Object +0x1d4).
enum Step : int {
    Dormant = 0,     // at the rail's start, waiting to see the mouse
    // (1, looking round on the spot, is never entered by world 1's droid: nothing sets it)
    Watching = 2,    // the mouse is close: it turns to it
    Lingering = 3,   // the mouse went away: half a second more
    Returning = 4,   // back along the rail to its start
    Raging = 5,      // winding up
    Charging = 6,    // along the rail, faster and faster
    Coasting = 7,    // from the last rail point straight on
    Recoiling = 8,   // hit something; bounces back, then looks round
    Sliding = 9,     // went over a trigger line: out of control
};

// Effect numbers (sound table).
constexpr int kSoundRage = 0x08;
constexpr int kSoundHitWall = 0x09;
constexpr int kSoundSlide = 0x0b;
constexpr int kSoundNoticed = 0x28;
constexpr int kSoundGaveUp = 0x5b;
constexpr int kSoundCaught = 0x60;
constexpr int kEngineIdle = 5;     // looping engine sounds (Object::playMovementSound)
constexpr int kEngineCharge = 6;
constexpr int kEngineRage = 7;
constexpr int kMechSounds[] = {0xa8, 0xa4, 0xa5};

// Particle kinds.
constexpr int kParticleCaught = 0x0e;
constexpr int kParticleRecovered = 0x0f;
constexpr int kParticleRage = 0x10;
constexpr int kParticleSpark = 0x2c;
constexpr int kParticleExplosion = 0x2b;
constexpr int kParticleSmoke = 0x2d;
constexpr int kSoundExplosion = 0x8f;

constexpr int kTriggerLine = 0x81;

float wrapPositive(float a) {
    a = std::fmod(a, kTwoPi);
    if (a < 0.0f) a = a + kTwoPi;
    return a;
}

}  // namespace

Droid::Droid(int objectId, int floor, CatRoute rail, float radius, Vec2 size, const Sheets& sheets)
    : objectId_(objectId), floor_(floor), size_(size), radius_(radius), rail_(std::move(rail)) {
    // postInit @0x1a9162: on the rail's first point, heading along the first leg (in
    // degrees through get2DAngle, then radians).
    if (!rail_.points.empty()) position_ = rail_.points[0].position;
    if (rail_.points.size() >= 2) {
        const Vec2 d = rail_.points[1].position - rail_.points[0].position;
        heading_ = d.angleDegrees() * kDegrees;
    }
    // ObjControllerBoss::loadAssets @0x1a92aa: body and base 64 wide, the wreck 90 wide
    // looping at 200 ms.
    body.setSheet(sheets.body.width, sheets.body.height, 64.0f);
    base.setSheet(sheets.base.width, sheets.base.height, 64.0f);
    wreck.setSheet(sheets.wreck.width, sheets.wreck.height, 90.0f);
    wreck.setAnim(0, wreck.frameCount() - 1, SpriteAnim::Loop, 200);
}

void Droid::restore(const ObjectSnapshot& o) {
    state_ = o.state;
    step_ = o.step;
    position_ = o.position;
    heading_ = o.heading;
    railIndex_ = o.routeIndex;
    body.setFrame(o.frame);
}

void Droid::updateAnimations(int dtMs) {
    // (A part picture driven by a cutscene, +0x21c 0..5, runs instead; not used by world 1.)
    if (state_ == 0x3C) {
        if (scriptFlag_ == 1) wobble_ = float(dtMs) * 0.5f + wobble_;
    } else {
        // The dome turns faster while it rages, charges and rolls on (table @0x1a9bd4: 0.1, 0.5
        // degrees a millisecond).
        const float rate = (step_ >= Raging && step_ <= Coasting) ? 0.5f : 0.100000001f;
        wobble_ = float(dtMs) * rate + wobble_;
    }
    // The body faces its heading: eighths of a turn starting 67.5 degrees before east.
    float a = std::fmod(heading_ + -1.17809725f, kTwoPi);
    if (a < 0.0f) a = a + kTwoPi;
    const int facing = int(a * 1.27323949f);
    if (body.first() != facing * 2) body.setAnim(facing * 2, facing * 2 + 1, SpriteAnim::Loop, 400);
    body.update(dtMs);
}

// ObjControllerBoss::update @0x1a9598 for the droid.
void Droid::update(int dtMs, BossWorld& world) {
    if (state_ == 0x18) {
        // Broken (updateBrokenState @0x1abd38): smoke puffs (particle 0x30) about it every
        // 1.5 s; the machine sounds are off.
        playMovementSound(-1, world);
        wreck.update(dtMs);
        scriptFlag_ -= dtMs;
        if (scriptFlag_ <= 0) {
            const float x = world.random.within(-5.0f, 5.0f);
            const float y = world.random.within(-5.0f, 10.0f);
            world.particles.add(kParticleSmoke, floor_, position_ + Vec2{x, y}, {}, nullptr, 0, Vec2{-1.0f, -1.0f}, true);
            scriptFlag_ = 1500;
        }
        for (int s : kMechSounds) world.sounds.push_back({SoundEvent::Stop, s});
        return;
    }
    if (state_ == 0x16) {
        state_ = 0x17;
        step_ = Dormant;
    } else if (state_ != 0x17) {
        return;
    }
    searchTimerMs_ -= dtMs;
    if (searchTimerMs_ <= 0 && (step_ == 1 || step_ == Watching))
        searchTimerMs_ = world.random.next() % 4000 + 8000;
    updateAnimations(dtMs);
    const float speed = updateAccela(dtMs, world);
    turnAndMove(dtMs, speed);
}

// The end of ObjControllerBoss::update: the heading turns towards the aim by the turn rate ×
// speed / pi of the difference (never past it); Object::updatePositions moves the droid along
// it. Walls do not hold it there (type 301 skips the sphere test); its own rules stop it.
void Droid::turnAndMove(int dtMs, float speed) {
    const float aim = wrapPositive(aim_);
    const float h = wrapPositive(heading_);
    heading_ = h;
    float d = std::fmod(aim - h, kTwoPi);
    if (d > kPi) d = d + -kTwoPi;
    if (d < -kPi) d = d + kTwoPi;
    float step = d * (kValues.turnRate * speed);
    step = step * 0.318309873f;
    const float limit = std::fabs(d);
    step = std::min(limit, std::max(-limit, step));
    heading_ = wrapPositive(h + step);

    const float move = (float(dtMs) * speed) * 0.0500000007f;
    velocity_ = {libCos(heading_) * move, libSin(heading_) * move};
    position_ = position_ + velocity_;
}

void Droid::playMovementSound(int effect, BossWorld& world) {
    if (movementSound_ == effect) return;
    if (movementSound_ != -1) world.sounds.push_back({SoundEvent::Stop, movementSound_});
    if (effect != -1) world.sounds.push_back({SoundEvent::Play, effect, {}, true});
    movementSound_ = effect;
}

// canAccelaSeePlayer @0x1ac094: nothing in between, and the mouse in line with the droid,
// ahead or behind, within 25 degrees. Just after a crash (step 8) any clear line will do.
bool Droid::canSeeMouse(const BossWorld& world) const {
    Vec2 d = world.mousePosition - position_;
    if (world.walls.lineTouches(position_, world.mousePosition, world.viewLeft, 0)) return false;
    if (step_ == Recoiling) return true;
    d.normalise(1.0f);
    const float along = Vec2{libCos(heading_), libSin(heading_)}.dot(d);
    return along > 0.906307757f || along < -0.906307757f;
}

// "accelrobo_sensed_distance": the mouse is close (a little farther while it already watches).
bool Droid::mouseClose(const BossWorld& world) const {
    const float distance = (world.mousePosition - position_).length();
    float sensed = float(kValues.sensedDistance);
    if (step_ == Watching) sensed = sensed * 1.10000002f;
    return distance < sensed;
}

// accelaStartRage @0x1abdf8: it picks the way along the rail that leads towards the mouse
// (from the point it is at, or still heading for), turns to it and winds up.
void Droid::startRage(BossWorld& world) {
    Vec2 target = world.mousePosition;
    const int s = world.mouseState;
    if (s == 2 || s == 4 || s == 9) target = world.mouseStateEntry;
    Vec2 toMouse = target - position_;
    toMouse.normalise(1.0f);

    auto point = [&](int i) { return rail_.points[std::size_t(i)].position; };
    const int count = int(rail_.points.size());
    const Vec2 current = point(railIndex_);
    const float dx = current.x - position_.x, dy = current.y - position_.y;
    const float reach = radius_ * 0.400000006f;
    int next = railIndex_ + railDirection_;
    if (dx * dx + dy * dy > reach * reach) next = railIndex_;
    const int previous = railIndex_ - railDirection_;
    // (The original reads past the rail's end when `next` is its length; the rails of
    // world 1 never stop there, and the port keeps to the rail.)
    if (next >= count) next = count - 1;
    if (previous < 0) {
        railIndex_ = next;
    } else if (next < 0) {
        railDirection_ = -railDirection_;
        railIndex_ = previous;
    } else {
        Vec2 towardsNext = point(next) - position_;
        Vec2 towardsPrevious = point(previous) - position_;
        towardsNext.normalise(1.0f);
        towardsPrevious.normalise(1.0f);
        if (toMouse.dot(towardsPrevious) > toMouse.dot(towardsNext)) {
            railDirection_ = -railDirection_;
            railIndex_ = previous;
        } else {
            railIndex_ = next;
        }
    }
    heading_ = aim_ = (point(railIndex_) - position_).angleDegrees() * kDegrees;
    // (At floor 0, as the original passes it.)
    particleSlot_ = world.particles.add(kParticleRage, 0, position_ + Vec2{0.0f, size_.y * 0.5f + 10.0f}, {},
                                        &particleSlot_, 0, Vec2{-1.0f, -1.0f}, true);
    step_ = Raging;
    stepClockMs_ = world.gameTimeMs + kValues.rageBuildMs;
    world.sounds.push_back({SoundEvent::Play, kSoundRage});
}

// Object::updatePath @0x115614 for the droid: heads for rail point railIndex_; within 0.4
// radius of it the point is reached and the next one taken. While charging, a reached point
// ends the charge (the droid rolls on from it). On its way back the droid always goes
// backwards, and at the rail's start it falls dormant again. Returns the speed (0 when it
// stopped).
float Droid::followRail(float speed) {
    const int count = int(rail_.points.size());
    if (railIndex_ < 0 || railIndex_ >= count) return 0.0f;
    auto point = [&](int i) { return rail_.points[std::size_t(i)].position; };
    const float reach = radius_ * 0.400000006f;
    const float reachSquared = reach * reach;
    float dx = point(railIndex_).x - position_.x;
    float dy = point(railIndex_).y - position_.y;
    for (;;) {
        if (dx * dx + dy * dy > reachSquared) break;
        if (step_ == Charging && velocity_.lengthSquared() > 0.0f) {
            step_ = Coasting;
            return 0.0f;
        }
        // (Rail points carry no pauses or cuts on map 791.)
        if (state_ == 0x17) {
            if (step_ == Returning) railDirection_ = -1;
        }
        const int previous = railIndex_;
        railIndex_ += railDirection_;
        if (railIndex_ >= 0 && railIndex_ < count) {
            dx = point(railIndex_).x - position_.x;
            dy = point(railIndex_).y - position_.y;
            continue;
        }
        if (railIndex_ < 0 && rail_.startMode == 2 && rail_.endMode == 2) {
            railIndex_ = count - 2;
            break;
        }
        switch (rail_.endMode) {
        case 0:
            if (railIndex_ >= 2) {
                railIndex_ = previous;
                heading_ = aim_ = std::atan2(point(previous).y - point(previous - 1).y,
                                             point(previous).x - point(previous - 1).x);
                return 0.0f;
            }
            if (step_ == Returning) {
                // Back at the start: asleep again, facing along the first leg.
                railIndex_ = 0;
                step_ = Dormant;
                heading_ = aim_ = std::atan2(point(1).y - point(0).y, point(1).x - point(0).x);
                return 0.0f;
            }
            break;
        case 1:
            railIndex_ -= 2 * railDirection_;
            railDirection_ = -railDirection_;
            dx = point(railIndex_).x - position_.x;
            dy = point(railIndex_).y - position_.y;
            heading_ = aim_ = std::atan2(dy, dx);
            break;
        case 2:
            railDirection_ = 1;
            railIndex_ = 0;
            break;
        default:
            break;  // (mode 3 links to another route; not on map 791)
        }
        break;
    }
    if (railIndex_ < count) aim_ = std::atan2(dy, dx);
    return speed;
}

// The trigger lines (MapCollision::collideSphereSpecific with type 0x81): touching one sends
// its message to its tag (type 4 while sliding, else 0).
void Droid::touchTriggerLines(BossWorld& world) {
    Vec2 probe = position_;
    Vec2 found;
    if (world.walls.collideSphereOfType(probe, radius_, kTriggerLine, found, 0.0f) == 0) return;
    const Wall& line = world.walls.walls()[std::size_t(int(found.x))];
    if (world.sendMessage) world.sendMessage(step_ == Sliding ? 4 : 0, int(found.y), line.type);
}

// ObjControllerBoss::updateAccela @0x1ac158: one tick of the droid's behaviour; returns its
// speed for the move.
float Droid::updateAccela(int dtMs, BossWorld& world) {
    base.update(dtMs);
    bool faceAim = false;  // the heading jumps to the aim this tick
    switch (step_) {
    case Dormant:
        if (canSeeMouse(world)) {
            startRage(world);
            stepClockMs_ += 500;
        }
        break;
    case Watching:
        base.setAnim(1, -1, SpriteAnim::Hold, 200);
        heading_ = (world.mousePosition - position_).angleDegrees() * kDegrees;
        if (canSeeMouse(world)) {
            startRage(world);
        } else if (!mouseClose(world)) {
            step_ = Lingering;
            stepClockMs_ = world.gameTimeMs + 500;
        }
        break;
    case Lingering:
        if (world.gameTimeMs > stepClockMs_) {
            step_ = Returning;
            stepClockMs_ = 0;
            sensorFrame_ = spriteDirection(heading_, 8);
            sensorTick_ = 0;
            faceAim = true;
        }
        if (canSeeMouse(world)) {
            startRage(world);
        } else if (mouseClose(world)) {
            step_ = Watching;
            world.sounds.push_back({SoundEvent::Play, kSoundNoticed});
        }
        break;
    case Returning:
        base.setAnim(0, -1, SpriteAnim::Hold, 200);
        if (canSeeMouse(world)) startRage(world);
        break;
    case Raging:
        playMovementSound(kEngineRage, world);
        base.setAnim(1, -1, SpriteAnim::Hold, 200);
        if (world.gameTimeMs > stepClockMs_) {
            chargeSpeed_ = 0.0500000007f;
            step_ = Charging;
        }
        break;
    case Charging:
        playMovementSound(kEngineCharge, world);
        touchTriggerLines(world);
        break;
    case Coasting: {
        coastDistance_ = coastDistance_ + (coastSpeed_ * float(dtMs)) * 0.0500000007f;
        position_ = coastFrom_ + coastDirection_ * coastDistance_;
        LevelGeometry::SphereOptions options;
        options.viewLeft = 0.0f;
        if (world.walls.collideSphere(position_, radius_, options) == 0) break;
        // Crash: sparks fly back off the wall.
        step_ = Recoiling;
        base.setAnim(0, -1, SpriteAnim::Hold, 200);
        world.sounds.push_back({SoundEvent::Play, kSoundHitWall});
        playMovementSound(kEngineIdle, world);
        stepClockMs_ = 0;
        const Vec2 across{-coastDirection_.y, coastDirection_.x};
        for (int i = 0; i < 6; ++i) {
            const float offset = world.random.within(-5.0f, 5.0f);
            const Vec2 at = position_ + coastDirection_ * radius_ + across * offset;
            world.particles.add(kParticleSpark, floor_, at, {}, nullptr, 0, coastDirection_ * -1.0f, true);
        }
        sparked_ = true;
        break;
    }
    case Recoiling:
        if (coastDistance_ >= 0.00999999978f) {
            coastDistance_ = coastDistance_ + ((coastDistance_ * kValues.bounceBack) * float(dtMs)) * -0.0500000007f;
            position_ = coastFrom_ + coastDirection_ * coastDistance_;
            if (coastDistance_ < 0.00999999978f)
                particleSlot_ = world.particles.add(kParticleRecovered, 0, position_ + Vec2{0.0f, size_.y * 0.5f + 10.0f},
                                                    {}, &particleSlot_, 0, Vec2{-1.0f, -1.0f}, true);
        }
        if (coastDistance_ >= 0.00999999978f) break;
        // Back where the roll began: it looks round.
        playMovementSound(kEngineIdle, world);
        position_ = coastFrom_;
        stepClockMs_ += dtMs;
        if (canSeeMouse(world)) {
            startRage(world);
        } else if (mouseClose(world)) {
            step_ = Watching;
            return 0.0f;
        } else if (stepClockMs_ > kValues.searchMs) {
            step_ = Returning;
            stepClockMs_ = 0;
            sensorFrame_ = spriteDirection(heading_, 8);
            sensorTick_ = 0;
            world.sounds.push_back({SoundEvent::Play, kSoundGaveUp});
            faceAim = true;
        }
        break;
    case Sliding:
        touchTriggerLines(world);
        break;
    default:
        break;
    }

    switch (step_) {
    case Returning: {
        chargeSpeed_ = kValues.returnSpeed;
        const float speed = followRail(chargeSpeed_);
        if (faceAim) heading_ = aim_;
        return speed;
    }
    case Sliding:
        chargeSpeed_ = std::min(float(dtMs) * 0.349999994f,
                                chargeSpeed_ + (chargeSpeed_ * float(dtMs)) * 0.00249999994f);
        return followRail(chargeSpeed_);
    case Charging: {
        chargeSpeed_ = std::min(kValues.maxSpeed,
                                chargeSpeed_ + ((chargeSpeed_ * kValues.acceleration) * float(dtMs)) * 0.0500000007f);
        const float speed = followRail(chargeSpeed_);
        if (step_ != Coasting) return speed;
        // The charge ended at a rail point: from here it rolls straight on.
        coastDirection_ = {libCos(heading_), libSin(heading_)};
        coastFrom_ = position_ + coastDirection_ * chargeSpeed_ * float(dtMs) * 0.0500000007f;
        position_ = coastFrom_;
        coastDistance_ = 0.0f;
        coastSpeed_ = chargeSpeed_;
        return 0.0f;
    }
    default:
        return 0.0f;
    }
}

// ObjControllerBoss::receiveMessage @0x1a9ccc: the first trigger line sets it sliding (sound
// 0x0b, engine off); another one while it slides wins the fight.
void Droid::receiveMessage(int type, BossWorld& world) {
    if ((type | 4) != 4) return;
    if (step_ != Sliding) {
        step_ = Sliding;
        world.sounds.push_back({SoundEvent::Play, kSoundSlide});
        playMovementSound(-1, world);
        return;
    }
    winRequested = true;
}

void Droid::blowUp(BossWorld& world, const std::function<void()>& throwKey) {
    world.particles.add(kParticleExplosion, floor_, position_ + Vec2{0.0f, size_.y * 0.5f}, {}, nullptr, 0,
                        Vec2{-1.0f, -1.0f}, true);
    if (throwKey) throwKey();
    state_ = 0x18;
    scriptFlag_ = 1000;
    world.sounds.push_back({SoundEvent::Play, kSoundExplosion});
}

// ObjControllerBoss::touchPlayer @0x1a9be0: unless the mouse is protected or the droid
// broken, it catches the mouse (Player::kill): the machine sounds stop, a cloud and the
// death sound.
void Droid::touchMouse(bool mouseProtected, BossWorld& world) {
    if (mouseProtected || state_ == 0x18) return;
    for (int s : kMechSounds) world.sounds.push_back({SoundEvent::Stop, s});
    if (world.killMouse) world.killMouse();
    state_ = 0x15;
    playMovementSound(-1, world);
    caughtTimerMs_ = 5000;
    world.particles.add(kParticleCaught, floor_, position_, {}, nullptr, 0, Vec2{-1.0f, -1.0f}, true);
    world.sounds.push_back({SoundEvent::Play, kSoundCaught});
}

}  // namespace doc::game
