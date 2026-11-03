// Static collision world: world-space triangles from placed physics bodies, in a bounding-volume
// tree, with the queries a character controller needs. This is Beacon's own engine service: the
// original runs an in-house rigid-body engine; only the collision geometry and per-object flags
// come from the game's data. Box, sphere, capsule, cylinder and hull shapes are triangulated so
// all queries share one path.
#pragma once

#include "data/PhysicsFile.hpp"
#include "math/Transform.hpp"
#include "physics/Surface.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace beacon::physics {

using math::Vec3;

struct Triangle {
    Vec3 a, b, c;
    Vec3 normal;   // unit, from the winding (a, b, c)
    Surface surface;
};

struct RayHit {
    float distance = 0;
    Vec3 point, normal;
    std::uint32_t triangle = 0;
};

struct SweepHit {
    float distance = 0;   // along the sweep direction until first contact
    Vec3 point;           // contact point on the surface
    Vec3 normal;          // from the surface towards the sphere centre at contact
    std::uint32_t triangle = 0;
    Vec3 faceNormal;      // the touched triangle's normal, on the sphere's side
};

struct Contact {
    Vec3 normal;   // push direction for the sphere
    float depth = 0;
    std::uint32_t triangle = 0;
};

class CollisionWorld {
public:
    // Adds a shape transformed into the world by `world` (placement * body * shape frames).
    void addShape(const data::PhysicsShape& shape, const math::Mat4& world, const Surface& surface);
    void addTriangle(const Vec3& a, const Vec3& b, const Vec3& c, const Surface& surface);
    // Moving bodies: triangles added between beginBody() and endBody() belong to a body kept out of
    // the static tree; setBodyTransform() places them (transform applied to their pose as added).
    std::uint32_t beginBody();
    void endBody();
    void setBodyTransform(std::uint32_t body, const math::Mat4& transform);
    std::size_t bodyCount() const { return bodies_.size(); }
    // The body a triangle belongs to (-1: static) and the body's last change of transform.
    int bodyOf(std::uint32_t triangle) const { return inBody_[triangle]; }
    const math::Mat4& bodyDelta(std::uint32_t body) const { return bodies_[body].delta; }
    // Builds the tree; call after adding and before querying.
    void build();
    void clear();

    std::optional<RayHit> raycast(const Vec3& origin, const Vec3& direction, float maxDistance,
                                  std::uint32_t mask = kHitAll) const;
    // Moves a sphere from `from` along unit `direction` up to `maxDistance`; first contact.
    std::optional<SweepHit> sweepSphere(const Vec3& from, float radius, const Vec3& direction, float maxDistance,
                                        std::uint32_t mask = kHitAll) const;
    // Triangles the sphere penetrates, with push-out directions.
    void overlapSphere(const Vec3& centre, float radius, std::vector<Contact>& out,
                       std::uint32_t mask = kHitAll) const;

    const std::vector<Triangle>& triangles() const { return triangles_; }

private:
    struct Node {
        Vec3 lo, hi;
        std::uint32_t first = 0, count = 0;   // leaf: triangle range in order_
        std::uint32_t left = 0, right = 0;    // inner: child nodes
    };
    std::uint32_t buildNode(std::uint32_t first, std::uint32_t count, int depth);
    template <typename Box, typename Leaf>
    void walk(const Box& overlaps, const Leaf& leaf) const;

    struct Body {
        std::vector<std::uint32_t> triangles;   // indices into triangles_
        std::vector<Triangle> rest;             // as added
        Vec3 lo, hi;
        math::Mat4 transform = math::identity(), delta = math::identity();
    };
    void updateBodyBounds(Body& b);

    std::vector<Triangle> triangles_;
    std::vector<int> inBody_;   // body index, -1 for static triangles
    std::vector<std::uint32_t> order_;
    std::vector<Node> nodes_;
    std::vector<Body> bodies_;
    int openBody_ = -1;
};

}   // namespace physics
