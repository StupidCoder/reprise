// Things placed on a level's map besides walls: cheese, mouse holes, triggers. The original
// keeps them in one array of a large Object class with per-family controllers
// (ObjControllerPickups for cheese, ObjControllerHideable for holes); here each object is a
// plain record and the rules live in the session. Only the families of the first level are
// reconstructed; other types are created inert (see .analysis/docs/level-1-1.md).
#pragma once

#include "game/geometry.hpp"
#include "game/sprite_anim.hpp"

#include <array>
#include <string>
#include <vector>

namespace doc::game {

// Object types (the map vertex type of the entity).
inline bool isCheese(int type) { return type >= 6 && type <= 10; }        // 6 = biggest
// Cat lures on the trail (ObjControllerPickups): a ball of twine and catnip (cats see them,
// canSeeTrail @0x1a1514).
constexpr int kTwine = 0xC, kCatnip = 0x13;
inline bool isHomeHole(int type) { return type >= 0xF0 && type <= 0xF7; }  // level exits
// The ordinary exits; 0xf4..0xf7 are the secret ones (ObjControllerHideable::touchPlayer
// @0x1b4b88 sets LevelState +0x56; Player::hide counts the delivery for 0xf0..0xf3 only).
inline bool isMainHomeHole(int type) { return type >= 0xF0 && type <= 0xF3; }
inline bool isMouseHole(int type) { return type >= 0xF0 && type <= 0xF7; }
// Holes in walls (ObjControllerHideable types 20-23, opening up, left, down, right): the mouse
// hides in one and comes out of any hole with the same tag, the one a new path starts at.
inline bool isWallHole(int type) { return type >= 20 && type <= 23; }
// Pipes (world 3, ObjControllerHideable): openings 200..203 the mouse hides in, and flip-flaps
// 204..207 that spit it out at once where the tag matches (facing down, right, up, left).
inline bool isPipe(int type) { return type >= 200 && type <= 203; }
inline bool isPipeFlap(int type) { return type >= 204 && type <= 207; }
// A wall hole whose tag is 6 modulo 8 is a munch hole (ObjControllerHideable::init @0x1b3c30:
// type + 0x154, 0x168..0x16b, facing up, left, down, right): closed until a message opens it.
inline bool isMunchHole(int type) { return type >= 0x168 && type <= 0x16B; }
inline int placedType(int type, int tag) { return isWallHole(type) && tag % 8 == 6 ? type + 0x154 : type; }
// Cheese crumbs (type 260): eaten on touch, worth more in quick succession.
constexpr int kCrumb = 0x104;
// A bonus heart (type 13, cheese_inf.png frame 0): eaten on touch for 5 points.
constexpr int kBonusHeart = 13;
// What a sprung trap lets go of in the boss chase and fight (game modes 1 and 2): a bonus
// pickup like the hearts, eaten at once (Object::touchPlayer @0x118748; in the other modes a
// cheese of type 10, carried).
constexpr int kTrapPrize = 0x95;
// Points of the eaten bonus pickups (ObjControllerPickups::touchPlayer @0x1b8414): 13 hearts 5,
// 0x61 10, 0x62 15, 0x63 20, 0x95 25.
inline int bonusPoints(int type) {
    switch (type) {
    case 13: return 5;
    case 0x61: return 10;
    case 0x62: return 15;
    case 0x63: return 20;
    case 0x95: return 25;
    default: return 0;
    }
}
// The bonus pickups eaten on touch (hearts, 0x61..0x63, the trap prize), all cheese_inf.png:
// hearts frame 0, 0x61 frame 1, 0x62 frame 2, the others frame 3 (ObjControllerPickups::
// loadAssets @0x1b714c).
inline bool isBonusPickup(int type) { return type == 13 || (type >= 0x61 && type <= 0x63) || type == 0x95; }
inline int bonusFrame(int type) { return type == 0x61 ? 1 : type == 0x62 ? 2 : type == 13 ? 0 : 3; }
// Doors between floors (ObjControllerFloorTransport): up a floor, down a floor. Invisible.
constexpr int kDoorUp = 30, kDoorDown = 31;
// Floor doors to a set floor (ObjControllerFloorTransport::startFloorTransition @0x1b25ac):
// 34 to the floor in its tag, 35 back to the ground floor.
constexpr int kDoorToFloor = 34, kDoorToGround = 35;
inline bool isFloorDoor(int type) { return type == kDoorUp || type == kDoorDown || type == kDoorToFloor || type == kDoorToGround; }
// The clockwork mouse (a decoy carried in the trail like cheese) and the mouse trap holding a
// cheese, which only a thrown clockwork mouse sets off safely.
constexpr int kClockworkMouse = 11, kMouseTrap = 60;
// Speed cheese (cheese_speed.png): eaten on touch, the mouse runs faster for a while.
constexpr int kSpeedCheese = 14;
// The second speed item (speedy.png, 2-S): stronger and shorter (chilli2_speed_mult,
// chilli2_duration).
constexpr int kSpeedy = 209;
// (1.1.1) A dossier (ObjControllerPickups): its tag is the dossier's number; gone once found
// (Profile::hasDossier).
constexpr int kDossier = 431;
inline bool isSpeedItem(int type) { return type == kSpeedCheese || type == kSpeedy; }
// The boss cat of the chase (sprites/bosscat) and the chase's exit door.
constexpr int kBossCat = 0x35, kExitDoor = 359;
// The black ("mean") cat of world 2 (sprites/blackcat).
constexpr int kBlackCat = 0x34;
// The blind cat of world 2 (sprites/blindcat): walking pictures only; it sees nothing.
constexpr int kBlindCat = 0x36;
// The boss fight of world 1 (map 791): the boss droid riding its rail (Accela), Fluffy (the
// white cat, 0x3a; 0x3b a second Fluffy for later worlds), the entrance door that slams behind
// the mouse, the boss exit door, the oil patch and two markers on the droid's dock.
constexpr int kDroid = 301, kFluffy = 0x3a, kFluffy2 = 0x3b;
// World 4's fight: the shuriken launcher on its rail (ObjControllerSpawner, 0x113), the
// reflectors that turn a shuriken by a right angle (0x11a) and the breakable light (0x11b).
constexpr int kLauncher = 0x113, kReflector = 0x11a, kBreakable = 0x11b;
// World 3's fight (map 733): the vacuum, the bombs it is beaten with, the marker at the
// conveyor's end and the bomb dropper on the left wall (ObjControllerSpawner, types 271..274:
// wall spawners facing down, right, up and left; not drawn).
constexpr int kVacuum = 300, kBomb = 253, kBombTarget = 254;
inline bool isWallSpawner(int type) { return type >= 271 && type <= 274; }
// Cats drawn with the boss cat's pictures (bosscat*.png): the boss cat and Fluffy.
inline bool wearsBossCat(int type) { return type == kBossCat || type == kFluffy || type == kFluffy2; }
constexpr int kEntranceDoor = 357, kBossExitDoor = 358, kOilPatch = 372;
// The TV of world 2 (type 0x60, Object::init/update/touchPlayer): off (0x22) until the mouse
// touches it, then on (0x23) and a plaything for cats, which sit at its seats (its route).
constexpr int kTv = 0x60;
// Furniture the mouse hides in (ObjControllerHideable, types 24..26; the couch is 25).
constexpr int kCouch = 25;
// A teacup the mouse can wear (ObjControllerPickups; state 0x1d on the floor).
constexpr int kTeacup = 17;
// The cheese machine (Object::init @0x11aa30: entity types 94 and 95 become type 0x3d holding 2
// and 1 cheeses, 0x3d itself 3): it hands out the cheeses it holds, one for each message 0 or 3
// to its tag (a cheese button, type 150), and shows how many are left.
constexpr int kCheeseMachine = 0x3d;
inline bool isCheeseMachineEntity(int type) { return type == 94 || type == 95 || type == kCheeseMachine; }
inline int cheeseMachineLoad(int type) { return type == 94 ? 2 : type == 95 ? 1 : 3; }
constexpr int kMachineIdle = 0x2c, kMachineDispensing = 0x2b, kHeldInMachine = 0x35;
// A balloon (ObjControllerPickups): carried first in the trail, not counted against its length.
constexpr int kBalloon = 18;
// A worn teacup's flashing (ObjControllerPickups::update @0x1b7b64, Player::render @0x122622):
// its mark is half its whole time (cut to whole ms), a quarter once no more than a quarter is
// left; below the mark it flashes with a period of a tenth of the mark. 1: the period's late
// half (the cup shows), -1: its early half (the bare mouse shows), 0: not flashing (or exactly
// half way through a period).
inline int teacupFlashPhase(int leftMs, int wholeMs) {
    const float half = float(int(float(wholeMs) * 0.5f));
    const float quarter = half * 0.5f;
    const int mark = int(float(leftMs) > quarter ? half : quarter);
    if (leftMs >= mark) return 0;
    const int period = int(float(mark) * 0.100000001f);
    if (period < 1) return 0;
    const int at = leftMs % period;
    return at > period >> 1 ? 1 : at < period >> 1 ? -1 : 0;
}
constexpr int kTvOff = 0x26, kTvOn = 0x27;

// The key the boss droid throws out when it blows up, and the marker it flies to.
constexpr int kKey = 330, kKeyMarker = 333;
// The cats reconstructed so far: the brown cat, the boss cat and Fluffy.
// Floor trapdoors (ObjControllerFloorTransport, types 36..39: house, factory, spooky, manhole).
// They start shut and open on their tag's message 1; the original then treats them as a room
// door (loadAssets turns the type into 0x21).
// Map 711's sliding door (0x140) is set up and loaded the same way (ObjControllerFloorTransport
// ::init @0x1b1b3c, loadAssets: sprites/slidingdoor_map711.png, 72 wide, then type 0x21).
constexpr int kSlidingDoor = 0x140;
inline bool isTrapdoor(int type) { return (type >= 36 && type <= 39) || type == kSlidingDoor; }
// A room door (the original's type 0x21: the house door, and the trapdoors after loadAssets).
constexpr int kHouseDoor = 0x21;
inline bool isRoomDoor(int type) { return type == kHouseDoor || isTrapdoor(type); }

// ObjControllerPickups::loadAssets @0x1b72a6 and SceneGame::load @0x1059ac: in world 3 (the
// profile's world index 2) the teacup is a bucket, unless LevelState +0xc is set (-1 in every
// level so far: its meaning is not known).
inline bool usesBucket(int world) { return world == 2; }

// The ninja cat of world 4 (sprites/ninjacat): it throws shurikens.
constexpr int kNinjaCat = 0x38;
inline bool isPortedCat(int type) {
    return type == 0x32 || type == kBlackCat || type == kBlindCat || type == kBossCat || type == kFluffy ||
           type == kFluffy2 || type == kNinjaCat;
}

enum class ObjectState : int {
    Idle = 0,
    Hidden = 0x23,      // went into a hole (Object::hide @0x112e20)
    HoleClosed = 0x29,  // a home hole that still waits for cheese
    HoleOpen = 0x2a,
};

// The fields the original's level snapshot keeps for each active object
// (Snapshot<Object>::takeSnapshot @0x10b5cc); everything else stays as the reloaded level set it.
struct ObjectSnapshot {
    bool active = false, updating = false, inTrail = false, snappable = false;
    bool introPulse = false;  // (pulsed after the intro already; HYPOTHESIS: kept, run l3h2_win)
    int state = 0;       // +0x1c
    int step = 0;        // +0x1d4
    int tag = 0, floor = 0;
    Vec2 position;       // +0xf4
    float heading = 0.0f;  // +0x108
    int routeIndex = 0;  // +0x16c
    float speed = 0.0f;  // +0x24
    int frame = 0;       // the main picture's frame (+0x9c)
    int radius = 0;      // +0x104, kept as a whole number (Snapshot<Object> @0x10b69e)
    int trimmed = -1;    // +0x208: route points dropped so far (the rollback trims again)
    // Quick saves (state_io.hpp).
    template <class Io>
    void io(Io& io) { io(active, updating, inTrail, snappable, introPulse, state, step, tag, floor, position,
                           heading, routeIndex, speed, frame, radius, trimmed); }
};

struct LevelObject {
    int id = 0;
    int type = 0;
    int tag = 0;    // links the object to walls and messages (map vertex field d)
    int floor = 0;
    ObjectState state = ObjectState::Idle;

