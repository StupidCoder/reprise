#include "physics/rigid/RigidWorld.hpp"

#include "math/Vector.hpp"
#include "physics/rigid/ContactCache.hpp"
#include "physics/rigid/MeshContacts.hpp"
#include "physics/rigid/ShapeCasts.hpp"
#include "physics/rigid/SweptContacts.hpp"
#include "physics/rigid/PrimitiveContacts.hpp"

#include <algorithm>
#include <cmath>
#include <memory>

namespace beacon::physics::rigid {

using math::Vec3;

namespace {

struct Box {
    Vec3 min, max;
};

bool overlap(const Box& a, const Box& b) {
    return a.min.x <= b.max.x && b.min.x <= a.max.x && a.min.y <= b.max.y && b.min.y <= a.max.y &&
           a.min.z <= b.max.z && b.min.z <= a.max.z;
}

Vec3 absVec(const Vec3& v) { return {std::fabs(v.x), std::fabs(v.y), std::fabs(v.z)}; }

Vec3 column(const Mat3& m, int c) {
    return c == 0 ? Vec3{m.r[0].x, m.r[1].x, m.r[2].x}
                  : c == 1 ? Vec3{m.r[0].y, m.r[1].y, m.r[2].y} : Vec3{m.r[0].z, m.r[1].z, m.r[2].z};
}

Box orientedBox(const Pose& p, const Vec3& half) {
    const Vec3 h = absVec(column(p.rotation, 0)) * half.x + absVec(column(p.rotation, 1)) * half.y +
                   absVec(column(p.rotation, 2)) * half.z;
    return {p.translation - h, p.translation + h};
}

Pose compose(const Pose& a, const Pose& b) {
    return {multiply(a.rotation, b.rotation), apply(a.rotation, b.translation) + a.translation};
}

Pose bodyPose(const RigidState& s) { return {rotationMatrix(s.orientation), s.position}; }

Vec3 transformPoint(const Pose& p, const Vec3& v) { return apply(p.rotation, v) + p.translation; }

Box shapeBox(const Shape& s, const Pose& world) {
    switch (s.type) {
    case ShapeType::Sphere:
    case ShapeType::SweptSphere:
        return orientedBox(world, {s.radius, s.radius, s.radius});
    case ShapeType::Box:
        return orientedBox(world, s.halfExtents);
    case ShapeType::Capsule:
        return orientedBox(world, {s.halfLength + s.radius, s.radius, s.radius});
    case ShapeType::Cylinder:
        return orientedBox(world, {s.halfLength, s.radius, s.radius});
    case ShapeType::Hull: {
        Box b{{1e30f, 1e30f, 1e30f}, {-1e30f, -1e30f, -1e30f}};
        for (const Vec3& p : s.hull) {
            const Vec3 w = transformPoint(world, p);
            b.min = {std::min(b.min.x, w.x), std::min(b.min.y, w.y), std::min(b.min.z, w.z)};
            b.max = {std::max(b.max.x, w.x), std::max(b.max.y, w.y), std::max(b.max.z, w.z)};
        }
        return b;
    }
    case ShapeType::Mesh: {
        const Box local = orientedBox({world.rotation, transformPoint(world, s.mesh->centre)}, s.mesh->halfExtent);
        return local;
    }
    }
    return {};
}

bool hasSweptShape(const Body& b) {
    return std::any_of(b.shapes.begin(), b.shapes.end(), [](const Shape& s) { return s.type == ShapeType::SweptSphere; });
}

// Where a body would be after dt (0xdbe190, 0xdb6020): moved by dt v, turned by one Euler step
// of its spin, q + dt/2 (w, 0) q, normalised.
Pose predictedPose(const RigidState& s, float dt) {
    const math::Quat q = s.orientation;
    const Vec3 w = s.angularVelocity;
    const math::Quat wq{w.x * q.w + w.y * q.z - w.z * q.y, w.y * q.w + w.z * q.x - w.x * q.z,
                        w.z * q.w + w.x * q.y - w.y * q.x, -(w.x * q.x + w.y * q.y + w.z * q.z)};
    const math::Quat r{q.x + dt * wq.x * 0.5f, q.y + dt * wq.y * 0.5f, q.z + dt * wq.z * 0.5f, q.w + dt * wq.w * 0.5f};
    const float l = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z + r.w * r.w);
    return {rotationMatrix({r.x / l, r.y / l, r.z / l, r.w / l}), s.position + s.linearVelocity * dt};
}

// A body's box; with a swept shape the union of its boxes now and at the predicted pose (0xdb6020).
Box bodyBox(const Body& b, float dt) {
    Box box{{1e30f, 1e30f, 1e30f}, {-1e30f, -1e30f, -1e30f}};
    const bool swept = hasSweptShape(b);
    for (int k = 0; k < (swept ? 2 : 1); ++k) {
        const Pose p = k == 0 ? bodyPose(b.state) : predictedPose(b.state, dt);
        for (const Shape& s : b.shapes) {
            const Box sb = shapeBox(s, compose(p, s.offset));
            box.min = {std::min(box.min.x, sb.min.x), std::min(box.min.y, sb.min.y), std::min(box.min.z, sb.min.z)};
            box.max = {std::max(box.max.x, sb.max.x), std::max(box.max.y, sb.max.y), std::max(box.max.z, sb.max.z)};
        }
    }
    return box;
}

SweptPrimitive sweptPrimitive(const Shape& s) {
    SweptPrimitive p;
    p.kind = SweptPrimitive::Kind(int(s.type));
    p.radius = s.radius;
    p.halfLength = s.halfLength;
    p.halfExtents = s.halfExtents;
    p.mesh = s.mesh;
    p.mirrored = s.mirrored;
    p.hull = s.hull.data();
    p.hullCount = int(s.hull.size());
    return p;
}

bool movable(MotionType m) { return m == MotionType::Active || m == MotionType::OneWay; }

bool sharesGroup(const std::uint16_t (&a)[2], const std::uint16_t (&b)[2]) {
    for (std::uint16_t x : a) {
        for (std::uint16_t y : b) {
            if (x == y && x != 0xffff) {
                return true;
            }
        }
    }
    return false;
}

// Bodies the broadphase pairs (0xdb93f0 and its siblings): motion types allowed by table 0xbfafbe
// (fixed, keyframe: not with fixed or keyframe; one-way not with one-way; trigger not with
// trigger), each one's self mask meeting the other's target mask, no shared group.
bool pairable(const RigidState& a, const RigidState& b) {
    const unsigned ta = unsigned(a.motion), tb = unsigned(b.motion);
    if ((0xbfafbeu >> ((24 - tb - 5 * ta) & 0x1f) & 1) == 0) {
        return false;
    }
    if ((a.filterSelf & b.filterTarget) == 0 || (b.filterSelf & a.filterTarget) == 0) {
        return false;
    }
    return !sharesGroup(a.group, b.group);
}

std::unique_ptr<SupportShape> supportOf(const Shape& s) {
    switch (s.type) {
    case ShapeType::Sphere:
        return std::make_unique<SphereSupport>(s.radius);
    case ShapeType::Box:
        return std::make_unique<BoxSupport>(s.halfExtents);
    case ShapeType::Capsule:
        return std::make_unique<CapsuleSupport>(s.halfLength, s.radius);
    case ShapeType::Cylinder:
        return std::make_unique<CylinderSupport>(s.halfLength, s.radius);
    case ShapeType::Hull:
        return std::make_unique<HullSupport>(s.hull.data(), int(s.hull.size()));
    case ShapeType::Mesh:
    case ShapeType::SweptSphere:
        break;
    }
    return nullptr;
}

// The dedicated math for a primitive pair, with `first` the shape the math treats as its A
// (sphere-sphere, box-sphere, capsule-sphere, box-capsule, capsule-capsule, box-box). Normal from
// `first` towards the other.
bool dedicatedPair(const Shape& first, const Pose& pf, const Shape& second, const Pose& ps, ConvexContact& c) {
    using T = ShapeType;
    if (first.type == T::Sphere && second.type == T::Sphere) {
        c = sphereSphere(first.radius, pf, second.radius, ps);
    } else if (first.type == T::Box && second.type == T::Sphere) {
        c = boxSphere(first.halfExtents, pf, second.radius, ps);
    } else if (first.type == T::Capsule && second.type == T::Sphere) {
        c = capsuleSphere(first.halfLength, first.radius, pf, second.radius, ps);
    } else if (first.type == T::Box && second.type == T::Capsule) {
        c = boxCapsule(first.halfExtents, pf, second.halfLength, second.radius, ps);
    } else if (first.type == T::Capsule && second.type == T::Capsule) {
        c = capsuleCapsule(first.halfLength, first.radius, pf, second.halfLength, second.radius, ps);
    } else if (first.type == T::Box && second.type == T::Box) {
        c = boxBox(first.halfExtents, pf, second.halfExtents, ps);
    } else {
        return false;
    }
    return true;
}

// Contacts of one shape pair into the cache: points in the shapes' frames, normals in world space
// from B towards A, distances in world space. Returns false for pairs not ported yet.
bool shapeContacts(const Shape& a, const Pose& pa, const Shape& b, const Pose& pb, ContactCache& out) {
    if (a.type == ShapeType::SweptSphere || b.type == ShapeType::SweptSphere) {
        return false;   // swept spheres only give swept candidates
    }
    if (a.type == ShapeType::Mesh || b.type == ShapeType::Mesh) {
        const bool meshIsA = a.type == ShapeType::Mesh;
        const Shape& mesh = meshIsA ? a : b;
        const Shape& prim = meshIsA ? b : a;
        const MeshPlacement placement{mesh.mesh, meshIsA ? pa : pb, mesh.mirrored};
        const Pose& primPose = meshIsA ? pb : pa;
        ContactCache c;
        if (prim.type == ShapeType::Cylinder) {
            cylinderMeshContacts(placement, prim.halfLength, prim.radius, primPose, c);
        } else if (prim.type == ShapeType::Sphere) {
            sphereMeshContacts(placement, prim.radius, primPose, c);
        } else if (prim.type == ShapeType::Box) {
            boxMeshContacts(placement, prim.halfExtents, primPose, c);
        } else if (prim.type == ShapeType::Capsule) {
            capsuleMeshContacts(placement, prim.halfLength, prim.radius, primPose, c);
        } else if (prim.type == ShapeType::Hull) {
            hullMeshContacts(placement, prim.hull.data(), int(prim.hull.size()), primPose, c);
        } else {
            return false;   // meshes against meshes: not ported
        }
        for (int i = 0; i < c.size(); ++i) {
            const CachedContact& p = c[i];
            if (meshIsA) {
                out.add(p.distance, p.normal, p.pointA, p.pointB, p.subA, p.subB, p.extra);
            } else {
                out.add(p.distance, p.normal * -1.0f, p.pointB, p.pointA, p.subB, p.subA, p.extra);
            }
        }
        return true;
    }
    // The detection passes the pair functions a threshold of 0 (0xdbe190): a result is kept only
    // when its distance is below it, i.e. the shapes overlap (0xe0eb70 and the others).
    constexpr float kThreshold = 0;
    ConvexContact c;
    if (dedicatedPair(a, pa, b, pb, c)) {
        // Math from A to B: the stored normal points from B to A.
        if (c.distance < kThreshold) {
            out.add(c.distance, c.normal * -1.0f, c.pointA, c.pointB, 1, 0);
        }
        return true;
    }
    if (dedicatedPair(b, pb, a, pa, c)) {
        if (c.distance < kThreshold) {
            out.add(c.distance, c.normal, c.pointB, c.pointA, 1, 0);
        }
        return true;
    }
    // Pairs with a cylinder or a hull: the full convex search with the pair's own A and B
    // (0xe0f310.., 0xe101c0.., 0xe11ab0..).
    const auto sa = supportOf(a), sb = supportOf(b);
    const auto mc = penetrationFull(*sa, pa, *sb, pb);
    if (!mc) {
        return true;
    }
    Vec3 n = apply(pa.rotation, mc->normal);
    n = n * (1.0f / std::sqrt(math::dot(n, n)));
    const float d = math::dot(transformPoint(pa, mc->pointA) - transformPoint(pb, mc->pointB), n);
    if (mc->distance < kThreshold) {
        out.add(d, n, mc->pointA, mc->pointB, 1, 0);
    }
    return true;
}

// Pairs the solver takes (0xddcc80): motion-type combinations of mask 0xaf2b80 at bit
// 24 - typeA - 5 typeB (fixed / one-way with active, active with all but triggers, keyframe with
// active / one-way, one-way with fixed / active / keyframe), not two sleeping bodies and not a
// sleeping body against a fixed one.
bool solvedPair(const RigidState& a, const RigidState& b) {
    const int ta = int(a.motion), tb = int(b.motion);
    if ((0xaf2b80u >> ((24 - ta - 5 * tb) & 0x1f) & 1) == 0) {
        return false;
    }
    if (a.sleeping && b.sleeping) {
        return false;
    }
    if ((ta == 0 && b.sleeping) || (a.sleeping && tb == 0)) {
        return false;
    }
    return true;
}

}   // namespace

