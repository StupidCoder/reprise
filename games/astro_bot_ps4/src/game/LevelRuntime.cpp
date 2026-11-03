#include "game/LevelRuntime.hpp"

#include "data/Bytes.hpp"
#include "data/Xml.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace beacon::game {

namespace {

constexpr float kEpsilon = 1.1920929e-07f;     // 0x1199180
constexpr float kRateChange = 2.0f;            // rate ramps by 2 per second (0x252750: rate - (dt + dt))

enum class TriggerType { None = 0, TriggerObject = 1, RideOn = 2, LookAt = 3, LookAtAndTriggerObject = 4, Parent = 5 };
enum class MoveType { Loop = 0, OneShot = 1, Replay = 2 };
enum class TriggerOff { Return = 0, Stop = 1, Ignore = 2 };

// Enum attributes are stored by name (values from moving_object_joint.lox); numbers are accepted too.
int enumValue(const LevelObject& o, std::string_view name, std::initializer_list<std::pair<const char*, int>> values,
              int fallback) {
    const std::string v = o.text(name);
    for (const auto& [n, i] : values) {
        if (v == n) {
            return i;
        }
    }
    if (!v.empty() && (std::isdigit(static_cast<unsigned char>(v[0])) != 0)) {
        return std::atoi(v.c_str());
    }
    return fallback;
}

}   // namespace

LevelLink resolveLink(const Level& level, const LevelObject& o, std::string_view name) {
    LevelLink link;
    for (const auto& t : o.links(name)) {
        auto it = level.byUuid.find(t.uuid);
        if (it != level.byUuid.end()) {   // UUIDs naming no object are skipped (0x552a00)
            link.targets.push_back(it->second);
        }
    }
    link.requireAll = o.linkRequiresAll(name);
    return link;
}

namespace {
}   // namespace

struct LevelRuntime::MovingJoint {
    std::size_t object = 0;
    std::shared_ptr<const data::AnimationClip> clip;
    std::size_t locatorJoint = 1;
    data::Skeleton skeleton;   // identity base pose for sampling the locator clip

    // Attributes.
    TriggerType triggerType = TriggerType::None;
    MoveType moveType = MoveType::Loop;
    TriggerOff triggerOff = TriggerOff::Ignore;
    float startDelay = 0, completion = 100, speed = 1, speedReturn = 1;
    LevelLink start, stop;

    // Setup (0x59deb0).
    bool ignoreWithoutStop = false;    // no StopTrigger and TriggerOff Ignore (+0x5a8)

    // Update state.
    bool on = false;                   // +0x2d42, what links to this object see
    bool triggeredBefore = false;      // +0x2d41
    int delayState = 0;                // 0 idle, 1 counting the start delay, 2 playing (+0x440)
    float delayTime = 0;               // +0x448
    float progress = 0;                // +0x444, clip time driven by the playback
    float rate = 0;                    // +0x450
    bool completed = false;            // +0x4c0, a cycle ended this update
};

struct LevelRuntime::TriggerBox {
    std::size_t object = 0;
    int triggerType = 0;   // BeInside 0, BeOutside 1, Enter 2, Leave 3
    bool oneshot = true, inversion = false, noArea = false;
    int capacity = 1;
    float detachScale = 1;
    int markerType = 0;    // Heros 0, VrPlayer 1, AllPlayers 2, Object 3 (others not ported)
    bool markerRoot = false;   // MarkerJoint AR_Root: the VR player's root (the rig), not its head
    LevelLink markerObject;
    math::Vec3 markerOffset;
    float markerRadius = 0;

    int state = 0;           // +0xc
    bool detectedBefore = false;   // edge memory for Enter/Leave (+0x78)
};

LevelRuntime::LevelRuntime() = default;
LevelRuntime::~LevelRuntime() = default;

