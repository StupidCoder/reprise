// =============================================================================================
// StageObjects.hpp - the placed objects of a course and their recovered behaviours
// =============================================================================================
//
// What the player sees: the things standing in a course (coins, plants, switches, enemies...)
// react to the protagonist. Objects without a recovered behaviour stay where they were placed.
//
// The original creates every placement through a factory table of {class name, creator} pairs at
// 0x48a1cc (208 entries; the placement's UnitConfig.ParameterConfigName selects the entry). Each
// class is an actor whose vtable holds init (+0x08), the generic movement 0x2d6e90 (+0x28: the
// animation, then the nerve keeper 0x2c8700, then control +0x70), the sensor contact handler
// attackSensor (+0x60: called with its own and the other sensor on a contact, e.g. HeyhoRail
// 0x3a71fc, RideDragon 0x144aa0) and receiveMsg (+0x64: the send routines 0x2d934c, 0x2ec064,
// 0x2ec094 call the receiving sensor's host through it). Class map and evidence:
// .analysis/docs/level-pullup.md.
//
// Sensors and messages: every actor has spheres ("sensors") declared in its archive's
// InitSensor.byml (name, radius, type, joint, offsets). A sensor stores its world position (+0x08),
// radius (+0x14), host actor (+0x2c) and kind (+0x04; 0xb for the MapObj sensors of items).
// When two sensors overlap (distance below the radius sum) the engine calls the attackSensor of
// their hosts. The player's handler 0x15838c branches on its own sensor (Foot / Head / Body /
// FlashRange) and, for a MapObj sensor (kind 0xb, 0x2f677c), sends the touch message 0x2d934c
// (message type tag 0x4fde08). An item's receiveMsg accepts it (the coin's family check 0x2f290c)
// and collects itself. As for the course star (Goal.hpp), an overlap at the end of a tick is
// handled on the next one.
// =============================================================================================
#pragma once

#include "game/ActorCollider.hpp"

#include "game/AnimFrame.hpp"
#include "game/VecLib.hpp"

#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace captain_toad::RecoveredStage {

using captain_toad::RecoveredVec::M34;
using captain_toad::RecoveredVec::V3;

// A sensor sphere declared in data: centre = host translation + up * y (the joint and the X/Z
// offsets of the declarations used so far are zero or the root joint).
struct SensorDecl {
    std::string name, type;
    std::string joint;   // the joint the sensor follows (InitSensor "Joint"; empty: the root)
    float radius = 0;
    float y = 0;
    float z = 0;   // touch points: offset along the object's front
    float x = 0;
    // A joint sensor on an unanimated model: the joint's rest matrix relative to the model root
    // (exporter "joint_rest"); the sensor sits at world . jointRest . (x, y, z).
    bool hasJointRest = false;
    M34 jointRest{};
};

// The protagonist's contact sensors: offsets along his up and radii from the table 0x4f6998
// (filled by the static constructor ending at 0x44b098; entries of {0, up, 0, radius}), which
// 0x2da968 applies to the sensors: the first three entries for Super, the three 0x30 bytes further
// on for Mini (its second argument). The names, defaults and flags come from the sensor table
// 0x4f687c (Hold r100, Foot, Head, Body, Eye r700, FlashRange r400). The Super values agree with the
// sensor objects in memory (run front-input-a checkpoint).
struct PlayerSensor {
    const char* name;
    float up, radius;
    int id;   // the sensor's id field (+0x44) the attackSensor 0x15838c branches on
};
inline constexpr PlayerSensor kPlayerSensors[] = {
    {"Foot", 22.5f, 29.0f, 0x23},
    {"Body", 42.0f, 32.0f, 0x05},
    {"Head", 125.0f, 55.0f, 0x0b},
};
inline constexpr PlayerSensor kPlayerSensorsMini[] = {
    {"Foot", 15.0f, 20.0f, 0x23},
    {"Body", 28.0f, 22.0f, 0x05},
    {"Head", 82.0f, 45.0f, 0x0b},
};
// The set of the player's power form (0 Super, 1 Mini).
inline const PlayerSensor (&playerSensors(bool mini))[3] {
    return mini ? kPlayerSensorsMini : kPlayerSensors;
}

// A link of a placement to another placement (stage switches, move targets, rail points...).
struct LinkTarget {
    std::string id, unit;
    M34 placement{};
    std::vector<V3> points;   // rails: the rail points
    bool closed = false;
    V3 translation() const { return {placement.m[3], placement.m[7], placement.m[11]}; }
};