std::vector<BodyContact> RigidWorld::contactsOf(int body) const {
    std::vector<BodyContact> out;
    for (auto it = reports_.rbegin(); it != reports_.rend(); ++it) {
        const ContactReport& r = *it;
        if (r.bodyA != body && r.bodyB != body) {
            continue;
        }
        const bool isA = r.bodyA == body;
        BodyContact c;
        c.other = isA ? r.bodyB : r.bodyA;
        c.count = r.count;
        for (int i = 0; i < r.count; ++i) {
            const ContactReport::Point& p = r.points[i];
            c.points[i] = {isA ? p.pointA : p.pointB, isA ? p.normal : p.normal * -1.0f, p.distance};
        }
        out.push_back(c);
    }
    return out;
}

int RigidWorld::add(Body body) {
    bodies_.push_back(std::move(body));
    return int(bodies_.size()) - 1;
}

const ContactManifold* RigidWorld::manifold(int a, int b) const {
    if (a > b) {
        std::swap(a, b);
    }
    const auto it = pairs_.find(std::uint32_t(a) << 16 | std::uint32_t(b));
    return it == pairs_.end() ? nullptr : &it->second.manifold;
}

void RigidWorld::bodyBounds(std::size_t i, Vec3& min, Vec3& max) const {
    if (boxes_.size() < bodies_.size()) {
        boxes_.resize(bodies_.size());
    }
    const Body& b = bodies_[i];
    CachedBox& c = boxes_[i];
    const bool still = (b.state.motion == MotionType::Fixed || b.state.motion == MotionType::Keyframe) &&
                       !hasSweptShape(b);
    const math::Quat& q = b.state.orientation;
    if (!(still && c.valid && c.position.x == b.state.position.x && c.position.y == b.state.position.y &&
          c.position.z == b.state.position.z && c.orientation.x == q.x && c.orientation.y == q.y &&
          c.orientation.z == q.z && c.orientation.w == q.w)) {
        const Box box = bodyBox(b, params_.timeStep);
        c = {box.min, box.max, b.state.position, q, still};
    }
    min = c.min, max = c.max;
}

