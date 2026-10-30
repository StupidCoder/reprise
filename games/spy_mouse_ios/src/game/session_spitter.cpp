// World 2's boss fight in the session (map 795): setting up the Spitter, the rail spheres and
// the wall devices, their messages, and the projectiles' collisions and update
// (SceneGame::collideObjects @0x1009dc, updatePure @0x100e18, Object::touchProjectile @0x112ef4,
// Object::receiveMessage @0x115f18, Player::touchProjectile @0x1244a4). The rules of the
// parts themselves are in spitter.cpp and projectiles.cpp.
#include "game/session.hpp"

#include <algorithm>
#include <cstdlib>

namespace doc::game {
namespace {

constexpr int kStopTimeMs = 15000;  // "target_stop_time"
constexpr int kBeamTag = 123, kBeamWall = 0x8f;
constexpr int kSoundEmitterOn = 0xaa, kSoundBeam = 0xac;
constexpr int kParticleVanish = 0x11;  // a throw in the air when the beam comes on
constexpr int kParticleHit = 0x00;     // the mouse struck

}  // namespace

Spitter* Session::spitterFor(int objectId) {
    for (Spitter& s : spitters_)
        if (s.objectId() == objectId) return &s;
    return nullptr;
}

RailTarget* Session::railTargetFor(int objectId) {
    for (RailTarget& t : railTargets_)
        if (t.objectId == objectId) return &t;
    return nullptr;
}

BeamEmitter* Session::emitterFor(int objectId) {
    for (BeamEmitter& e : emitters_)
        if (e.objectId == objectId) return &e;
    return nullptr;
}

void Session::setUpSpitterRoom() {
    for (LevelObject& o : objects_) {
        if (o.type == kSpitter) {
            const Spitter::Sheets sheets{level_.bossSheet("sprites/boss2/boss2.png"),
                                         level_.bossSheet("sprites/boss2/boss2_destroyed.png")};
            spitters_.emplace_back(o.id, o.floor, o.position, o.radius, o.size,
                                   CatRoute::link(level_.floor(o.floor).map, o.tag), sheets);
            o.anim = spitters_.back().body;
        } else if (o.type == kRailTarget) {
            RailTarget t;
            t.objectId = o.id;
            t.floor = o.floor;
            // (Object::touchProjectile @0x113270: the entity text as a number, if any.)
            t.messageTag = o.text.empty() ? -1 : std::atoi(o.text.c_str());
            t.position = o.position;
            t.radius = o.radius;
            t.walk.route = CatRoute::link(level_.floor(o.floor).map, o.tag);
            t.anim = o.anim;
            railTargets_.push_back(t);
        } else if (o.type == kBeamEmitter) {
            BeamEmitter e;
            e.objectId = o.id;
            e.floor = o.floor;
            e.anim = o.anim;
            emitters_.push_back(e);
        }
    }
    // Object::postInit @0x11769c: a device's partner is the first other device.
    for (BeamEmitter& e : emitters_)
        for (const BeamEmitter& other : emitters_)
            if (other.objectId != e.objectId) {
                e.partner = other.objectId;
                break;
            }
    projectiles_.setPictureSheet(kPictureFork, level_.bossSheet("sprites/boss2/boss2_fork.png").width,
                                 level_.bossSheet("sprites/boss2/boss2_fork.png").height);
    projectiles_.setPictureSheet(kPictureSpoon, level_.bossSheet("sprites/boss2/boss2_spoon.png").width,
                                 level_.bossSheet("sprites/boss2/boss2_spoon.png").height);
    projectiles_.setPictureSheet(kPictureKnife, level_.bossSheet("sprites/boss2/boss2_knife.png").width,
                                 level_.bossSheet("sprites/boss2/boss2_knife.png").height);
}

// The fight's parts in Session::updateObject; their objects mirror them.
void Session::updateSpitterRoomObject(LevelObject& o, int dtMs) {
    if (Spitter* s = spitterFor(o.id)) {
        if (!o.updating) return;
        BossWorld world = bossWorld(o.floor);
        s->update(dtMs, world);
        o.position = s->position();
        o.state = ObjectState(s->state());
        o.anim = s->body;  // (Object +0x38; the wreck, +0x3b8, is drawn while broken)
    } else if (RailTarget* t = railTargetFor(o.id)) {
        t->update(dtMs, levelOnTop(), wallsOf(o.floor), viewLeft_);
        o.position = t->position;
        o.state = ObjectState(t->state);
        o.updating = t->updating;
        o.anim = t->anim;
    } else if (BeamEmitter* e = emitterFor(o.id)) {
        e->update(dtMs, soundEvents_);
        o.state = ObjectState(e->state);
        o.updating = e->updating;
        o.anim = e->anim;
    }
}

bool Session::levelOnTop() const {
    return !cutscene_.active && !intro_.active && !notice_.active() && !kiska_.active() && !pause_.active() &&
           !levelOver_ && !results_.active() && !worldComplete_.active() && !hand_.active();
}

void Session::sendMessage(const GameMessage& message, bool applyMap) {
    int floor = shownFloor_;
    if (message.sender != -1)
        if (const LevelObject* from = objectById(message.sender)) floor = from->floor;
    if (applyMap) {
        LevelGeometry& walls = floorWalls_[std::size_t(floor)];
        if (message.type == 0) walls.toggleTag(message.tag);
        else walls.setTagEnabled(message.tag, message.type == 2, message.targetType);
    }
    // (By index: an object a message makes is not met here; none does in the fight.)
    for (std::size_t i = 0; i < objects_.size(); ++i) {
        LevelObject& o = objects_[i];
        if (o.floor != floor || o.id == message.sender) continue;
        // (Trapdoors are type 0x21 in the original once loaded.)
        const int type = isRoomDoor(o.type) ? 0x21 : o.type;
        if (message.tag == o.tag || message.targetType == type) receiveMessage(o, message);
    }
}

void Session::receiveMessage(LevelObject& o, const GameMessage& message) {
    if (turretReceiveMessage(o, message)) return;
    if (Switch* s = switchFor(o.id)) {
        switchReceiveMessage(o, *s, message);
    } else if (Door* d = doorFor(o.id)) {
        doorReceiveMessage(o, *d, message);
    } else if (isRoomDoor(o.type)) {
        trapdoorReceiveMessage(o, message);
    } else if (o.type == kCheeseMachine) {
        // Object::receiveMessage @0x116382: message 0 or 3 to an idle machine with cheeses
        // left: it hands one out (sound 0x78, frames 1..last at 100 ms, one release due).
        if ((message.type == 0 || message.type == 3) && o.machineCount >= 1 && o.state == ObjectState(kMachineIdle)) {
            sound(0x78);
            o.state = ObjectState(kMachineDispensing);
            --o.machineCount;
            o.machineReleases = 1;
            o.anim.setAnim(1, o.anim.frameCount() - 1, SpriteAnim::Once, 100);
        }
    } else if (Droid* droid = droidFor(o.id)) {
        BossWorld world = bossWorld(o.floor);
        droid->receiveMessage(message.type, world);
        o.state = ObjectState(droid->state());
    } else if (Spitter* spitter = spitterFor(o.id)) {
        spitter->receiveMessage(message.type);
    } else if (isWallSpawner(o.type)) {
        if (message.type == 0) dropBomb(o);
    } else if (RailTarget* t = railTargetFor(o.id)) {
        // A sphere hears of the Spitter's end (stops for good) and of the other sphere being
        // hit while it is stopped itself (both stop for good; a throw stuck in this one stays
        // for the whole stop).
        if (message.type != 4) return;
        const LevelObject* from = objectById(message.sender);
        if (!from) return;
        if (from->type == kSpitter) {
            const Spitter* s = spitterFor(from->id);
            if (!s || s->state() != 0x1c) return;
            t->updating = false;
            t->anim.setAnim(0, -1, SpriteAnim::Hold, 200);
        } else if (from->type == kRailTarget && t->state == 0x20) {
            if (RailTarget* other = railTargetFor(from->id)) {
                other->stopMs = 60001;
                other->updating = false;
            }
            t->stopMs = 60001;
            t->updating = false;
            for (int i = 0; i < Projectiles::kSlots; ++i) {
                Projectile& p = projectiles_.slot(i);
                if (p.inUse() && p.status != ProjectileStatus::Gone && p.stuckTo == o.id) {
                    p.timerMs = float(kStopTimeMs);
                    break;
                }
            }
            if (LevelObject* other = objectById(from->id)) other->updating = false;
        }
        o.updating = t->updating;
        o.anim = t->anim;
    } else if (BeamEmitter* e = emitterFor(o.id)) {
        if (message.type != 4) return;
        if (e->state == 0x26) {
            // Lit for the stop's time; with the partner lit as well the beam is on: the wall
            // line switches on, the Spitter hears of it and every throw in the air vanishes.
            sound(kSoundEmitterOn);
            e->state = 0x27;
            e->timerMs = kStopTimeMs;
            e->anim.setAnim(0, e->anim.frameCount() - 1, SpriteAnim::Loop, 200);
            BeamEmitter* partner = emitterFor(e->partner);
            if (partner && partner->state == 0x27) {
                partner->step = 1;
                e->step = 1;
                sound(kSoundBeam, SoundEvent::PlayOnce, true);
                floorWalls_[std::size_t(o.floor)].setTagEnabled(kBeamTag, true, kBeamWall);
                GameMessage toBoss;
                toBoss.type = 4;
                toBoss.sender = e->objectId;
                toBoss.targetType = kSpitter;
                sendMessage(toBoss, false);
                for (int i = 0; i < Projectiles::kSlots; ++i) {
                    const Projectile& p = projectiles_.slots()[std::size_t(i)];
                    if (!p.inUse() || p.status != ProjectileStatus::Flying) continue;
                    particles_.add(kParticleVanish, p.floor, p.position);
                    projectiles_.destroy(i);
                }
            }
        } else {
            // Told again while lit (the Spitter is done): it shuts down.
            e->step = 2;
            e->timerMs = 500;
            sound(kSoundBeam, SoundEvent::Stop);
        }
        o.state = ObjectState(e->state);
        o.anim = e->anim;
    }
}

void Session::collideProjectiles() {
    for (int i = 0; i < Projectiles::kSlots; ++i) {
        Projectile& p = projectiles_.slot(i);
        if (!p.inUse() || p.status != ProjectileStatus::Flying) continue;
        const Vec2 at = p.position;
        // The mouse first (if it may be hit), then what it carries.
        if ((p.playerTrigger | 2) == 2 && (p.hits & 1) != 0 && !mouseCaught_) {
            Vec2 d = at - mouse_.position;
            const float distance = d.normalise(1.0f);
            if ((mouse_.reachDistance + p.size) - distance > 0.0f) {
                projectiles_.trigger(i, random_, soundEvents_);
                projectileHitsMouse(i);
                continue;
            }
            bool stuck = false;
            for (int id : carried_) {
                LevelObject& o = objects_[std::size_t(id)];
                Vec2 e = at - o.position;
                const float gap = e.normalise(1.0f);
                if ((o.radius + p.size) - gap > 0.0f) {
                    projectiles_.stickInto(i, o.id, o.position, random_, soundEvents_);
                    touchProjectile(o, i);
                    stuck = true;
                    break;
                }
            }
            if (stuck) continue;
        }
        // The objects that can be hit by it.
        for (std::size_t k = 0; k < objects_.size(); ++k) {
            LevelObject& o = objects_[k];
            if (!o.active) continue;
            bool canHit = false;
            if (isPortedCat(o.type)) canHit = p.playerTrigger == 0 && (p.hits & 2) != 0;
            else if (o.type >= 300 && o.type <= 306) canHit = p.playerTrigger == 0 && (p.hits & 4) != 0;
            else if (o.type == kRailTarget) canHit = true;
            else if (o.type == kBalloon) canHit = (p.hits & 8) != 0;
            else if (o.type == kReflector || o.type == kBreakable) canHit = true;
            // (Types 0x10a, 0x4b and 0x159..0x15c and the flagged pickups are not
            // reconstructed.)
            if (!canHit) continue;
            Vec2 d = at - o.position;
            const float distance = d.normalise(1.0f);
            if ((o.radius + p.size) - distance > 0.0f) touchProjectile(o, i);
        }
        // The walls, tested with a copy of its position: bouncy walls (0x6d) first.
        if (p.picture == kPictureBossShuriken) continue;
        Vec2 copy = at;
        Vec2 normal{};
        const LevelGeometry& walls = wallsOf(shownFloor_);
        unsigned touched = walls.collideSphereSpecific(copy, p.size, 0x6d, &normal, viewLeft_);
        if (touched == 0) touched = walls.collideSphereSpecific(copy, p.size, 0, &normal, viewLeft_);
        if (touched & (contact::kSolid | contact::kBouncy)) {
            // (Bouncing, for bouncy walls and kinds 3 and 4, is not reconstructed; the fight
            // has neither.)
            projectiles_.trigger(i, random_, soundEvents_);
        }
    }
}

void Session::touchProjectile(LevelObject& o, int projectile) {
    if (turretTouchProjectile(o, projectile)) return;
    const Projectile& thrown = projectiles_.slots()[std::size_t(projectile)];
    if (o.type == kBalloon) {
        // Object::touchProjectile @0x112f54: a balloon in the trail is let go (Player::drop), and
        // it pops (0x1e, running); the throw flies on.
        if (o.inTrail) dropFromTrail(o.id);
        o.state = ObjectState(0x22);
        o.updating = true;
        return;
    }
    if (o.inTrail) {
        // @0x11351a: something carried struck by a shuriken (picture 0) or a dart (7) slows the
        // mouse ("shuriken_slowdown" / "poison_slowdown", 0.45 of its speed, recovering); a
        // cheese struck counts for the "shuriken_cheese" goal.
        if (thrown.picture == kPictureShuriken || thrown.picture == kPictureDart) {
            mouse_.acceleration = mouse_.acceleration * 0.449999988f;
            if (isCheese(o.type)) ++uses_.shurikenCheese;
        }
        return;
    }
    RailTarget* t = railTargetFor(o.id);
    if (!t || t->state != 0x1f) return;
    // A moving sphere struck: it tells its device (cutlery) or its route's tag (anything else),
    // stops for the stop's time showing its last frame, and the throw stays in it as long.
    projectiles_.trigger(projectile, random_, soundEvents_);
    const Projectile& p = projectiles_.slots()[std::size_t(projectile)];
    GameMessage m;
    m.sender = o.id;
    if (isCutlery(p.picture)) {
        m.type = 4;
        m.tag = t->messageTag;
    } else {
        m.type = 0;
        m.tag = o.tag;
    }
    sendMessage(m, true);
    t->state = 0x20;
    t->stopMs = kStopTimeMs;
    t->anim.setAnim(t->anim.frameCount() - 1, -1, SpriteAnim::Hold, 200);
    o.state = ObjectState(t->state);
    o.anim = t->anim;
    // (The projectiles are updated once more at once, with no time: the hit one starts its
    // stay, the ones before it move a tick's way.)
    updateProjectiles(0);
    projectiles_.slot(projectile).timerMs = float(t->stopMs);
    // In a boss level the other sphere hears of it.
    if (level_.info().mode == "boss") {
        GameMessage other;
        other.type = m.type;
        other.sender = o.id;
        other.targetType = kRailTarget;
        sendMessage(other, true);
    }
}

void Session::projectileHitsMouse(int projectile) {
    // Player::touchProjectile @0x1244a4 (1.1.1): only a walking or swimming mouse is hit. Cutlery
    // (pictures 3..5) and shurikens (0 and 2) are gone at once and the mouse falls to them:
    // state 0xB (Player::setState: it stops and is killed; the level-over wait becomes 1300 ms,
    // the normal modes set their own 1000 after, the boss modes keep it, run lb4_win) with its
    // cutlery or shuriken death picture; a worn teacup stops a shuriken. (The cheats'
    // invincibility and the other pictures' effects are not reconstructed.)
    if (mouse_.state != 0 && mouse_.state != 4) return;
    const int picture = projectiles_.slots()[std::size_t(projectile)].picture;
    if (isShuriken(picture) && mouse_.cup >= 0) return;
    if (isShuriken(picture) || isCutlery(picture)) {
        projectiles_.destroy(projectile);
        mouse_.velocity = {};
        killMouse(mouse_.position);
        mouse_.state = 0xB;  // (1.0.5: 10)
        progress_.resultsDelayMs = 1300;
        const int sequence = isCutlery(picture) ? Mouse::kCutleryDeath : Mouse::kShurikenDeath;
        mouse_.sprite.playSequence(mouse_.sequenceIds[sequence], 0, 0);
        return;
    }
    killMouse(mouse_.position);
    particles_.add(kParticleHit, mouse_.floor, mouse_.position);
}

void Session::updateProjectiles(int dtMs) {
    Projectiles::Scene scene;
    scene.shownFloor = shownFloor_;
    scene.viewLeft = viewLeft_;
    scene.objectPosition = [this](int id) -> const Vec2* {
        const LevelObject* o = objectById(id);
        return o ? &o->position : nullptr;
    };
    scene.random = &random_;
    scene.sounds = &soundEvents_;
    projectiles_.update(dtMs, kTickMs, scene);
}

void Session::blowUpSpitter(Spitter& spitter, LevelObject& o) {
    BossWorld world = bossWorld(o.floor);
    const Vec2 at = spitter.position() + Vec2{0.0f, 9.99999975e-05f};  // (1.1.1 @0x1d47bc)
    const int floor = o.floor;
    const int id = spitter.objectId();
    spitter.blowUp(world, [&] { throwKey(at, floor); });
    // (throwKey may have added an object: look the Spitter's up again.)
    LevelObject& self = objects_[std::size_t(id)];
    self.state = ObjectState(spitter.state());
    self.anim = spitter.body;
}

// Player::drop(false, id) @0x1206f0: the item leaves the trail where it is (a cheese with the
// drop sound while the mouse is in play); it may not be picked up again for 500 ms.
void Session::dropFromTrail(int id) {
    if (mouse_.position.x < 0.0f) return;
    const auto it = std::find(carried_.begin(), carried_.end(), id);
    if (it == carried_.end()) return;
    carried_.erase(it);
    LevelObject& item = objects_[std::size_t(id)];
    item.inTrail = false;
    if (isCheese(item.type) && !mouseCaught_) sound(0x1f);
    if (item.type != kClockworkMouse) {
        droppedItem_ = id;
        dropGuardMs_ = 500;
    }
}

}  // namespace doc::game