// One placed object: its identity in the stage data, its placement and current world matrix, and
// the state the session needs to present it.
struct Object {
    std::string id, cls, unit, model;
    M34 placement{};
    M34 world{};
    std::vector<M34> draws;   // where the model is drawn when not just once at `world` (e.g. coin stacks)
    std::map<std::string, double> numbers;        // numeric placement parameters (e.g. CoinStackNum)
    std::map<std::string, std::string> strings;   // string placement parameters (e.g. ItemType)
    std::vector<SensorDecl> sensors;
    std::vector<SensorDecl> screenPoints;   // touch points (InitScreenPoint.byml)
    std::string effectUser, soundUser;      // EffectYaml / SeDataBase entries (InitEffect, InitAudio)
    std::string reactionTable;              // hit reaction table key (default: the model)
    std::map<std::string, std::vector<LinkTarget>> links;
    // The world matrix of one of the model's joints in the pose of `posed` (session; empty: none).
    std::function<bool(const std::string&, M34&)> jointMatrix;
    // The pose the joints have during a tick: the clip, frame and placement at the end of the
    // previous one. The joint matrices are computed after the objects' movement, so a sensor on a
    // joint reads last tick's pose (traced: the pickaxe item's Body sensor follows its PopUp clip
    // one tick behind, route pullup-pickaxe-a).
    struct Pose {
        bool valid = false;
        std::string clip;
        float frame = 0;
        M34 world{};
    } posed;
    // A sensor's centre on its joint (the joint's matrix applied to (0, y, z)); false without one.
    bool jointSensorCentre(const SensorDecl& s, V3& out) const {
        M34 m;
        if (s.joint.empty() || !jointMatrix || !jointMatrix(s.joint, m)) {
            return false;
        }
        out = {m.m[1] * s.y + m.m[2] * s.z + m.m[3], m.m[5] * s.y + m.m[6] * s.z + m.m[7],
               m.m[9] * s.y + m.m[10] * s.z + m.m[11]};
        return true;
    }
    const LinkTarget* link(const std::string& name) const {
        auto it = links.find(name);
        return it == links.end() || it->second.empty() ? nullptr : &it->second.front();
    }
    // Skeletal animation (animated models only): the clip player of the generic movement and the
    // archive's clips (name -> length in 60 Hz frames, looping).
    captain_toad::RecoveredAnimFrame::Skeletal anim;
    std::map<std::string, std::pair<float, bool>> clips;
    bool hasClip(const std::string& name) const { return clips.count(name) > 0; }
    // Material animations: colour constants (".cmcla") and texture transforms (".cmtsa") of the
    // archive, by file name -> (length, looping), each kind with its own player (the original's
    // separate animation players, started together by startAction 0x2fce24).
    struct MaterialPlayer {
        std::string file;   // empty: none (the materials show their rest values)
        float frame = 0, end = 1;
        bool loop = false, playing = false, fresh = false;
        void start(const std::string& f, float length, bool looping) {
            file = f;
            frame = 0;
            end = length;
            loop = looping;
            playing = true;
            fresh = true;   // the start tick shows frame 0 (as the skeletal clips)
        }
        void update() {
            if (fresh || !playing) {
                fresh = false;
                return;
            }
            frame += 2.0f;   // one 30 Hz tick of a 60 Hz animation
            if (frame >= end) {
                if (loop && end > 0) {
                    frame = std::fmod(frame, end);
                } else {
                    frame = end;
                    playing = false;
                }
            }
        }
    };
    std::map<std::string, std::pair<float, bool>> materialClips;
    MaterialPlayer colorAnim, uvAnim, patternAnim;
    // Standalone visibility animations (".vis" here; the skeletal clips carry their own) and their
    // player.
    std::map<std::string, std::pair<float, bool>> visibilityClips;
    MaterialPlayer visAnim;
    // startAction: start the clips of that name the archive has (unknown names are ignored, as
    // the original does for actions an archive lacks).
    // The current action as the effect and sound keepers see it (also for actors without that
    // clip, e.g. a map part's named nerves) and its age in ticks.
    std::string action;
    int actionAge = -1;
    unsigned actionStarts = 0;   // counts startAction calls (observers compare it to catch every start)
    // Actors with named nerves (init through 0x2cdb24): their action keeper (update 0x24cd6c)
    // takes the frame from the nerve, step - 1 after the nerve executor (0x2c8700) ran, in steps
    // of 2 (literal 0x24ceb8), and skips ticks with a pending nerve (step < 0).
    bool nerveDriven = false;
    int nerveStep = -1;
    void startAction(const std::string& name) {
        action = name;
        actionAge = 0;
        actionStarts++;
        auto it = clips.find(name);
        if (it != clips.end()) {
            anim.start(name, it->second.first, it->second.second);
        }
        startMaterial(colorAnim, name + ".cmcla");
        startMaterial(uvAnim, name + ".cmtsa");
        startMaterial(patternAnim, name + ".cmtpa");
        auto v = visibilityClips.find(name);
        if (v != visibilityClips.end()) {
            visAnim.start(name, v->second.first, v->second.second);
        }
    }
    // 0x2c6b84 + 0x2de2bc: a visibility animation held at a frame (e.g. "Glasses" at 1 with
    // IsPutGlass).
    void setVisibilityFrame(const std::string& name, float frame) {
        auto v = visibilityClips.find(name);
        if (v != visibilityClips.end()) {
            visAnim.start(name, v->second.first, false);
            visAnim.frame = frame;
            visAnim.playing = false;
        }
    }
    void startMaterial(MaterialPlayer& player, const std::string& file) {
        auto it = materialClips.find(file);
        if (it != materialClips.end()) {
            player.start(file, it->second.first, it->second.second);
        }
    }
    // 0x2fb538: a colour animation held at a frame (e.g. "Color" at the NPC's KinopioColor);
    // 0x2d3558: the same for a texture pattern.
    void setColorFrame(const std::string& name, float frame) { holdMaterial(colorAnim, name + ".cmcla", frame); }
    void setPatternFrame(const std::string& name, float frame) { holdMaterial(patternAnim, name + ".cmtpa", frame); }
    void holdMaterial(MaterialPlayer& player, const std::string& file, float frame) {
        auto it = materialClips.find(file);
        if (it != materialClips.end()) {
            player.start(it->first, it->second.first, false);
            player.frame = frame;
            player.playing = false;
        }
    }
    bool isActionEnd() const { return !anim.running() || anim.ended(); }
    // Objects with several collision parts (exporter "slot"): each part's matrix and whether it
    // collides (e.g. a conveyor's blocks; empty: the parts follow `world`).
    std::vector<M34> partWorlds;
    std::vector<bool> partShown;
    float alpha = 1.0f;   // drawn with this opacity (a fading object; 1: as its material says)
    bool alive = true;    // a dead object is neither drawn nor collided with
    bool visible = true;
    bool collision = true;