void RigidWorld::collectPairs() {
    std::vector<Box> boxes(bodies_.size());
    for (std::size_t i = 0; i < bodies_.size(); ++i) {
        bodyBounds(i, boxes[i].min, boxes[i].max);
    }
    for (auto& [key, pair] : pairs_) {
        pair.kept = false;
    }
    // Fixed and keyframed bodies never pair with each other (motion table 0xbfafbe), so only pairs
    // with another body are looked for (the pairs are keyed by body ids: the order found does not
    // matter).
    const auto still = [&](const Body& b) {
        return b.state.motion == MotionType::Fixed || b.state.motion == MotionType::Keyframe;
    };
    std::vector<std::uint32_t> movers;
    for (std::size_t i = 0; i < bodies_.size(); ++i) {
        if (bodies_[i].enabled && !still(bodies_[i])) {
            movers.push_back(std::uint32_t(i));
        }
    }
    std::vector<std::pair<std::uint32_t, std::uint32_t>> candidates;
    for (std::uint32_t m : movers) {
        for (std::size_t k = 0; k < bodies_.size(); ++k) {
            if (k == m || !bodies_[k].enabled || (!still(bodies_[k]) && k < m)) {
                continue;   // mover pairs once
            }
            candidates.emplace_back(std::min(m, std::uint32_t(k)), std::max(m, std::uint32_t(k)));
        }
    }
    for (const auto& [i, j] : candidates) {
        const Body& a = bodies_[i];
        const Body& b = bodies_[j];
        if (!pairable(a.state, b.state)) {
            continue;
        }
        if (a.state.sleeping && b.state.sleeping) {
            continue;
        }
        if (!overlap(boxes[i], boxes[j])) {
            continue;
        }
        const std::uint32_t key = std::uint32_t(i) << 16 | std::uint32_t(j);
        auto [it, fresh] = pairs_.try_emplace(key);
        if (fresh) {
            it->second.manifold.reset(std::uint16_t(i), std::uint16_t(j));
        }
        it->second.kept = true;
    }
    for (auto it = pairs_.begin(); it != pairs_.end();) {
        it = it->second.kept ? std::next(it) : pairs_.erase(it);
    }
}

