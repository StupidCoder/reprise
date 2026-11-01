// Recovered collision checks used by the player collider (original game, ARM11 main code).
//
// The level's walkable surfaces are stored as a KCL collision model (Kcl.hpp): a set of triangular
// "prisms". When the player moves, the collider asks two kinds of questions:
//   - sphere check: which prisms touch a sphere around the body (ground, walls), and
//   - segment check: which prisms does a line segment cross (for example straight down, to find
//     the ground below).
// Both return "hit records" with world-space normals, the contact point and the penetration depth;
// the collider (PlayerCollider.hpp) turns them into pushes and the ground contact.
//
// The engine runs these checks per collision "part" (a placed collision model). The opening has
// one static part: the stage terrain (Season1OpeningStepA), placed with identity rotation and
// translation (200, -750, 50) at scale 1 (read from the checkpoint at parts 0x3360489c). The
// engine's containers (part trees, object pools, tag filters) are reduced here to a list of parts
// that pass the "PlayerOnly" filter; the arithmetic and the order of results follow the original.
// Compile with -ffp-contract=off.
//   0x28404c / 0x25f9dc  sphere check per part      0x28646c  segment check per part
//   0x284e3c             hit info (world normals, vertices)
//   0x284f90             sphere contact point by contact class
//   0x2b3ffc             prism vertex k (0..2)
#pragma once

#include "game/Kcl.hpp"
#include "game/VecLib.hpp"

