// A world of rigid bodies stepped as the game's physics library does (findings in
// .analysis/docs/physics.md, "World step"). Units are the library's (world scale 3: positions and
// velocities; RigidWorld::kScale converts).
//
// Per step:
//   1. gravity on active bodies (applyExternalForce, 0xcf9820);
//   2. pairs: bodies whose boxes overlap, at least one of them movable (active / one-way);
//   3. kept pairs: manifold refresh against the current poses (0xe0d6e0);
//   4. detection per pair and shape pair into a 4-point cache, then each point into the
//      pair's manifold (0xdbe190 -> contact table -> 0xe0cee0);
//      Swept spheres (shape type 6) instead give candidates for the pair's swept list
//      (SweptContacts) in steps where the pair moves fast;
//   4b. contact reports for the game (0xcf6a10);
//   5. solver: set-up (restitution only during a manifold's first two updates) and the pair's
//      rolling-friction row, warm start, three iterations (points, then the rolling row),
//      velocity change added (0xdd2bb0);
//   6. integration (0xd99cb0);
//   7. position pass, two iterations (0xe69e00);
//   8. sleep (0xde0390): islands of this step's pairs between active / keyframe / one-way
//      bodies (0xcf6390, 0xe75750); a still body counts up, an island whose bodies are all awake
//      and past the count goes to sleep, a mixed island wakes up.
// Solver order: pairs with points, stably sorted by the two bodies' levels from the ground
// (breadth-first over the contacts from non-moving bodies); in each pair the body nearer the ground
// gets its inverse mass and inertia scaled by 1/1.6. The pair order before that sort is by body
// ids (lower id = A; hypothesis: the library's pair array order).
#pragma once

#include "data/CollisionMesh.hpp"
#include "physics/rigid/ContactManifold.hpp"
#include "physics/rigid/ContactSolver.hpp"
#include "physics/rigid/ConvexSolver.hpp"
#include "physics/rigid/RigidBody.hpp"
#include "physics/rigid/ShapeCasts.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <vector>

namespace beacon::physics::rigid {

// SweptSphere: the library's shape type 6, a sphere for swept (continuous) contacts only (a pfb
// sphere whose body record has byte +0x201 set gets one in addition to its sphere).
enum class ShapeType : std::uint8_t { Sphere = 0, Box = 1, Capsule = 2, Cylinder = 3, Hull = 4, Mesh = 5, SweptSphere = 6 };

struct Shape {
    ShapeType type = ShapeType::Sphere;
    Pose offset;                    // shape frame within the body (rotation may scale for meshes)
    math::Quat turn;                // the shape's rotation alone (shape +0x2c; turns mesh facet normals)
    float radius = 0;               // sphere, capsule, cylinder
    float halfLength = 0;           // capsule, cylinder (along the shape's x axis)
    math::Vec3 halfExtents;         // box
    std::vector<math::Vec3> hull;   // hull points
    std::vector<std::uint32_t> hullIndices;   // hull triangles (three corners each)
    const data::LargeMesh* mesh = nullptr;
    bool mirrored = false;          // hull / mesh scale turns it inside out
    // Shape +0x3c / +0x40: masks a query must match ((target & query self) and (query target &
    // self)), all bits by default.
    std::uint32_t filterSelf = 0xffffffffu, filterTarget = 0xffffffffu;
};

// The box of a shape at a pose (the broadphase's).
void shapeBounds(const Shape& shape, const Pose& pose, math::Vec3& min, math::Vec3& max);

// A query's closest hit (the library's world cast: 0xcf8080 over the broadphase trees, 0xdc9980
// per body and shape, the cast table per shape; see ShapeCasts.hpp).
struct QueryHit {
    int body = -1, shape = -1;
    float t = 1;                 // along the movement, 0..1
    math::Vec3 point, normal;    // on the shape; from the shape towards the ray
    int island = -1, facet = -1;   // mesh hits: the facet
};

// What a query may hit: bodies and shapes whose target mask meets `self` and whose self mask meets
// `target`, outside both groups.
struct QueryFilter {
    std::uint32_t self = 0xffffffffu, target = 0xffffffffu;
    std::uint16_t group[2] = {0xffff, 0xffff};
};

struct Body {
    RigidState state;
    RigidBody mass;
    std::vector<Shape> shapes;
    bool enabled = true;
};

struct WorldParams {
    float timeStep = 1.0f / 60.0f;                         // world +0x2760
    math::Vec3 gravity{0, -9.8f * 3.0f, 0};                // library units
    float positionBias = 0.8f;                             // world +0x2764
    int velocityIterations = 3, positionIterations = 2;    // world +0x2768.. (3, 3, 3, 2)
    float priorityMassRatio = 1.6f;                        // step parameters +0x64
    bool sleep = true;                                     // world flags +0x2780 bit 0 (game: 3)
    std::uint32_t sleepCount = 180;                        // world +0x2778
    float sleepVelocity = 0.1f;                            // world +0x277c (library units)
};

// A pair's contact points as reported to the game (0xcf6a10: copied from the manifold after
// detection, before the solver; world space, library units).
struct ContactReport {
    struct Point {
        math::Vec3 pointA, pointB;   // on body A, on body B
        math::Vec3 normal;           // from B towards A
        float distance = 0;
    };
    int bodyA = 0, bodyB = 0;
    int count = 0;
    Point points[ContactManifold::kMaxPoints];
};

// One contact of a body as the engine's query returns it (0xcb1420): the other body and, per
// point, the point on the queried body, the normal from the other body towards it, the distance.
struct BodyContact {
    int other = 0;
    int count = 0;
    struct Point {
        math::Vec3 position, normal;
        float distance = 0;
    } points[ContactManifold::kMaxPoints];
};

class RigidWorld {
public:
    static constexpr float kScale = 3.0f;   // world units -> library units