void RigidWorld::detect(int ia, int ib, Pair& pair) {
    Body& a = bodies_[std::size_t(ia)];
    Body& b = bodies_[std::size_t(ib)];
    const Pose pa = bodyPose(a.state), pb = bodyPose(b.state);
    // Swept shapes take part only while the pair moves fast: relative spin squared above 0.05 or
    // relative velocity squared above 0.25 (0xdbe190).
    bool sweeping = false;
    Pose nextA, nextB;
    if (hasSweptShape(a) || hasSweptShape(b)) {
        const Vec3 dv = a.state.linearVelocity - b.state.linearVelocity;
        const Vec3 dw = a.state.angularVelocity - b.state.angularVelocity;
        sweeping = 0.05f < math::dot(dw, dw) || 0.25f < math::dot(dv, dv);
        if (sweeping) {
            nextA = predictedPose(a.state, params_.timeStep);
            nextB = predictedPose(b.state, params_.timeStep);
        }
    }
    ContactCache cache;
    std::vector<CachedContact> candidates;
    for (std::size_t i = 0; i < a.shapes.size(); ++i) {
        const Shape& sa = a.shapes[i];
        const Pose wa = compose(pa, sa.offset);
        for (std::size_t j = 0; j < b.shapes.size(); ++j) {
            const Shape& sb = b.shapes[j];
            const Pose wb = compose(pb, sb.offset);
            if (sa.type == ShapeType::SweptSphere || sb.type == ShapeType::SweptSphere) {
                if (!sweeping) {
                    continue;
                }
                std::vector<CachedContact> found;
                sweptShapeContacts(sweptPrimitive(sa), wa, compose(nextA, sa.offset), std::uint32_t(i), sweptPrimitive(sb),
                                   wb, compose(nextB, sb.offset), std::uint32_t(j), found);
                for (CachedContact c : found) {
                    c.pointA = transformPoint(sa.offset, c.pointA);
                    c.pointB = transformPoint(sb.offset, c.pointB);
                    keepSweptCandidate(candidates, c);
                }
                continue;
            }
            ContactCache shapeCache;
            if (!shapeContacts(sa, wa, sb, wb, shapeCache)) {
                continue;
            }
            // Points into the bodies' frames.
            for (int k = 0; k < shapeCache.size(); ++k) {
                const CachedContact& c = shapeCache[k];
                cache.add(c.distance, c.normal, transformPoint(sa.offset, c.pointA),
                          transformPoint(sb.offset, c.pointB), c.subA, c.subB, c.extra);
            }
        }
    }
    const auto subData = [](const CachedContact& c) {
        ContactSubData sub;
        sub.a[0] = std::uint32_t(c.subA), sub.a[1] = std::uint32_t(c.subA >> 32);
        sub.b[0] = std::uint32_t(c.subB), sub.b[1] = std::uint32_t(c.subB >> 32);
        return sub;
    };
    for (int i = 0; i < cache.size(); ++i) {
        const CachedContact& c = cache[i];
        pair.manifold.addPoint(c.distance, c.normal, c.pointA, c.pointB, subData(c));
    }
    // The swept list (0xe0db70, 0xe0d9d0): rebuilt from this detection's candidates; a candidate
    // within 1e-4 (squared, both ends) of a listed one is dropped. Bodies get the swept flag when
    // both move (active, keyframe, one-way) and carry a swept shape, or when a candidate's sweep
    // overlapped (flag 0x10 on the swept side).
    pair.swept.clear();
    if (!candidates.empty()) {
        const auto takesPart = [](MotionType m) {
            return m == MotionType::Active || m == MotionType::Keyframe || m == MotionType::OneWay;
        };
        if (takesPart(a.state.motion) && takesPart(b.state.motion)) {
            a.state.swept = a.state.swept || hasSweptShape(a);
            b.state.swept = b.state.swept || hasSweptShape(b);
        }
        for (const CachedContact& c : candidates) {
            if (c.subA & 0x10) {
                b.state.swept = true;
            }
            if (c.subB & 0x10) {
                a.state.swept = true;
            }
            const bool listed = std::any_of(pair.swept.begin(), pair.swept.end(), [&](const Pair::SweptPoint& p) {
                const Vec3 da = p.localA - c.pointA, db = p.localB - c.pointB;
                return math::dot(da, da) < 1e-4f && math::dot(db, db) < 1e-4f;
            });
            if (!listed) {
                Pair::SweptPoint p;
                p.distance = c.distance;
                p.normal = c.normal;
                p.localA = c.pointA;
                p.localB = c.pointB;
                p.sub = subData(c);
                p.row.normal = c.normal;
                pair.swept.push_back(p);
            }
        }
    }
    pair.restitution = 0.5f * (a.mass.restitution + b.mass.restitution);
    pair.friction = std::sqrt(a.mass.friction * b.mass.friction);
    pair.rolling = 0.5f * (a.mass.rollingFriction + b.mass.rollingFriction);
}