    V3 translation() const { return {placement.m[3], placement.m[7], placement.m[11]}; }
    V3 up() const {
        V3 u{placement.m[1], placement.m[5], placement.m[9]};
        captain_toad::RecoveredVec::normalize(u);
        return u;
    }
    std::string string(const std::string& key, const std::string& fallback) const {
        auto it = strings.find(key);
        return it == strings.end() ? fallback : it->second;
    }
    double number(const std::string& key, double fallback) const {
        auto it = numbers.find(key);
        return it == numbers.end() ? fallback : it->second;
    }
    const SensorDecl* sensor(const std::string& name) const {
        for (const auto& s : sensors) {
            if (s.name == name) {
                return &s;
            }
        }
        return nullptr;
    }
};

// An area of the course (AreaCubeBase: its matrix maps the unit box x, z in [-500, 500], y in
// [0, 1000]) with its parameters and stage-switch links (link name -> switch ids).
struct Area {
    std::string id, kind;
    M34 matrix{};
    std::map<std::string, double> numbers;
    std::map<std::string, std::vector<std::string>> links;
    bool contains(const V3& p) const {
        const V3 d{p.x - matrix.m[3], p.y - matrix.m[7], p.z - matrix.m[11]};
        float local[3];
        for (int k = 0; k < 3; k++) {   // the box axes are the matrix columns (orthogonal, scaled)
            const V3 axis{matrix.m[k], matrix.m[4 + k], matrix.m[8 + k]};
            local[k] = captain_toad::RecoveredVec::dot(d, axis) / captain_toad::RecoveredVec::dot(axis, axis);
        }
        return std::fabs(local[0]) <= 500 && local[1] >= 0 && local[1] <= 1000 && std::fabs(local[2]) <= 500;
    }
    const std::string* link(const std::string& name) const {
        auto it = links.find(name);
        return it == links.end() || it->second.empty() ? nullptr : &it->second.front();
    }
};

// What objects report back to the course (the HUD and the save data read these).
struct Counters {
    int coins = 0;
    int collectItems = 0;
    std::set<int> collectItemIds;   // CollectItemId of the gems taken (0x335fdc)
    bool treasureTaken = false;     // course flag +0xe set by the gold mushroom treasure (0x1a2158)
};

