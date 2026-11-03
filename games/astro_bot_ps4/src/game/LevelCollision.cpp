#include "game/LevelCollision.hpp"

#include "game/LevelRuntime.hpp"

#include "data/CollisionMesh.hpp"
#include "data/Hash.hpp"

#include <cmath>

namespace beacon::game {

namespace {

constexpr std::uint32_t kArtSelf = 0x10;         // art_bg +0x1ca4 (0x5eb211)
constexpr std::uint32_t kRobotBits = 1;          // the robot hero's body self bits (0x3df080)
constexpr std::uint32_t kOtherHeroBits = 0xf & ~kRobotBits;   // 0x3df120

// The body's frame in its object (the body frame; shapes are not scaled by the node scale).
math::Mat4 bodyMatrix(const data::PhysicsBody& b) {
    return math::compose(b.bodyRotation, b.bodyPosition, {1, 1, 1});
}

// A placement's translation, rotation (as a quaternion) and scale.
physics::Placement placementOf(const LevelTransform& t) { return physics::placementOf(t.matrix()); }

void addBakedMeshes(const std::filesystem::path& file, physics::CollisionWorld& world, LevelCollisionStats& stats,
                    physics::PhysicsWorld* bodies) {
    data::Bytes b;
    if (!data::loadFile(file, b) || b.size() <= 16) {
        return;
    }
    const std::uint32_t entries = data::u32(b, 4);
    std::size_t at = 16;
    for (std::uint32_t e = 0; e < entries && at + 32 <= b.size(); ++e) {
        const std::uint32_t flags = data::u32(b, at + 12);
        at += 16;
        if (data::u32(b, at) != 11) {
            ++stats.badFiles;
            return;
        }
        const std::uint32_t size = data::u32(b, at + 4);
        at += 16;
        if (at + size > b.size()) {
            ++stats.badFiles;
            return;
        }
        data::TriangleSoup soup;
        if (!data::decodeCollisionMesh(data::ByteView(b).subspan(at, size), soup)) {
            ++stats.badFiles;
            return;
        }
        at += size;
        std::uint32_t material = 0;
        if (at + 20 <= b.size() && data::u32(b, at) == 4) {
            material = data::u32(b, at + 16);
            at += 20;
        }
        physics::Surface surface;
        surface.owner = 0xffffffffu;   // not a placement
        surface.mask = (flags >> 16) & physics::kHitAll;
        surface.material = material;
        for (std::size_t i = 0; i + 2 < soup.indices.size(); i += 3) {
            world.addTriangle(soup.vertices[soup.indices[i]], soup.vertices[soup.indices[i + 1]],
                              soup.vertices[soup.indices[i + 2]], surface);
        }
        if (bodies) {
            auto mesh = std::make_shared<data::LargeMesh>();
            if (data::decodeLargeMesh(data::ByteView(b).subspan(at - size, size), *mesh)) {
                physics::BodyOptions options;
                options.surface = surface;
                bodies->addMesh(std::move(mesh), options);
            }
        }
        ++stats.bakedMeshes;
    }
}

}   // namespace

const data::PhysicsFile* PhysicsLibrary::find(const std::string& stem, std::string* error) {
    if (auto it = cache_.find(stem); it != cache_.end()) {
        return it->second.get();
    }
    std::unique_ptr<data::PhysicsFile>& slot = cache_[stem];
    auto path = files_.find(stem, ".pfb");
    data::Bytes bytes;
    if (!path || !data::loadFile(*path, bytes)) {
        if (error) {
            *error = stem + ".pfb not found";
        }
        return nullptr;
    }
    if (auto file = data::loadPhysicsFile(bytes, error)) {
        slot = std::make_unique<data::PhysicsFile>(std::move(*file));
    }
    return slot.get();
}

LevelCollisionStats buildLevelCollision(const Level& level, const std::filesystem::path& gameRoot,
                                        data::ObjectDefinitionLibrary& defs, PhysicsLibrary& physics,
                                        physics::CollisionWorld& world, const std::vector<char>* moving,
                                        physics::PhysicsWorld* bodies) {
    LevelCollisionStats stats;
    world.clear();
    for (std::size_t i = 0; i < level.objects.size(); ++i) {
        const LevelObject& o = level.objects[i];
        if (o.asTemplate || !o.flag("HasCollision").value_or(false) || o.odx.empty()) {
            continue;
        }
        const data::ObjectDefinition* def = defs.find(o.odx);
        if (!def || def->physics.empty()) {
            continue;
        }
        const data::PhysicsFile* file = physics.find(def->physics);
        if (!file) {
            ++stats.missingFiles;
            continue;
        }
        physics::Surface surface;
        surface.owner = std::uint32_t(i);
        const bool hitVr = o.flag("HitVrHero").value_or(true), hitTv = o.flag("HitTvHero").value_or(true),
                   hitOther = o.flag("HitOther").value_or(true);
        surface.mask = (hitVr ? physics::kHitHero : 0u) | (hitTv ? physics::kHitVr : 0u) |
                       (hitOther ? physics::kHitOther : 0u);
        surface.material = def->physicsMaterial.empty() ? 0 : data::fnv1a(def->physicsMaterial);
        surface.slope = o.flag("IsSlope").value_or(false);
        const math::Mat4 placement = o.transform.matrix();
        const bool moves = moving && i < moving->size() && (*moving)[i];
        if (moves) {
            stats.movingBodies.emplace_back(i, world.beginBody());
        }
        for (const auto& body : file->bodies) {
            const math::Mat4 m = math::multiply(placement, bodyMatrix(body));
            for (const auto& shape : body.shapes) {
                world.addShape(shape, m, surface);
            }
            ++stats.bodies;
        }
        if (bodies) {
            physics::BodyOptions options;
            options.motion = moves ? physics::rigid::MotionType::Keyframe : physics::rigid::MotionType::Fixed;
            options.surface = surface;
            // The object's filters (art_bg: self +0x1ca4 = 0x10; target +0x1ca8 by 0x5ecda0: all bits
            // with HitOther, else none, then the robot's bits (1) by HitVrHero and the other heroes'
            // (0xf without the robot's) by HitTvHero; applied to the bodies by 0xbd40f0 / 0xbd4200).
            options.filterSelf = kArtSelf;
            options.filterTarget = hitOther ? 0xffffffffu : 0u;
            options.filterTarget = hitTv ? options.filterTarget | kOtherHeroBits : options.filterTarget & ~kOtherHeroBits;
            options.filterTarget = hitVr ? options.filterTarget | kRobotBits : options.filterTarget & ~kRobotBits;
            const std::size_t skipped = bodies->skippedShapes();
            stats.physicsBodies.emplace_back(
                i, bodies->addBodies(*file, placementOf(o.transform), options));
            stats.physicsSkippedShapes += bodies->skippedShapes() - skipped;
        }
        if (moves) {
            world.endBody();
        }
        ++stats.placements;
    }
    addBakedMeshes(gameRoot / "data" / "multi_platformer" / "levels" / level.name / "pfx" / "platform_design.lcb", world,
                   stats, bodies);
    world.build();
    stats.triangles = world.triangles().size();
    return stats;
}

void syncHiddenCollision(const LevelRuntime& runtime, const LevelCollisionStats& stats, physics::PhysicsWorld& world,
                         std::map<int, std::pair<std::uint32_t, std::uint32_t>>& saved) {
    for (const auto& [placement, ids] : stats.physicsBodies) {
        const bool off = runtime.hidden(placement);
        for (int id : ids) {
            auto it = saved.find(id);
            if (off && it == saved.end()) {
                const auto& st = world.library().body(id).state;
                saved[id] = {st.filterSelf, st.filterTarget};
                world.setFilters(id, 0, 0);
            } else if (!off && it != saved.end()) {
                world.setFilters(id, it->second.first, it->second.second);
                saved.erase(it);
            }
        }
    }
}

}   // namespace game