// Levels from the ground (0xddd0b0 / 0xddd460): bodies that do not move (fixed, keyframe,
// trigger) are level 0; a breadth-first walk over the contact pairs (neighbours in pair order)
// gives each reached movable body its distance; unreached bodies keep 255.
std::vector<std::uint8_t> RigidWorld::groundLevels(const std::vector<const PairKey*>& order) const {
    const std::size_t n = bodies_.size();
    std::vector<std::vector<int>> neighbours(n);
    for (const PairKey* k : order) {
        neighbours[std::size_t(k->a)].push_back(k->b);
        neighbours[std::size_t(k->b)].push_back(k->a);
    }
    std::vector<std::uint8_t> level(n, 0xff);
    std::vector<int> frontier;
    for (std::size_t i = 0; i < n; ++i) {
        if (!movable(bodies_[i].state.motion)) {
            frontier.push_back(int(i));
        }
    }
    std::uint8_t current = 0, next = 1;
    while (!frontier.empty()) {
        std::vector<int> reached;
        for (int u : frontier) {
            level[std::size_t(u)] = current;
            for (int v : neighbours[std::size_t(u)]) {
                if (next < level[std::size_t(v)]) {
                    level[std::size_t(v)] = next;
                    reached.push_back(v);
                }
            }
        }
        if (next == 0xff) {
            break;
        }
        frontier.swap(reached);
        current = next++;
    }
    return level;
}

