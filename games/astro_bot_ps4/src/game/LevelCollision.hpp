// Static collision of a level, assembled the way the level data describes it:
//   - every placement with a HasCollision attribute that is true, whose object definition names
//     physics bodies (IPhysics/File -> .pfb), contributes those bodies at the placement's
//     transform (art_bg is the bulk: 33k placements; reactive_object, foliage);
//   - HitTvHero / HitVrHero / HitOther (default true, art_bg.lox) select who the surface stops;
//     IsSlope marks slide surfaces; IPhysics/Material names the surface material;
//   - levels/<name>/pfx/platform_design.lcb adds baked meshes in six levels:
//     u32 version (4; 0 in mine_coaster_04), u32 entry count, 8 bytes zero, then per entry
//     {u32 10, u32 16, u32 1, u32 flags (0x00070000: hit flags in byte 2, hypothesis)},
//     {u32 11, u32 size, u32 1, u32 0} + serialized mesh, {u32 4, u32 4, u32 1, u32 0} + u32
//     material hash ("Stone", "Water").
// Placements under moving parents are added at their initial pose; moving them is part of the
// object runtime (later step).
#pragma once

#include "data/GameFiles.hpp"
#include "data/ObjectDefinition.hpp"
#include "game/LevelDocument.hpp"
#include "physics/CollisionWorld.hpp"
#include "physics/PhysicsWorld.hpp"

#include <map>
#include <memory>
#include <string>

namespace beacon::game {

struct LevelCollisionStats {
    std::size_t placements = 0, bodies = 0, triangles = 0, bakedMeshes = 0, missingFiles = 0, badFiles = 0;
    // Placements built as moving collision bodies: (placement index, world body id).
    std::vector<std::pair<std::size_t, std::uint32_t>> movingBodies;
    // With a PhysicsWorld: each placement's library bodies (placement index, body ids), and the
    // shapes it could not take.
    std::vector<std::pair<std::size_t, std::vector<int>>> physicsBodies;
    std::size_t physicsSkippedShapes = 0;
};

class PhysicsLibrary {
public:
    explicit PhysicsLibrary(const data::GameFiles& files) : files_(files) {}
    // Bodies of a .pfb by stem; null if missing or unreadable.
    const data::PhysicsFile* find(const std::string& stem, std::string* error = nullptr);

private:
    const data::GameFiles& files_;
    std::map<std::string, std::unique_ptr<data::PhysicsFile>> cache_;
};

// `moving` (optional, per placement): placements that move at run time (under moving joints)
// become collision bodies of their own, posed with CollisionWorld::setBodyTransform.
// `bodies` (optional): the same placements as bodies of the ported physics library (fixed;
// keyframe for moving placements), with the level's baked meshes.
// Hidden visibility-group targets have their physics off (0xaf3e70 -> 0x772130(t, 0, 2)): their
// bodies match nothing (filters 0 / 0, the old ones kept in `saved`) until shown again.
class LevelRuntime;
void syncHiddenCollision(const LevelRuntime& runtime, const LevelCollisionStats& stats, physics::PhysicsWorld& world,
                         std::map<int, std::pair<std::uint32_t, std::uint32_t>>& saved);

LevelCollisionStats buildLevelCollision(const Level& level, const std::filesystem::path& gameRoot,
                                        data::ObjectDefinitionLibrary& defs, PhysicsLibrary& physics,
                                        physics::CollisionWorld& world, const std::vector<char>* moving = nullptr,
                                        physics::PhysicsWorld* bodies = nullptr);

}   // namespace game