bool LevelRuntime::load(const Level& level, const std::filesystem::path& root, std::string* error) {
    nodes_.assign(level.objects.size(), {});
    joints_.clear();
    order_.clear();
    std::vector<math::Mat4> placed(level.objects.size());
    for (std::size_t i = 0; i < level.objects.size(); ++i) {
        placed[i] = level.objects[i].transform.matrix();
        auto it = level.byUuid.find(level.objects[i].parent);
        if (it != level.byUuid.end() && it->second != i) {
            nodes_[i].parent = int(it->second);
        }
    }
    // Depth (parents first); a cycle would be a broken document, cut it.
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        int d = 0;
        for (int p = nodes_[i].parent; p >= 0 && d <= int(nodes_.size()); p = nodes_[std::size_t(p)].parent) {
            ++d;
        }
        if (d > int(nodes_.size())) {
            nodes_[i].parent = -1;
            d = 0;
        }
        nodes_[i].depth = d;
        // Hypothesis: art_bg answers virtual +0xc8 with true (the arms and banana sign of the
        // ROOFTOPS gorilla moment are hidden in the original until their group appears).
        nodes_[i].art = level.objects[i].className == "art_bg";
        order_.push_back(i);
    }
    std::stable_sort(order_.begin(), order_.end(),
                     [&](std::size_t a, std::size_t b) { return nodes_[a].depth < nodes_[b].depth; });
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        Node& n = nodes_[i];
        n.local = n.parent >= 0 ? math::multiply(math::inverseAffine(placed[std::size_t(n.parent)]), placed[i])
                                : placed[i];
        n.world = placed[i];
    }

    auto resolve = [&](const LevelObject& o, std::string_view name) { return resolveLink(level, o, name); };

    for (std::size_t i = 0; i < level.objects.size(); ++i) {
        const LevelObject& o = level.objects[i];
        if (o.className != "moving_object_joint") {
            continue;
        }
        auto j = std::make_unique<MovingJoint>();
        j->object = i;
        j->triggerType = TriggerType(enumValue(o, "TriggerType",
                                               {{"None", 0}, {"TriggerObject", 1}, {"RideOn", 2}, {"LookAt", 3},
                                                {"LookAtAndTriggerObject", 4}, {"Parent", 5}},
                                               0));
        j->moveType = MoveType(enumValue(o, "MoveType", {{"Loop", 0}, {"OneShot", 1}, {"Replay", 2}}, 0));
        j->triggerOff = TriggerOff(enumValue(o, "TriggerOff", {{"Return", 0}, {"Stop", 1}, {"Ignore", 2}}, 2));
        j->startDelay = o.number("StartDelayTime").value_or(0);
        j->completion = o.number("CompletionThreshold").value_or(100);
        j->speed = o.number("Speed").value_or(1);
        j->speedReturn = o.number("SpeedReturn").value_or(1);
        j->start = resolve(o, "StartTrigger");
        j->stop = resolve(o, "StopTrigger");
        j->ignoreWithoutStop = j->stop.empty() && j->triggerOff == TriggerOff::Ignore;
        if (j->triggerType == TriggerType::None) {
            j->delayState = 1;   // setup starts the delay count (0x59deb0: +0x58a = 1)
        }
        const std::string locator = o.text("Locator");
        if (!locator.empty()) {
            data::Bytes bytes;
            if (data::loadFile(root / (locator + ".anim"), bytes)) {
                if (auto clip = data::loadAnimation(bytes, error)) {
                    j->clip = std::make_shared<const data::AnimationClip>(std::move(*clip));
                }
            }
        }
        if (j->clip) {
            const std::size_t joints = j->clip->joints.size();
            j->locatorJoint = joints > 1 ? 1 : 0;
            j->skeleton.parent.assign(joints, -1);
            j->skeleton.nameHash.assign(joints, 0);
            j->skeleton.basePose.assign(joints, {});
            j->skeleton.scaleCompensate.assign(joints, 0);
            for (std::size_t k = 0; k < joints; ++k) {
                j->skeleton.order.push_back(std::uint16_t(k));
            }
        }
        nodes_[i].joint = int(joints_.size());
        joints_.push_back(std::move(j));
    }
    boxes_.clear();
    for (std::size_t i = 0; i < level.objects.size(); ++i) {
        const LevelObject& o = level.objects[i];
        if (o.className != "trigger_box" && o.className != "trigger_box_for_trap" &&
            o.className != "trigger_box_for_enemy") {
            continue;
        }
        auto b = std::make_unique<TriggerBox>();
        b->object = i;
        b->triggerType = enumValue(o, "TriggerType", {{"BeInside", 0}, {"BeOutside", 1}, {"Enter", 2}, {"Leave", 3}}, 0);
        b->oneshot = o.flag("Oneshot").value_or(true);
        b->inversion = o.flag("Inversion").value_or(false);
        b->noArea = o.flag("NoArea").value_or(false);
        b->capacity = int(o.number("Capacity").value_or(1));
        b->detachScale = o.number("DetachScale").value_or(1);
        b->markerType = enumValue(o, "MarkerType",
                                  {{"Heros", 0}, {"VrPlayer", 1}, {"AllPlayers", 2}, {"Object", 3}, {"Enemies", 4},
                                   {"VrHero", 5}, {"Traps", 6}, {"Breakable", 7}, {"CrewBot", 8}},
                                  0);
        b->markerObject = resolve(o, "MarkerObject");
        b->markerRoot = o.text("MarkerJoint") == "AR_Root";
        b->markerRadius = o.number("MarkerRadius").value_or(0);
        if (auto v = o.text("MarkerOffset"); !v.empty()) {
            float xyz[3] = {0, 0, 0};
            std::sscanf(v.c_str(), "%f %f %f", &xyz[0], &xyz[1], &xyz[2]);
            b->markerOffset = {xyz[0], xyz[1], xyz[2]};
        }
        nodes_[i].box = int(boxes_.size());
        boxes_.push_back(std::move(b));
    }
    visTriggers_.clear();
    for (std::size_t i = 0; i < level.objects.size(); ++i) {
        const LevelObject& o = level.objects[i];
        if (o.className != "visibility_trigger") {
            continue;
        }
        VisibilityTrigger t;
        t.object = i;
        t.on = resolve(o, "TriggerOn");
        t.onDelay = o.number("TriggerOnDelay").value_or(0);
        t.offDelay = o.number("TriggerOffDelay").value_or(0);
        nodes_[i].visTrigger = int(visTriggers_.size());
        visTriggers_.push_back(std::move(t));
    }
    suspended_.assign(level.objects.size(), 0);
    groups_.clear();
    for (std::size_t i = 0; i < level.objects.size(); ++i) {
        const LevelObject& o = level.objects[i];
        if (o.className != "visibility_group") {
            continue;
        }
        VisibilityGroup g;
        g.object = i;
        const std::string targets = o.text("Targets");
        std::size_t at = 0;
        while (at < targets.size()) {
            const std::size_t comma = std::min(targets.find(',', at), targets.size());
            auto it = level.byUuid.find(std::string_view(targets).substr(at, comma - at));
            if (it != level.byUuid.end()) {
                g.targets.push_back(it->second);
            }
            at = comma + 1;
        }
        g.appear = resolve(o, "TriggerAppear");
        g.disappear = resolve(o, "TriggerDisappear");
        groups_.push_back(std::move(g));
    }
    for (auto& g : groups_) {
        if (!g.appear.empty()) {
            setGroupShown(g, false);   // 0xaf3530: hidden at the start
        }
    }
    loadCoins(level, root);
    // Everything below a moving joint moves with it.
    for (std::size_t i : order_) {
        Node& n = nodes_[i];
        n.underJoint = n.joint >= 0 || (n.parent >= 0 && nodes_[std::size_t(n.parent)].underJoint);
    }
    return true;
}