void RigidWorld::solve() {
    std::vector<SolverBody> solverBodies(bodies_.size());
    for (std::size_t i = 0; i < bodies_.size(); ++i) {
        solverBodies[i] = makeSolverBody(bodies_[i].state, bodies_[i].mass);
    }
    ContactSolverParams p;
    p.timeStep = params_.timeStep;
    p.positionBias = params_.positionBias;

    // Pairs with contact points, in pair order; then stably ordered by the sum of the two bodies'
    // levels (0xde0270), so contacts nearer the ground are solved first.
    std::vector<PairKey> keys;
    for (auto& [key, pair] : pairs_) {
        const RigidState& a = bodies_[key >> 16].state;
        const RigidState& b = bodies_[key & 0xffff].state;
        if ((pair.manifold.size() > 0 || !pair.swept.empty()) && solvedPair(a, b)) {
            keys.push_back({int(key >> 16), int(key & 0xffff), &pair});
        }
    }
    std::vector<const PairKey*> order;
    for (const PairKey& k : keys) {
        order.push_back(&k);
    }
    const std::vector<std::uint8_t> level = groundLevels(order);
    std::stable_sort(order.begin(), order.end(), [&](const PairKey* x, const PairKey* y) {
        return level[std::size_t(x->a)] + level[std::size_t(x->b)] < level[std::size_t(y->a)] + level[std::size_t(y->b)];
    });

    // Within a pair the body nearer the ground counts as heavier: its inverse mass and inertia
    // are scaled by 1 / max(1.6, 1) (step parameter +0x64).
    const float heavier = 1.0f / std::max(params_.priorityMassRatio, 1.0f);
    auto withPriority = [&](const PairKey& k, auto&& f) {
        SolverBody& sa = solverBodies[std::size_t(k.a)];
        SolverBody& sb = solverBodies[std::size_t(k.b)];
        SolverBody* scaled = level[std::size_t(k.a)] < level[std::size_t(k.b)]   ? &sa
                             : level[std::size_t(k.b)] < level[std::size_t(k.a)] ? &sb
                                                                                 : nullptr;
        if (!scaled) {
            f(sa, sb);
            return;
        }
        const float massInv = scaled->massInv;
        const Mat3 inertiaInv = scaled->inertiaInv;
        scaled->massInv = massInv * heavier;
        for (Vec3& row : scaled->inertiaInv.r) {
            row = row * heavier;
        }
        f(sa, sb);
        scaled->massInv = massInv;
        scaled->inertiaInv = inertiaInv;
    };

    for (const PairKey* k : order) {
        ContactManifold& m = k->pair->manifold;
        // Restitution only while the contact is new (manifold duration < 2).
        const float e = m.duration() < 2 ? k->pair->restitution : 0.0f;
        withPriority(*k, [&](SolverBody& sa, SolverBody& sb) {
            for (int i = 0; i < m.size(); ++i) {
                ContactPoint& cp = m.point(i);
                setupContactPoint(cp, e, bodies_[std::size_t(k->a)].state, bodies_[std::size_t(k->b)].state, sa, sb, p);
                warmStartContactPoint(cp, sa, sb);
            }
            // The swept list, only in steps where one of the bodies has the swept flag.
            const RigidState& stA = bodies_[std::size_t(k->a)].state;
            const RigidState& stB = bodies_[std::size_t(k->b)].state;
            if (stA.swept || stB.swept) {
                for (Pair::SweptPoint& sp : k->pair->swept) {
                    setupSweptPoint(sp.row, sp.distance, sp.normal, sp.localA, sp.localB, p.timeStep, stA, stB, sa, sb);
                }
            } else {
                k->pair->swept.clear();
            }
            setupRolling(k->pair->rollingRow, k->pair->rolling, bodies_[std::size_t(k->a)].state,
                         bodies_[std::size_t(k->b)].state, sa, sb);
            warmStartRolling(k->pair->rollingRow, sa, sb);
        });
    }
    for (int it = 0; it < params_.velocityIterations; ++it) {
        for (const PairKey* k : order) {
            withPriority(*k, [&](SolverBody& sa, SolverBody& sb) {
                for (int i = 0; i < k->pair->manifold.size(); ++i) {
                    solveContactPoint(k->pair->manifold.point(i), sa, sb, k->pair->friction);
                }
                for (Pair::SweptPoint& sp : k->pair->swept) {
                    solveSweptPoint(sp.row, sp.localA, sp.localB, sa, sb);
                }
                solveRolling(k->pair->rollingRow, sa, sb);
            });
        }
    }
    // Velocity change and damping for every body (0xdd2bb0 after the iterations):
    // v = linear damping (v + dv), w = angular damping (w + dw) (state +0x18 / +0x1c).
    for (std::size_t i = 0; i < bodies_.size(); ++i) {
        RigidState& s = bodies_[i].state;
        s.linearVelocity = (s.linearVelocity + solverBodies[i].deltaLinearVelocity) * s.linearDamping;
        s.angularVelocity = (s.angularVelocity + solverBodies[i].deltaAngularVelocity) * s.angularDamping;
    }
    for (Body& b : bodies_) {
        integrate(b.state, params_.timeStep);
        b.state.swept = false;
    }
    for (int it = 0; it < params_.positionIterations; ++it) {
        for (const PairKey* k : order) {
            withPriority(*k, [&](SolverBody& sa, SolverBody& sb) {
                for (int i = 0; i < k->pair->manifold.size(); ++i) {
                    correctContactPosition(k->pair->manifold.point(i), bodies_[std::size_t(k->a)].state,
                                           bodies_[std::size_t(k->b)].state, sa, sb, params_.positionBias);
                }
            });
        }
    }
}