// A contact sphere of an object this tick; `tag` tells the behaviour which one was touched.
struct Sphere {
    V3 centre;
    float radius;
    int tag;
};

// The player as the objects see him this tick.
struct PlayerView {
    V3 pos, up;
    V3 front{0, 0, 1};
    V3 vel{0, 0, 0};     // the movement of the last tick (ladder grab direction tests)
    bool mini = false;   // the Mini form: its own contact sensors (playerSensors)
};

class World;

// A recovered class behaviour. touch() is the receiveMsg path for the player's touch message;
// control() is the class's per-tick update (nerve + control).
struct Behaviour {
    virtual ~Behaviour() = default;
    virtual void control(World&, Object&) {}
    // The spheres the player's sensors can touch: by default the MapObj sensors of the object's
    // archive at its placement (tag = declaration index).
    virtual void contactSpheres(const Object& o, std::vector<Sphere>& out) const;
    // The player touched sphere `tag` on the previous tick. Returns true when the message was
    // accepted.
    virtual bool touch(World&, Object&, int /*tag*/) { return false; }
    // The touch screen picked this object (vtable +0x68, World::screenTouch). Returns true when
    // the object reacts.
    virtual bool screenTouch(World&, Object&) { return false; }
    // The touch pointer's messages to a collision touch target (0x18d42c): the tap (0x31af40)
    // and the hold (0x31af10). True: taken.
    virtual bool touchTarget() const { return false; }   // answers the pointer's queries (0x2c0c1c)
    virtual bool assistTouch(World&, Object&) { return false; }
    virtual bool assistHold(World&, Object&) { return false; }
    // An explosion (0x2ec0d4, BlockPow) reached one of the object's sensors.
    virtual bool explosion(World&, Object&) { return false; }
    // Pushes the player out of its Body sensor (the push message, type tag 0x4fdb7c, as the
    // opening's companion): e.g. the brigade NPCs (attackSensor 0x1a3fd0 -> 0x2f401c).
    virtual bool pushesPlayer(const Object&) const { return false; }
    // The sensor that pushes the player (the NPCs' Body; the opening door's Push).
    virtual const char* pushSensor() const { return "Body"; }
    // The step-on message (type 0x4fe37c, sent by 0x338f14: a ladder's PlayerDummy reached the
    // object's enemy body from above). Returns true when accepted.
    virtual bool stepOn(World&, Object&) { return false; }
    // A thrown object's attack (0x2efdb0, message type 0x4fdd3c, tested by 0x2f2984) reached one
    // of the object's sensors; `from` is the thrown object's position. Returns true when accepted.
    // `direct`: an attack of the player himself (the invincible touch / pickaxe) rather than a
    // thrown object.
    virtual bool thrownHit(World&, Object&, const V3& /*from*/, bool /*direct*/ = false) { return false; }
    // The pickaxe's attack (0x3381ac, message 0x4d6e50, tested by 0x2fc6d0) reached one of the
    // object's MapObj sensors; `at` is the pickaxe sensor; `attackerMini`: the player is Mini.
    virtual bool pickaxeHit(World&, Object&, const Sphere& /*at*/, bool /*attackerMini*/) { return false; }

    // Pulling (the pull-out keeper protocol, StagePullOut.hpp): can the player's Hold sensor take
    // this object; the request that reserves it (0x2e2418 family); the pull itself (0x33743c);
    // releasing a reservation (0x2dc2f0).
    enum class PullReply { None, Success, Quick, Carry, Miss };
    virtual bool pullable(const Object&) const { return false; }
    virtual bool pullRequest(World&, Object&) { return false; }
    virtual PullReply pull(World&, Object&) { return PullReply::None; }
    virtual void pullCancel(World&, Object&) {}
};

class World {
public:
    std::vector<Object> objects;
    std::vector<std::unique_ptr<Behaviour>> behaviours;   // parallel to objects (null: none)
    Counters counters;
    // Stage switches (StageSwitch link targets, by id): on once an object turns them on.
    std::set<std::string> switchesOn;
    bool switchOn(const Object& o, const std::string& linkName) const {
        const LinkTarget* t = o.link(linkName);
        return t && switchesOn.count(t->id) > 0;
    }
    void turnOn(const Object& o, const std::string& linkName) {
        if (const LinkTarget* t = o.link(linkName)) {
            setSwitch(t->id, true, o.id);
        }
    }
    void setSwitch(const std::string& id, bool on, const std::string& by) {
        const bool changed = on ? switchesOn.insert(id).second : switchesOn.erase(id) > 0;
        if (changed && getenv("DIORAMA_DEBUG")) {
            fprintf(stderr, "stage switch %s %s (by %s, tick %ld)\n", id.c_str(), on ? "on" : "off", by.c_str(),
                    tickCount);
        }
    }
    bool switchOnId(const std::string& id) const { return switchesOn.count(id) > 0; }