void LevelRuntime::setGroupShown(VisibilityGroup& g, bool shown) {
    g.shown = shown;
    for (std::size_t t : g.targets) {
        suspended_[t] = shown ? 0 : 1;   // one shared flag per object (suspend bit 0x10)
        if (nodes_[t].art) {
            nodes_[t].hidden = !shown;
        }
    }
}

bool LevelRuntime::jointState(std::size_t object, int& state, float& progress) const {
    const Node& n = nodes_[object];
    if (n.joint < 0) {
        return false;
    }
    state = joints_[std::size_t(n.joint)]->delayState;
    progress = joints_[std::size_t(n.joint)]->progress;
    return true;
}

bool LevelRuntime::isOn(std::size_t object) const {
    const Node& n = nodes_[object];
    if (n.external) {
        return n.externalOn;
    }
    if (n.joint >= 0) {
        return joints_[std::size_t(n.joint)]->on;
    }
    if (n.box >= 0) {
        const TriggerBox& b = *boxes_[std::size_t(n.box)];
        return ((b.state | 2) == 3) != b.inversion;
    }
    if (n.visTrigger >= 0) {
        const int e = visTriggers_[std::size_t(n.visTrigger)].event;
        return e == 1 || e == 2;   // 0x9a5a50
    }
    return false;
}

