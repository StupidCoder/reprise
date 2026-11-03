#include "physics/CollisionWorld.hpp"

#include "math/Vector.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace beacon::physics {

using namespace math;

namespace {

constexpr float kPi = 3.14159265f;
constexpr int kLeafSize = 4;

// Closest point on triangle abc to p (Ericson, Real-Time Collision Detection, 5.1.5).
Vec3 closestOnTriangle(const Vec3& p, const Vec3& a, const Vec3& b, const Vec3& c) {
    const Vec3 ab = b - a, ac = c - a, ap = p - a;
    const float d1 = dot(ab, ap), d2 = dot(ac, ap);
    if (d1 <= 0 && d2 <= 0) {
        return a;
    }
    const Vec3 bp = p - b;
    const float d3 = dot(ab, bp), d4 = dot(ac, bp);
    if (d3 >= 0 && d4 <= d3) {
        return b;
    }
    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) {
        return a + ab * (d1 / (d1 - d3));
    }
    const Vec3 cp = p - c;
    const float d5 = dot(ab, cp), d6 = dot(ac, cp);
    if (d6 >= 0 && d5 <= d6) {
        return c;
    }
    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) {
        return a + ac * (d2 / (d2 - d6));
    }
    const float va = d3 * d6 - d5 * d4;
    if (va <= 0 && d4 - d3 >= 0 && d5 - d6 >= 0) {
        return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
    }
    const float denom = 1 / (va + vb + vc);
    return a + ab * (vb * denom) + ac * (vc * denom);
}

bool insideTriangle(const Vec3& p, const Triangle& t) {
    return dot(cross(t.b - t.a, p - t.a), t.normal) >= 0 && dot(cross(t.c - t.b, p - t.b), t.normal) >= 0 &&
           dot(cross(t.a - t.c, p - t.c), t.normal) >= 0;
}

// Smallest t >= 0 with |o + t d - c| = r for a unit d; false if none.
bool raySphere(const Vec3& o, const Vec3& d, const Vec3& c, float r, float& t) {
    const Vec3 m = o - c;
    const float b = dot(m, d), cc = dot(m, m) - r * r;
    if (cc > 0 && b > 0) {
        return false;
    }
    const float disc = b * b - cc;
    if (disc < 0) {
        return false;
    }
    t = std::max(0.0f, -b - std::sqrt(disc));
    return true;
}

// Ray against the side of the capsule core segment p-q with radius r (end caps tested separately).
bool rayCylinder(const Vec3& o, const Vec3& d, const Vec3& p, const Vec3& q, float r, float& t) {
    const Vec3 axis = q - p, m = o - p;
    const float aa = dot(axis, axis);
    if (aa < 1e-12f) {
        return false;
    }
    const float md = dot(m, axis), nd = dot(d, axis);
    const Vec3 mPerp = m - axis * (md / aa), dPerp = d - axis * (nd / aa);
    const float a = dot(dPerp, dPerp), b = dot(mPerp, dPerp), c = dot(mPerp, mPerp) - r * r;
    if (a < 1e-12f) {
        return false;   // moving parallel to the edge: caps handle it
    }
    if (c > 0 && b > 0) {
        return false;
    }
    const float disc = b * b - a * c;
    if (disc < 0) {
        return false;
    }
    const float s = std::max(0.0f, (-b - std::sqrt(disc)) / a);
    const float along = (md + s * nd) / aa;
    if (along < 0 || along > 1) {
        return false;
    }
    t = s;
    return true;
}

bool rayBox(const Vec3& o, const Vec3& invD, const Vec3& lo, const Vec3& hi, float maxT) {
    float t0 = 0, t1 = maxT;
    for (int axis = 0; axis < 3; ++axis) {
        const float oa = component(o, axis), inv = component(invD, axis);
        float a = (component(lo, axis) - oa) * inv, b = (component(hi, axis) - oa) * inv;
        if (a > b) {
            std::swap(a, b);
        }
        t0 = std::max(t0, a);
        t1 = std::min(t1, b);
        if (t0 > t1) {
            return false;
        }
    }
    return true;
}

Vec3 rotate(const Quat& q, const Vec3& v) {
    const Vec3 u{q.x, q.y, q.z};
    return u * (2 * dot(u, v)) + v * (q.w * q.w - dot(u, u)) + cross(u, v) * (2 * q.w);
}

}   // namespace

void CollisionWorld::clear() {
    triangles_.clear();
    inBody_.clear();
    order_.clear();
    nodes_.clear();
    bodies_.clear();
    openBody_ = -1;
}

std::uint32_t CollisionWorld::beginBody() {
    openBody_ = int(bodies_.size());
    bodies_.emplace_back();
    return std::uint32_t(openBody_);
}

