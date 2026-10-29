// World 3's boss fight in the session (map 733): setting up the vacuum, updating it with what
// it may pull (the mouse, the first bomb), the bombs riding the conveyor, and the bomb dropper
// (ObjControllerSpawner::receiveMessage @0x1bb474). The vacuum's own rules are in vacuum.cpp.
#include "game/session.hpp"

namespace doc::game {
namespace {

constexpr int kConveyorWall = 0x71;      // the conveyor's line type
constexpr int kConveyorKind = 10;        // its contact kind (MapCollision::collideSphereSpecific)
constexpr float kConveyorSpeed = 0.075f; // units a ms (Object::update @0x1192ce)
constexpr float kBombRadius = 3.0f;

}  // namespace

Vacuum* Session::vacuumFor(int objectId) {
    for (Vacuum& v : vacuums_)
        if (v.objectId() == objectId) return &v;
    return nullptr;
}

void Session::setUpVacuumRoom() {
    for (LevelObject& o : objects_) {
        if (o.type != kVacuum) continue;
        const Vacuum::Sheets sheets{level_.bossSheet("sprites/boss3/boss3_part1.png"),
                                    level_.bossSheet("sprites/boss3/boss3_part0.png"),
                                    level_.bossSheet("sprites/boss3/boss3_part2.png"),
                                    level_.bossSheet("sprites/vacuum_effect.png")};
        vacuums_.emplace_back(o.id, o.floor, o.position, o.size, CatRoute::link(level_.floor(o.floor).map, o.tag), sheets);
        o.anim = vacuums_.back().body;
    }
    // World 3's henchman takes his cry and splash pictures (by the level's world).
    if (vacuums_.empty() || level_.info().world != 2) return;
    for (Cat& cat : cats_)
        if (cat.type() == kFluffy2)
            cat.setHenchmanSheets(level_.bossSheet("sprites/boss3/boss3_cry.png"),
                                  level_.bossSheet("sprites/boss3/boss3_splash.png"));
}

// The vacuum (ObjControllerBoss::update) and the bombs (Object::update @0x119298: one touching
// a conveyor line moves right with it).
void Session::updateVacuumRoomObject(LevelObject& o, int dtMs) {
    if (Vacuum* v = vacuumFor(o.id)) {
        if (!o.updating) return;
        BossWorld world = bossWorld(o.floor);
        VacuumTargets targets;
        targets.mouse = &mouse_.position;
        targets.bomb = firstObjectOfType(kBomb);
        targets.bombTarget = firstObjectOfType(kBombTarget);
        v->update(dtMs, world, targets);
        o.position = v->position();
        o.state = ObjectState(v->state());
        o.anim = v->body;
        return;
    }
    if (o.type != kBomb || !o.updating) return;
    Vec2 found;
    if (wallsOf(o.floor).collideSphereOfType(o.position, kBombRadius, kConveyorWall, found, viewLeft_) == kConveyorKind)
        o.position.x = o.position.x + float(dtMs) * kConveyorSpeed;
}

// ObjControllerSpawner::receiveMessage, message 0: a wall spawner makes an object of the type
// its entity names (+0x198, the entity's tag) two radii out from it, into the room (a left
// spawner to its right, and so on), by SceneGame::addEntity: the first unused object slot, tag
// 0; the picture is loaded after the set-up, so the object stands on that point.
void Session::dropBomb(const LevelObject& dropper) {
    const int type = dropper.tag;
    if (type < 1) return;
    Vec2 at = dropper.position;
    const float out = dropper.radius + dropper.radius;
    switch (dropper.type) {
    case 271: at.y = at.y + out; break;
    case 272: at.x = at.x + dropper.radius * -2.0f; break;
    case 273: at.y = at.y + dropper.radius * -2.0f; break;
    default: at.x = at.x + out; break;
    }
    int slot = 0;
    while (slot < int(objects_.size()) && objects_[std::size_t(slot)].active) ++slot;
    LevelObject made = level_.spawnObject(slot, type, {-99999.0f, -99999.0f}, 0, dropper.floor);
    made.position = at;
    // Object::loadAssets @0x118138: the bomb's two frames loop at 85 ms.
    if (made.type == kBomb) made.anim.setAnim(0, made.anim.frameCount() - 1, SpriteAnim::Loop, 85);
    if (slot == int(objects_.size())) objects_.push_back(made);
    else objects_[std::size_t(slot)] = made;
}

}  // namespace doc::game