bool LevelRuntime::sensorState(std::size_t object, int& state) const {
    if (nodes_[object].box < 0) {
        return false;
    }
    state = boxes_[std::size_t(nodes_[object].box)]->state;
    return true;
}

// One trigger box step: count the markers inside, apply the Capacity rule (0xc57460), then the
// sensor state machine (0xc57280; on/off delays are 0 for trigger boxes, 0xc542b0).
void LevelRuntime::updateTriggerBox(TriggerBox& b, float dt, const RuntimeInputs& in) {
    (void)dt;
    const bool active = (b.state | 2) == 3;
    const math::Mat4 toBox = math::inverseAffine(nodes_[b.object].world);
    const float half = 0.5f * (active ? b.detachScale : 1.0f);
    auto inside = [&](const math::Vec3& p, float radius) {
        if (b.noArea) {
            return true;
        }
        const float x = toBox[0] * p.x + toBox[4] * p.y + toBox[8] * p.z + toBox[12];
        const float y = toBox[1] * p.x + toBox[5] * p.y + toBox[9] * p.z + toBox[13];
        const float z = toBox[2] * p.x + toBox[6] * p.y + toBox[10] * p.z + toBox[14];
        return std::fabs(x) <= half + radius && std::fabs(y) <= half + radius && std::fabs(z) <= half + radius;
    };
    int count = 0, total = 0;
    if (b.markerType == 0 || b.markerType == 2) {
        if (in.hasHero) {
            ++total;
            count += inside(in.heroMarker.value_or(in.heroFeet), 0) ? 1 : 0;
        }
    }
    if (b.markerType == 1 || b.markerType == 2) {
        ++total;
        // **[hypothesis]** MarkerJoint AR_Root names the VR player avatar's root joint, which
        // stands at the rig (ROOFTOPS' spitter trigger only reaches 1.48 m up).
        if (b.markerRoot && in.hasRig) {
            count += inside(in.rig, 0) ? 1 : 0;
        } else {
            count += in.hasHead && inside(in.head, 0) ? 1 : 0;
        }
    }
    if (b.markerType == 3) {
        for (std::size_t t : b.markerObject.targets) {
            if (nodes_[t].gone) {
                continue;   // **[hypothesis]** a removed object drops out of the marker list
            }
            ++total;
            const math::Mat4& w = nodes_[t].world;
            const math::Vec3 p{w[12] + b.markerOffset.x, w[13] + b.markerOffset.y, w[14] + b.markerOffset.z};
            count += inside(p, b.markerRadius) ? 1 : 0;
        }
    }
    const int counted = (b.triggerType == 1 || b.triggerType == 3) ? total - count : count;
    const bool level = b.capacity >= 0 ? counted >= b.capacity : counted == total + 1 + b.capacity;
    bool detected = level;
    if (b.triggerType >= 2) {   // Enter / Leave: only the rising edge
        detected = level && !b.detectedBefore;
    }
    b.detectedBefore = level;

    switch (b.state) {
    case 0:
    case 2:
        if (b.state == 2 && b.oneshot && b.inversion) {
            break;   // latched off
        }
        b.state = detected ? 1 : 2;   // on delay 0
        break;
    case 1:
        if (b.oneshot && !b.inversion) {
            break;   // latched on
        }
        if (b.oneshot && b.inversion ? !detected : !detected) {
            b.state = 2;   // off delay 0 (Enter/Leave skip the leaving state)
        }
        break;
    case 3:
        b.state = detected ? 1 : 2;
        break;
    }
}

// 0x552f00: any target on, or with ":1" all of them. (An optional distance filter on the targets'
// positions, +0x131/+0x134, is not ported.)
// A visibility group's trigger link (0xaf3f40): fired, and a visibility_trigger target must have
// switched on in this update (event 1).
bool LevelRuntime::groupTriggerFired(const LevelLink& link) const {
    if (!fired(link)) {
        return false;
    }
    const std::size_t first = link.targets.front();
    return nodes_[first].visTrigger < 0 || visTriggers_[std::size_t(nodes_[first].visTrigger)].event == 1;
}