    bool active = true;       // takes part in the level
    bool updating = false;    // its controller runs every tick
    bool snappable = false;   // drawn paths snap onto it
    bool passable = false;    // open for the mouse (holes)
    bool snapsToCentre = false;   // paths end at its centre (holes), else at its rim
    bool exit = false;        // a hidden mouse can leave by it (+0x868, Object::getTouched)
    bool inTrail = false;     // carried by the mouse

    Vec2 anchor;              // the map vertex it was placed on
    Vec2 restPosition;        // where a carried item was before it was moved aside
    Vec2 position;            // centre
    Vec2 size;                // sprite frame size
    float radiusFactor = 0.3f;
    float radius = 0.0f;      // collision radius: radiusFactor * |size|

    float pulseMs = 0.0f;     // highlight after a snap or pickup, counts down
    bool introPulse = false;  // cheese: pulsing from the end of the level intro (+0x1d4 = 1)
    int pulseDelayMs = 0;     // cheese: the intro pulse is held until this has run out (+0x1f0)
    int timerMs = -1;         // cheese: sparkle countdown; holes: opening fanfare
    int snapCooldownMs = 0;
    int idleSparkleMs = 0;    // cheese: time since its last shine (drawing)
    SpriteAnim sparkle;       // cheese: the sparkle burst after a pickup