    explicit RigidWorld(const WorldParams& params = {}) : params_(params) {}

    int add(Body body);
    const WorldParams& params() const { return params_; }
    Body& body(int id) { return bodies_[std::size_t(id)]; }
    const Body& body(int id) const { return bodies_[std::size_t(id)]; }
    int size() const { return int(bodies_.size()); }

    void step();

    // This step's contact reports (pairs with points, in pair order).
    const std::vector<ContactReport>& reports() const { return reports_; }
    // The reports of one body, newest pair first (the library's per-body lists are built by
    // pushing each report in front), as the engine's query reads them.
    std::vector<BodyContact> contactsOf(int body) const;

    // The first hit of a sphere (radius 0: a ray) moving from `start` by `direction`, if any.
    // `accept` (optional) may refuse bodies, like the library's own callback slot (0xdc91f0
    // arguments 4 / 5, which the game's queries leave empty).
    std::optional<QueryHit> castSphere(const math::Vec3& start, const math::Vec3& direction, float radius,
                                       const QueryFilter& filter = {},
                                       const std::function<bool(int body)>& accept = {}) const;
    // The first hit of a ray (0xcf7900 -> 0xdc2d80 -> the ray table; front faces of meshes).
    std::optional<QueryHit> castRay(const math::Vec3& start, const math::Vec3& direction, const QueryFilter& filter = {},
                                    const std::function<bool(int body)>& accept = {}) const;
    // The same for a capsule (0xcf87d0 -> 0xdbc4c0 -> the convex cast table; ShapeCasts.hpp).
    std::optional<QueryHit> castCapsule(const CapsuleRay& capsule, const QueryFilter& filter = {},
                                        const std::function<bool(int body)>& accept = {}) const;

    // The manifold of a pair, if the two bodies are in contact (lower id first).
    const ContactManifold* manifold(int a, int b) const;
    template <class F>
    void forEachManifold(F f) const {
        for (const auto& [key, pair] : pairs_) {
            f(int(key >> 16), int(key & 0xffff), pair.manifold);
        }
    }

private:
    struct Pair {
        ContactManifold manifold;
        bool kept = false;
        float restitution = 0, friction = 0, rolling = 0;
        RollingRow rollingRow;   // kept across steps (its accumulated impulse)
        // The swept list (pair +0x0c count, +0x2c0 entries of 0x7c bytes), rebuilt by every
        // detection from the swept candidates.
        struct SweptPoint {
            float distance = 0;
            math::Vec3 normal, localA, localB;
            ContactSubData sub;
            ConstraintRow row;
        };
        std::vector<SweptPoint> swept;
    };

    struct PairKey {
        int a, b;
        Pair* pair;
    };

    // Body boxes (broadphase), kept while a fixed or keyframed body's pose is unchanged.
    struct CachedBox {
        math::Vec3 min, max, position;
        math::Quat orientation;
        bool valid = false;
    };
    void bodyBounds(std::size_t i, math::Vec3& min, math::Vec3& max) const;
    mutable std::vector<CachedBox> boxes_;

    void collectPairs();
    std::vector<std::uint8_t> groundLevels(const std::vector<const PairKey*>& order) const;
    void detect(int a, int b, Pair& pair);
    void solve();
    void updateSleep();

    WorldParams params_;
    std::vector<Body> bodies_;
    std::map<std::uint32_t, Pair> pairs_;
    std::vector<ContactReport> reports_;
};

}   // namespace beacon::physics::rigid