bool LevelRuntime::fired(const LevelLink& link) const {
    if (link.requireAll) {
        return std::all_of(link.targets.begin(), link.targets.end(), [&](std::size_t t) { return isOn(t); });
    }
    return std::any_of(link.targets.begin(), link.targets.end(), [&](std::size_t t) { return isOn(t); });
}

void LevelRuntime::updateJoint(MovingJoint& j, float dt, const RuntimeInputs& in) {
    // Trigger (0x59eb90). A StopTrigger that fires switches the joint off; with a StopTrigger
    // that does not fire, TriggerType None counts as triggered.
    bool triggered = false;
    bool decided = false;
    if (!j.stop.empty()) {
        if (fired(j.stop)) {
            decided = true;
        } else if (j.triggerType == TriggerType::None) {
            triggered = true;
            decided = true;
        }
    }
    if (!decided) {
        switch (j.triggerType) {
        case TriggerType::TriggerObject:
            triggered = fired(j.start);
            break;
        case TriggerType::Parent: {
            const int p = nodes_[j.object].parent;
            triggered = p >= 0 && isOn(std::size_t(p));
            break;
        }
        case TriggerType::RideOn:
            // A hero's support (hero +0x1b0) is one of the nodes listed by the joint's node
            // extension (+0x70 -> +0xa0, read as its children: hypothesis), i.e. the hero stands
            // on a placement parented directly to the joint.
            triggered = in.heroSupport >= 0 &&
                        nodes_[std::size_t(in.heroSupport)].parent == int(j.object);
            break;
        case TriggerType::LookAt:                   // player looking at it: not ported yet
        case TriggerType::LookAtAndTriggerObject:   // both: not ported yet
        case TriggerType::None:
            break;
        }
    }
    // What linked objects see (0x59e6d0): the rising edge of the trigger; while it stays
    // triggered, the updates after a completed cycle (unless no StopTrigger and Ignore).
    if (!j.triggeredBefore) {
        j.on = triggered;
    } else {
        j.on = !j.ignoreWithoutStop && j.completed;
    }
    j.triggeredBefore = triggered;

    // Start delay and playback (0x252610 / 0x252750).
    j.completed = false;
    const float length = j.clip ? j.clip->duration : 0.0f;
    if (j.delayState == 0) {
        if (triggered) {
            j.delayTime = 0;
            j.delayState = 1;
        }
    } else if (j.delayState == 1) {
        if (!triggered && j.triggerOff != TriggerOff::Ignore) {
            j.delayState = 0;
        } else {
            j.delayTime += dt;
            if (j.startDelay < j.delayTime) {
                j.delayState = 2;   // start callback (plays the sequencer); progress restarts
                j.progress = 0;
            }
        }
    } else {
        const bool useReturn = !triggered && j.triggerOff == TriggerOff::Return;
        j.progress += j.rate * dt * (useReturn ? j.speedReturn : j.speed);
        if (j.progress >= length) {
            if (j.moveType == MoveType::Loop) {
                j.progress -= length;
                j.completed = true;
            } else {
                j.progress = length;
                if (j.moveType == MoveType::Replay) {
                    j.delayState = 0;
                    j.completed = true;
                }
            }
        }
        if (triggered) {
            j.rate = 1;
        } else {
            const float fraction = length > 0 ? j.progress / length : 100.0f;
            if (j.completion * 0.01f > fraction && j.triggerOff == TriggerOff::Stop) {
                j.rate = std::max(0.0f, j.rate - kRateChange * dt);
            } else if (j.triggerOff == TriggerOff::Return) {
                j.rate = std::max(-1.0f, j.rate - kRateChange * dt);
                if (j.progress < kEpsilon) {   // back at the start
                    j.progress = 0;
                    j.rate = 0;
                    j.delayState = 0;
                }
            } else {
                j.rate = 1;
            }
        }
    }
    // While the playback does not drive the clip it waits at its start: with a StartTrigger the
    // setup pauses the sequencer (0x59deb0); a TriggerType Parent joint without one also waits
    // (run roof_bridge2: moj_wood_cylinder_rote at its clip's first frame until its parent fires).
    // Hypothesis: no joint's sequencer plays by itself (the earlier "plays once" was inferred).

    if (j.clip) {
        const float time = j.progress;
        std::vector<data::JointPose> pose;
        j.clip->sample(j.skeleton, time, pose);
        const data::JointPose& p = pose[j.locatorJoint];
        nodes_[j.object].local = math::compose(p.rotation, p.translation, p.scale);
    }
}