    // The course's areas (scene.json stage_areas).
    std::vector<Area> areas;
    // The switch areas (the scene's switch-area director, created by 0x256fdc for the area groups
    // SwitchOnArea and SwitchKeepOnArea): an area whose SwitchKill switch is on no longer acts;
    // a SwitchOnArea turns its SwitchAreaOn switch on when the player is inside (it stays on);
    // a SwitchKeepOnArea holds its switch on while the player is inside and turns it off when he
    // leaves. The director's update (its vtable is filled at run time) is not read; its timing is
    // fitted to a trace: once per tick after the actors (World::tick), with the player's position
    // at the start of the tick; the switch's listeners see it on the next tick.
    void areaSwitches(const V3& playerPos) {
        for (const Area& a : areas) {
            const bool onArea = a.kind == "SwitchOnArea", keepArea = a.kind == "SwitchKeepOnArea";
            const std::string* target = a.link("SwitchAreaOn");
            if ((!onArea && !keepArea) || !target) {
                continue;
            }
            if (const std::string* kill = a.link("SwitchKill"); kill && switchOnId(*kill)) {
                continue;
            }
            const bool inside = a.contains(playerPos);
            if (inside) {
                setSwitch(*target, true, a.id);
            } else if (keepArea) {
                setSwitch(*target, false, a.id);
            }
        }
    }
    const Area* areaAt(const std::string& kind, const V3& p) const {
        for (const Area& a : areas) {
            if (a.kind == kind && a.contains(p)) {
                return &a;
            }
        }
        return nullptr;
    }
    std::set<std::string> unported;   // classes without a behaviour (logged once)
    bool playerAttacked = false;      // an enemy's attack reached the player this tick (session: damage)
    // The attacking sensor of the thrownHit being delivered (centre, radius; radius 0: unknown).
    Sphere attacker{{0, 0, 0}, 0, 0};
    std::vector<V3> playerPushes;   // pushes enemies gave the player this tick (session: collider push)
    // The player would take an enemy's attack now (session: no damage blink, not invincible, not
    // missing): the reply of his receiveMsg the attacker reads (0x2f0914).
    bool playerDamageable = true;
    bool playerDown = false;   // missing (Miss / DeathAreaFall): sights skip him (0x2d76d0)
    // A binder (ladder) controls the player: his action machine does not run while bound; the
    // session places him and plays the clip each tick (the original's player bind).
    struct PlayerBind {
        bool active = false;
        int binder = -1;
        V3 pos, front;
        std::string clip;
        float frame = 0;
        bool walkOn = false;   // at the release: he walks on (0x335d38) instead of standing
        bool fall = false;     // at the release: he falls (release mode 5, 0x336620)
    } bind;
    bool dashHeld = false;   // the pad's dash button this tick (pad vtable +0x50)
    // The protagonist's headlamp beam this tick (player +0x2d8 position, +0x2e4 direction, +0x2d0
    // the collision-limited length): the FlashRange sensor's cone (0x15a344).
    struct Headlamp {
        bool on = false;
        V3 pos{}, dir{0, 0, 1};
        float length = 1500.0f;
    } headlamp;
    // The player's power state (+0xd0 -> +0x44 -> [0]: 0 Super, 1 Mini) and the power-up requests
    // items make (the session applies them).
    int playerPower = 0;
    int powerUpRequest = -1;                  // a state the player should change to (mushroom: 0)
    bool invincibleRequest = false;           // the pickaxe (0x2dc424)
    bool playerInvincible = false;            // the player's invincible mode (session, before the tick)
    std::vector<size_t> invincibleBodyHits;   // enemies the invincible player's body hit this tick
    // The length of one of the player's clips in frames (binders that wait for a clip's end).
    std::function<float(const std::string&)> playerClipFrames;
    float stickX = 0, stickY = 0;   // the player's converted circle pad this tick
    bool playerOnGround = true;
    bool playerFree = true;   // in a state a binder may take him from (walking / waiting)
    // The placed object whose collision the segment from `from` along `dir` hits first (-1 none).
    std::function<int(const V3& from, const V3& dir)> ray;
    // The deepest contact of a sphere with the course collision: its normal and depth (session).
    // The course's sphere check for actor colliders (ActorCollider.hpp): every contact.
    captain_toad::RecoveredActorCollider::SphereQuery sphereContacts;
    // Does the segment from + dir hit any collision (course or map part)? (collider 0x2dc838)
    std::function<bool(const V3& from, const V3& dir)> segmentHits;
    // The nearest hit of that segment: its face normal and distance along the segment (session).
    std::function<bool(const V3& from, const V3& dir, V3& normal, float& distance)> segmentFirst;
    int indexOf(const Object& o) const { return int(&o - objects.data()); }
    PlayerView player;                          // the player as of the last tick
    V3 cameraEye{};                             // the game camera's position (the scene camera list [0] +8 -> +0x34)
    bool startCameraView = false;               // the current camera is the Start camera (0x2a6a5c "スタート")
    bool guidesBlocked = false;                 // the scene's demo / stop states (hints stay hidden)
    std::map<std::string, int> actor2dFrames;   // GUIDE_BALLOON sequence lengths (scene "actor2d")
    int researchFrame = 0;                      // the research play's frame (logs only)
    long tickCount = 0;                         // object ticks run so far
    std::string stageName;                      // the course's stage (e.g. "PullUpStage")
    // Effect / sound hook of hit reactions; `at` (optional) is where it happens when that is not the
    // object's own position (a coin of a stack).
    // stopScene: the reaction controller's scene stop applies (0x28a0a0); false for reactions the
    // original plays without it (the item generator's coin pop).
    std::function<void(const Object&, const char* reaction, const V3* at, bool stopScene)> hitReaction;