void CollisionWorld::endBody() {
    if (openBody_ >= 0) {
        updateBodyBounds(bodies_[std::size_t(openBody_)]);
    }
    openBody_ = -1;
}

void CollisionWorld::updateBodyBounds(Body& b) {
    b.lo = {1e30f, 1e30f, 1e30f};
    b.hi = {-1e30f, -1e30f, -1e30f};
    for (std::uint32_t i : b.triangles) {
        const Triangle& t = triangles_[i];
        b.lo = min(b.lo, min(t.a, min(t.b, t.c)));
        b.hi = max(b.hi, max(t.a, max(t.b, t.c)));
    }
}

void CollisionWorld::setBodyTransform(std::uint32_t body, const math::Mat4& m) {
    Body& b = bodies_[body];
    b.delta = math::multiply(m, math::inverseAffine(b.transform));
    b.transform = m;
    for (std::size_t k = 0; k < b.triangles.size(); ++k) {
        const Triangle& r = b.rest[k];
        Triangle& t = triangles_[b.triangles[k]];
        t.a = transformPoint(m, r.a);
        t.b = transformPoint(m, r.b);
        t.c = transformPoint(m, r.c);
        const Vec3 n = cross(t.b - t.a, t.c - t.a);
        t.normal = dot(n, n) < 1e-20f ? r.normal : normalize(n);
    }
    updateBodyBounds(b);
}

void CollisionWorld::addTriangle(const Vec3& a, const Vec3& b, const Vec3& c, const Surface& surface) {
    const Vec3 n = cross(b - a, c - a);
    if (dot(n, n) < 1e-14f) {
        return;   // degenerate
    }
    triangles_.push_back({a, b, c, normalize(n), surface});
    inBody_.push_back(openBody_);
    if (openBody_ >= 0) {
        Body& body = bodies_[std::size_t(openBody_)];
        body.triangles.push_back(std::uint32_t(triangles_.size() - 1));
        body.rest.push_back(triangles_.back());
    }
}

void CollisionWorld::addShape(const data::PhysicsShape& shape, const math::Mat4& world, const Surface& surface) {
    // Local shape frame, then the caller's world transform.
    auto place = [&](const Vec3& p) { return transformPoint(world, rotate(shape.rotation, p) + shape.position); };
    auto tri = [&](const Vec3& a, const Vec3& b, const Vec3& c) { addTriangle(place(a), place(b), place(c), surface); };
    auto quad = [&](const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d) {
        tri(a, b, c);
        tri(a, c, d);
    };
    using data::ShapeKind;
    switch (shape.kind) {
    case ShapeKind::Box: {
        const Vec3 h = shape.halfExtents;
        Vec3 v[8];
        for (int i = 0; i < 8; ++i) {
            v[i] = {i & 1 ? h.x : -h.x, i & 2 ? h.y : -h.y, i & 4 ? h.z : -h.z};
        }
        quad(v[0], v[2], v[3], v[1]);   // -z
        quad(v[4], v[5], v[7], v[6]);   // +z
        quad(v[0], v[4], v[6], v[2]);   // -x
        quad(v[1], v[3], v[7], v[5]);   // +x
        quad(v[0], v[1], v[5], v[4]);   // -y
        quad(v[2], v[6], v[7], v[3]);   // +y
        break;
    }
    case ShapeKind::Sphere:
    case ShapeKind::Capsule: {
        // Rows from the bottom pole to the top pole; capsules along local Y (hypothesis) get the
        // equator twice, once per half, which forms the straight band between the hemispheres.
        const int seg = 12, half = 3;
        const float r = shape.radius, hl = shape.kind == ShapeKind::Capsule ? shape.halfLength : 0;
        std::vector<std::pair<float, float>> rows;   // latitude, y offset
        for (int i = 0; i <= half; ++i) {
            rows.emplace_back(-kPi / 2 + kPi / 2 * i / half, -hl);
        }
        for (int i = hl > 0 ? 0 : 1; i <= half; ++i) {
            rows.emplace_back(kPi / 2 * i / half, hl);
        }
        auto point = [&](std::size_t row, int s) -> Vec3 {
            const float ang = 2 * kPi * s / seg, lat = rows[row].first;
            return {r * std::cos(lat) * std::cos(ang), r * std::sin(lat) + rows[row].second,
                    r * std::cos(lat) * std::sin(ang)};
        };
        for (std::size_t row = 0; row + 1 < rows.size(); ++row) {
            for (int s = 0; s < seg; ++s) {
                quad(point(row, s), point(row + 1, s), point(row + 1, s + 1), point(row, s + 1));
            }
        }
        break;
    }
    case ShapeKind::Cylinder: {
        const int seg = 12;
        const float r = shape.radius, hl = shape.halfLength;
        auto rim = [&](float y, int s) -> Vec3 {
            const float ang = 2 * kPi * s / seg;
            return {r * std::cos(ang), y, r * std::sin(ang)};
        };
        for (int s = 0; s < seg; ++s) {
            quad(rim(-hl, s), rim(hl, s), rim(hl, s + 1), rim(-hl, s + 1));
            tri({0, -hl, 0}, rim(-hl, s), rim(-hl, s + 1));
            tri({0, hl, 0}, rim(hl, s + 1), rim(hl, s));
        }
        break;
    }
    case ShapeKind::Convex:
    case ShapeKind::Mesh: {
        const auto& g = shape.geometry;
        for (std::size_t i = 0; i + 2 < g.indices.size(); i += 3) {
            tri(g.vertices[g.indices[i]], g.vertices[g.indices[i + 1]], g.vertices[g.indices[i + 2]]);
        }
        break;
    }
    }
}