void LevelRuntime::update(float dt, const RuntimeInputs& in) {
    for (auto& t : visTriggers_) {   // 0x9a5890
        if (suspended_[t.object]) {
            continue;
        }
        t.time += dt;
        t.event = 0;
        switch (t.state) {
        case 0: {
            bool fire = false;
            if (!t.on.empty()) {
                fire = fired(t.on);
            } else if (in.hasRig) {
                fire = !(nodes_[t.object].world[14] < in.rigZ);   // the rig has passed the trigger's z
            }
            if (fire) {
                if (t.onDelay == 0) {
                    t.event = 1;
                    t.state = 2;
                } else {
                    t.state = 3;
                }
                t.time = 0;
            }
            break;
        }
        case 1:
            if (t.time >= t.offDelay) {
                t.event = 2;
                t.state = 0;
                t.time = 0;
            }
            break;
        case 2:
            if (!t.on.empty() && !fired(t.on)) {
                t.state = 0;
            }
            break;
        case 3:
            if (t.time >= t.onDelay) {
                t.event = 1;
                t.state = 2;
                t.time = 0;
            }
            break;
        }
    }
    for (auto& b : boxes_) {
        updateTriggerBox(*b, dt, in);   // traced: hidden visibility groups do not hold trigger boxes
    }
    for (auto& g : groups_) {   // 0xaf3f40
        if (g.shown && groupTriggerFired(g.disappear)) {
            setGroupShown(g, false);
        } else if (!g.shown && groupTriggerFired(g.appear)) {
            setGroupShown(g, true);
        }
    }
    for (std::size_t i : order_) {   // parents first: a Parent trigger sees this update's state
        if (nodes_[i].joint >= 0 && !suspended_[i]) {
            updateJoint(*joints_[std::size_t(nodes_[i].joint)], dt, in);
        }
    }
    for (std::size_t i : order_) {
        Node& n = nodes_[i];
        if (n.underJoint) {
            n.world = n.parent >= 0 ? math::multiply(nodes_[std::size_t(n.parent)].world, n.local) : n.local;
        }
    }
    updateCoins(dt, in);
}

// Coins ------------------------------------------------------------------------------------------

void LevelRuntime::loadCoins(const Level& level, const std::filesystem::path& root) {
    coins_.clear();
    coinsCollected_ = 0;
    coinKinds_[0] = CoinKind{};
    coinKinds_[1] = CoinKind{0.30f, 360, 10, 0};
    // Tuning: <Coin> / <CoinBig> (bound by 0x39c890: CollisionRadius +0x1f0, AngleSpeed +0x400;
    // <Coin><CoinNumBig> +0x4b0 is what a big coin counts, 0x3da190).
    data::Bytes bytes;
    if (data::loadFile(root / "data" / "multi_platformer" / "multi_platformer.xml", bytes)) {
        if (auto xml = data::parseXml(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()))) {
            const char* names[2] = {"Coin", "CoinBig"};
            for (int k = 0; k < 2; ++k) {
                if (const data::XmlNode* n = xml->child(names[k])) {
                    if (auto v = n->childText("CollisionRadius")) {
                        coinKinds_[k].collisionRadius = std::strtof(v->c_str(), nullptr);
                    }
                    if (auto v = n->childText("AngleSpeed")) {
                        coinKinds_[k].angleSpeed = std::strtof(v->c_str(), nullptr);
                    }
                }
            }
            if (const data::XmlNode* n = xml->child("Coin")) {
                if (auto v = n->childText("CoinNumBig")) {
                    coinKinds_[1].value = std::atoi(v->c_str());
                }
            }
        }
    }
    for (std::size_t i = 0; i < level.objects.size(); ++i) {
        const LevelObject& o = level.objects[i];
        if (o.className != "coin" && o.className != "coin_big") {
            continue;
        }
        Coin c;
        c.object = i;
        c.kind = o.className == "coin_big" ? 1 : 0;
        c.spawn = resolveLink(level, o, "SpawnTrigger");
        c.spawnDelay = o.number("SpawnDelay").value_or(0);
        // Setup 0x3d3c60: a coin with a SpawnTrigger starts hidden, waiting; others are out.
        c.state = c.spawn.empty() ? 1 : 0;
        nodes_[i].coin = int(coins_.size());
        coins_.push_back(std::move(c));
    }
}

