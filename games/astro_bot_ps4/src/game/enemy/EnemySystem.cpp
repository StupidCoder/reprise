#include "game/enemy/EnemySystem.hpp"

#include "data/Animation.hpp"
#include "data/Hash.hpp"
#include "data/PhysicsFile.hpp"
#include "data/Skeleton.hpp"
#include "anim/Pose.hpp"
#include "physics/PhysicsWorld.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <optional>

namespace beacon::game::enemy {

namespace {

struct KindInfo {
    const char* className;
    const char* tuningBlock;   // in basic_enemies.xml
    int id;                    // +0x1cc8
    const char* definition;    // the object definition the class builds its model from
};

// The class factories construct the enemy with its kind id, tuning block and a definition name
// (0x2da4d0: 0x7d2, "mup_ch_enemy_basic"); the spawn (0x2da770) builds the model object (model,
// skeleton, clips, transitions, physics) from that name, and from the placement's Odx only when
// the name is empty. The placements' own Odx (often mup_ch_enemy_spiky) is then unused.
constexpr KindInfo kKinds[] = {
    {"enemy_standard", "EnemyStandard", 0x7d2, "mup_ch_enemy_basic"},
    // enemy_spiky (creator 0x92b280 -> 0x92b6b0, vtable 0x16cbe78 on the basic enemy 0x2d9b20): the
    // same root process and behaviours (0x92c230 picks the default root for every kind but 0x7d5),
    // its own tuning block and model; the guard (+0x3aaf) shields it while attacking.
    {"enemy_spiky", "EnemySpiky", 0x7d4, "mup_ch_enemy_spiky"},
};

constexpr std::uint32_t kRootMotionJoint = 0x45279a6eu;   // the joint whose motion moves the body

void addClips(const data::ObjectDefinition* def, const data::GameFiles& files,
              const std::vector<std::uint32_t>& frameJoints, std::map<std::uint32_t, EnemyClip>& out,
              EnemyTransitions& transitions) {
    if (!def) {
        return;
    }
    std::optional<data::Skeleton> skeleton;
    int rootJoint = -1;
    if (auto path = files.find(def->skeleton, ".skel")) {
        data::Bytes bytes;
        if (data::loadFile(*path, bytes)) {
            skeleton = data::loadSkeleton(bytes);
        }
    }
    std::vector<std::pair<std::uint32_t, int>> tracked;   // (name hash, joint index)
    if (skeleton) {
        for (std::size_t j = 0; j < skeleton->nameHash.size(); ++j) {
            if (skeleton->nameHash[j] == kRootMotionJoint) {
                rootJoint = int(j);
            }
            for (std::uint32_t h : frameJoints) {
                if (skeleton->nameHash[j] == h) {
                    tracked.emplace_back(h, int(j));
                }
            }
        }
    }
    for (const auto& a : def->animations) {
        const std::uint32_t h = data::fnv1a(a.name);
        if (out.count(h) != 0 || a.clips.empty()) {
            continue;
        }
        auto path = files.find(a.clips[0], ".anim");
        data::Bytes bytes;
        if (!path || !data::loadFile(*path, bytes)) {
            continue;
        }
        auto clip = data::loadAnimation(bytes);
        if (!clip) {
            continue;
        }
        EnemyClip c;
        c.speed = a.speed > 0 ? a.speed : 1;
        c.length = clip->duration / c.speed;
        c.loop = a.loop;
        c.rate = clip->sampleRate;
        if (skeleton && rootJoint >= 0) {
            std::vector<data::JointPose> pose;
            std::vector<math::Mat4> model;
            bool moves = false;
            for (std::uint32_t f = 0; f < clip->frameCount; ++f) {
                clip->sample(*skeleton, float(f) / clip->sampleRate, pose);
                c.root.push_back(pose[std::size_t(rootJoint)].translation);
                if (!tracked.empty()) {
                    // The bodies' joint frames with the root-motion joint held at its base pose.
                    pose[std::size_t(rootJoint)].translation = skeleton->basePose[std::size_t(rootJoint)].translation;
                    anim::jointWorldMatrices(*skeleton, pose, model);
                    for (const auto& [h, j] : tracked) {
                        const physics::Placement p = physics::placementOf(model[std::size_t(j)]);
                        c.joints[h].at.push_back(p.position);
                        c.joints[h].turn.push_back(p.rotation);
                    }
                }
                const auto& d = c.root.back();
                moves = moves || std::fabs(d.x - c.root[0].x) + std::fabs(d.y - c.root[0].y) + std::fabs(d.z - c.root[0].z) > 1e-6f;
            }
            if (!moves) {
                c.root.clear();
            }
        }
        out[h] = std::move(c);
    }
    if (const data::XmlNode* anim = def->xml.child("IAnimation")) {
        anim->forEach("Transition", [&](const data::XmlNode& t) {
            const auto key = std::make_pair(data::fnv1a(t.childText("Source", "")), data::fnv1a(t.childText("Destination", "")));
            if (transitions.count(key) == 0) {
                transitions[key] = std::strtof(t.childText("JointsBlendDuration", "0").c_str(), nullptr);
            }
        });
    }
}

}   // namespace

// The root dynamics' body of an enemy (0x7dd0c0): the definition's .pfb body named by
// Bind/RootCollision, framed by the skeleton's Bind/BodyJoint (0x7e83b0: the joint's model matrix;
// the offset +0x280 is 0) times the body's own offset from that frame as created (+0xc0, fixed at
// set-up: (root x frame)^-1 x the body's pose). The body is created at the placement times the
// record's body frame (0xbd91a0, +0x230 / +0x23c), so the offset is the joint's base frame^-1 x
// that body frame: for enemy_torso_col a quarter turn about z (the cylinder's x axis upright)
// 0.0025 above joint_torso. The frame follows the animated joint every update.
static void attachRootBody(BasicEnemy& e, const data::ObjectDefinition* def, const data::GameFiles& files,
                           physics::PhysicsWorld& world, std::vector<std::shared_ptr<const data::PhysicsFile>>& keep) {
    if (!def || def->physics.empty()) {
        return;
    }
    data::Bytes bytes;
    auto path = files.find(def->physics, ".pfb");
    if (!path || !data::loadFile(*path, bytes)) {
        return;
    }
    auto pf = data::loadPhysicsFile(bytes);
    if (!pf) {
        return;
    }
    // (Searches the file wherever it is kept: `pf` is moved into `shared` further down.)
    const data::PhysicsFile* file = &*pf;
    auto recordNamed = [&](const std::string& name) {
        for (std::size_t b = 0; b < file->bodies.size(); ++b) {
            if (!name.empty() && file->bodies[b].bodyName == name) {
                return b;
            }
        }
        return file->bodies.size();
    };
    const std::size_t record = recordNamed(e.tuning().text("Bind/RootCollision"));
    if (record == pf->bodies.size()) {
        return;
    }
    // Base-pose model frames of the skeleton's joints.
    std::optional<data::Skeleton> skeleton;
    std::vector<math::Mat4> model;
    if (auto skel = files.find(def->skeleton.empty() ? def->model : def->skeleton, ".skel")) {
        data::Bytes sb;
        if (data::loadFile(*skel, sb)) {
            skeleton = data::loadSkeleton(sb);
        }
        if (skeleton) {
            anim::jointWorldMatrices(*skeleton, skeleton->basePose, model);
        }
    }
    auto baseFrame = [&](std::uint32_t joint, math::Vec3& at, math::Quat& turn) {
        for (std::size_t j = 0; skeleton && j < skeleton->nameHash.size(); ++j) {
            if (skeleton->nameHash[j] == joint) {
                const physics::Placement p = physics::placementOf(model[j]);
                at = p.position;
                turn = p.rotation;
            }
        }
    };
    // A record's body frame in the joint's base frame (at, turn): turn^-1 (p - at), turn^-1 q.
    auto inJoint = [](const data::PhysicsBody& b, const math::Vec3& at, const math::Quat& turn, math::Vec3& shift,
                      math::Quat& twist) {
        auto mul = [](const math::Quat& a, const math::Quat& c) {
            return math::Quat{a.w * c.x + a.x * c.w + a.y * c.z - a.z * c.y, a.w * c.y - a.x * c.z + a.y * c.w + a.z * c.x,
                              a.w * c.z + a.x * c.y - a.y * c.x + a.z * c.w, a.w * c.w - a.x * c.x - a.y * c.y - a.z * c.z};
        };
        const math::Quat inv{-turn.x, -turn.y, -turn.z, turn.w};
        const math::Vec3 d = b.bodyPosition - at;
        const math::Quat r = mul(mul(inv, math::Quat{d.x, d.y, d.z, 0}), turn);
        shift = {r.x, r.y, r.z};
        twist = mul(inv, b.bodyRotation);
    };
    auto shared = std::make_shared<const data::PhysicsFile>(std::move(*pf));
    file = shared.get();
    const std::uint32_t bodyJoint = data::fnv1a(e.tuning().text("Bind/BodyJoint"));
    math::Vec3 offset, shift;
    math::Quat turn, twist;
    baseFrame(bodyJoint, offset, turn);
    inJoint(shared->bodies[record], offset, turn, shift, twist);
    e.attachBody(world, *shared, record, bodyJoint, offset, turn, shift, twist);
    // The sensors (virtual +0x3a8) reach forward from the RootCollision body's height; their bodies
    // follow the root body (0x7dd0c0 -> 0x7deb20).
    e.attachSensors(world, shared->bodies[record].bodyPosition.y);
    if (const std::size_t dead = recordNamed(e.tuning().text("Bind/DeadCollision")); dead < shared->bodies.size()) {
        e.attachDeadBody(world, *shared, dead);
    }
    if (const std::size_t damage = recordNamed(e.tuning().text("Bind/DamageJoint")); damage < shared->bodies.size()) {
        // Hung on the joint its record names (enemy_head_col on joint_head).
        const std::uint32_t joint = data::fnv1a(shared->bodies[damage].name);
        if (std::getenv("BEACON_DEBUG_ATTACK")) {   // research aid
            std::fprintf(stderr, "damage body %zu: record %zu '%s' joint '%s'\n", e.placement(), damage,
                         shared->bodies[damage].bodyName.c_str(), shared->bodies[damage].name.c_str());
        }
        math::Vec3 at, dshift;
        math::Quat jt, dtwist;
        baseFrame(joint, at, jt);
        inJoint(shared->bodies[damage], at, jt, dshift, dtwist);
        e.attachDamageBody(world, *shared, damage, joint, at, jt, dshift, dtwist);
    }
    keep.push_back(std::move(shared));
}

void EnemySystem::load(const Level& level, data::ObjectDefinitionLibrary& defs, const data::GameFiles& files,
                       const LevelSchemaLibrary& schemas, physics::PhysicsWorld* physics) {
    enemies_.clear();
    physicsFiles_.clear();
    physics_ = physics;
    std::map<std::string, std::optional<EnemyTuning>> tunings;
    for (std::size_t i = 0; i < level.objects.size(); ++i) {
        const LevelObject& o = level.objects[i];
        const KindInfo* kind = nullptr;
        for (const auto& k : kKinds) {
            if (o.className == k.className) {
                kind = &k;
            }
        }
        if (!kind) {
            continue;
        }
        auto& tuning = tunings[kind->tuningBlock];
        if (!tuning) {
            tuning = EnemyTuning::load(files.root(), kind->tuningBlock);
        }
        if (!tuning) {
            continue;
        }
        const data::ObjectDefinition* def = kind->definition[0] ? defs.find(kind->definition)
                                                                : o.odx.empty() ? nullptr : defs.find(o.odx);
        std::map<std::uint32_t, EnemyClip> clips;
        EnemyTransitions transitions;
        // Joints bodies hang on: the root dynamics' and the damage body's (the name of the .pfb
        // record Bind/DamageJoint names: joint_head for the basic enemy).
        std::vector<std::uint32_t> frameJoints{data::fnv1a(tuning->text("Bind/BodyJoint"))};
        if (def && !def->physics.empty()) {
            data::Bytes bytes;
            if (auto path = files.find(def->physics, ".pfb"); path && data::loadFile(*path, bytes)) {
                if (auto pf = data::loadPhysicsFile(bytes)) {
                    const std::string damage = tuning->text("Bind/DamageJoint");
                    for (const auto& b : pf->bodies) {
                        if (!damage.empty() && b.bodyName == damage) {
                            frameJoints.push_back(data::fnv1a(b.name));
                        }
                    }
                }
            }
        }
        addClips(def, files, frameJoints, clips, transitions);
        enemies_.push_back(std::make_unique<BasicEnemy>(level, i, kind->id, *tuning, std::move(clips),
                                                        std::move(transitions), data::fnv1a(o.uuid)));
        // Contact radius with other enemies: the widest shape of the model's first physics body
        // (the root; basic: sphere) times the placement scale.
        float contact = 0.07f;
        if (def && !def->physics.empty()) {
            data::Bytes bytes;
            if (auto path = files.find(def->physics, ".pfb"); path && data::loadFile(*path, bytes)) {
                if (auto pf = data::loadPhysicsFile(bytes); pf && !pf->bodies.empty()) {
                    float r = 0;
                    for (const auto& sh : pf->bodies.front().shapes) {
                        r = std::max(r, sh.radius);
                    }
                    if (r > 0) {
                        contact = r;
                    }
                }
            }
        }
        if (physics) {
            attachRootBody(*enemies_.back(), def, files, *physics, physicsFiles_);
        }
        definitions_.push_back(def ? def->name : std::string());
        contactRadius_.push_back(contact * o.transform.scale[1]);
        punchedBy_.push_back(0);
    }
}

void EnemySystem::update(const EnemyWorld& world) {
    for (auto& e : enemies_) {
        // Targets of a visibility group that has not appeared yet are not updated (runs hop1,
        // sens7: the enemies listed by the level's hidden "vg_" groups keep their spawn state;
        // the suspending path itself is not found, see LevelRuntime).
        if (world.runtime && world.runtime->suspended(e->placement())) {
            continue;
        }
        e->update(world);
    }
    // The enemies push each other through their bodies' contacts in the world step.
}

int EnemySystem::punch(const math::Vec3& from, const math::Vec3& to, float radius, int window,
                       const math::Vec3& heroFeet) {
    if (physics_) {
        constexpr std::uint32_t kHeroSelf = 1;
        const auto hit = physics_->castSphere(from, to, radius, physics::kHitAll, {0x20000, 0x91041a0fu & ~kHeroSelf});
        if (!hit || !hit->surface) {
            return 0;
        }
        for (std::size_t i = 0; i < enemies_.size(); ++i) {
            BasicEnemy& e = *enemies_[i];
            if (e.placement() != hit->surface->owner || e.removed() || punchedBy_[i] == window) {
                continue;
            }
            punchedBy_[i] = window;
            math::Vec3 dir{e.position().x - heroFeet.x, 0, e.position().z - heroFeet.z};
            const float len = std::sqrt(dir.x * dir.x + dir.z * dir.z);
            dir = len > 0 ? math::Vec3{dir.x / len, 0, dir.z / len} : math::Vec3{};
            EnemyHit h;
            h.type = 1;
            h.position = to;
            h.direction = dir;
            e.hit(h);
            return 1;
        }
        return 0;
    }
    int hits = 0;
    for (std::size_t i = 0; i < enemies_.size(); ++i) {
        BasicEnemy& e = *enemies_[i];
        if (!e.collides() || e.removed() || punchedBy_[i] == window) {
            continue;
        }
        // The enemy's body: a sphere of the contact radius resting on its feet (approximation of
        // the physics bodies the original's query meets).
        const float r = contactRadius_[i];
        const math::Vec3 c{e.position().x, e.position().y + r, e.position().z};
        const math::Vec3 d{to.x - from.x, to.y - from.y, to.z - from.z};
        const float dd = d.x * d.x + d.y * d.y + d.z * d.z;
        float t = 0;
        if (dd > 0) {
            t = std::clamp(((c.x - from.x) * d.x + (c.y - from.y) * d.y + (c.z - from.z) * d.z) / dd, 0.0f, 1.0f);
        }
        const math::Vec3 p{from.x + d.x * t - c.x, from.y + d.y * t - c.y, from.z + d.z * t - c.z};
        if (p.x * p.x + p.y * p.y + p.z * p.z > (r + radius) * (r + radius)) {
            continue;
        }
        punchedBy_[i] = window;
        // Push direction: from the hero to the enemy, horizontal (0x674c80).
        math::Vec3 dir{e.position().x - heroFeet.x, 0, e.position().z - heroFeet.z};
        const float len = std::sqrt(dir.x * dir.x + dir.z * dir.z);
        dir = len > 0 ? math::Vec3{dir.x / len, 0, dir.z / len} : math::Vec3{};
        EnemyHit h;
        h.type = 1;   // 2 when the attack component's +0x298 is set (not for the punch)
        h.position = to;
        h.direction = dir;
        e.hit(h);
        ++hits;
    }
    return hits;
}

bool EnemySystem::hitPlacement(std::size_t placement, const EnemyHit& hit) {
    for (auto& e : enemies_) {
        if (e->placement() == placement && !e->removed()) {
            e->hit(hit);
            return true;
        }
    }
    return false;
}

const BasicEnemy* EnemySystem::forPlacement(std::size_t placement) const {
    for (const auto& e : enemies_) {
        if (e->placement() == placement) {
            return e.get();
        }
    }
    return nullptr;
}

}   // namespace beacon::game::enemy