void RigidWorld::updateSleep() {
    // Bodies that take part: active, keyframe, one-way (mask 0xe).
    auto takesPart = [](MotionType m) {
        return m == MotionType::Active || m == MotionType::Keyframe || m == MotionType::OneWay;
    };
    const float limit = params_.sleepVelocity * params_.sleepVelocity;
    for (Body& b : bodies_) {
        RigidState& s = b.state;
        if (!b.enabled || !takesPart(s.motion) || s.sleeping) {
            continue;
        }
        if (math::dot(s.linearVelocity, s.linearVelocity) < limit &&
            math::dot(s.angularVelocity, s.angularVelocity) < limit) {
            if (s.sleepCount != 0xffff) {
                ++s.sleepCount;
            }
        } else {
            s.sleepCount = 0;
        }
    }
    // Islands: bodies in this step's pairs, joined when both take part.
    const std::size_t n = bodies_.size();
    std::vector<int> parent(n, -1);   // -1: in no pair
    auto find = [&](int x) {
        while (parent[std::size_t(x)] != x) {
            parent[std::size_t(x)] = parent[std::size_t(parent[std::size_t(x)])];
            x = parent[std::size_t(x)];
        }
        return x;
    };
    for (const auto& [key, pair] : pairs_) {
        const int a = int(key >> 16), b = int(key & 0xffff);
        for (int x : {a, b}) {
            if (parent[std::size_t(x)] < 0) {
                parent[std::size_t(x)] = x;
            }
        }
        if (takesPart(bodies_[std::size_t(a)].state.motion) && takesPart(bodies_[std::size_t(b)].state.motion)) {
            parent[std::size_t(find(a))] = find(b);
        }
    }
    struct Counts {
        int asleep = 0, awake = 0, ready = 0;
    };
    std::vector<Counts> counts(n);
    for (std::size_t i = 0; i < n; ++i) {
        const RigidState& s = bodies_[i].state;
        if (parent[i] < 0 || !takesPart(s.motion)) {
            continue;
        }
        Counts& c = counts[std::size_t(find(int(i)))];
        if (s.sleeping) {
            ++c.asleep;
        } else {
            ++c.awake;
            c.ready += s.sleepCount > params_.sleepCount ? 1 : 0;
        }
    }
    for (std::size_t i = 0; i < n; ++i) {
        RigidState& s = bodies_[i].state;
        if (parent[i] < 0 || !takesPart(s.motion)) {
            continue;
        }
        const Counts& c = counts[std::size_t(find(int(i)))];
        if (c.ready >= 1 && c.ready == c.asleep + c.awake) {
            // 0xd998a0: not for fixed or trigger bodies, only those that may sleep.
            if (s.useSleep && !s.sleeping) {
                s.sleeping = true;
                s.sleepCount = 0;
            }
        } else if (c.asleep > 0 && c.awake > 0) {
            s.sleeping = false;
            s.sleepCount = 0;
        }
    }
}

void RigidWorld::step() {
    for (Body& b : bodies_) {
        if (b.enabled) {
            applyExternalForce(b.state, b.mass, params_.gravity * b.mass.mass, {}, params_.timeStep);
        }
    }
    collectPairs();
    for (auto& [key, pair] : pairs_) {
        const RigidState& a = bodies_[key >> 16].state;
        const RigidState& b = bodies_[key & 0xffff].state;
        pair.manifold.refresh(a.position, a.orientation, b.position, b.orientation);
        detect(int(key >> 16), int(key & 0xffff), pair);
    }
    reports_.clear();
    for (const auto& [key, pair] : pairs_) {
        const ContactManifold& m = pair.manifold;
        if (m.size() == 0) {
            continue;
        }
        ContactReport r;
        r.bodyA = int(key >> 16);
        r.bodyB = int(key & 0xffff);
        const Pose pa = bodyPose(bodies_[std::size_t(r.bodyA)].state);
        const Pose pb = bodyPose(bodies_[std::size_t(r.bodyB)].state);
        for (int i = 0; i < m.size(); ++i) {
            const ContactPoint& cp = m.point(i);
            r.points[i] = {transformPoint(pa, cp.localPointA), transformPoint(pb, cp.localPointB), cp.normal(),
                           cp.distance};
        }
        r.count = m.size();
        reports_.push_back(r);
    }
    solve();
    if (params_.sleep) {
        updateSleep();
    }
}

void shapeBounds(const Shape& shape, const Pose& pose, Vec3& min, Vec3& max) {
    const Box b = shapeBox(shape, pose);
    min = b.min, max = b.max;
}

namespace {

// The swept box of a movement (start to start + direction) grown by `grow` per axis.
Box sweptBox(const Vec3& start, const Vec3& direction, const Vec3& grow) {
    const Vec3 end = start + direction;
    return {Vec3{std::min(start.x, end.x), std::min(start.y, end.y), std::min(start.z, end.z)} - grow,
            Vec3{std::max(start.x, end.x), std::max(start.y, end.y), std::max(start.z, end.z)} + grow};
}

// The nearest hit over the bodies and shapes the filter lets through (0xdc9980 / 0xdbc4c0).
template <typename Bounds, typename CastShape>
std::optional<QueryHit> castShapes(const std::vector<Body>& bodies_, const Bounds& bounds, const Box& sweep,
                                   const QueryFilter& filter, const std::function<bool(int body)>& accept,
                                   const CastShape& castShape) {
    CastResult best;
    QueryHit hit;
    for (std::size_t i = 0; i < bodies_.size(); ++i) {
        const Body& b = bodies_[i];
        if (!b.enabled || (b.state.filterTarget & filter.self) == 0 || (filter.target & b.state.filterSelf) == 0 ||
            sharesGroup(b.state.group, filter.group) || !overlap(bounds(i), sweep) ||
            (accept && !accept(int(i)))) {
            continue;
        }
        const Pose pose = bodyPose(b.state);
        for (std::size_t k = 0; k < b.shapes.size(); ++k) {
            const Shape& s = b.shapes[k];
            if ((s.filterTarget & filter.self) == 0 || (filter.target & s.filterSelf) == 0) {
                continue;
            }
            if (castShape(s, compose(pose, s.offset), best)) {
                hit.body = int(i);
                hit.shape = int(k);
            }
        }
    }
    if (hit.body < 0) {
        return std::nullopt;
    }
    hit.t = best.t;
    hit.point = best.point;
    hit.normal = best.normal;
    hit.island = best.island;
    hit.facet = best.facet;
    return hit;
}

}   // namespace