void LevelRuntime::updateCoins(float dt, const RuntimeInputs& in) {
    // Shared spin (0x3d9b70 / 0x3d9fb0): angle += AngleSpeed * pi / 180 * dt, kept in [-pi, pi).
    constexpr float kPi = 3.14159265f;
    for (CoinKind& k : coinKinds_) {
        const float a = k.spin + k.angleSpeed * kPi / 180.0f * dt;
        k.spin = std::fmod(a + (a < 0 ? -kPi : kPi), 2 * kPi) + (a < 0 ? kPi : -kPi);
    }
    for (Coin& c : coins_) {
        if (suspended_[c.object]) {
            continue;
        }
        switch (c.state) {
        case 0:   // 0x3d8db0: SpawnTrigger fires (or is empty) -> SpawnDelay -> appear (0x3d6860)
            if (c.timer < 0 && (c.spawn.empty() || fired(c.spawn))) {
                c.timer = c.spawnDelay;
            }
            if (c.timer >= 0) {
                c.timer -= dt;
                if (c.timer <= 0) {
                    c.state = 1;   // appear effect ef_coin_appear, shown
                }
            }
            break;
        case 1: {   // 0x3d9090 -> 0x3d4a60: follow the placement, then the pick-up test 0x3d4c30
            if (!in.hasHero) {
                break;
            }
            const math::Mat4& w = nodes_[c.object].world;
            const float dx = in.heroFeet.x - w[12];
            const float dy = in.heroFeet.y + 0.5f * in.heroScale - w[13];
            const float dz = in.heroFeet.z - w[14];
            const float d = std::sqrt(std::max(dx * dx + dy * dy + dz * dz, 1.1754944e-38f));
            if (d < coinKinds_[c.kind].collisionRadius) {
                c.state = 2;   // 0x3d91c0: marked taken (+0x1c52), hidden, ef_coin_pickup
            }
            break;
        }
        case 2:   // 0x3d4dd0: count it, play the pick-up sound, remove the object
            coinsCollected_ += coinKinds_[c.kind].value;
            c.state = 4;
            break;
        default:
            break;
        }
    }
}

bool LevelRuntime::coinState(std::size_t object, int& state) const {
    if (nodes_[object].coin < 0) {
        return false;
    }
    state = coins_[std::size_t(nodes_[object].coin)].state;
    return true;
}

void LevelRuntime::moveObject(std::size_t object, const math::Vec3& at) {
    Node& n = nodes_[object];
    n.world[12] = at.x;
    n.world[13] = at.y;
    n.world[14] = at.z;
    n.local = n.parent >= 0 ? math::multiply(math::inverseAffine(nodes_[std::size_t(n.parent)].world), n.world)
                            : n.world;
}

bool LevelRuntime::visible(std::size_t object) const {
    if (nodes_[object].hidden) {
        return false;
    }
    if (nodes_[object].coin >= 0) {
        return coins_[std::size_t(nodes_[object].coin)].state == 1;
    }
    return true;
}

math::Mat4 LevelRuntime::pose(std::size_t object) const {
    const Node& n = nodes_[object];
    if (n.coin < 0) {
        return n.world;
    }
    // The coin's rotation is its own times the shared spin about y (0x3d4a60: q_coin * q_spin).
    const float a = coinKinds_[coins_[std::size_t(n.coin)].kind].spin;
    const math::Quat spin{0, std::sin(a * 0.5f), 0, std::cos(a * 0.5f)};
    return math::multiply(n.world, math::compose(spin, {}, {1, 1, 1}));
}

}   // namespace game
