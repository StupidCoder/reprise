#include "physics/PhysicsWorld.hpp"

#include "math/Vector.hpp"
#include "physics/rigid/MeshBuilder.hpp"

#include <algorithm>
#include <cmath>

namespace beacon::physics {

using math::Quat;
using math::Vec3;
using rigid::Mat3;

namespace {

Quat multiply(const Quat& a, const Quat& b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

Vec3 times(const Vec3& a, const Vec3& b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }

Vec3 column(const Mat3& m, int c) {
    return c == 0 ? Vec3{m.r[0].x, m.r[1].x, m.r[2].x}
                  : c == 1 ? Vec3{m.r[0].y, m.r[1].y, m.r[2].y} : Vec3{m.r[0].z, m.r[1].z, m.r[2].z};
}

// A rotation with its columns scaled (the scale in the shape's own frame).
Mat3 scaledColumns(const Mat3& m, const Vec3& s) {
    Mat3 out = m;
    for (auto& row : out.r) {
        row = {row.x * s.x, row.y * s.y, row.z * s.z};
    }
    return out;
}

}   // namespace

Placement placementOf(const math::Mat4& m) {
    const float sx = math::length(Vec3{m[0], m[1], m[2]}), sy = math::length(Vec3{m[4], m[5], m[6]}),
                sz = math::length(Vec3{m[8], m[9], m[10]});
    const float r00 = m[0] / sx, r10 = m[1] / sx, r20 = m[2] / sx;
    const float r01 = m[4] / sy, r11 = m[5] / sy, r21 = m[6] / sy;
    const float r02 = m[8] / sz, r12 = m[9] / sz, r22 = m[10] / sz;
    Quat q;
    const float trace = r00 + r11 + r22;
    if (trace > 0) {
        const float k = std::sqrt(trace + 1.0f) * 2.0f;
        q = {(r21 - r12) / k, (r02 - r20) / k, (r10 - r01) / k, 0.25f * k};
    } else if (r00 > r11 && r00 > r22) {
        const float k = std::sqrt(1.0f + r00 - r11 - r22) * 2.0f;
        q = {0.25f * k, (r01 + r10) / k, (r02 + r20) / k, (r21 - r12) / k};
    } else if (r11 > r22) {
        const float k = std::sqrt(1.0f + r11 - r00 - r22) * 2.0f;
        q = {(r01 + r10) / k, 0.25f * k, (r12 + r21) / k, (r02 - r20) / k};
    } else {
        const float k = std::sqrt(1.0f + r22 - r00 - r11) * 2.0f;
        q = {(r02 + r20) / k, (r12 + r21) / k, 0.25f * k, (r10 - r01) / k};
    }
    return {{m[12], m[13], m[14]}, q, {sx, sy, sz}};
}

PhysicsWorld::PhysicsWorld() = default;

// The record's shapes at the placement scale `s` (0xbd91a0 / 0xbdd350); also used when a node's
// scale changes later (0xbd4660 rebuilds the shapes at the new scale).
void PhysicsWorld::buildShapes(const data::PhysicsBody& record, const Vec3& s, std::vector<rigid::Shape>& shapes) {
    // Scaled lengths of the body's axes (the body frame's rotation applied to the unit axes, then
    // the object's scale).
    const Mat3 bodyAxes = rigid::rotationMatrix(record.bodyRotation);
    float axisScale[3];
    for (int i = 0; i < 3; ++i) {
        axisScale[i] = math::length(times(s, column(bodyAxes, i)));
    }
    const float largest = std::max(std::fabs(s.x), std::max(std::fabs(s.y), std::fabs(s.z)));
    const bool mirrored = s.x * s.y * s.z < 0;
    for (const data::PhysicsShape& shape : record.shapes) {
        rigid::Shape out;
        out.turn = shape.rotation;
        out.offset.rotation = rigid::rotationMatrix(shape.rotation);
        out.offset.translation = times(s, shape.position) * kScale;
        switch (shape.kind) {
        case data::ShapeKind::Sphere:
            out.type = rigid::ShapeType::Sphere;
            out.radius = shape.radius * largest * kScale;
            break;
        case data::ShapeKind::Box:
            out.type = rigid::ShapeType::Box;
            out.halfExtents = Vec3{shape.halfExtents.x * axisScale[0], shape.halfExtents.y * axisScale[1],
                                   shape.halfExtents.z * axisScale[2]} *
                              kScale;
            break;
        case data::ShapeKind::Capsule:
        case data::ShapeKind::Cylinder:
            out.type = shape.kind == data::ShapeKind::Capsule ? rigid::ShapeType::Capsule : rigid::ShapeType::Cylinder;
            out.halfLength = shape.halfLength * axisScale[0] * kScale;
            out.radius = shape.radius * axisScale[1] * kScale;
            break;
        case data::ShapeKind::Convex:
            out.type = rigid::ShapeType::Hull;
            out.hull = shape.geometry.vertices;
            out.hullIndices = shape.geometry.indices;
            out.offset.rotation = scaledColumns(out.offset.rotation, s * kScale);
            out.mirrored = mirrored;
            break;
        case data::ShapeKind::Mesh:
            if (!shape.largeMesh) {
                // Kind 6 (0xbdd350 -> PhysicsWorldImpl 0xcb2170): built at load from the vertices
                // times the placement's scale (each part at least 0.1), the shape at the body's
                // origin with the world scale alone.
                std::shared_ptr<const data::LargeMesh> built = runtimeMesh(shape.geometry, s);
                if (!built) {
                    ++skippedShapes_;   // the builder refused it (too small, no triangles left)
                    continue;
                }
                out.type = rigid::ShapeType::Mesh;
                out.mesh = built.get();
                meshes_.push_back(std::move(built));
                out.turn = {};
                out.offset.rotation = scaledColumns(Mat3{}, {kScale, kScale, kScale});
                out.offset.translation = {};
                break;
            }
            out.type = rigid::ShapeType::Mesh;
            out.mesh = shape.largeMesh.get();
            meshes_.push_back(shape.largeMesh);
            out.offset.rotation = scaledColumns(out.offset.rotation, s * kScale);
            out.mirrored = mirrored;
            break;
        }
        shapes.push_back(std::move(out));
    }
}

int PhysicsWorld::addBody(const data::PhysicsFile& file, std::size_t index, const Placement& placement,
                          const BodyOptions& options) {
    const data::PhysicsBody& record = file.bodies[index];
    const Mat3 object = rigid::rotationMatrix(placement.rotation);
    // The placement's scale alone (0xbd91a0 / 0xbdd350: the record's node scale only enters the
    // model's sync matrix).
    const Vec3 s = placement.scale;

    rigid::Body body;
    body.state.motion = options.motion;
    body.state.filterSelf = options.filterSelf;
    body.state.filterTarget = options.filterTarget;
    body.state.position = (placement.position + apply(object, times(s, record.bodyPosition))) * kScale;
    body.state.orientation = multiply(placement.rotation, record.bodyRotation);
    {
        // The record's mass properties for every body (the solver weighs bodies by motion type
        // alone; a fixed body made active later flies with them).
        body.mass.mass = record.mass;
        body.mass.restitution = record.restitution;
        body.mass.friction = record.friction;
        body.state.linearDamping = record.linearDamping;
        body.state.angularDamping = record.angularDamping;
        body.state.maxLinearVelocity = record.maxLinearVelocity;
        body.state.maxAngularVelocity = record.maxAngularVelocity;
    }
    buildShapes(record, s, body.shapes);
    if (body.shapes.empty()) {
        return -1;
    }
    if (body.mass.mass > 0) {
        // Inertia of the shapes' box about the body (0xcb5880 -> 0xde5fe0): the box's full extents
        // twice the larger of |min| and |max| per axis; I = m (b^2 + c^2) / 12.
        Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
        for (const rigid::Shape& sh : body.shapes) {
            Vec3 a, b;
            rigid::shapeBounds(sh, sh.offset, a, b);
            lo = {std::min(lo.x, a.x), std::min(lo.y, a.y), std::min(lo.z, a.z)};
            hi = {std::max(hi.x, b.x), std::max(hi.y, b.y), std::max(hi.z, b.z)};
        }
        const Vec3 e{2 * std::max(std::fabs(lo.x), std::fabs(hi.x)), 2 * std::max(std::fabs(lo.y), std::fabs(hi.y)),
                     2 * std::max(std::fabs(lo.z), std::fabs(hi.z))};
        const float m = body.mass.mass;
        body.mass.inertia.r[0] = {m * (e.y * e.y + e.z * e.z) / 12, 0, 0};
        body.mass.inertia.r[1] = {0, m * (e.x * e.x + e.z * e.z) / 12, 0};
        body.mass.inertia.r[2] = {0, 0, m * (e.x * e.x + e.y * e.y) / 12};
    }
    const int id = world_.add(std::move(body));
    surfaces_.push_back(options.surface);
    deltas_.push_back(math::identity());
    offsets_.push_back({times(s, record.bodyPosition), record.bodyRotation});
    sources_.resize(std::size_t(id) + 1);
    sources_[std::size_t(id)] = {&record, s};
    return id;
}

std::shared_ptr<const data::LargeMesh> PhysicsWorld::runtimeMesh(const data::TriangleSoup& soup, const Vec3& scale) {
    const Vec3 k{std::max(scale.x, 0.1f), std::max(scale.y, 0.1f), std::max(scale.z, 0.1f)};
    for (const BuiltMesh& m : built_)
        if (m.soup == &soup && m.scale.x == k.x && m.scale.y == k.y && m.scale.z == k.z) return m.mesh;
    std::vector<Vec3> vertices = soup.vertices;
    if (k.x != 1 || k.y != 1 || k.z != 1)
        for (Vec3& v : vertices) v = times(v, k);
    auto mesh = std::make_shared<data::LargeMesh>();
    if (!rigid::buildLargeMesh(vertices, soup.indices, *mesh)) mesh.reset();
    built_.push_back({&soup, k, mesh});
    return mesh;
}

std::vector<int> PhysicsWorld::addBodies(const data::PhysicsFile& file, const Placement& placement,
                                         const BodyOptions& options) {
    std::vector<int> ids;
    for (std::size_t i = 0; i < file.bodies.size(); ++i) {
        if (const int id = addBody(file, i, placement, options); id >= 0) {
            ids.push_back(id);
        }
    }
    return ids;
}

int PhysicsWorld::addMesh(std::shared_ptr<const data::LargeMesh> mesh, const BodyOptions& options) {
    rigid::Body body;
    body.state.motion = options.motion;
    body.state.filterSelf = options.filterSelf;
    body.state.filterTarget = options.filterTarget;
    body.mass.mass = 0;
    rigid::Shape shape;
    shape.type = rigid::ShapeType::Mesh;
    shape.mesh = mesh.get();
    shape.offset.rotation = scaledColumns(Mat3{}, {kScale, kScale, kScale});
    body.shapes.push_back(shape);
    meshes_.push_back(std::move(mesh));
    surfaces_.push_back(options.surface);
    deltas_.push_back(math::identity());
    offsets_.push_back({});
    return world_.add(std::move(body));
}

int PhysicsWorld::addPrimitive(rigid::ShapeType type, float halfLength, float radius, float mass,
                               const BodyOptions& options, const Vec3& offset, const Quat& turn) {
    rigid::Body body;
    body.state.motion = options.motion;
    body.state.filterSelf = options.filterSelf;
    body.state.filterTarget = options.filterTarget;
    body.state.useSleep = false;   // 0xd99850: state byte 0 = 0x0c (no sleep bit)
    body.mass.mass = mass;
    rigid::Shape shape;
    shape.type = type;
    shape.halfLength = halfLength * kScale;
    shape.radius = radius * kScale;
    shape.turn = turn;
    shape.offset.rotation = rigid::rotationMatrix(turn);
    shape.offset.translation = offset * kScale;
    body.shapes.push_back(shape);
    surfaces_.push_back(options.surface);
    deltas_.push_back(math::identity());
    offsets_.push_back({});
    return world_.add(std::move(body));
}

int PhysicsWorld::addCapsule(float halfLength, float radius, float mass, const BodyOptions& options, const Vec3& offset,
                             const Quat& turn) {
    return addPrimitive(rigid::ShapeType::Capsule, halfLength, radius, mass, options, offset, turn);
}

int PhysicsWorld::addCylinder(float halfLength, float radius, float mass, const BodyOptions& options, const Vec3& offset,
                              const Quat& turn) {
    return addPrimitive(rigid::ShapeType::Cylinder, halfLength, radius, mass, options, offset, turn);
}

void PhysicsWorld::setOrientation(int body, const Quat& q) { world_.body(body).state.orientation = q; }

void PhysicsWorld::setPose(int body, const Vec3& position, const Quat& rotation) {
    rigid::RigidState& s = world_.body(body).state;
    const math::Mat4 before = math::compose(s.orientation, s.position * (1.0f / kScale), {1, 1, 1});
    s.position = position * kScale;
    s.orientation = rotation;
    const math::Mat4 after = math::compose(rotation, position, {1, 1, 1});
    deltas_[std::size_t(body)] = math::multiply(after, math::inverseAffine(before));
}

void PhysicsWorld::movePlacement(const std::vector<int>& bodies, const math::Mat4& world) {
    const Placement p = placementOf(world);
    const Mat3 r = rigid::rotationMatrix(p.rotation);
    for (int id : bodies) {
        // A changed scale (a joint's clip scaling it) rebuilds the shapes at the new scale
        // (0xbd4660: when the node's scale differs from the stored one by at least FLT_EPSILON).
        if (std::size_t(id) < sources_.size() && sources_[std::size_t(id)].record) {
            Source& src = sources_[std::size_t(id)];
            if (math::length(p.scale - src.scale) >= 1.1920929e-07f) {
                std::vector<rigid::Shape> shapes;
                buildShapes(*src.record, p.scale, shapes);
                if (!shapes.empty()) {
                    world_.body(id).shapes = std::move(shapes);
                    src.scale = p.scale;
                    offsets_[std::size_t(id)].position = times(p.scale, src.record->bodyPosition);
                }
            }
        }
        const Offset& o = offsets_[std::size_t(id)];
        setPose(id, p.position + rigid::apply(r, o.position), multiply(p.rotation, o.rotation));
    }
}

Vec3 PhysicsWorld::position(int body) const { return world_.body(body).state.position * (1.0f / kScale); }

bool PhysicsWorld::moving(int body) const {
    const rigid::MotionType m = world_.body(body).state.motion;
    return m == rigid::MotionType::Keyframe || m == rigid::MotionType::Active;
}

void PhysicsWorld::step(float) { world_.step(); }

void PhysicsWorld::setMotion(int body, rigid::MotionType type) {
    rigid::RigidState& s = world_.body(body).state;
    s.motion = type;
    s.sleeping = false;
    s.sleepCount = 0;
}

void PhysicsWorld::setFilters(int body, std::uint32_t self, std::uint32_t target) {
    rigid::RigidState& s = world_.body(body).state;
    s.filterSelf = self;
    s.filterTarget = target;
}

void PhysicsWorld::setGroups(int body, std::uint16_t a, std::uint16_t b) {
    rigid::RigidState& s = world_.body(body).state;
    s.group[0] = a;
    s.group[1] = b;
}

Vec3 PhysicsWorld::linearVelocity(int body) const { return world_.body(body).state.linearVelocity * (1.0f / kScale); }

void PhysicsWorld::setLinearVelocity(int body, const Vec3& v) {
    rigid::RigidState& s = world_.body(body).state;   // 0xca5fa0 wakes it
    s.sleeping = false;
    s.sleepCount = 0;
    s.linearVelocity = v * kScale;
}

Vec3 PhysicsWorld::angularVelocity(int body) const { return world_.body(body).state.angularVelocity; }

void PhysicsWorld::setAngularVelocity(int body, const Vec3& w) { world_.body(body).state.angularVelocity = w; }

void PhysicsWorld::setMaxAngularVelocity(int body, float w) { world_.body(body).state.maxAngularVelocity = w; }

void PhysicsWorld::moveTo(int body, const Vec3& target, float dt) {
    rigid::RigidState& s = world_.body(body).state;
    if (dt <= 0) {
        dt = world_.params().timeStep;
    }
    if (dt > 0 && s.motion != rigid::MotionType::Fixed) {
        s.linearVelocity = (target * kScale - s.position) * (1.0f / dt);
    }
}

void PhysicsWorld::turnTo(int body, const Quat& target, float dt) {
    rigid::RigidState& s = world_.body(body).state;
    if (dt <= 0) {
        dt = world_.params().timeStep;
    }
    if (s.motion == rigid::MotionType::Fixed) {
        return;
    }
    const Quat& q = s.orientation;
    Quat t = target;
    if (q.x * t.x + q.y * t.y + q.z * t.z + q.w * t.w < 0) {
        t = {-t.x, -t.y, -t.z, -t.w};   // the shorter way
    }
    // d = t q^-1; w = axis x angle / dt (small turns: 2 (t - q) q^-1 / dt).
    const Quat qi{-q.x, -q.y, -q.z, q.w};
    Quat d = multiply(t, qi);
    const float dl = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z + d.w * d.w);
    d = {d.x / dl, d.y / dl, d.z / dl, d.w / dl};
    const float sin2 = 1.0f - d.w * d.w;
    if (sin2 <= 1e-6f) {
        const Quat diff{t.x - q.x, t.y - q.y, t.z - q.z, t.w - q.w};
        const Quat w = multiply(diff, qi);
        s.angularVelocity = Vec3{w.x, w.y, w.z} * (2.0f / dt);
        return;
    }
    const float angle = 2.0f * std::acos(std::max(-1.0f, std::min(1.0f, d.w)));
    s.angularVelocity = Vec3{d.x, d.y, d.z} * (angle / (std::sqrt(sin2) * dt));
}

void PhysicsWorld::cancelGravity(int body) {
    rigid::Body& b = world_.body(body);
    b.state.sleeping = false;   // both force calls wake the body first (0xca65a0 / 0xca6510)
    b.state.sleepCount = 0;
    rigid::applyExternalForce(b.state, b.mass, world_.params().gravity * -b.mass.mass, {}, world_.params().timeStep);
}

void PhysicsWorld::applyForce(int body, const Vec3& force, const Vec3& torque, float dt) {
    rigid::Body& b = world_.body(body);
    b.state.sleeping = false;
    b.state.sleepCount = 0;
    rigid::applyExternalForce(b.state, b.mass, force * kScale, torque, dt);
}

void PhysicsWorld::setMass(int body, float mass, const rigid::Mat3& inertia) {
    rigid::RigidBody& m = world_.body(body).mass;
    m.mass = mass;
    m.inertia = inertia;
}

void PhysicsWorld::setFriction(int body, float friction) { world_.body(body).mass.friction = friction; }

std::function<bool(int)> PhysicsWorld::acceptMask(std::uint32_t hitMask) const {
    return [this, hitMask](int body) { return (surfaces_[std::size_t(body)].mask & hitMask) != 0; };
}

std::optional<PhysicsHit> PhysicsWorld::moduleHit(const std::optional<rigid::QueryHit>& hit) const {
    if (!hit) {
        return std::nullopt;
    }
    const auto finite = [](const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };
    if (!finite(hit->point) || !finite(hit->normal)) {
        return std::nullopt;
    }
    PhysicsHit out;
    out.fraction = hit->t;
    out.point = hit->point * (1.0f / kScale);
    out.normal = hit->normal;
    const rigid::Body& body = world_.body(hit->body);
    const rigid::Shape& shape = body.shapes[std::size_t(hit->shape)];
    if (shape.type == rigid::ShapeType::Mesh && hit->facet >= 0) {
        const data::MeshFacet& facet =
            shape.mesh->islands[std::size_t(hit->island)].facets[std::size_t(hit->facet)];
        const Mat3 turn = rigid::rotationMatrix(multiply(body.state.orientation, shape.turn));
        out.normal = rigid::apply(turn, facet.normal);
    }
    out.body = hit->body;
    out.surface = &surfaces_[std::size_t(hit->body)];
    return out;
}

std::optional<PhysicsHit> PhysicsWorld::castRay(const Vec3& from, const Vec3& to, std::uint32_t hitMask,
                                                const rigid::QueryFilter& filter) const {
    return moduleHit(world_.castRay(from * kScale, (to - from) * kScale, filter, acceptMask(hitMask)));
}

std::optional<PhysicsHit> PhysicsWorld::castSphere(const Vec3& from, const Vec3& to, float radius,
                                                   std::uint32_t hitMask, const rigid::QueryFilter& filter) const {
    return moduleHit(
        world_.castSphere(from * kScale, (to - from) * kScale, radius * kScale, filter, acceptMask(hitMask)));
}

std::optional<PhysicsHit> PhysicsWorld::castCapsule(const Vec3& from, const Vec3& to, float radius, float halfLength,
                                                    const Quat& rotation, std::uint32_t hitMask,
                                                    const rigid::QueryFilter& filter) const {
    const rigid::CapsuleRay capsule{from * kScale, (to - from) * kScale, rigid::rotationMatrix(rotation),
                                    radius * kScale, halfLength * kScale};
    return moduleHit(world_.castCapsule(capsule, filter, acceptMask(hitMask)));
}

}   // namespace physics
