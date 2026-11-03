// Run-time state of a level's placed objects, re-implemented from the game's object code
// (.analysis/docs/objects.md): link resolution, the placement hierarchy and moving joints.
//
// Links (0x552a00, 0x552f00): a Link attribute is a comma-separated UUID list; UUIDs that name no
// object are skipped; a ":1" entry makes the link require all targets to be on instead of any.
// An object is "on" through its class (virtual slot +0xa0); classes not ported yet are never on.
//
// Hierarchy: placements store world transforms; a placement whose Parent is another placement
// keeps its local transform relative to that parent and follows it when the parent moves.
//
// Visibility groups (visibility_group, vtable 0x16e5780, update 0xaf3f40): shown at the start
// unless TriggerAppear is set; shown -> hidden when TriggerDisappear fires, hidden -> shown when
// TriggerAppear fires (a visibility_trigger target must also be in its state 1). Objects listed
// in a hidden group's Targets are not updated (traced for all 312 moving joints of forest_castle;
// the suspending code path is not found yet: the group's own hide routine 0xaf3e70 only acts on
// classes whose slot +0xc8 is true, which moving joints are not).
//
// Trigger boxes (trigger_box and the _for_trap / _for_enemy variants with the same attributes;
// sensor class vtable 0x16d1370, docs/objects.md): a unit cube in the placement's frame, grown
// by DetachScale while active; counts the markers inside by MarkerType (heroes: their origin;
// VR player: the head; Object: the linked object's origin + MarkerOffset), NoArea counts all as
// inside; Capacity rule and state machine 0xc57460 / 0xc57280; "on" = (state 1 or 3) XOR
// Inversion (0x38abe0).
//
// Visibility triggers (visibility_trigger, vtable 0x16d4c40, update 0x9a5890): fire when their
// TriggerOn link fires or, without a link, once the VR player's rig passes their z; event 1 after
// TriggerOnDelay ("on" for one update), back to idle when the link stops firing.
//
// Coins (coin / coin_big, vtable 0x1690278, constructor 0x3d3930, setup 0x3d3c60, state machine
// 0x3d4820): wait for SpawnTrigger + SpawnDelay (hidden), then wait to be picked up: a hero whose
// centre (feet + half its scale up) is closer than CollisionRadius collects it; the next update
// adds the coin's value (1, coin_big: CoinNumBig) and removes it. All coins of a kind spin
// together about their own up axis at AngleSpeed (0x3d9b70 / 0x3d9fb0). Tuning: <Coin> and
// <CoinBig> in multi_platformer.xml.
//
// Moving joints (moving_object_joint, vtable 0x16a4fa8): a locator that plays its own clip
// (attribute Locator -> <level>/anim/<UUID>_locator_anim_play.anim, joint 1 = the locator's
// local pose in its parent). Trigger evaluation 0x59eb90, start delay 0x252610, playback
// 0x252750 (Speed/SpeedReturn, MoveType, TriggerOff, CompletionThreshold), update 0x59e6d0.
#pragma once

#include "data/Animation.hpp"
#include "game/LevelDocument.hpp"
#include "math/Transform.hpp"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace beacon::game {

// What the level's objects sense of the players each step.
struct RuntimeInputs {
    bool hasHero = false;
    math::Vec3 heroFeet;   // the hero's origin (feet)
    // The hero's trigger marker (vtable 0x16d15f8, radius 0): the world position of joint 2 of
    // its animated skeleton, the body joint under the root-motion joint (run trigbox5). Without a
    // pose (replay tools) the feet stand in for it.
    std::optional<math::Vec3> heroMarker;
    bool hasHead = false;
    math::Vec3 head;       // the VR player's head (main+0x48 -> +0x4b0)
    int heroSupport = -1;  // placement the hero stands on (moving collision only), or -1
    bool hasRig = false;
    float rigZ = 0;        // the VR player's rig position along z (main+0x48 -> +0x38)
    math::Vec3 rig;        // the rig's position (the VR player's root)
    float heroScale = 0.22f;   // the hero's model scale (+0xc0)
};

struct LevelLink {
    std::vector<std::size_t> targets;   // object indices
    bool requireAll = false;            // ":1"

    bool empty() const { return targets.empty(); }
};

// Resolves a Link attribute against the level (UUIDs naming no object are skipped, 0x552a00).
LevelLink resolveLink(const Level& level, const LevelObject& o, std::string_view name);

class LevelRuntime {
public:
    LevelRuntime();
    ~LevelRuntime();

    // Resolves links and the hierarchy and loads the moving joints' clips (missing clips leave
    // the joint at its placed pose).
    bool load(const Level& level, const std::filesystem::path& root, std::string* error = nullptr);
    void update(float dt, const RuntimeInputs& in = {});   // the game's 1/60 s step