void CollisionWorld::build() {
    order_.clear();
    for (std::uint32_t i = 0; i < triangles_.size(); ++i) {
        if (inBody_[i] < 0) {
            order_.push_back(i);   // the tree holds the static triangles only
        }
    }
    nodes_.clear();
    nodes_.reserve(order_.size() / 2 + 1);
    if (!order_.empty()) {
        buildNode(0, std::uint32_t(order_.size()), 0);
    }
}

std::uint32_t CollisionWorld::buildNode(std::uint32_t first, std::uint32_t count, int depth) {
    const std::uint32_t index = std::uint32_t(nodes_.size());
    nodes_.emplace_back();
    Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f}, clo = lo, chi = hi;
    for (std::uint32_t i = first; i < first + count; ++i) {
        const Triangle& t = triangles_[order_[i]];
        lo = min(lo, min(t.a, min(t.b, t.c)));
        hi = max(hi, max(t.a, max(t.b, t.c)));
        const Vec3 centre = (t.a + t.b + t.c) * (1.0f / 3);
        clo = min(clo, centre);
        chi = max(chi, centre);
    }
    nodes_[index].lo = lo;
    nodes_[index].hi = hi;
    if (count <= kLeafSize || depth > 48) {
        nodes_[index].first = first;
        nodes_[index].count = count;
        return index;
    }
    const Vec3 ext = chi - clo;
    const int axis = ext.x > ext.y && ext.x > ext.z ? 0 : ext.y > ext.z ? 1 : 2;
    const std::uint32_t mid = first + count / 2;
    std::nth_element(order_.begin() + first, order_.begin() + mid, order_.begin() + first + count,
                     [&](std::uint32_t a, std::uint32_t b) {
                         const Triangle &ta = triangles_[a], &tb = triangles_[b];
                         return component(ta.a + ta.b + ta.c, axis) < component(tb.a + tb.b + tb.c, axis);
                     });
    const std::uint32_t left = buildNode(first, mid - first, depth + 1);
    const std::uint32_t right = buildNode(mid, first + count - mid, depth + 1);
    nodes_[index].left = left;
    nodes_[index].right = right;
    return index;
}

template <typename Box, typename Leaf>
void CollisionWorld::walk(const Box& overlaps, const Leaf& leaf) const {
    for (const Body& b : bodies_) {
        if (!b.triangles.empty() && overlaps(b.lo, b.hi)) {
            for (std::uint32_t i : b.triangles) {
                leaf(i);
            }
        }
    }
    if (nodes_.empty()) {
        return;
    }
    std::uint32_t stack[128];
    int top = 0;
    stack[top++] = 0;
    while (top > 0) {
        const Node& n = nodes_[stack[--top]];
        if (!overlaps(n.lo, n.hi)) {
            continue;
        }
        if (n.count > 0) {
            for (std::uint32_t i = n.first; i < n.first + n.count; ++i) {
                leaf(order_[i]);
            }
        } else if (top + 2 <= 128) {
            stack[top++] = n.left;
            stack[top++] = n.right;
        }
    }
}