    float rotation = 0.0f;    // a carried clockwork mouse faces what it follows (+0x10c)
    // A thrown clockwork mouse flies in an arc from `arcStart` to `arcTarget` (travelArc).
    Vec2 arcStart, arcTarget;
    int arcTimeMs = 0;
    bool flying = false;      // the key: thrown, on its way to its marker (+0x1d4 = 1)

    // The TV: how long it plays once touched (+0x1d8; 60001 = for good), the time left
    // (+0x1dc), its seats (route points, linked by tag) and the cats sitting at them (+0x828,
    // object ids, 0 = free), and its picture's turn in degrees (+0x108).
    int playMs = 0;
    std::vector<Vec2> seats;
    std::array<int, 16> watchers{};
    float pictureDegrees = 0.0f;

    // Ambient trigger (type 601): plays its map decoration's animation after a delay.
    int triggerTimerMs = 0, triggerDelayMs = 0, triggerSpreadMs = 0, triggerExtraMs = -1;

    std::string text;         // the map entity's text (a rail sphere names its device's tag)
    std::string sprite;       // image under res/, empty if the object is not drawn
    int frameWidth = 0;
    int frames = 1;           // frames in the sheet
    // Its picture's frame animation (Sprite at Object+0x38): holes open and close with it,
    // carried cheese turns to face along the trail.
    SpriteAnim anim;

