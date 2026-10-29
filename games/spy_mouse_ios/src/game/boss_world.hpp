// What a boss sees of the level during its update, and how it acts on it: the droid of
// world 1 and the Spitter of world 2 (ObjControllerBoss).
#pragma once

#include "game/cat.hpp"
#include "game/level_geometry.hpp"
#include "game/particles.hpp"
#include "game/random.hpp"
#include "game/sound.hpp"

#include <functional>
#include <string>
#include <vector>

namespace doc::game {

class Projectiles;

// SceneGame::sendMessage's message (GameMessage): its kind, the tag of the walls and objects
// it is for, who sent it (an object id, -1 for the scene) and a type of object (or of wall)
// it is also for.
struct GameMessage {
    int type = 0;
    int tag = -1;
    int sender = -1;
    int targetType = -1;
    int extra = -1;
};

struct BossWorld {
    const LevelGeometry& walls;
    GameRandom& random;
    Particles& particles;
    int gameTimeMs = 0;     // SceneGame +8
    float viewLeft = 0.0f;  // SceneGame +0x13838
    // The mouse (Player +0x1b8), its state (+0x130) and where it entered that state (+0x1fc):
    // a hidden mouse is looked for where it hid.
    Vec2 mousePosition;
    int mouseState = 0;
    Vec2 mouseStateEntry;
    std::vector<SoundEvent>& sounds;  // effects to play, in order (the session's own list)
    // SceneGame::sendMessage(GameMessage, true) for a trigger line touched: the map reacts and
    // the objects it names receive it, at once (the droid may be one of them).
    std::function<void(int type, int tag, int lineType)> sendMessage;
    std::function<void()> killMouse;  // Player::kill
    // What Object::canSeePlayer needs of the mouse.
    CatTarget mouse;
    // SceneGame::sendMessage in full (`applyMap`: the walls with the tag react first).
    std::function<void(const GameMessage&, bool applyMap)> send;
    std::function<void(const std::string&)> fireEvent;  // EventManager::fireEvent
    std::function<void(int tag, bool enabled, int type)> setWalls;  // Map::setEnabled
    Projectiles* projectiles = nullptr;  // SceneGame +0x1165c
    int floor = 0;
};

}  // namespace doc::game