    std::size_t objectCount() const { return nodes_.size(); }
    const math::Mat4& world(std::size_t object) const { return nodes_[object].world; }
    bool movesWithJoint(std::size_t object) const { return nodes_[object].underJoint; }
    bool isOn(std::size_t object) const;
    // An object that no longer exists (a defeated, removed enemy): trigger boxes no longer count it
    // as a marker (MarkerType Object: an existing object's handle; trigger_box_for_enemy's death
    // checks - NoArea, Inversion, Oneshot - turn on once none of their enemies is left).
    void setGone(std::size_t object) {
        if (object < nodes_.size()) {
            nodes_[object].gone = true;
        }
    }
    // A link "fires" (0x552f00): any target on, or all with ":1"; false when empty.
    bool linkFired(const LevelLink& link) const { return fired(link); }
    bool suspended(std::size_t object) const { return suspended_[object] != 0; }
    // Model and physics switched off by a hidden visibility group (0xaf3e70 -> 0x772130(t, 0, 2)).
    bool hidden(std::size_t object) const { return nodes_[object].hidden; }
    std::size_t movingJointCount() const { return joints_.size(); }
    // A moving joint's start-delay state (0 idle, 1 counting, 2 playing) and clip progress, for
    // comparison with traces of the original (+0x2c50 / +0x2c54); false for other objects.
    bool jointState(std::size_t object, int& state, float& progress) const;
    // A trigger box's sensor state (0 start, 1 on, 2 off, 3 leaving; +0x1cbc); false otherwise.
    bool sensorState(std::size_t object, int& state) const;
    std::size_t triggerBoxCount() const { return boxes_.size(); }

    // Coins: whether the object is drawn (false for coins not spawned yet or collected), the pose
    // it is drawn with (the placement's world, for coins with the shared spin), the coins taken.
    bool visible(std::size_t object) const;
    bool isCoin(std::size_t object) const { return nodes_[object].coin >= 0; }
    math::Mat4 pose(std::size_t object) const;
    int coinsCollected() const { return coinsCollected_; }
    std::size_t coinCount() const { return coins_.size(); }
    // A coin's state (0 waiting to spawn, 1 waiting to be taken, 2 taken, 4 removed); false otherwise.
    bool coinState(std::size_t object, int& state) const;
    // Objects run outside the runtime (lost bots) report their "on" state here (virtual +0xa0).
    void setOn(std::size_t object, bool on) {
        nodes_[object].external = true;
        nodes_[object].externalOn = on;
    }
    // Verification: puts a static object somewhere else (as the harness does to coins it moves).
    void moveObject(std::size_t object, const math::Vec3& at);

private:
    struct Node {
        int parent = -1;    // placed parent, or -1
        math::Mat4 local;   // relative to the parent (or world when there is none)
        math::Mat4 world;
        int joint = -1;     // index into joints_
        int box = -1;       // index into boxes_
        int visTrigger = -1;   // index into visTriggers_
        int coin = -1;      // index into coins_
        bool underJoint = false;
        int depth = 0;
        bool art = false;   // art_bg: a hidden visibility group switches its model and physics off
        bool hidden = false;
        bool external = false, externalOn = false;   // setOn
        bool gone = false;   // setGone: the object no longer exists (a defeated enemy)
    };
    struct MovingJoint;

    struct TriggerBox;
    struct VisibilityTrigger {
        std::size_t object = 0;
        LevelLink on;
        float onDelay = 0, offDelay = 0;
        int state = 0;      // +0x1ca4
        float time = 0;     // +0x1ca8
        int event = 0;      // +0x1cac: 1 switched on this update, 2 switched off
    };
    struct CoinKind {        // <Coin> / <CoinBig>
        float collisionRadius = 0.22f;
        float angleSpeed = 360;   // degrees per second
        int value = 1;            // coin_big: <Coin><CoinNumBig>
        float spin = 0;           // shared angle (radians, -pi..pi)
    };
    struct Coin {
        std::size_t object = 0;
        int kind = 0;             // 0 coin, 1 coin_big (virtual +0x208)
        LevelLink spawn;          // SpawnTrigger
        float spawnDelay = 0;     // SpawnDelay
        float timer = -1;         // +0x2910
        int state = 1;
    };
    struct VisibilityGroup {
        std::size_t object = 0;
        std::vector<std::size_t> targets;
        LevelLink appear, disappear;
        bool shown = true;
    };

    bool fired(const LevelLink& link) const;
    bool groupTriggerFired(const LevelLink& link) const;
    void setGroupShown(VisibilityGroup& g, bool shown);
    void updateJoint(MovingJoint& j, float dt, const RuntimeInputs& in);
    void updateTriggerBox(TriggerBox& b, float dt, const RuntimeInputs& in);
    void loadCoins(const Level& level, const std::filesystem::path& root);
    void updateCoins(float dt, const RuntimeInputs& in);

    std::vector<Node> nodes_;
    std::vector<std::size_t> order_;   // parents before children
    std::vector<std::unique_ptr<MovingJoint>> joints_;
    std::vector<VisibilityGroup> groups_;
    std::vector<std::unique_ptr<TriggerBox>> boxes_;
    std::vector<VisibilityTrigger> visTriggers_;
    std::vector<Coin> coins_;
    CoinKind coinKinds_[2];
    int coinsCollected_ = 0;   // hero +0x44c
    std::vector<char> suspended_;   // per object: listed by a hidden visibility group
};

}   // namespace game