    using Factory = std::function<std::unique_ptr<Behaviour>(Object&)>;
    static std::map<std::string, Factory>& factories() {
        static std::map<std::string, Factory> f;
        return f;
    }

    // Create the behaviours of every object from the class table. The generic model init
    // (0x2fd7fc) first starts the clip named after the model, where the archive has one (e.g. the
    // swaying leaves CommonLeafB; observed for every placed model in run loadB); the class's own
    // nerves start their actions later.
    void create() {
        behaviours.clear();
        for (auto& o : objects) {
            if (o.hasClip(o.model) || o.materialClips.count(o.model + ".cmcla") ||
                o.materialClips.count(o.model + ".cmtsa") || o.visibilityClips.count(o.model)) {
                o.startAction(o.model);
            }
            auto it = factories().find(o.cls);
            if (it != factories().end()) {
                behaviours.push_back(it->second(o));
            } else {
                behaviours.push_back(nullptr);
                if (unported.insert(o.cls).second && getenv("DIORAMA_DEBUG")) {
                    fprintf(stderr, "stage object class %s: no behaviour yet\n", o.cls.c_str());
                }
            }
        }
    }

    // The protagonist's Hold sensor (kind 2, 100 above the translation, radius 100; observed in
    // memory) against the pullable objects' Body sensors after this tick's movement: the overlaps
    // the player's attackSensor collects into its list (+0xe8, 0x1fef14) for the next tick.
    static constexpr float kHoldUp = 100, kHoldRadius = 100;
    std::vector<size_t> holdContacts;

