// The engine's physics module (app\Module\Physics: PhysicsWorld, PhysicsWorldImpl,
// PhysicsRigidBody, IfcPhysics) on top of the ported library (physics/rigid). Findings in
// .analysis/docs/physics.md ("Integration into the port", "Module layer").
//
// Game units here, library units below: positions, sizes and velocities are multiplied by the
// world scale (3) on the way in and divided on the way out (0xcb1210 / 0xcb1250, 0xca5fa0).
//
// Bodies of a placed object come from its .pfb (PhysicsImportPfb 0xc9cc70) placed as IfcPhysics
// does (0xbd91a0): the body at the object's transform times the body record's body frame (+0x230 /
// +0x23c; not the node frame), each shape scaled by the object's scale - positions
// component-wise, spheres by the largest component, box
// half extents and capsule / cylinder half lengths and radii by the length of the scaled body
// axis they lie along (the body's rotation, not the shape's); hulls and meshes carry the scale
// in their shape frame. Capsules and cylinders lie along their shape's x axis (the library's
// convention; the files turn them with the body frame or the shape's rotation).
#pragma once

#include "data/CollisionMesh.hpp"
#include "data/PhysicsFile.hpp"
#include "math/Transform.hpp"
#include "physics/Surface.hpp"
#include "physics/rigid/RigidWorld.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace beacon::physics {

// An object's placement in the level (game units).
struct Placement {
    math::Vec3 position;
    math::Quat rotation;
    math::Vec3 scale{1, 1, 1};
};

// A placement from its world matrix (column-major; the scale = the column lengths).
Placement placementOf(const math::Mat4& m);

// How the bodies of an object are created and what their surfaces are.
struct BodyOptions {
    rigid::MotionType motion = rigid::MotionType::Fixed;
    std::uint32_t filterSelf = 0xffffffffu, filterTarget = 0xffffffffu;
    Surface surface;
};

// A query's first hit (game units).
struct PhysicsHit {
    float fraction = 0;          // along from -> to
    math::Vec3 point, normal;    // on the surface; normal from the surface towards the query
    int body = -1;
    const Surface* surface = nullptr;
};

class PhysicsWorld {
public:
    static constexpr float kScale = 3.0f;   // library units per game unit (MupMain 0xca8c50)

    PhysicsWorld();

    // The bodies of a .pfb placed by `placement`; returns their ids. Kind-6 meshes are built from
    // their triangles here (rigid::buildLargeMesh), once per file shape and scale.
    std::vector<int> addBodies(const data::PhysicsFile& file, const Placement& placement, const BodyOptions& options);
    // One body of a .pfb (record `record`), placed as addBodies places it; active bodies take the
    // record's mass, surface, damping and velocity limits (inertia: set it with setMass).
    int addBody(const data::PhysicsFile& file, std::size_t record, const Placement& placement,
                const BodyOptions& options);
    // A serialized mesh at identity (level .lcb meshes).
    int addMesh(std::shared_ptr<const data::LargeMesh> mesh, const BodyOptions& options);
    // A lone capsule / cylinder along its shape's x axis (game units; the module's 0xcaa360 ->
    // 0xcb56a0 and 0xcaa220 -> 0xcb54c0), the shape at `offset` turned by `turn` in the body.
    int addCapsule(float halfLength, float radius, float mass, const BodyOptions& options,
                   const math::Vec3& offset = {}, const math::Quat& turn = {});
    int addCylinder(float halfLength, float radius, float mass, const BodyOptions& options,
                    const math::Vec3& offset = {}, const math::Quat& turn = {});

    // Places a body (game units), e.g. a placement under a moving joint; delta() is its change of
    // transform by the last call (what a hero standing on it is carried by).
    void setPose(int body, const math::Vec3& position, const math::Quat& rotation);
    math::Vec3 position(int body) const;
    const math::Mat4& delta(int body) const { return deltas_[std::size_t(body)]; }
    // Moves the bodies one addBodies call made with its placement now at `world`; a changed scale
    // rebuilds their shapes.
    void movePlacement(const std::vector<int>& bodies, const math::Mat4& world);
    bool moving(int body) const;   // keyframed or dynamic (motion types 1 and 2)