namespace captain_toad::RecoveredCollision {

using captain_toad::RecoveredVec::M34;
using captain_toad::RecoveredVec::V3;

// A placed collision model.
struct Part {
    const captain_toad::RecoveredKcl::Model* kcl = nullptr;
    M34 world{};               // +0x44 model -> world
    M34 inverse{};             // +0x74 world -> model
    float scale = 1;           // +0x110
    float depthScale = 1;      // +0x114
    bool valid = true;         // stage objects: a dead or hidden object's collision is invalidated
    bool playerOnly = false;   // registered for the player only (TransparentWallPlayerOnly, 0x19bc20)
    // The previous tick's world -> model matrix (moving map parts): the ground follow 0x1e589c
    // carries the player standing on the part by its movement since then.
    M34 previousInverse{};
    bool moved = false;
};

// Hit record (0x8c bytes in the original; fields named by their offsets).
struct HitInfo {
    int part = -1;      // +0x00 part index
    int prism = -1;     // +0x04 prism index in the part's model
    V3 faceNormal;      // +0x0c world, normalised
    V3 edge[3];         // +0x18, +0x24, +0x30 edge-plane normals (world, normalised)
    V3 vertex[3];       // +0x3c, +0x48, +0x54 triangle corners (world)
    float depth = 0;    // +0x60 penetration depth (sphere) or distance along the segment
    V3 contact;         // +0x64 contact point (world)
    V3 query;           // +0x70 sphere: world centre
    V3 projected;       // +0x7c sphere: face normal * (normal . direction)
    uint8_t kind = 0;   // +0x88 contact class (face, edge or corner, see Kcl.hpp)
};

inline V3 toV(const captain_toad::RecoveredKcl::Vec3& v) {
    return {v.x, v.y, v.z};
}
inline captain_toad::RecoveredKcl::Vec3 toK(const V3& v) {
    return {v.x, v.y, v.z};
}

// 0x2b3ffc: corner k of a prism. KCL stores only one corner (k = 0) explicitly; the other two
// are where the edge planes meet, at distance `length` from the stored corner.
inline V3 prismVertex(const captain_toad::RecoveredKcl::Model& m, const captain_toad::RecoveredKcl::Prism& prism,
                      int k) {
    const V3 P = toV(m.positions.at(prism.pos));
    if (k == 0) {
        return P;
    }
    const float eps = 1.1920929e-07f;   // 0x34000000
    if (k == 1) {
        const V3 A = toV(m.normals.at(prism.e2));
        const V3 F = toV(m.normals.at(prism.face));
        const V3 E3 = toV(m.normals.at(prism.e3));
        float cx = A.z * F.y;
        float cy = A.x * F.z;
        float cz = A.y * F.x;
        cx = cx - A.y * F.z;
        cy = cy - A.z * F.x;
        cz = cz - A.x * F.y;
        float denominator = cx * E3.x;
        denominator = denominator + cy * E3.y;
        denominator = denominator + cz * E3.z;
        float s0 = eps;
        if (denominator > s0) {
            s0 = denominator;
        }
        float k2 = prism.length / s0;
        return {P.x + k2 * cx, P.y + k2 * cy, P.z + k2 * cz};
    }
    if (k == 2) {
        const V3 F = toV(m.normals.at(prism.face));
        const V3 E1 = toV(m.normals.at(prism.e1));
        const V3 E3 = toV(m.normals.at(prism.e3));
        float cx = F.z * E1.y;
        float cz = F.y * E1.x;
        float cy = F.x * E1.z;
        cx = cx - F.y * E1.z;
        cz = cz - F.x * E1.y;
        cy = cy - F.z * E1.x;
        float denominator = cx * E3.x;
        denominator = denominator + cy * E3.y;
        denominator = denominator + cz * E3.z;
        float s0 = eps;
        if (denominator > s0) {
            s0 = denominator;
        }
        float k2 = prism.length / s0;
        return {P.x + k2 * cx, P.y + k2 * cy, P.z + k2 * cz};
    }
    return {0, 0, 0};
}

// 0x284e3c: world-space normals (normalised) and corners of a hit prism.
inline void fillHitInfo(HitInfo& hit, const Part& part, int partIndex, int prismIndex) {
    const auto& model = *part.kcl;
    const auto& prism = model.prisms.at(prismIndex);
    hit.part = partIndex;
    hit.prism = prismIndex;
    hit.faceNormal = captain_toad::RecoveredVec::mulDir(part.world, toV(model.normals.at(prism.face)));
    hit.edge[0] = captain_toad::RecoveredVec::mulDir(part.world, toV(model.normals.at(prism.e1)));
    hit.edge[1] = captain_toad::RecoveredVec::mulDir(part.world, toV(model.normals.at(prism.e2)));
    hit.edge[2] = captain_toad::RecoveredVec::mulDir(part.world, toV(model.normals.at(prism.e3)));
    captain_toad::RecoveredVec::normalize(hit.faceNormal);
    for (auto& edge : hit.edge) {
        captain_toad::RecoveredVec::normalize(edge);
    }
    for (int k = 0; k < 3; k++) {
        hit.vertex[k] = captain_toad::RecoveredVec::mulPoint(part.world, prismVertex(model, prism, k));
    }
}

// 0x284f90: contact point (model space) of a sphere centred at c against a prism, by contact class:
//   1     the face: c projected onto the triangle's plane;
//   2..4  an edge: the face point, then moved onto the edge's plane;
//   5..7  a corner: the corner itself.
inline V3 sphereContact(const captain_toad::RecoveredKcl::Model& m, const captain_toad::RecoveredKcl::Prism& prism,
                        const V3& c, uint8_t kind) {
    if (kind >= 5 && kind <= 7) {
        return prismVertex(m, prism, kind - 5);
    }
    if (kind < 1 || kind > 4) {
        return {0, 0, 0};
    }
    const V3 v0 = prismVertex(m, prism, 0);
    const V3 n = toV(m.normals.at(prism.face));
    V3 d = captain_toad::RecoveredVec::sub(c, v0);
    float s = d.x * n.x;
    s = s + d.y * n.y;
    s = -s - d.z * n.z;   // one VNMLA instruction: -(s + d.z * n.z) as -s - d.z*n.z
    V3 out = captain_toad::RecoveredVec::addScaled(n, c, s);
    if (kind == 1) {
        return out;
    }
    V3 e = c;
    V3 E;
    if (kind == 2) {
        E = toV(m.normals.at(prism.e1));
        e = captain_toad::RecoveredVec::sub(e, v0);
    } else if (kind == 3) {
        E = toV(m.normals.at(prism.e2));
        e = captain_toad::RecoveredVec::sub(e, v0);
    } else {
        E = toV(m.normals.at(prism.e3));
        e = captain_toad::RecoveredVec::sub(e, prismVertex(m, prism, 1));
    }
    float t = e.x * E.x;
    t = t + e.y * E.y;
    t = -t - e.z * E.z;
    return captain_toad::RecoveredVec::addScaled(E, out, t);
}

// Sphere check (0x28404c -> 0x25f9dc): hits in query order, appended to `out`.
// `dir` is the movement direction; each hit records the face normal scaled by its component
// along that direction (used by the collider to tell walls from floors).
// `actor`: an actor's query, which does not see player-only parts.
inline int sphereCheck(const std::vector<Part>& parts, const V3& centre, float radius, const V3& dir,
                       std::vector<HitInfo>& out, int maxHits = 256, bool actor = false) {
    int added = 0;
    for (size_t partIndex = 0; partIndex < parts.size(); partIndex++) {
        const Part& part = parts[partIndex];
        if (!part.valid || (actor && part.playerOnly)) {
            continue;
        }
        V3 local = captain_toad::RecoveredVec::mulPoint(part.inverse, centre);
        float r = radius * part.scale;
        captain_toad::RecoveredKcl::HitRing ring(maxHits);
        std::vector<const captain_toad::RecoveredKcl::Model*> models{part.kcl};
        captain_toad::RecoveredKcl::sphereQuery(models, toK(local), r, uint32_t(maxHits), ring);
        for (int i = 0; i < ring.count; i++) {
            const auto& kclHit = ring.at(i);
            HitInfo hit;
            V3 c = sphereContact(*part.kcl, part.kcl->prisms.at(kclHit.prism), local, kclHit.kind);
            hit.query = centre;
            hit.contact = captain_toad::RecoveredVec::mulPoint(part.world, c);
            fillHitInfo(hit, part, int(partIndex), kclHit.prism);
            hit.depth = kclHit.depth * part.depthScale;
            hit.projected = dir;
            hit.kind = kclHit.kind;
            float k = hit.faceNormal.x * hit.projected.x;
            k = k + hit.faceNormal.y * hit.projected.y;
            k = k + hit.faceNormal.z * hit.projected.z;
            hit.projected = captain_toad::RecoveredVec::scale(hit.faceNormal, k);
            out.push_back(hit);
            added++;
        }
    }
    return added;
}

// Segment check (0x28646c): the segment p -> p + dir (world); hits in march order, appended to `out`.
inline int segmentCheck(const std::vector<Part>& parts, const V3& p, const V3& dir, std::vector<HitInfo>& out,
                        int maxHits = 256) {
    int added = 0;
    float lengthSquared = dir.x * dir.x;
    lengthSquared = lengthSquared + dir.y * dir.y;
    lengthSquared = lengthSquared + dir.z * dir.z;
    const float length = std::sqrt(lengthSquared);
    for (size_t partIndex = 0; partIndex < parts.size(); partIndex++) {
        const Part& part = parts[partIndex];
        if (!part.valid) {
            continue;
        }
        V3 a = captain_toad::RecoveredVec::mulPoint(part.inverse, p);
        V3 b = captain_toad::RecoveredVec::mulPoint(part.inverse, captain_toad::RecoveredVec::add(p, dir));
        V3 d = captain_toad::RecoveredVec::sub(b, a);
        std::vector<captain_toad::RecoveredKcl::SegmentHit> ring;
        uint32_t count = 0;
        std::vector<const captain_toad::RecoveredKcl::Model*> models{part.kcl};
        captain_toad::RecoveredKcl::segmentQuery(models, toK(a), toK(d), &ring, maxHits, uint32_t(maxHits), &count);
        for (const auto& kclHit : ring) {
            HitInfo hit;
            V3 c = captain_toad::RecoveredVec::addScaled(d, a, kclHit.t);
            hit.contact = captain_toad::RecoveredVec::mulPoint(part.world, c);
            fillHitInfo(hit, part, int(partIndex), kclHit.prism);
            hit.depth = kclHit.t * length;
            hit.kind = kclHit.kind;
            out.push_back(hit);
            added++;
        }
    }
    return added;
}

}   // namespace captain_toad::RecoveredCollision
