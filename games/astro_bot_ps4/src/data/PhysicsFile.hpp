// Physics bodies of an object (.pfb, named by an object definition's IPhysics/File), as the
// engine's importer reads them (PhysicsImportPfb, 0xc9cc70). Little-endian, packed:
//
//   u32 version (1), 12 bytes zero
//   chunks: u32 type, u32 stride, u32 count, u32 0, then the chunk data
//     1  rigid bodies: `count` records of `stride` (0x280) bytes, each followed by its shapes
//        +0x000 char[256] name ("metalbeam")     +0x100 char[256] body name ("JsRigidBody_...")
//        +0x201 u8 flag (one file only)            +0x204 u32 motion type (0, 1, 2: meaning open)
//        +0x208 f32 position[3], +0x214 quat[4], +0x224 scale[3]: the node frame (the model's;
//              only the visual sync matrix uses it, 0xbd91a0 / 0xbdd350 desc +0xb0..+0xd0)
//        +0x230 f32 position[3], +0x23c quat[4]: the body frame (desc +0x90 / +0xa0): the rigid
//              body sits at placement x (placement scale * position, rotation); its shapes are
//              relative to it and scaled by the placement's scale alone (not the node scale)
//        +0x24c f32 mass, +0x254 restitution, +0x258 friction, +0x264 max linear velocity,
//        +0x268 max angular velocity, +0x26c linear damping, +0x274 angular damping (copied
//        into the body description by the importer 0xc9cc70; names by the physics library's
//        rigid body members, pairing to be confirmed in the body creation 0xcb5880. The
//        enemies' 60 rad/s matches the spin limit seen on thrown enemies). +0x250, +0x25c,
//        +0x260, +0x270: open.
//        +0x278 u32 shape stride (56), +0x27c u32 shape count
//        shape: u32 kind, f32 position[3], quat[4] (x y z w), kind data at +0x20:
//          0 sphere    +0x34 radius
//          1 box       +0x20 half extents[3]
//          2, 3        +0x2c half length, +0x30 radius, along local Y (cylinder verified
//                      against its definition's Size; capsule by the same field order)
//          4 convex    followed by a vertex block and an index block
//          6 mesh      followed by a vertex block and an index block
//          7 mesh      followed by one serialized-mesh block ("PELM", see CollisionMesh.hpp)
//        block: u32 kind (3 vertices, 4 indices, 6 serialized mesh), u32 element size,
//               u32 element count, u32 0, then size * count bytes
//     2  joints (`count` x `stride` bytes; six joint kinds, each naming two bodies: not read yet)
//     5  per-body overrides keyed by an FNV-1a-64 of the name (not read yet)
#pragma once

#include "data/Bytes.hpp"
#include "math/Transform.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace beacon::data {

struct LargeMesh;

enum class ShapeKind { Sphere, Box, Capsule, Cylinder, Convex, Mesh };

struct TriangleSoup {
    std::vector<math::Vec3> vertices;
    std::vector<std::uint32_t> indices;   // three per triangle
};

struct PhysicsShape {
    ShapeKind kind = ShapeKind::Box;
    std::uint32_t rawKind = 0;   // as stored (2 and 3 kept apart until their meaning is settled)
    math::Vec3 position;
    math::Quat rotation;
    math::Vec3 halfExtents;   // box
    float radius = 0;         // sphere, capsule, cylinder
    float halfLength = 0;     // capsule, cylinder (local Y axis: hypothesis)
    TriangleSoup geometry;    // convex hull or mesh, in shape space
    // Kind 7: the serialized mesh as the physics library keeps it (islands and trees).
    std::shared_ptr<const LargeMesh> largeMesh;
    std::size_t blobOffset = 0, blobSize = 0;   // kind 7: where the serialized mesh lies in the file
};

struct PhysicsBody {
    std::string name, bodyName;
    std::uint32_t motionType = 0;
    math::Vec3 bodyPosition;   // the rigid body's frame in the object
    math::Quat bodyRotation;
    math::Vec3 nodePosition, nodeScale{1, 1, 1};   // the model's frame (not used by the physics)
    math::Quat nodeRotation;
    float mass = 0;
    float restitution = 0, friction = 0;
    float maxLinearVelocity = 0;    // library units (world scale 3)
    float maxAngularVelocity = 0;   // rad/s
    float linearDamping = 1, angularDamping = 1;
    std::vector<PhysicsShape> shapes;
};

struct PhysicsFile {
    std::vector<PhysicsBody> bodies;
    std::size_t jointCount = 0;
};

std::optional<PhysicsFile> loadPhysicsFile(ByteView bytes, std::string* error = nullptr);

}   // namespace data