    // The body interface the game's controllers use (module 0xca5xxx-0xca6xxx; game units):
    void setMotion(int body, rigid::MotionType type);                        // 0xca5c70 (wakes it)
    void setOrientation(int body, const math::Quat& q);                      // 0xca5f40
    rigid::MotionType motion(int body) const { return world_.body(body).state.motion; }
    void setFilters(int body, std::uint32_t self, std::uint32_t target);     // 0xca6390 / 0xca63b0
    void setGroups(int body, std::uint16_t a, std::uint16_t b);              // state +0x10 / +0x12
    math::Quat orientation(int body) const { return world_.body(body).state.orientation; }
    math::Vec3 linearVelocity(int body) const;                               // 0xca5f50
    void setLinearVelocity(int body, const math::Vec3& v);                   // 0xca5fa0
    math::Vec3 angularVelocity(int body) const;                              // 0xca6080
    void setAngularVelocity(int body, const math::Vec3& w);                  // 0xca6090
    void setMaxAngularVelocity(int body, float w);                           // 0xca60f0 (state +0x24)
    // Velocities that bring a non-fixed body to `target` in dt (<= 0: the world's step):
    // 0xca5e50 -> 0xd998c0 (v = (target - position) / dt), 0xca5ef0 -> 0xd99970 (the shortest turn).
    void moveTo(int body, const math::Vec3& target, float dt = -1);
    void turnTo(int body, const math::Quat& target, float dt = -1);
    // -mass x gravity through the force step (0xca65a0): the next world step adds none. This, the
    // force step and setLinearVelocity wake the body.
    void cancelGravity(int body);
    void applyForce(int body, const math::Vec3& force, const math::Vec3& torque, float dt);   // 0xca6510
    // Mass and inertia (library units) and friction (0xca6120 / 0xca67f0 / 0xca6330).
    void setMass(int body, float mass, const rigid::Mat3& inertia);
    void setFriction(int body, float friction);
    float timeStep() const { return world_.params().timeStep; }

    void step(float dt);

    // The module's queries (game units; among surfaces stopping `hitMask`): the first surface met
    // moving from `from` to `to` by
    //   a ray (0xc22810 -> 0xcb8910; starting inside a primitive misses it),
    //   a sphere (0xc22e20 -> 0xcb8d50; starting within reach: fraction 0),
    //   a capsule along its x axis turned by `rotation` (0xc23300 -> 0xcb9190).
    // As the module reports them: a mesh hit's normal is the facet's stored normal turned by the
    // body and shape rotations (0xd97eb0; not corrected for scale); a hit whose point or normal is
    // not a number is no hit.
    // `filter`: the query's self / target masks against the bodies' (+0x8 / +0xc); level bodies
    // keep the importer's all-ones masks, so only gameplay bodies (enemies 0x1000) are told apart.
    std::optional<PhysicsHit> castRay(const math::Vec3& from, const math::Vec3& to, std::uint32_t hitMask = kHitAll,
                                      const rigid::QueryFilter& filter = {}) const;
    std::optional<PhysicsHit> castSphere(const math::Vec3& from, const math::Vec3& to, float radius,
                                         std::uint32_t hitMask = kHitAll, const rigid::QueryFilter& filter = {}) const;
    std::optional<PhysicsHit> castCapsule(const math::Vec3& from, const math::Vec3& to, float radius, float halfLength,
                                          const math::Quat& rotation, std::uint32_t hitMask = kHitAll,
                                          const rigid::QueryFilter& filter = {}) const;

    std::size_t bodyCount() const { return std::size_t(world_.size()); }
    std::size_t skippedShapes() const { return skippedShapes_; }   // kind-6 meshes the builder refused
    std::size_t runtimeMeshes() const { return built_.size(); }    // kind-6 meshes built (per file shape and scale)
    rigid::RigidWorld& library() { return world_; }
    const rigid::RigidWorld& library() const { return world_; }
    const Surface& surface(int body) const { return surfaces_[std::size_t(body)]; }

private:
    std::optional<PhysicsHit> moduleHit(const std::optional<rigid::QueryHit>& hit) const;
    std::function<bool(int)> acceptMask(std::uint32_t hitMask) const;

    rigid::RigidWorld world_;
    std::vector<math::Mat4> deltas_;   // per body
    struct Offset {
        math::Vec3 position;   // within the placement, scaled (game units)
        math::Quat rotation;
    };
    std::vector<Offset> offsets_;      // per body
    struct Source {                    // what a file body was built from (null for others)
        const data::PhysicsBody* record = nullptr;
        math::Vec3 scale{1, 1, 1};
    };
    std::vector<Source> sources_;      // per body (shorter: the rest have none)
    void buildShapes(const data::PhysicsBody& record, const math::Vec3& s, std::vector<rigid::Shape>& shapes);
    std::vector<Surface> surfaces_;   // per body
    std::vector<std::shared_ptr<const data::LargeMesh>> meshes_;   // kept alive for the shapes
    struct BuiltMesh {
        const data::TriangleSoup* soup;
        math::Vec3 scale;
        std::shared_ptr<const data::LargeMesh> mesh;   // null: the builder refused it
    };
    std::vector<BuiltMesh> built_;   // run-time meshes by file shape and scale
    std::shared_ptr<const data::LargeMesh> runtimeMesh(const data::TriangleSoup& soup, const math::Vec3& scale);
    int addPrimitive(rigid::ShapeType type, float halfLength, float radius, float mass, const BodyOptions& options,
                     const math::Vec3& offset, const math::Quat& turn);
    std::size_t skippedShapes_ = 0;
};

}   // namespace physics