std::optional<QueryHit> RigidWorld::castSphere(const Vec3& start, const Vec3& direction, float radius,
                                               const QueryFilter& filter,
                                               const std::function<bool(int body)>& accept) const {
    const CastRay ray{start, direction, radius};
    return castShapes(bodies_, [this](std::size_t i) { Box b; bodyBounds(i, b.min, b.max); return b; }, sweptBox(start, direction, {radius, radius, radius}), filter, accept,
                      [&](const Shape& s, const Pose& world, CastResult& best) {
                          switch (s.type) {
                          case ShapeType::Sphere:
                              return physics::rigid::castSphere(s.radius, world, ray, best);
                          case ShapeType::Box:
                              return castBox(s.halfExtents, world, ray, best);
                          case ShapeType::Capsule:
                              return physics::rigid::castCapsule(s.halfLength, s.radius, world, ray, best);
                          case ShapeType::Cylinder:
                              return castCylinder(s.halfLength, s.radius, world, ray, best);
                          case ShapeType::Hull:
                              return castHull({s.hull.data(), int(s.hull.size()), s.hullIndices.data(),
                                               int(s.hullIndices.size()), s.mirrored},
                                              world, ray, best);
                          case ShapeType::Mesh:
                              return castMesh({s.mesh, world, s.mirrored}, ray, best);
                          case ShapeType::SweptSphere:
                              break;   // never hit by queries (the cast table's empty entry)
                          }
                          return false;
                      });
}

std::optional<QueryHit> RigidWorld::castRay(const Vec3& start, const Vec3& direction, const QueryFilter& filter,
                                            const std::function<bool(int body)>& accept) const {
    const CastRay ray{start, direction, 0};
    return castShapes(bodies_, [this](std::size_t i) { Box b; bodyBounds(i, b.min, b.max); return b; }, sweptBox(start, direction, {}), filter, accept,
                      [&](const Shape& s, const Pose& world, CastResult& best) {
                          switch (s.type) {
                          case ShapeType::Sphere:
                              return rayCastSphere(s.radius, world, ray, best);
                          case ShapeType::Box:
                              return rayCastBox(s.halfExtents, world, ray, best);
                          case ShapeType::Capsule:
                              return rayCastCapsule(s.halfLength, s.radius, world, ray, best);
                          case ShapeType::Cylinder:
                              return rayCastCylinder(s.halfLength, s.radius, world, ray, best);
                          case ShapeType::Hull:
                              return rayCastHull({s.hull.data(), int(s.hull.size()), s.hullIndices.data(),
                                                  int(s.hullIndices.size()), s.mirrored},
                                                 world, ray, best);
                          case ShapeType::Mesh:
                              return rayCastMesh({s.mesh, world, s.mirrored}, ray, best);
                          case ShapeType::SweptSphere:
                              break;
                          }
                          return false;
                      });
}

std::optional<QueryHit> RigidWorld::castCapsule(const CapsuleRay& capsule, const QueryFilter& filter,
                                                const std::function<bool(int body)>& accept) const {
    // The capsule's box: its axis' extent plus the radius.
    const Vec3 axis = apply(capsule.rotation, Vec3{capsule.halfLength, 0, 0});
    const Vec3 grow{std::fabs(axis.x) + capsule.radius, std::fabs(axis.y) + capsule.radius,
                    std::fabs(axis.z) + capsule.radius};
    return castShapes(bodies_, [this](std::size_t i) { Box b; bodyBounds(i, b.min, b.max); return b; }, sweptBox(capsule.start, capsule.direction, grow), filter, accept,
                      [&](const Shape& s, const Pose& world, CastResult& best) {
                          switch (s.type) {
                          case ShapeType::Sphere:
                              return capsuleCastSphere(s.radius, world, capsule, best);
                          case ShapeType::Box:
                              return capsuleCastBox(s.halfExtents, world, capsule, best);
                          case ShapeType::Capsule:
                              return capsuleCastCapsule(s.halfLength, s.radius, world, capsule, best);
                          case ShapeType::Cylinder:
                              return capsuleCastCylinder(s.halfLength, s.radius, world, capsule, best);
                          case ShapeType::Hull:
                              return capsuleCastHull({s.hull.data(), int(s.hull.size()), s.hullIndices.data(),
                                                      int(s.hullIndices.size()), s.mirrored},
                                                     world, capsule, best);
                          case ShapeType::Mesh:
                              return capsuleCastMesh({s.mesh, world, s.mirrored}, capsule, best);
                          case ShapeType::SweptSphere:
                              break;
                          }
                          return false;
                      });
}

}   // namespace beacon::physics::rigid
