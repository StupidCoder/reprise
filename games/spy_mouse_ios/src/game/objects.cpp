#include "game/objects.hpp"

#include "game/switches.hpp"

#include <cstdio>
#include <string>

namespace doc::game {

int cheesePoints(int type) {
    static const int points[5] = {50, 25, 20, 15, 10};
    return isCheese(type) ? points[type - 6] : 0;
}

namespace {

// ObjControllerFloorTransport::loadNextRoomDoorSprite @0x1b26dc: the house door's picture by the
// level's map (two of them by floor), the lava trapdoor in worlds 7 and 8 (profile world 6, 7),
// else none (the plain door_house.png).
SpriteSheet roomDoorSprite(int world, int map, int floor) {
    switch (map) {
    case 711: return {"sprites/slidingdoor_map711.png", 73};
    case 727: return {"sprites/door_house_map727.png", 64};
    case 728: return floor == 1 ? SpriteSheet{"sprites/door_house_map728_1.png", 64} : SpriteSheet{"sprites/door_house_map728.png", 32};
    case 729: return floor == 1 ? SpriteSheet{"sprites/slidingdoor_map729_1.png", 55} : SpriteSheet{"sprites/slidingdoor_map729.png", 44};
    case 730: return {"sprites/slidingdoor_map730.png", 30};
    default: break;
    }
    if (world == 6 || world == 7) return {"sprites/trapdoor_lava.png", 40};
    return {};
}

}  // namespace

SpriteSheet spriteFor(int type, int tag, int world, int map, int floor) {
    // ObjControllerSwitch::loadAssets @0x1ae4cc: the button set in the tag's colour, 20 wide.
    // ObjControllerDoors::loadAssets @0x1b06ac: bars by the profile's world (bamboo in world
    // 4, spooky in 6, wood in 7 and 8, else metal) in the tag's colour, 32 wide.
    if (type == 65 || type == 66 || type == 67 || type == 80 || type == 81 || (type >= 0x97 && type <= 0x9a))
        return {std::string("sprites/trigger_set_") + tagColour(tag) + ".png", 20};
    if (type >= 210 && type <= 225) {
        const char* kind = world == 3 ? "bamboo" : world == 5 ? "spooky" : (world == 6 || world == 7) ? "wood" : "metal";
        return {std::string("sprites/bars_") + kind + "_" + tagColour(tag) + ".png", 32};
    }
    if (isCheeseMachineEntity(type)) return {"sprites/cheese_machine.png", 48};  // Object::loadAssets @0x117e02
    if (type == 0x96) {
        // ObjControllerSwitch::loadAssets: the cheese button by tag: cheese, light, bomb, "!".
        const char* kind = tag < 100 ? "cheese" : tag < 200 ? "light" : tag < 300 ? "bomb" : "exclamation";
        return {std::string("sprites/trigger_") + kind + ".png", 20};
    }
    if (type == kSlidingDoor) return {"sprites/slidingdoor_map711.png", 72};
    if (type == kHouseDoor) {
        // ObjControllerFloorTransport::loadAssets @0x1b2038: the map's own door, else the house
        // door, 96 wide.
        const SpriteSheet own = roomDoorSprite(world, map, floor);
        return own.path.empty() ? SpriteSheet{"sprites/door_house.png", 96} : own;
    }
    if (isTrapdoor(type)) {
        // ObjControllerFloorTransport::loadAssets @0x1b2038: 96 wide (the manhole 54).
        static const char* const kinds[4] = {"house", "factory", "spooky", "manhole"};
        return {std::string("sprites/trapdoor_") + kinds[type - 36] + ".png", type == 39 ? 54 : 96};
    }
    if (isCheese(type)) {
        // "sprites/cheese0%d.png" with 11 - type: the biggest cheese (6) uses cheese05.png.
        char path[32];
        std::snprintf(path, sizeof path, "sprites/cheese0%d.png", 11 - type);
        return {path, 24};
    }
    if (isMouseHole(type)) {
        // Facing from the type: 0xf0/0xf4 up, 0xf1/0xf5 left, 0xf3/0xf7 right, else down.
        const int facing = type | 4;
        const char* side = facing == 0xF4 ? "u" : facing == 0xF5 ? "l" : facing == 0xF7 ? "r" : "d";
        return {std::string("sprites/home_mousehole_") + side + ".png", 32};
    }
    if (isWallHole(type) || isMunchHole(type)) {
        // ObjControllerHideable::loadAssets @0x1b3ec4: "sprites/mouse_hole_%s.png", or
        // "sprites/munch_hole_%s.png" (6 frames) for a munch hole.
        static const char* const sides[4] = {"u", "l", "d", "r"};
        if (isMunchHole(type)) return {std::string("sprites/munch_hole_") + sides[type - 0x168] + ".png", 32};
        return {std::string("sprites/mouse_hole_") + sides[type - 20] + ".png", 32};
    }
    if (type == 0x32) return {"sprites/browncat01.png", 32};
    if (type == kBossCat) return {"sprites/bosscat01.png", 32};
    if (type == kBlackCat) return {"sprites/blackcat01.png", 32};
    if (type == kNinjaCat) return {"sprites/ninjacat01.png", 32};
    if (type == kBlindCat) return {"sprites/blindcat01.png", 32};
    if (type == kTv) return {"sprites/tv_l.png", 64};
    if (type == kCouch) return {"sprites/couch.png", 64};  // ObjControllerHideable::loadAssets (7 frames)
    // ObjControllerPickups::loadAssets @0x1b72a6: the teacup, 32 wide, or a bucket, 34 wide.
    if (type == kTeacup) return usesBucket(world) ? SpriteSheet{"sprites/bucket.png", 34} : SpriteSheet{"sprites/teacup.png", 32};
    if (type == kTwine) return {"sprites/twine_ball.png", 32};
    if (type == kCatnip) return {"sprites/catnip.png", 24};  // (its frames loop over a set time)
    if (type == kBalloon) return {"sprites/balloon.png", 24};  // (7 frames: 0-3 float, 4-6 pop)  // Object::loadAssets @0x1178c6: always the left-facing set, turned
    if (type == kExitDoor) return {"sprites/exit_door.png", 48};  // Object::loadAssets
    if (type == kEntranceDoor) return {"sprites/entrance_door.png", 48};  // (9 frames)
    if (type == kBossExitDoor) return {"sprites/exit_door_boss.png", 48};  // (8 frames)
    if (type == kOilPatch) return {"sprites/boss1_oil.png", 90};
    if (type == kKey) return {"sprites/keepsake.png", 32};  // ObjControllerPickups::loadAssets: one frame
    // ObjControllerBoss::loadAssets: the droid's base picture, 64 wide.
    if (type == kDroid) return {"sprites/boss1/boss1_part0.png", 64};
    // World 2's fight: ObjControllerBoss::loadAssets @0x1a9370 and Object::loadAssets.
    // ObjControllerHideable::loadAssets @0x1b3ef0: pipes and their flip-flaps by facing.
    if (type >= 200 && type <= 207) {
        static const char* const kFacing[4] = {"d", "r", "u", "l"};
        return {std::string(type < 204 ? "sprites/pipe_" : "sprites/pipe_flipflap_") + kFacing[(type - 200) % 4] + ".png", 32};
    }
    if (type == 302) return {"sprites/boss2/boss2.png", 64};
    // World 3's fight: ObjControllerBoss::loadAssets @0x1a91d0 (the body), Object::loadAssets
    // @0x1176a8 (the bomb, looping at 85 ms) and ObjControllerSpawner::loadAssets @0x1ba794 (a
    // wall spawner takes the flip-flap's picture of its facing).
    if (type == kVacuum) return {"sprites/boss3/boss3_part1.png", 64};
    if (type == kBomb) return {"sprites/bomb.png", 28};
    if (isWallSpawner(type)) {
        static const char* const kFacing[4] = {"d", "r", "u", "l"};
        return {std::string("sprites/pipe_flipflap_") + kFacing[type - 271] + ".png", 32};
    }
    // World 4's fight: ObjControllerSpawner::loadAssets (the shuriken launcher, tag 0x66, the
    // whole picture one frame; any other tag the turret), Object::loadAssets @0x1176a8 (the
    // reflectors by tag, 32 wide, none for tag 1; the breakable light, 40 wide) and
    // ObjControllerSwitch::loadAssets @0x1ae4cc (the buttons by tag, 20 wide).
    if (type == kLauncher) return tag == 0x66 ? SpriteSheet{"sprites/boss4/shuriken_launcher.png", -1} : SpriteSheet{"sprites/boss4_turret.png", -1};
    if (type == kReflector) return tag == 1 ? SpriteSheet{} : SpriteSheet{"sprites/boss4/reflector_" + std::to_string(tag) + ".png", 32};
    if (type == kBreakable) return {"sprites/boss4/breakable.png", 40};
    if (type == 0x9b) return {"sprites/boss4/trigger_" + std::to_string(tag) + ".png", 20};
    if (type == 366) return {"sprites/target_moving.png", 48};  // (1.1.1 @0x117bbe; 1.0.5 32)
    if (type == 367) return {"sprites/emitter_boss2.png", 32};
    if (type == kFluffy) return {"sprites/bosscat01.png", 32};
    // ObjControllerPickups::loadAssets: "sprites/cwmouse" (an image set by facing, 32 wide);
    // Object::loadAssets: the trap and its cheese mask.
    if (type == kClockworkMouse) return {"sprites/cwmouse01.png", 32};
    if (type == kMouseTrap) return {"sprites/mouse_trap.png", 32};
    if (type == kSpeedCheese) return {"sprites/cheese_speed.png", 22};  // ObjControllerCat::loadAssets
    if (type == kSpeedy) return {"sprites/speedy.png", 22};  // ObjControllerPickups::loadAssets @0x1b7038
    if (type == kCrumb || isBonusPickup(type)) return {"sprites/cheese_inf.png", 16};
    if (type == 410) return {"sprites/car01.png", 93};  // Object::loadAssets: one frame each
    if (type == 411) return {"sprites/car02.png", 93};
    if (type == 412) return {"sprites/car03.png", 42};
    if (type == 420 || type == 421) return {"mapfluff/butterfly_anim.png", 16};  // ObjControllerPickups::loadAssets: frame 0
    if (type == kDossier) return {"sprites/dossier_icon.png", 32};  // loadAssets @0x1b75ea (1.1.1)
    return {};
}

LevelObject makeObject(int id, int type, Vec2 vertex, int tag, SheetSize sheetSize, int world, int map, int floor) {
    LevelObject o;
    o.id = id;
    o.type = type;
    o.tag = tag;
    o.anchor = vertex;
    const SpriteSheet sheet = spriteFor(type, tag, world, map, floor);
    o.sprite = sheet.path;
    // (A frame width of -1, Sprite::setImage's, is the whole picture.)
    o.frameWidth = sheet.frameWidth < 0 ? sheetSize.width : sheet.frameWidth;
    if (!sheet.path.empty() && sheetSize.height > 0) o.size = {float(o.frameWidth), float(sheetSize.height)};

    if (isCheese(type)) {
        // ObjControllerPickups::init: animated, a snap target.
        o.updating = true;
        o.snappable = true;
    } else if (isHomeHole(type)) {
        // ObjControllerHideable::init: closed until enough cheese is carried; smaller
        // collision circle than other objects. (1.1.1 @0x1b3c90: the secret exits, 0xf4..0xf7,
        // run too; 1.0.5 left them still. Run l38_watch.)
        o.updating = true;
        o.snapsToCentre = true;
        o.passable = true;  // observed set at level start (reference run l11_obj)
        o.radiusFactor = 0.2f;
        o.state = ObjectState::HoleClosed;
    } else if (isWallHole(type)) {
        // ObjControllerHideable::init @0x1b3c30: no update of its own, a snap target, open,
        // touch circle 0.2 of the frame, and a way out for a hidden mouse (run l18_holes).
        o.snappable = true;
        o.snapsToCentre = true;
        o.passable = true;
        o.exit = true;
        o.radiusFactor = 0.2f;
    } else if (isMunchHole(type)) {
        // ObjControllerHideable::init: closed (0x25), not a snap target, a way out for a hidden
        // mouse, touch circle 0.4 of the frame (run l24_watch: passable at the boot).
        o.state = ObjectState::HoleClosed;
        o.passable = true;
        o.exit = true;
        o.snapsToCentre = true;  // (+0x868: a path ends in its middle)
        o.radiusFactor = 0.400000006f;
    } else if (type == kExitDoor) {
        // Object::init @0x11aa30: open (0x26), a way through for the mouse, touch circle 0.2.
        o.state = ObjectState::HoleOpen;
        o.passable = true;
        o.exit = true;
        o.radiusFactor = 0.200000003f;
    } else if (isPortedCat(type)) {
        // ObjControllerCat::init @0x19f8e4: the brown cat; collision circle 0.28 of the frame.
        o.updating = true;
        o.radiusFactor = 0.280000001f;
    } else if (isFloorDoor(type)) {
        // ObjControllerFloorTransport::init @0x1b1ac0: not drawn (no size), a fixed touch circle
        // of 5; paths snap onto its middle; passable (seen at the boot, run l17_win).
        o.snappable = true;
        o.snapsToCentre = true;
        o.passable = true;
    } else if (isCheeseMachineEntity(type)) {
        // Object::init @0x11aa30: idle (0x28), running, not a snap target, not passable.
        o.type = kCheeseMachine;
        o.state = ObjectState(kMachineIdle);
        o.machineCount = cheeseMachineLoad(type);
        o.updating = true;
    } else if (isRoomDoor(type)) {
        // ObjControllerFloorTransport::init @0x1b1ac0 (house doors, trapdoors and the sliding
        // door alike): shut (0x25), running, not a snap target,
        // paths end in its middle; passable (seen at the boot, run l35_watch).
        o.state = ObjectState::HoleClosed;
        o.updating = true;
        o.passable = true;
        o.snapsToCentre = true;
    } else if (type == kClockworkMouse) {
        // ObjControllerPickups::init: a snap target with 0.7 of the default touch circle.
        o.snappable = true;
        o.radiusFactor = 0.300000012f * 0.699999988f;
    } else if (type == kMouseTrap) {
        // Object::init @0x11aa30: armed (state 0x19), running, counted as a cheese of the level,
        // touch circle 0.16 of the frame, a 3 s fade once sprung.
        o.updating = true;
        o.snappable = true;
        o.state = ObjectState(0x1d);
        o.timerMs = 3000;
        o.radiusFactor = 0.159999996f;
    } else if (isBonusPickup(type)) {
        // ObjControllerPickups::init: a snap target with the default touch circle (0.3).
        o.snappable = true;
    } else if (type == kCrumb) {
        // ObjControllerPickups::init: a wider touch circle (half the frame's diagonal); paths
        // snap onto it (Object::init's default, kept).
        o.radiusFactor = 0.5f;
        o.snappable = true;
    } else if (type >= 410 && type <= 421) {
        // Object::init: cars and butterflies run their own updates (state 0x1d); cars are snap
        // targets (Object::init's default), butterflies not (1.1.1 @0x11b1f4 clears +6).
        o.updating = true;
        o.snappable = type < 420;
        o.state = ObjectState(0x1d);
    } else if (type == kDossier) {
        // ObjControllerPickups::loadAssets @0x1b75b4 (1.1.1): running, a snap target (Object::
        // init's default), not one of the level's pickups (+0x871); a found one is not there.
        o.updating = true;
        o.snappable = true;
    } else if (type == kEntranceDoor || type == kBossExitDoor) {
        // Object::init @0x11aa30: open (0x26), running, a way through, an exit for a hidden
        // mouse; that flag (+0x868) also makes a path end in its middle (SceneGame::snapPath
        // @0x1015a4, run lbf_win). (A level of type 4 would start them closed.)
        o.state = ObjectState::HoleOpen;
        o.updating = true;
        o.passable = true;
        o.exit = true;
        o.snapsToCentre = true;
    } else if (type == kCouch) {
        // ObjControllerHideable::init @0x1b3d10: a way out for a hidden mouse (+0x868, which
        // also ends paths in its middle), touch circle 0.15 of the frame (its pulse grows it by
        // 0.175, +0x30: drawn in the view); passable and a snap target as Object::init leaves
        // them (run l28_watch).
        o.exit = true;
        o.snapsToCentre = true;
        o.passable = true;
        o.snappable = true;
        o.radiusFactor = 0.150000006f;
    } else if (type == kBalloon) {
        // ObjControllerPickups::init @0x1b6a36: running; a snap target (Object::init's default).
        o.updating = true;
        o.snappable = true;
    } else if (type == kTwine) {
        // ObjControllerPickups::init @0x2987b8: on the floor (state 0x1d), running, a snap
        // target, rolling for its first second (+0x208 = 1000) on a clock that starts at
        // Object::init's 500 (+0x1dc; run l45_watch).
        o.state = ObjectState(0x21);
        o.updating = true;
        o.snappable = true;
        o.rollMs = 1000;
        o.rollClockMs = 500;
    } else if (type == kCatnip) {
        // ObjControllerPickups::init: a snap target, not running (+0x1d8/+0x1dc cleared).
        o.snappable = true;
    } else if (type == kTeacup) {
        // ObjControllerPickups::init @0x1b6a1c: on the floor (state 0x1d), a snap target
        // (Object::init's default); how long it can be worn is the entity's value, else 10 s
        // (+0x1d8, +0x1dc).
        o.state = ObjectState(0x21);
        o.snappable = true;
        o.playMs = tag > 0 ? tag : 10000;
        o.timerMs = o.playMs;
    } else if (type == kTv) {
        // Object::init @0x11aa30: off (0x22) on frame 0, a snap target, playing "for good"
        // once touched (+0x1d8 = 60001, never counted down), no seat taken.
        o.state = ObjectState(kTvOff);
        o.snappable = true;
        o.playMs = 60001;
        o.timerMs = 0;
    } else if (type == kDroid) {
        // ObjControllerBoss::init @0x1a8afa: running, in its cutscene state (0x3c), a touch
        // circle of "mech_size_nudge" (0.2) of its frame.
        o.state = ObjectState(0x40);
        o.updating = true;
        o.radiusFactor = 0.200000003f;
    } else if (isPipe(type)) {
        // ObjControllerHideable::init @0x1b3ca4: a way out for a hidden mouse (+0x868, paths end
        // in its middle), touch circle 0.15 of the frame, pulse growth 0.125, low pass; passable
        // and a snap target as Object::init leaves them (run l32_watch).
        o.exit = true;
        o.snapsToCentre = true;
        o.passable = true;
        o.snappable = true;
        o.radiusFactor = 0.150000006f;
    } else if (isPipeFlap(type)) {
        // ObjControllerHideable::init: a flip-flap runs, is no snap target and not passable.
        o.updating = true;
    } else if (type == 302) {
        // ObjControllerBoss::init @0x1a8b28: running, asleep (0x15) until the intro is over, a
        // touch circle of "spitter_size_nudge" (0.25) of its frame (run lbf2_world2: 23.71).
        o.state = ObjectState(0x19);
        o.updating = true;
        o.radiusFactor = 0.25f;
    } else if (type == kVacuum) {
        // ObjControllerBoss::init @0x1a8b28: running, asleep (0x15) until the intro is over;
        // postInit @0x1a9128 sets its touch circle to 22.
        o.state = ObjectState(0x19);
        o.updating = true;
    } else if (type == kBomb) {
        // Object::init @0x11aa30: running, on the floor (0x1d), not passable, carried by
        // conveyors (+0x1bd), drawn in the high pass, a touch circle of 3.
        o.state = ObjectState(0x21);
        o.updating = true;
    } else if (type == kBombTarget) {
        // Object::init: a marker, not drawn, not running (a snap target by Object::init's
        // default, run lbf3_watch).
        o.snappable = true;
    } else if (isWallSpawner(type)) {
        // ObjControllerSpawner::init @0x1ba22c: running, not passable, no snap target, not drawn
        // (pass mask 0); its touch circle is Object's default 0.3 of the frame.
        o.updating = true;
    } else if (type == kLauncher) {
        // ObjControllerSpawner::init @0x1ba22c: running, not passable; the boss's launcher (tag
        // 0x66) waits (0x34) for the fight, any other starts at 0x2b; 3 s on its clock.
        o.state = ObjectState(tag == 0x66 ? 0x38 : 0x2f);
        o.updating = true;
        o.timerMs = 3000;
    } else if (type == kReflector) {
        // Object::init @0x11aa30: running, no snap target, not passable; turning a throw to its
        // left (0x3d: tags 1, 3, 4) or to its right (0x3e: tags 2, 5; 1.1.1). Its touch circle is 3
        // (postInit @0x116e98), and tag 1, without a picture, stands 16 lower.
        o.state = ObjectState(tag == 2 || tag == 5 ? 0x3e : 0x3d);
        o.updating = true;
        o.snappable = false;
    } else if (type == kBreakable) {
        // Object::init: whole (0x2d in 1.1.1; 1.0.5 0x29), no snap target, not passable.
        o.state = ObjectState(0x2d);
        o.snappable = false;
    } else if (type == 366) {
        // Object::init @0x11aa30: a rail sphere, moving (0x1b), running, touch circle
        // "target_sizenudge" (0.4) of its frame.
        o.state = ObjectState(0x1f);
        o.updating = true;
        o.radiusFactor = 0.400000006f;
    } else if (type == 367) {
        // Object::init: a wall device, dark (0x22), running; no touch circle.
        o.state = ObjectState(0x26);
        o.updating = true;
        o.radiusFactor = 0.0f;
    } else if (type == 332 || type == 333) {
        // Object::init @0x11aa30: markers, not drawn, not running.
        o.radiusFactor = 0.150000006f;
    } else if (isSpeedItem(type)) {
        // ObjControllerPickups::init: a snap target, not running.
        o.snappable = true;
    } else if (type == kKey) {
        // ObjControllerPickups::init: a pickup, a snap target (Object::init's default).
        o.snappable = true;
    } else if (type == 601) {
        o.updating = true;  // ambient sound trigger; its timer is not reconstructed
    }

    // Object::postInit: the sprite stands on its vertex; radius from the frame diagonal.
    const float diagonal = o.size.length();
    o.radius = (diagonal > 0.01f ? o.radiusFactor : 0.01f) * diagonal;
    if (isFloorDoor(type)) o.radius = 5.0f;
    if (type == kVacuum) o.radius = 22.0f;
    if (type == kBomb || type == kReflector) o.radius = 3.0f;
    if (type == 366) o.radius = o.radius * 0.699999988f;  // (1.1.1 Object::postInit @0x116e98, run lbf2_round)
    o.position = {vertex.x, vertex.y + o.size.y * 0.5f};
    if (isMouseHole(type) || isWallHole(type) || isMunchHole(type) || type == kExitDoor) {
        // ObjControllerHideable::postInit @0x1b3e54: holes sit 8 units lower (and the exit door).
        o.position.y = vertex.y + ((o.position.y - vertex.y) + -8.0f);
    }
    if (type == kReflector && tag == 1) o.position.y = o.position.y + 16.0f;
    return o;
}

}  // namespace doc::game