    // One game tick after the player's update: deliver last tick's contacts, run the objects'
    // control, then record this tick's contacts.
    void tick(const PlayerView& playerNow) {
        const V3 tickStartPos = player.pos;
        playerPrevPos = player.pos;
        player = playerNow;
        tickCount++;
        playerAttacked = false;
        playerPushes.clear();
        for (const auto& [index, tag] : pendingTouches_) {
            Object& o = objects[index];
            if (o.alive && behaviours[index]) {
                behaviours[index]->touch(*this, o, tag);
            }
        }
        pendingTouches_.clear();
        // The invincible player's own contact sensors against enemy bodies (0x2d937c, message
        // 0x4fe08c, from his sensor handler 0x15915c / 0x15933c / 0x159498), delivered in the same
        // phase: a pickaxe taken just now (0x2a2ee8 sets the mode at once) already counts (traced:
        // pickup and knock-away both at 1453, route pullup-pickaxe-a).
        invincibleBodyHits.clear();
        if (playerInvincible || invincibleRequest) {
            for (size_t index : pendingBodyContacts_) {
                Object& o = objects[index];
                if (o.alive && behaviours[index] && behaviours[index]->thrownHit(*this, o, player.pos, true)) {
                    invincibleBodyHits.push_back(index);
                }
            }
        }
        pendingBodyContacts_.clear();
        holdContacts.clear();
        // The generic movement 0x2d6e90: animation advance, then the nerve and control, then the
        // skip flag is cleared.
        for (size_t i = 0; i < objects.size(); i++) {
            Object& o = objects[i];
            if (!o.alive) {
                continue;
            }
            o.anim.update();
            if (o.actionAge >= 0) {
                o.actionAge++;
            }
            o.colorAnim.update();
            o.uvAnim.update();
            o.patternAnim.update();
            o.visAnim.update();
            if (behaviours[i]) {
                behaviours[i]->control(*this, o);
            }
            o.anim.endMovement();
        }
        for (size_t i = 0; i < objects.size(); i++) {
            const Object& o = objects[i];
            if (!o.alive || !behaviours[i]) {
                continue;
            }
            if (behaviours[i]->pullable(o)) {
                if (const SensorDecl* body = o.sensor("Body")) {
                    const V3 hold = captain_toad::RecoveredVec::add(
                        player.pos, captain_toad::RecoveredVec::scale(player.up, kHoldUp));
                    const V3 d = captain_toad::RecoveredVec::sub(hold, sensorCentre(o, *body));
                    const float r = kHoldRadius + body->radius;
                    if (captain_toad::RecoveredVec::dot(d, d) < r * r) {
                        holdContacts.push_back(i);
                    }
                }
            }
            spheres_.clear();
            behaviours[i]->contactSpheres(o, spheres_);
            for (const Sphere& s : spheres_) {
                if (playerTouches(player, s.centre, s.radius)) {
                    pendingTouches_.push_back({i, s.tag});
                }
            }
        }
        for (size_t i = 0; i < objects.size(); i++) {   // the player's contact sensors vs enemy bodies
            const Object& o = objects[i];
            if (!o.alive || !behaviours[i]) {
                continue;
            }
            for (const auto& s : o.sensors) {
                if (s.type != "EnemyBody") {
                    continue;
                }
                const auto& m = o.world.m;
                const V3 c{m[3] + m[1] * s.y, m[7] + m[5] * s.y, m[11] + m[9] * s.y};
                if (playerTouches(player, c, s.radius)) {
                    pendingBodyContacts_.push_back(i);
                    break;
                }
            }
        }
        for (Object& o : objects) {   // the joint matrices of this tick (read during the next)
            o.posed = {o.anim.running(), o.anim.name, o.anim.frame, o.world};
        }
        // The switch areas after the actors, with the player's position at the start of the tick
        // (traced, RevolverRuinsStage's bridge: inside the area from frame 101's position, the
        // bridge's Break nerve and the binder's first request at 105).
        if (tickCount > 1) {
            areaSwitches(tickStartPos);
        }
    }
    V3 playerPrevPos{};   // the player's position at the start of this tick (before his move)

    static V3 sensorCentre(const Object& o, const SensorDecl& s) {
        return captain_toad::RecoveredVec::add(o.translation(), captain_toad::RecoveredVec::scale(o.up(), s.y));
    }

    // Does one of the player's contact sensors overlap the sphere? (distance below the radius sum)
    static bool playerTouches(const PlayerView& p, const V3& centre, float radius) {
        for (const auto& ps : playerSensors(p.mini)) {
            const V3 c = captain_toad::RecoveredVec::add(p.pos, captain_toad::RecoveredVec::scale(p.up, ps.up));
            const V3 d = captain_toad::RecoveredVec::sub(c, centre);
            const float r = ps.radius + radius;
            if (captain_toad::RecoveredVec::dot(d, d) < r * r) {
                return true;
            }
        }
        return false;
    }

