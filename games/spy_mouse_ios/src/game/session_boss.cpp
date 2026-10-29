// Boss levels: the map's events and their cutscenes (SceneCutscene @0x1c98d4.., its child
// scenes SceneScriptSequence @0x1cc76c and SceneSpeech @0x187694, the script functions of
// ScriptSequence used by world 1).
//
// The scene stack decides what runs in a frame. While a cutscene is on top of the level, a
// frame either belongs to the cutscene itself (it takes its next step and pushes the step's
// scene, or ends; the level does not move), to a script sequence (the level's whole update,
// then the sequence's function) or to a speech (a reduced level update while the bubble shows).
#include "game/session.hpp"

#include <cmath>

namespace doc::game {

LevelObject* Session::firstObjectOfType(int type) {
    for (LevelObject& o : objects_)
        if (o.type == type) return &o;
    return nullptr;
}

// EventManager::fireEvent @0x195ee8: the event's cutscene (SceneCutscene::notify @0x1c8b6c
// pushes the cutscene scene, whose showScene loads the steps).
void Session::fireEvent(const std::string& event) {
    const data::MapEvents* events = level_.events();
    if (!events) return;
    const auto callback = events->callbacks.find(event);
    if (callback == events->callbacks.end()) return;
    const auto cutscene = events->cutscenes.find(callback->second);
    if (cutscene == events->cutscenes.end()) return;
    startCutscene(callback->second, cutscene->second);
}

// SceneCutscene::showScene @0x1c98d4: the steps are queued; the mouse is taken out of the
// player's hands (Player::setState(5): its path is dropped and the HUD retracts) and the
// pause button goes.
void Session::startCutscene(const std::string& name, const std::vector<data::CutsceneStep>& steps) {
    cutscene_ = CutsceneRun{};
    cutscene_.active = true;
    cutscene_.name = name;
    for (const data::CutsceneStep& s : steps) cutscene_.queue.push_back(s);
    // A hidden mouse first comes out of the hole it is in (Object::getTouched).
    if (mouse_.state == 2 && mouse_.hole >= 0) comeOutOf(objects_[std::size_t(mouse_.hole)]);
    mouse_.state = 5;
    path_.truncate(0);  // (Player +0x188: the path's points are forgotten)
    hud_.retractAll(0);
    hud_.pauseHidden = true;
}

// SceneCutscene::hideScene @0x1c95c0: after the boss's intro the level is snapshotted for
// retries; the mouse is the player's again (setState(0) protracts the HUD), the pause button
// returns, and after any cutscene but the win the boss starts (state 0x16).
void Session::endCutscene() {
    const std::string name = cutscene_.name;
    cutscene_ = CutsceneRun{};
    if (name == "boss_intro") takeCheckpoint();  // SnapshotHandler::takeSnapshot
    hud_.pauseHidden = false;
    mouse_.state = 0;
    hud_.protractAll(0);
    // The boss of the profile's current world wakes (the first object of its type: world 1
    // the droid 301, world 2 the Spitter 302; the level's own world stands in for the profile).
    if (name == "win") return;
    if (LevelObject* boss = firstObjectOfType(bossTypeOfWorld(level_.info().world))) {
        if (Droid* droid = droidFor(boss->id)) droid->setState(0x16);
        if (Spitter* spitter = spitterFor(boss->id)) spitter->wake();
        if (Vacuum* vacuum = vacuumFor(boss->id)) vacuum->wake();
        boss->state = ObjectState(0x16);
    }
}

// SceneCutscene::hideScene @0x1c973e and ScriptSequence::blowUpDroid @0x1d463e: the boss type
// of a world (the profile's current world, +0x18), -1 for none.
int Session::bossTypeOfWorld(int world) {
    switch (world) {
    case 0: return kDroid;
    case 1: return kSpitter;
    case 2: return 300;
    case 3: return 0x10a;  // (not in world 4's fight room)
    case 6: return 0x132;
    default: return -1;
    }
}

// SceneCutscene::update @0x1c950c (when its child has gone) and the child scenes' updates.
void Session::cutsceneTick(int dtMs) {
    switch (cutscene_.child) {
    case CutsceneRun::Child::None:
        if (cutscene_.queue.empty()) {
            endCutscene();  // Engine::popScene
            return;
        }
        startCutsceneStep(cutscene_.queue.front());
        cutscene_.queue.pop_front();
        return;
    case CutsceneRun::Child::Script: {
        // SceneScriptSequence::update @0x1cc76c: the level's update, then the function; a
        // level that is over ends the sequence.
        cutscene_.elapsedMs += dtMs;  // (Scene::update, before the function)
        step(dtMs);
        runSequence();
        if (progress_.over || cutscene_.scriptState == 2) {
            cutscene_.scriptState = 2;
            cutscene_.child = CutsceneRun::Child::None;
        } else if (cutscene_.scriptState == 0) {
            cutscene_.scriptState = 1;
        }
        return;
    }
    case CutsceneRun::Child::Speech:
        speechTick(dtMs);
        return;
    }
}

// SceneSpeech::update @0x187694. Under the speech the level only runs its particles (and keeps
// its music going, SceneGame::updateBoss). The bubble moves first (Scene::update); then a tap
// sends the line out, a bubble that has gone shows the next line (in again, sound 0x4c), and
// with no lines left the scene ends once the bubble is gone.
void Session::speechTick(int dtMs) {
    particles_.update(dtMs);
    updateProjectiles(dtMs);
    CutsceneRun& c = cutscene_;
    c.box.update(dtMs);
    if (c.speeches.empty()) {
        if (!c.box.isShowing()) c.child = CutsceneRun::Child::None;
    } else if (c.tapped) {
        c.speeches.pop_front();
        c.box.goOut();
        c.tapped = false;
    } else if (!c.box.isShowing()) {
        c.shown = c.speeches.front();
        c.box.goIn();
        sound(0x4c);
    }
}

// SceneCutscene::addInstruction @0x1c8bd8: a dialogue pushes the speech scene with its lines,
// a sequence the script scene with the named function, a path the script scene walking the
// actor along the linked path.
void Session::startCutsceneStep(const data::CutsceneStep& s) {
    cutscene_.step = s;
    cutscene_.scriptState = 0;
    cutscene_.elapsedMs = 0;
    switch (s.kind) {
    case data::CutsceneStep::Dialogue:
        // SceneSpeech::addDialogue @0x1873c0: the dialogue's lines in order.
        cutscene_.child = CutsceneRun::Child::Speech;
        cutscene_.speeches.clear();
        if (const std::vector<data::Speech>* lines = level_.dialogue(s.name))
            for (const data::Speech& line : *lines) cutscene_.speeches.push_back(line);
        // (SceneSpeech::showScene makes the box and sends it out at once: it lingers a tick, so
        // the first line comes a tick later, run lbf_explore2.)
        cutscene_.box = BounceFrame{};
        cutscene_.box.goOut();
        cutscene_.tapped = false;
        break;
    case data::CutsceneStep::Sequence:
    case data::CutsceneStep::Path:
        cutscene_.child = CutsceneRun::Child::Script;
        if (s.kind == data::CutsceneStep::Path)
            cutscene_.path = CatRoute::link(level_.floor(mouse_.floor).map, s.tag);
        break;
    default:
        // (Letterbox markers: not used by world 1.)
        break;
    }
}

void Session::runSequence() {
    const data::CutsceneStep& s = cutscene_.step;
    if (s.kind == data::CutsceneStep::Path) {
        sequenceWalkPath();
        return;
    }
    if (s.name == "showFluffy") sequenceShowFluffy();
    else if (s.name == "slamEntrance") sequenceSlamEntrance();
    else if (s.name == "wait") sequenceWait();
    else if (s.name == "hideFluffy") sequenceHideFluffy();
    else if (s.name == "closeExit") sequenceCloseExit();
    else if (s.name == "blowUpDroid") sequenceBlowUpDroid();
    else if (s.name == "launchHenchman") sequenceLaunchHenchman();
    else if (s.name == "shockHenchman") sequenceShockHenchman();
    else if (s.name == "turretSwitch") sequenceTurretSwitch();
    else if (s.name == "dunkHenchman2") sequenceDunkHenchman2();
    else cutscene_.scriptState = 2;  // (functions of later worlds)
}

// ScriptSequence::showFluffy @0x1b9f70 (1.0.5): Fluffy steps out (state 0x3c, his pose picture) facing
// the way his world's fight has him.
void Session::sequenceShowFluffy() {
    if (Cat* fluffy = [&]() -> Cat* {
            const LevelObject* o = firstObjectOfType(kFluffy);
            return o ? catFor(o->id) : nullptr;
        }()) {
        // Facing by the profile's world (the level's own here): west, in world 2 south-west
        // (and in world 6 north-west), as get2DAngle's degrees.
        const int world = level_.info().world;
        const Vec2 facing = world == 1 ? Vec2{-1.0f, -1.0f} : world == 5 ? Vec2{-1.0f, 1.0f} : Vec2{-1.0f, 0.0f};
        fluffy->showInCutscene(facing.angleDegrees() * 0.0174532924f);
        if (LevelObject* o = firstObjectOfType(kFluffy)) o->state = ObjectState(fluffy->state());
    }
    cutscene_.scriptState = 2;
}

// ScriptSequence::walkPath @0x1d4088. The mouse takes the path as its drawn path from its
// second point and walks it; the step ends when it has reached the end and stands. Fluffy walks
// it at "fluffy_cutscene_mult" times his speed, his own route set aside meanwhile, and gets his
// old state back at the end.
void Session::sequenceWalkPath() {
    const std::string& actor = cutscene_.step.actor;
    if (cutscene_.scriptState == 0) {
        cutscene_.pathPoint = 0;
        if (actor == "mouse") {
            path_.clear();
            for (const CatRoute::Point& p : cutscene_.path.points) path_.append(p.position);
            path_.resampledEnd = 0;
            path_.smoothedEnd = 0;
            mouse_.pathIndex = 1;
            return;
        }
        if (actor == "fluffy") {
            if (LevelObject* o = firstObjectOfType(kFluffy))
                if (Cat* fluffy = catFor(o->id)) {
                    cutscene_.oldState = fluffy->state();
                    fluffy->startScriptedWalk();
                    o->state = ObjectState(fluffy->state());
                }
            return;
        }
        cutscene_.scriptState = 2;
        return;
    }
    if (actor == "mouse") {
        if (mouse_.pathIndex >= path_.size() && mouse_.velocity.lengthSquared() <= 0.0f) cutscene_.scriptState = 2;
        return;
    }
    LevelObject* o = firstObjectOfType(kFluffy);
    Cat* fluffy = o ? catFor(o->id) : nullptr;
    if (!fluffy) {
        cutscene_.scriptState = 2;
        return;
    }
    CatWorld world = catWorld(o->floor);
    const float speed = fluffy->scriptedWalk(cutscene_.path, cutscene_.pathPoint, world);
    catEffects(world, fluffy->objectId());
    if (!(speed > 0.0f)) {
        fluffy->setState(cutscene_.oldState);
        cutscene_.scriptState = 2;
    }
    o->position = fluffy->position();
    o->state = ObjectState(fluffy->state());
}

// ScriptSequence::slamEntrance @0x1d19f4: the entrance door is told to close (message 2: it
// slams shut, the screen shakes); when its picture has run, the mouse turns towards it, a
// puff (particle 0x28) and a short shake follow.
void Session::sequenceSlamEntrance() {
    LevelObject* door = firstObjectOfType(kEntranceDoor);
    if (!door) {
        cutscene_.scriptState = 2;
        return;
    }
    if (cutscene_.scriptState == 0) {
        sendExitDoorMessage(*door, 2);
        return;
    }
    if (!door->anim.finished()) return;
    const Vec2 d = door->position - mouse_.position;
    mouse_.heading = d.angleDegrees() * 0.0174532924f;
    particles_.add(0x25, mouse_.floor, mouse_.position);
    shakeScreen(150, 0);
    cutscene_.scriptState = 2;
}

// ScriptSequence::wait @0x1d1580: half a second of the level's clock.
void Session::sequenceWait() {
    if (cutscene_.scriptState == 0) {
        cutscene_.timerMs = gameTimeMs_ + 500;
        return;
    }
    if (gameTimeMs_ > cutscene_.timerMs) cutscene_.scriptState = 2;
}

// ScriptSequence::hideFluffy @0x1d0b68: Fluffy leaves play (state 0x12).
void Session::sequenceHideFluffy() {
    if (LevelObject* o = firstObjectOfType(kFluffy))
        if (Cat* fluffy = catFor(o->id)) {
            fluffy->setState(0x12);
            o->state = ObjectState(0x12);
        }
    cutscene_.scriptState = 2;
}

// ScriptSequence::launchHenchman @0x1d577c: the henchman (type 0x3b) is put in the beaten boss
// of the profile's world (the level's here) and flies in an arc (height 1.5, 8 ms a unit) to the
// marker 332, his picture running by the tick and turned along the flight; every 150 ms of the
// step he yells a letter of "ARGH!" (a text particle, by his flight time). Landed: the splash
// picture runs (not shown in world 3) and the step ends.
void Session::sequenceLaunchHenchman() {
    LevelObject* o = firstObjectOfType(kFluffy2);
    Cat* henchman = o ? catFor(o->id) : nullptr;
    if (!henchman) {
        cutscene_.scriptState = 2;
        return;
    }
    if (cutscene_.scriptState == 0) {
        const int world = level_.info().world;
        const int lastFrame = world == 2 ? 7 : 0;
        const LevelObject* boss = firstObjectOfType(world == 2 ? kVacuum : bossTypeOfWorld(world));
        const LevelObject* marker = firstObjectOfType(332);
        if (!boss || !marker) {
            cutscene_.scriptState = 2;
            return;
        }
        cutscene_.arcStart = boss->position;
        cutscene_.arcEnd = marker->position;
        cutscene_.travelMs = 0;
        henchman->launchHenchman(boss->position, lastFrame);
        sound(0x0c);
    } else if (cutscene_.scriptState == 1) {
        henchman->common.update(kTickMs);
        const Vec2 before = henchman->position();
        Vec2 at = before;
        int travelMs = cutscene_.travelMs;
        if (!travelArc(cutscene_.arcStart, cutscene_.arcEnd, at, travelMs, 1.5f, 8.0f)) {
            henchman->flyHenchman(at, (at - before).angleDegrees() + 90.0f);
            cutscene_.travelMs = travelMs;
        } else {
            henchman->landHenchman();
            cutscene_.scriptState = 2;
        }
        if (cutscene_.elapsedMs % 150 <= kTickMs) {
            const char* letter = travelMs < 751 ? "A" : travelMs < 1051 ? "R" : travelMs < 1201 ? "G"
                               : travelMs < 1351 ? "H" : "!";
            particles_.add(0x36, o->floor, henchman->position(), letter, nullptr, 0, Vec2{-1.0f, -1.0f}, true);
        }
    }
    o->position = henchman->position();
    o->state = ObjectState(henchman->state());
}

// ScriptSequence::shockHenchman @0x1d227c: the henchman is shocked (state 0x13, frame 8 of his
// world 3 picture, 1 elsewhere; sound 0x0d); after a second he shakes (frames 8..11, else 2..5).
void Session::sequenceShockHenchman() {
    LevelObject* o = firstObjectOfType(kFluffy2);
    Cat* henchman = o ? catFor(o->id) : nullptr;
    if (!henchman) {
        cutscene_.scriptState = 2;
        return;
    }
    const bool world3 = level_.info().world == 2;
    if (cutscene_.scriptState == 0) {
        henchman->shockHenchman(world3 ? 8 : 1);
        sound(0x0d);
    } else if (cutscene_.scriptState == 1 && cutscene_.elapsedMs > 1000) {
        henchman->shakeHenchman(world3 ? 8 : 2, world3 ? 11 : 5);
        cutscene_.scriptState = 2;
    }
    o->state = ObjectState(henchman->state());
}

// ScriptSequence::closeExit @0x1d2134: the boss exit door closes (its frames back to the first
// at 200 ms, state 0x25); the step ends when its picture has run.
void Session::sequenceCloseExit() {
    LevelObject* door = firstObjectOfType(kBossExitDoor);
    if (!door) {
        cutscene_.scriptState = 2;
        return;
    }
    if (cutscene_.scriptState == 0) {
        door->anim.setAnim(door->anim.frameCount() - 1, 0, SpriteAnim::Once, 200);
        door->state = ObjectState::HoleClosed;
        return;
    }
    if (door->anim.finished()) cutscene_.scriptState = 2;
}

// SnapshotHandler::rollback: the level as the checkpoint left it.
// ScriptSequence::blowUpDroid @0x1d4604: world 1's boss droid explodes and throws out the key.
void Session::sequenceBlowUpDroid() {
    if (LevelObject* o = firstObjectOfType(kSpitter); o && bossTypeOfWorld(level_.info().world) == kSpitter)
        if (Spitter* spitter = spitterFor(o->id)) blowUpSpitter(*spitter, *o);
    if (LevelObject* o = firstObjectOfType(kVacuum); o && bossTypeOfWorld(level_.info().world) == kVacuum)
        if (Vacuum* vacuum = vacuumFor(o->id)) {
            BossWorld world = bossWorld(o->floor);
            const Vec2 at = vacuum->position();
            const int floor = o->floor;
            const int id = vacuum->objectId();
            vacuum->blowUp(world, [&] { throwKey(at, floor); });
            // (throwKey may have added an object: look the vacuum's up again.)
            LevelObject& self = objects_[std::size_t(id)];
            self.state = ObjectState(vacuum->state());
            self.anim = vacuum->body;
        }
    if (LevelObject* o = firstObjectOfType(kDroid))
        if (Droid* droid = droidFor(o->id)) {
            BossWorld world = bossWorld(o->floor);
            const Vec2 at = droid->position();
            const int floor = o->floor;
            droid->blowUp(world, [&] { throwKey(at, floor); });
            // (throwKey may have added an object: look the droid's up again.)
            LevelObject& self = objects_[std::size_t(droid->objectId())];
            self.state = ObjectState(droid->state());
        }
    cutscene_.scriptState = 2;
}

// throwKey @0x19d814: the key (type 330) comes into play where the droid was, running, and
// flies to the level's key marker (ObjControllerPickups::update, step 1).
void Session::throwKey(Vec2 at, int floor) {
    int slot = 0;
    while (slot < int(objects_.size()) && objects_[std::size_t(slot)].active) ++slot;
    // (Its picture is loaded after its set-up: no touch circle, as for the trap's cheese.)
    LevelObject made = level_.spawnObject(slot, kKey, {-99999.0f, -99999.0f}, 0, floor);
    made.position = at;
    made.radius = 0.0f;
    made.updating = true;
    made.flying = true;
    made.arcTimeMs = 0;
    if (slot == int(objects_.size())) objects_.push_back(made);
    else objects_[std::size_t(slot)] = made;
}

// SnapshotHandler::takeSnapshot @0x10b8bc: the fields the original keeps (others, such as
// the cats' awareness timers and pictures or the mouse's path, start afresh on a reload).
void Session::takeCheckpoint() {
    auto c = std::make_shared<Checkpoint>();
    c->floor = mouse_.floor;
    c->clock = progress_.clock;
    c->playTimeMs = progress_.playTimeMs;
    c->delivered = progress_.delivered;
    c->score = progress_.score;
    for (const Wall& w : floorWalls_[std::size_t(mouse_.floor)].walls()) c->wallsEnabled.push_back(w.enabled);
    c->mousePosition = mouse_.position;
    c->mouseState = mouse_.state;
    c->mouseHeading = mouse_.heading;
    c->carried = carried_;
    c->scoreBoost = mouse_.scoreBoost;
    c->chilliMs = mouse_.chilliMs;
    c->chilliTotalMs = mouse_.chilliTotalMs;
    for (const LevelObject& o : objects_) {
        ObjectSnapshot r;
        r.active = o.active;
        if (r.active) {
            r.updating = o.updating;
            r.inTrail = o.inTrail;
            r.introPulse = o.introPulse;
            r.snappable = o.snappable;
            r.state = int(o.state);
            r.tag = o.tag;
            r.floor = o.floor;
            r.position = o.position;
            r.frame = o.anim.frame();
            if (const Cat* cat = catFor(o.id)) {
                r.step = cat->chaseStep();
                r.heading = cat->heading();
                r.routeIndex = cat->routeIndex();
                r.speed = cat->speed();
                r.frame = cat->walk.frame();
            } else if (const Droid* droid = droidFor(o.id)) {
                r.step = droid->step();
                r.heading = droid->heading();
                r.routeIndex = droid->railIndex();
                r.frame = droid->body.frame();
            } else if (const Spitter* spitter = spitterFor(o.id)) {
                r.heading = spitter->heading();
                r.routeIndex = spitter->routeIndex();
                r.speed = 1.0f;
                r.frame = spitter->body.frame();
            } else if (const Vacuum* vacuum = vacuumFor(o.id)) {
                r.heading = vacuum->heading();
                r.routeIndex = vacuum->routeIndex();
                r.frame = vacuum->body.frame();
            } else if (const RailTarget* t = railTargetFor(o.id)) {
                r.heading = t->heading;
                r.routeIndex = t->walk.index;
                r.speed = 0.75f;
                r.frame = t->anim.frame();
            } else if (const BeamEmitter* e = emitterFor(o.id)) {
                r.step = e->step;
                r.frame = e->anim.frame();
            }
        }
        c->objects.push_back(r);
    }
    checkpoint_ = std::move(c);
}

// SnapshotHandler::rollback: the kept fields come back over the reloaded level.
void Session::restoreCheckpoint(const Checkpoint& c) {
    LevelGeometry& walls = floorWalls_[std::size_t(c.floor)];
    for (std::size_t i = 0; i < c.wallsEnabled.size() && i < walls.walls().size(); ++i)
        walls.setWallEnabled(i, c.wallsEnabled[i]);
    progress_.clock = c.clock;
    progress_.delivered = c.delivered;
    progress_.score = c.score;
    mouse_.floor = c.floor;
    shownFloor_ = c.floor;  // (the snapshot's floor is the one shown after the reload)
    mouse_.position = c.mousePosition;
    mouse_.state = c.mouseState;
    mouse_.heading = c.mouseHeading;
    carried_ = c.carried;
    mouse_.scoreBoost = c.scoreBoost;
    mouse_.chilliMs = c.chilliMs;
    mouse_.chilliTotalMs = c.chilliTotalMs;
    for (std::size_t i = 0; i < c.objects.size() && i < objects_.size(); ++i) {
        const ObjectSnapshot& r = c.objects[i];
        LevelObject& o = objects_[i];
        o.active = r.active;
        if (!r.active) continue;
        o.updating = r.updating;
        o.inTrail = r.inTrail;
        o.introPulse = r.introPulse;
        o.snappable = r.snappable;
        o.state = ObjectState(r.state);
        o.tag = r.tag;
        o.floor = r.floor;
        o.position = r.position;
        o.anim.setFrame(r.frame);
        if (Cat* cat = catFor(o.id)) {
            if (level_.info().mode == "boss") {
                cat->restore(r);
            } else {
                // (resetCats runs after the rollback: the cat stays on its start.)
                o.position = cat->position();
                o.state = ObjectState(cat->state());
                o.anim.setFrame(0);
            }
        }
        if (Droid* droid = droidFor(o.id)) droid->restore(r);
        if (Spitter* spitter = spitterFor(o.id)) spitter->restore(r);
        if (Vacuum* vacuum = vacuumFor(o.id)) vacuum->restore(r);
        if (RailTarget* t = railTargetFor(o.id)) {
            t->state = r.state;
            t->updating = r.updating;
            t->position = r.position;
            t->heading = r.heading;
            t->walk.index = r.routeIndex;
            t->anim.setFrame(r.frame);
        }
        if (BeamEmitter* e = emitterFor(o.id)) {
            e->state = r.state;
            e->step = r.step;
            e->updating = r.updating;
            e->anim.setFrame(r.frame);
        }
    }
}

}  // namespace doc::game