    // The cheese machine: cheeses still to hand out (+0x204) and the releases due when its
    // picture has run (+0x1d8); a cheese it holds keeps the machine's tag (+0x208, -1 once out).
    int machineCount = 0;
    int machineReleases = -1;
    int heldBy = -1;

    // Twine: rolls (its frame steps every 128 ms) while +0x208 runs, on its clock +0x1dc.
    int rollMs = 0, rollClockMs = 0;
    // Quick saves (state_io.hpp).
    template <class Io>
    void io(Io& io) { io(id, type, tag, floor, state, active, updating, snappable, passable, snapsToCentre,
                           exit, inTrail, anchor, restPosition, position, size, radiusFactor, radius, pulseMs,
                           introPulse, pulseDelayMs, timerMs, snapCooldownMs, idleSparkleMs, sparkle, rotation,
                           arcStart, arcTarget, arcTimeMs, flying, playMs, seats, watchers, pictureDegrees,
                           triggerTimerMs, triggerDelayMs, triggerSpreadMs, triggerExtraMs, text, sprite,
                           frameWidth, frames, anim, machineCount, machineReleases, heldBy, rollMs,
                           rollClockMs); }
};

// Score of one cheese by type (Profile::getCheeseScoreFromType): 50, 25, 20, 15, 10.
int cheesePoints(int type);

// Sprite sheet and frame width the original loads for an object type (Object::loadAssets
// and the controllers' loadAssets); empty for types not reconstructed.
struct SpriteSheet {
    std::string path;
    int frameWidth = 0;
};
// `map` and `floor` pick a room door's picture (loadNextRoomDoorSprite).
SpriteSheet spriteFor(int type, int tag = 0, int world = 0, int map = 0, int floor = 0);

// Creates an object from its map entity. `sheetSize` is the size of its sprite sheet
// (frames are frameWidth x its height); empty for objects without a sprite.
// Object::init @0x11aa30, Object::postInit @0x116e98 and the controllers' init/postInit.
LevelObject makeObject(int id, int type, Vec2 vertex, int tag, SheetSize sheetSize, int world = 0, int map = 0,
                       int floor = 0);

}  // namespace doc::game