    // The touch screen (0x18e084): the touched point's ray from the camera against every live
    // object's touch points; the nearest along the ray is the target (objects that refuse the
    // touch query 0x4d611c, i.e. dead or hidden ones here, are skipped). Returns the object's
    // index, or -1.
    int pickScreenPoint(const V3& origin, const V3& dir) const {
        int best = -1;
        float bestT = 1e30f;
        for (size_t i = 0; i < objects.size(); i++) {
            const Object& o = objects[i];
            if (!o.alive || !o.visible) {
                continue;
            }
            for (const SensorDecl& sp : o.screenPoints) {
                const M34& m = o.world;   // centre: the object's matrix applied to (0, y, z)
                const V3 c{m.m[3] + m.m[1] * sp.y + m.m[2] * sp.z, m.m[7] + m.m[5] * sp.y + m.m[6] * sp.z,
                           m.m[11] + m.m[9] * sp.y + m.m[10] * sp.z};
                const V3 oc = captain_toad::RecoveredVec::sub(c, origin);
                const float t = captain_toad::RecoveredVec::dot(oc, dir);   // dir is normalised
                if (t <= 0) {
                    continue;
                }
                const V3 closest = captain_toad::RecoveredVec::sub(oc, captain_toad::RecoveredVec::scale(dir, t));
                const float d2 = captain_toad::RecoveredVec::dot(closest, closest);
                if (d2 > sp.radius * sp.radius) {
                    continue;
                }
                // The hit's distance along the ray (+0x60 of the hit record): where it enters the
                // sphere.
                const float entry = t - std::sqrt(sp.radius * sp.radius - d2);
                if (entry < bestT) {
                    bestT = entry;
                    best = int(i);
                }
            }
        }
        return best;
    }
    // Deliver a touch to the picked object; true when it reacted.
    bool screenTouch(int index) {
        if (index < 0 || !behaviours[size_t(index)]) {
            return false;
        }
        const bool reacted = behaviours[size_t(index)]->screenTouch(*this, objects[size_t(index)]);
        if (getenv("DIORAMA_DEBUG")) {
            fprintf(stderr, "touch %s (%s): %s\n", objects[size_t(index)].id.c_str(),
                    objects[size_t(index)].cls.c_str(), reacted ? "reacts" : "no reaction");
        }
        return reacted;
    }

    // A sound the code plays by its PlayInfo name (the actor's sound keeper, 0x2bdb00).
    // `choice`: the entry of a choice script to play (a PlayInfo fed by a local variable, e.g. the
    // gem's "Got" by the gems already taken; -1: the owner's own choice).
    std::function<void(const Object&, const char* play, int choice)> playSound;
    void sound(const Object& o, const char* play, int choice = -1) {
        if (playSound) {
            playSound(o, play, choice);
        }
    }

    void reaction(const Object& o, const char* name, const V3* at = nullptr, bool stopScene = true) {
        if (hitReaction) {
            hitReaction(o, name, at, stopScene);
        }
    }

private:
    std::vector<std::pair<size_t, int>> pendingTouches_;
    std::vector<size_t> pendingBodyContacts_;
    std::vector<Sphere> spheres_;
};

inline void Behaviour::contactSpheres(const Object& o, std::vector<Sphere>& out) const {
    for (size_t s = 0; s < o.sensors.size(); s++) {
        if (o.sensors[s].type == "MapObj") {
            out.push_back({World::sensorCentre(o, o.sensors[s]), o.sensors[s].radius, int(s)});
        }
    }
}

// IsConnectToCollision (0x2f7040 builds the connector, kept at +0x90): the object rides the map
// part a ray finds below it, keeping its pose in that part's frame (0x2b0454 each update: the
// part's matrix times the stored pose). Each class casts its own ray when it attaches (0x2f73f8):
// e.g. the lever from 50 above its position, 400 down along the world's Y (0x14d830).
struct CollisionConnector {
    int parent = -1;
    bool tried = false;
    M34 local{};
    // Attaches once (with IsConnectToCollision) to the part the ray from..from+ray hits; true
    // when the object is connected.
    bool connect(const World& w, const Object& o, const M34& base, const V3& from, const V3& ray) {
        if (tried) {
            return parent >= 0;
        }
        tried = true;
        if (o.number("IsConnectToCollision", 0) == 0 || !w.ray) {
            return false;
        }
        parent = w.ray(from, ray);
        if (parent >= 0) {
            local = captain_toad::RecoveredVec::mulAffine(
                captain_toad::RecoveredVec::inverseAffine(w.objects[size_t(parent)].world), base);
        }
        return parent >= 0;
    }
    // The coins' and hints' ray (hypothesis: from the base, CollisionCheckDist along -up).
    bool connectBelow(const World& w, const Object& o, const M34& base) {
        const float dist = float(o.number("CollisionCheckDist", 300));
        return connect(w, o, base, {base.m[3], base.m[7], base.m[11]},
                       captain_toad::RecoveredVec::scale(o.up(), -dist));
    }
    // The base pose carried along by the part (unchanged when not connected).
    M34 follow(const World& w, const M34& base) const {
        return parent >= 0 ? captain_toad::RecoveredVec::mulAffine(w.objects[size_t(parent)].world, local) : base;
    }
};

// Registers a behaviour class under its creator-table name at static initialisation.
template <class T>
struct Register {
    explicit Register(const char* cls) {
        World::factories()[cls] = [](Object& o) {
            return std::make_unique<T>(o);
        };
    }
};

}   // namespace captain_toad::RecoveredStage