std::optional<RayHit> CollisionWorld::raycast(const Vec3& o, const Vec3& d, float maxDistance,
                                              std::uint32_t mask) const {
    std::optional<RayHit> best;
    float limit = maxDistance;
    const Vec3 inv{d.x != 0 ? 1 / d.x : 1e30f, d.y != 0 ? 1 / d.y : 1e30f, d.z != 0 ? 1 / d.z : 1e30f};
    walk([&](const Vec3& lo, const Vec3& hi) { return rayBox(o, inv, lo, hi, limit); },
         [&](std::uint32_t i) {
             const Triangle& t = triangles_[i];
             if (!(t.surface.mask & mask)) {
                 return;
             }
             // Möller-Trumbore, both faces.
             const Vec3 e1 = t.b - t.a, e2 = t.c - t.a, p = cross(d, e2);
             const float det = dot(e1, p);
             if (std::fabs(det) < 1e-12f) {
                 return;
             }
             const float inv = 1 / det;
             const Vec3 s = o - t.a;
             const float u = dot(s, p) * inv;
             if (u < 0 || u > 1) {
                 return;
             }
             const Vec3 q = cross(s, e1);
             const float v = dot(d, q) * inv;
             if (v < 0 || u + v > 1) {
                 return;
             }
             const float dist = dot(e2, q) * inv;
             if (dist < 0 || dist > limit) {
                 return;
             }
             limit = dist;
             best = RayHit{dist, o + d * dist, dot(t.normal, d) < 0 ? t.normal : -t.normal, i};
         });
    return best;
}

std::optional<SweepHit> CollisionWorld::sweepSphere(const Vec3& from, float r, const Vec3& d, float maxDistance,
                                                    std::uint32_t mask) const {
    std::optional<SweepHit> best;
    float limit = maxDistance;
    // Triangles first touched at the same distance (a shared edge or vertex): the face that most
    // opposes the sweep wins, so a sweep landing on an edge reports the face it lands on.
    constexpr float kTie = 1e-6f;
    const Vec3 to = from + d * maxDistance;
    const Vec3 slo = min(from, to) - Vec3{r, r, r}, shi = max(from, to) + Vec3{r, r, r};
    walk(
        [&](const Vec3& lo, const Vec3& hi) {
            return lo.x <= shi.x && hi.x >= slo.x && lo.y <= shi.y && hi.y >= slo.y && lo.z <= shi.z && hi.z >= slo.z;
        },
        [&](std::uint32_t i) {
            const Triangle& t = triangles_[i];
            if (!(t.surface.mask & mask)) {
                return;
            }
            float hitT = best ? limit + kTie : limit;
            bool hit = false;
            // Face: the sphere's leading point meets the plane inside the triangle.
            const float d0 = dot(from - t.a, t.normal);
            const float side = d0 >= 0 ? 1.0f : -1.0f;
            const float approach = -side * dot(d, t.normal);
            if (approach > 1e-6f) {
                const float ft = std::max(0.0f, (std::fabs(d0) - r) / approach);
                if (ft < hitT && insideTriangle(from + d * ft - t.normal * (side * r), t)) {
                    hitT = ft;
                    hit = true;
                }
            }
            // Edges and vertices.
            const Vec3* v[3] = {&t.a, &t.b, &t.c};
            for (int k = 0; k < 3; ++k) {
                float et;
                if (rayCylinder(from, d, *v[k], *v[(k + 1) % 3], r, et) && et < hitT) {
                    hitT = et;
                    hit = true;
                }
                if (raySphere(from, d, *v[k], r, et) && et < hitT) {
                    hitT = et;
                    hit = true;
                }
            }
            if (!hit) {
                return;
            }
            const Vec3 centre = from + d * hitT;
            const Vec3 contact = closestOnTriangle(centre, t.a, t.b, t.c);
            const Vec3 n = normalize(centre - contact, t.normal * side);
            if (dot(n, d) >= 0) {
                return;   // touching but moving away (or along)
            }
            const Vec3 face = t.normal * side;
            if (best && hitT > best->distance - kTie && dot(face, d) >= dot(best->faceNormal, d)) {
                return;   // a tie that does not oppose the sweep more than the current face
            }
            limit = std::min(limit, hitT);
            best = SweepHit{hitT, contact, n, i, face};
        });
    return best;
}

void CollisionWorld::overlapSphere(const Vec3& c, float r, std::vector<Contact>& out, std::uint32_t mask) const {
    const Vec3 slo = c - Vec3{r, r, r}, shi = c + Vec3{r, r, r};
    walk(
        [&](const Vec3& lo, const Vec3& hi) {
            return lo.x <= shi.x && hi.x >= slo.x && lo.y <= shi.y && hi.y >= slo.y && lo.z <= shi.z && hi.z >= slo.z;
        },
        [&](std::uint32_t i) {
            const Triangle& t = triangles_[i];
            if (!(t.surface.mask & mask)) {
                return;
            }
            const Vec3 p = closestOnTriangle(c, t.a, t.b, t.c);
            const Vec3 delta = c - p;
            const float dd = dot(delta, delta);
            if (dd >= r * r) {
                return;
            }
            const float dist = std::sqrt(dd);
            const Vec3 n = dist > 1e-6f ? delta * (1 / dist) : (dot(c - t.a, t.normal) >= 0 ? t.normal : -t.normal);
            out.push_back({n, r - dist, i});
        });
}

}   // namespace physics
