// World 4's boss fight in the session (map 789): the henchman in his balloon over the pond,
// the shuriken launcher on its rail, the reflectors that turn its shurikens and the breakable
// light; their messages and what a shuriken does to them. The launcher's own rules are in
// turret.cpp.
#include "game/session.hpp"

#include <cmath>

namespace doc::game {
namespace {

constexpr int kSoundLauncherMoves = 0xa1;  // a burst over (playSoundOnce)
constexpr int kSoundReflect = 0x38;        // a shuriken turned (playSoundOnce)
constexpr int kSoundLightBreaks = 0x11;
constexpr int kSoundDunkStart = 0xa3;      // dunkHenchman2: the balloon lets go
constexpr int kSoundSplash = 0x0d;
constexpr int kParticleSpark = 0x2c;       // a turned shuriken's sparks
constexpr int kParticlePuff = 0x11;        // the balloon bursting
constexpr int kReflectorLeft = 0x3d, kReflectorRight = 0x3e;  // (1.1.1; 1.0.5 0x39, 0x3a)
constexpr int kLightWhole = 0x2d, kLightBroken = 0x2e;  // (1.1.1; 1.0.5 0x29, 0x2a)
constexpr float kDunkDepth = -20.0f;       // the henchman sinks 20
constexpr float kDunkMs = 100.0f;

}  // namespace

Launcher* Session::launcherFor(int objectId) {
    for (Launcher& l : launchers_)
        if (l.objectId() == objectId) return &l;
    return nullptr;
}

void Session::setUpTurretRoom() {
    // ProjectileManager::load: the launcher's shuriken, picture 3 (the whole picture one frame).
    const SheetSize shuriken = level_.bossSheet("sprites/boss4_shuriken.png");
    projectiles_.setPictureSheet(kPictureBossShuriken, shuriken.width, shuriken.height);
    for (const LevelObject& o : objects_) {
        // (Launchers of other tags, turrets that aim at the mouse, are not reconstructed.)
        if (o.type == kLauncher && o.tag == 0x66) launchers_.emplace_back(o.id, o.position, o.radius, 0.0f);
        if (o.type == kReflector) reflectorCatches_.push_back(ReflectorCatch{o.id});
    }
    // ObjControllerCat::init @0x19f9a6: in profile worlds 3 and 5 the henchman starts in his
    // balloon; world 4's (index 3) wears boss4.png in it, his cry picture and his splash.
    if (level_.info().world != 3) return;
    for (LevelObject& o : objects_) {
        if (o.type != kFluffy2) continue;
        if (Cat* cat = catFor(o.id)) {
            cat->rideBalloon(level_.bossSheet("sprites/boss4/boss4.png"));
            cat->setHenchmanSheets(level_.bossSheet("sprites/boss4/boss4_cry.png"),
                                   level_.bossSheet("sprites/boss4/boss4_splash.png"), 75.0f);
            o.state = ObjectState(cat->state());
        }
    }
}

void Session::updateTurretRoomObject(LevelObject& o, int dtMs) {
    if (Launcher* launcher = launcherFor(o.id)) {
        Launcher::Shot shot;
        bool burstOver = false;
        if (launcher->update(dtMs, shot, burstOver)) {
            // ObjControllerSpawner::update @0x1bb092: a shuriken (picture 2) flying straight,
            // hurting the mouse, cats and balloons; drawn below the henchman unless the launcher
            // is at its top stop with the second reflector turning left.
            ProjectileSpec spec;
            spec.flight = 1;
            spec.playerTrigger = 0;
            spec.hits = 1 | 0xa;
            spec.picture = kPictureBossShuriken;
            spec.layer = 2;
            spec.floor = o.floor;
            if (shot.stop == 3)
                for (const LevelObject& r : objects_)
                    if (r.type == kReflector && r.tag == 2 && r.state == ObjectState(kReflectorLeft)) spec.layer = 8;
            const int index = projectiles_.create(spec);
            if (index >= 0) projectiles_.fire(index, shot.at, shot.velocity, 3.14159274f, 0.0f, soundEvents_);
        }
        if (burstOver) sound(kSoundLauncherMoves, SoundEvent::PlayOnce);
        o.position = launcher->position();
        o.state = ObjectState(launcher->state());
        o.timerMs = launcher->clockMs();
        return;
    }
    if (o.type == kReflector) {
        // Object::update @0x11a450: a shuriken caught this tick (and not the tick before) is
        // turned by a right angle: one flying across goes up or down, one flying up or down goes
        // across (to the reflector's left, 0x3d, or right, 0x3e); it starts again from the
        // reflector's middle, with six sparks across its new way.
        for (ReflectorCatch& c : reflectorCatches_) {
            if (c.objectId != o.id) continue;
            for (std::size_t i = 0; i < c.now.size(); ++i) {
                const int index = c.now[i];
                if (index < 0 || index == c.before[i]) continue;
                sound(kSoundReflect, SoundEvent::PlayOnce);
                Projectile& p = projectiles_.slot(index);
                const Vec2 old = p.velocity;
                const int state = int(o.state);
                if (std::fabs(p.velocity.x) <= 0.0f) {
                    if (std::fabs(p.velocity.y) > 0.0f && (state == kReflectorRight || state == kReflectorLeft)) {
                        p.velocity.x = state == kReflectorRight ? p.velocity.y : -p.velocity.y;
                        p.velocity.y = 0.0f;
                    }
                } else if (state == kReflectorRight || state == kReflectorLeft) {
                    p.velocity.y = state == kReflectorRight ? p.velocity.x : -p.velocity.x;
                    p.velocity.x = 0.0f;
                }
                Vec2 way = (old * -1.0f) + p.velocity;
                way.normalise(1.0f);
                const Vec2 across{-way.y, way.x};
                for (int n = 0; n < 6; ++n)
                    particles_.add(kParticleSpark, o.floor, o.position + across * random_.within(-5.0f, 5.0f), {}, nullptr, 0,
                                   way, true);
                p.position = o.position;
            }
            c.before = c.now;
            c.now = {-1, -1, -1};
        }
        return;
    }
    if (o.type == kBreakable && o.anim.finished()) {
        // Broken, its picture run: it stops (+0x86b set: HYPOTHESIS, what that flag does is not
        // traced).
        o.updating = false;
    }
}

// Object::touchProjectile @0x112ef4 for the fight's parts. A reflector keeps up to three
// shurikens of the tick; the light, whole, breaks: it tells the walls and objects of its tag
// (message 0), runs its picture once at 100 ms (sound 0x11) and stays broken; the henchman in
// his balloon is hit (state 0x13) and the fight is won.
bool Session::turretTouchProjectile(LevelObject& o, int projectile) {
    if (o.type == kReflector) {
        for (ReflectorCatch& c : reflectorCatches_) {
            if (c.objectId != o.id) continue;
            for (int& slot : c.now)
                if (slot < 0) {
                    slot = projectile;
                    break;
                }
        }
        return true;
    }
    if (o.type == kBreakable) {
        if (o.state == ObjectState(kLightWhole)) {
            GameMessage m;
            m.type = 0;
            m.tag = o.tag;
            m.sender = o.id;
            sendMessage(m, true);
            o.anim.setAnim(0, o.anim.frameCount() - 1, SpriteAnim::Once, 100);
            sound(kSoundLightBreaks);
            o.updating = true;
            o.state = ObjectState(kLightBroken);
        }
        return true;
    }
    if (o.type == kFluffy2) {
        if (Cat* henchman = catFor(o.id); henchman && henchman->state() == 0x18) {
            henchman->setState(0x17);
            o.state = ObjectState(0x17);
            fireEvent("boss_win");
        }
        return true;
    }
    return false;
}

// Object::receiveMessage @0x116270: message 0 flips a reflector (its frame shows which way it
// turns); the one of tag 5 also moves between the drawing passes (2 when it turns right, 8
// when left: HYPOTHESIS, the pass only matters for the drawing order).
bool Session::turretReceiveMessage(LevelObject& o, const GameMessage& message) {
    if (o.type != kReflector) return false;
    if (message.type != 0) return true;
    const bool wasLeft = o.state == ObjectState(kReflectorLeft);
    o.state = ObjectState(wasLeft ? kReflectorRight : kReflectorLeft);
    o.anim.setFrame(wasLeft ? 1 : 0);
    return true;
}

// ScriptSequence::turretSwitch @0x1d1410: the launcher is switched on (or off again).
void Session::sequenceTurretSwitch() {
    if (cutscene_.scriptState == 0)
        if (LevelObject* o = firstObjectOfType(kLauncher))
            if (Launcher* launcher = launcherFor(o->id)) {
                launcher->toggle();
                o->state = ObjectState(launcher->state());
            }
    cutscene_.scriptState = 2;
}

// ScriptSequence::dunkHenchman2 @0x1d486c: the balloon bursts (a puff, sound 0xa3) and the
// henchman, posed on his cry picture's first frame, sinks 20 into the pond over 100 ms; then he
// throws out the key, splashes (frames 0..4 once at 150 ms, sound 0x0d) and cries (frames 1..
// at 125 ms, state 0x13).
void Session::sequenceDunkHenchman2() {
    LevelObject* o = firstObjectOfType(kFluffy2);
    Cat* henchman = o ? catFor(o->id) : nullptr;
    if (!henchman) {
        cutscene_.scriptState = 2;
        return;
    }
    if (cutscene_.scriptState == 0) {
        henchman->dunkHenchman();
        particles_.add(kParticlePuff, o->floor, henchman->position(), {}, nullptr, 0, Vec2{}, true);
        cutscene_.startHeight = henchman->position().y;
        sound(kSoundDunkStart);
    } else if (cutscene_.scriptState == 1) {
        const float start = cutscene_.startHeight;
        const float end = start + kDunkDepth;
        float y = (end - start) / kDunkMs * float(cutscene_.elapsedMs) + start;
        y = end <= start ? std::min(start, std::max(end, y)) : std::min(end, std::max(start, y));
        henchman->sinkTo(y);
        if (float(cutscene_.elapsedMs) >= kDunkMs) {
            throwKey(henchman->position() + Vec2{0.0f, 5.0f}, o->floor);
            henchman->splashHenchman();
            sound(kSoundSplash);
            cutscene_.scriptState = 2;
        }
    }
    o->position = henchman->position();
    o->state = ObjectState(henchman->state());
}

}  // namespace doc::game
