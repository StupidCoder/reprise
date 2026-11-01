// Recovered KCL collision queries (original game, ARM11 main code).
//
// KCL is the level's collision format. It stores the walkable and solid surfaces as "prisms":
// a triangle described by one corner, a face normal, three edge-plane normals and a height
// (`length`, the distance from the stored corner to the opposite edge). An octree over the level's
// bounding box lists, for each cell, the prisms that touch it, so a query only tests nearby prisms.
// Two queries are recovered here, both used by the player collider (Collision.hpp):
//   - a sphere against the prisms around it (0x249c8c, per prism 0x2497b8), reporting the
//     penetration depth and whether the face, an edge or a corner was touched;
//   - a line segment marched through the octree cells (0x3f1ba4, per prism 0x3f1958), reporting the
//     fraction of the segment at which it hits.
//
// The data model is the KCL file (version 0x202, one model). The query code is transcribed from the
// original instructions (tools/arm_disasm.py listings) with the original operation order. Variable
// names such as s0..s14 are the original's VFP registers and the labels L_xxxxxx its branch
// targets, kept so the code can be compared with the disassembly line by line. Compile with
// -ffp-contract=off (the ARM11 VFP has no fused multiply-add).
// Validation: tools/validate_kcl.cpp replays traced calls (evidence/runs/player-prism-a).
#pragma once

#include <climits>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace captain_toad::RecoveredKcl {

struct Vec3 {
    float x, y, z;
};

// One prism record as stored in the file (20 bytes).
struct Prism {
    float length;          // height of the triangle (corner to opposite edge)
    uint16_t pos;          // index of the stored corner
    uint16_t face;         // index of the face normal
    uint16_t e1, e2, e3;   // indices of the three edge-plane normals
    uint16_t attribute;    // surface attribute (material, wall/floor codes)
    uint32_t globalIndex;
};
static_assert(sizeof(Prism) == 20);

struct Model {
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<Prism> prisms;   // in file order
    float thickness = 0;         // model header +0x10 (param_3[4] in 0x2497b8)
    Vec3 areaMin{};              // +0x14 origin of the octree
    uint32_t mask[3]{};          // +0x20..+0x28 inverted coordinate masks (cells outside the area)
    uint32_t shift = 0;          // +0x2c top-level cell size (log2)
    uint32_t shiftY = 0;         // +0x30
    uint32_t shiftZ = 0;         // +0x34
    std::vector<uint8_t> file;
    uint32_t base = 0;
    uint32_t prismOffset = 0;
    uint32_t octreeOffset = 0;

    void load(const std::vector<uint8_t>& bytes) {
        file = bytes;
        auto u32 = [&](size_t p) {
            uint32_t v;
            memcpy(&v, file.data() + p, 4);
            return v;
        };
        if (u32(0) != 0x202 || u32(12) != 1) {
            throw std::runtime_error("Unsupported KCL layout");
        }
        base = u32(u32(8));
        uint32_t positionsOffset = u32(base);
        uint32_t normalsOffset = u32(base + 4);
        uint32_t prismsOffset = u32(base + 8);
        uint32_t treeOffset = u32(base + 12);
        memcpy(&thickness, file.data() + base + 16, 4);
        prismOffset = prismsOffset;
        octreeOffset = treeOffset;
        memcpy(&areaMin, file.data() + base + 0x14, 12);
        for (int k = 0; k < 3; k++) {
            mask[k] = u32(base + 0x20 + 4 * k);
        }
        shift = u32(base + 0x2c);
        shiftY = u32(base + 0x30);
        shiftZ = u32(base + 0x34);
        for (uint32_t o = positionsOffset; o < normalsOffset; o += 12) {
            Vec3 v;
            memcpy(&v, file.data() + base + o, 12);
            positions.push_back(v);
        }
        for (uint32_t o = normalsOffset; o < prismsOffset; o += 12) {
            Vec3 v;
            memcpy(&v, file.data() + base + o, 12);
            normals.push_back(v);
        }
        for (uint32_t o = prismsOffset; o < treeOffset; o += 20) {
            Prism prism;
            memcpy(&prism, file.data() + base + o, 20);
            prisms.push_back(prism);
        }
    }
};

// ---- Sphere against a prism -------------------------------------------------------------------------

// 0x2497b8: sphere (centre p, radius s0) against one prism. Returns 1 on contact and writes the
// penetration depth and the contact class: 1 face, 2/3/4 edge e1/e2/e3, 5 corner e1/e2,
// 6 corner e2/e3, 7 corner e3/e1. On a rejection after the first depth store, `depth` keeps the
// last stored value (as the original's output slot does).
inline int prismSphere(const Model& m, const Prism& prism, const Vec3& p, float s0, float& depth, uint8_t& kind) {
    kind = 0;
    const Vec3& P = m.positions.at(prism.pos);
    const Vec3& ip = m.normals.at(prism.e1);     // [r1+8]
    const Vec3& R3 = m.normals.at(prism.e2);     // [r1+0xa]
    const Vec3& R0 = m.normals.at(prism.e3);     // [r1+0xc]
    const Vec3& R1 = m.normals.at(prism.face);   // [r1+6]

    // Signed distances of the centre from the three edge planes (s2, s1, s3) and the face (s4).
    float s4 = p.x - P.x;
    float s5 = p.y - P.y;
    float s6 = p.z - P.z;
    float s2 = s4 * ip.x;
    float s7 = s0 * s0;
    s2 = s2 + s5 * ip.y;
    s2 = s2 + s6 * ip.z;
    if (s2 >= s0) {
        return 0;
    }
    float s1 = s4 * R3.x;
    s1 = s1 + s5 * R3.y;
    s1 = s1 + s6 * R3.z;
    if (s1 >= s0) {
        return 0;
    }
    float s3 = s4 * R0.x;
    s3 = s3 + s5 * R0.y;
    s3 = s3 + s6 * R0.z;
    s3 = s3 - prism.length;
    if (s3 >= s0) {
        return 0;
    }
    const float s9 = 0.0f;   // 0x249bb4
    s4 = s4 * R1.x;
    s4 = s4 + s5 * R1.y;
    s4 = s4 + s6 * R1.z;   // face distance
    s6 = s0 - s4;
    depth = s6;
    if (s6 < s9) {
        return 0;
    }
    s5 = 1.0f;   // 0x249bb8
    float s8, s10, s11, s12, s13, s14, sq;
    auto dot = [](const Vec3& a, const Vec3& b) {
        float v = a.x * b.x;
        v = v + a.y * b.y;
        v = v + a.z * b.z;
        return v;
    };

    // 0x2498dc: find the edge plane the centre is furthest outside of, and decide between face,
    // edge and corner contact.
    if (s2 <= s1) {
        goto L_24991c;
    }
    if (s2 <= s3) {
        goto L_249a78;
    }
    if (s2 > s9) {
        goto L_249948;
    }
    if (s6 > m.thickness) {
        return 0;
    }
    goto L_249a94;

L_24991c:
    if (s1 <= s3) {
        goto L_249a78;
    }
    if (s1 > s9) {
        goto L_2499e0;
    }
    if (s6 > m.thickness) {
        return 0;
    }
    goto L_249a94;

L_249948:
    if (s1 <= s3) {
        goto L_24998c;
    }
    s3 = dot(ip, R3);
    s6 = s2 * s3;
    if (s6 <= s1) {
        goto L_249b68;
    }
    goto L_2499c0;

L_24998c:
    s1 = dot(ip, R0);
    s6 = s2 * s1;
    if (s6 <= s3) {
        goto L_249c00;
    }

L_2499c0:   // edge e1
    if (s2 > s4) {
        return 0;
    }
    s7 = s7 - s2 * s2;
    kind = 2;
    depth = s7;
    goto L_249b3c;

L_2499e0:
    if (s3 <= s2) {
        goto L_249a24;
    }
    s2 = dot(R3, R0);
    s6 = s1 * s2;
    if (s6 <= s3) {
        goto L_249bbc;
    }
    goto L_249a58;

L_249a24:
    s3 = dot(R3, ip);
    s6 = s1 * s3;
    if (s6 <= s2) {
        goto L_249b68;
    }

L_249a58:   // edge e2
    if (s1 > s4) {
        return 0;
    }
    s7 = s7 - s1 * s1;
    kind = 3;
    depth = s7;
    goto L_249b3c;

L_249a78:
    if (s3 > s9) {
        goto L_249a9c;
    }
    if (s6 > m.thickness) {
        return 0;
    }

L_249a94:   // face
    kind = 1;
    return 1;

L_249a9c:
    if (s2 <= s1) {
        goto L_249ae0;
    }
    s1 = dot(R0, ip);
    s6 = s3 * s1;
    if (s6 <= s2) {
        goto L_249c00;
    }
    goto L_249b14;

L_249ae0:
    s2 = dot(R0, R3);
    s6 = s3 * s2;
    if (s6 <= s1) {
        goto L_249bbc;
    }

L_249b14:   // edge e3
    if (s3 > s4) {
        return 0;
    }
    s7 = s7 - s3 * s3;
    kind = 4;
    depth = s7;

L_249b3c:   // depth = sqrt(r^2 - d^2) - face distance
    sq = depth;
    {
        float root = std::sqrt(sq);
        s0 = root - s4;
    }
    depth = s0;
    if (s0 >= s9) {
        return 1;
    }
    kind = 0;
    return 0;

L_249b68:   // corner e1/e2: c = s3, a = s2 (e1 distance), b = s1 (e2 distance)
    s2 = -s2 + s3 * s1;
    s5 = -s5 + s3 * s3;
    kind = 5;
    s6 = s2 / s5;
    s1 = s1 - s6 * s3;
    s8 = ip.x * s6;
    s14 = ip.y * s6;
    s13 = ip.z * s6;
    s8 = s8 + s1 * R3.x;
    s14 = s14 + s1 * R3.y;
    s13 = s13 + s1 * R3.z;
    goto L_249c40;

L_249bbc:   // corner e2/e3: c = s2, a = s1, b = s3
    s1 = -s1 + s2 * s3;
    s5 = -s5 + s2 * s2;
    kind = 6;
    s6 = s1 / s5;
    s3 = s3 - s6 * s2;
    s8 = R3.x * s6;
    s14 = R3.y * s6;
    s13 = R3.z * s6;
    s8 = s8 + s3 * R0.x;
    s14 = s14 + s3 * R0.y;
    s13 = s13 + s3 * R0.z;
    goto L_249c40;

L_249c00:   // corner e3/e1: c = s1, a = s3, b = s2
    s3 = -s3 + s1 * s2;
    s5 = -s5 + s1 * s1;
    kind = 7;
    s6 = s3 / s5;
    s2 = s2 - s6 * s1;
    s8 = R0.x * s6;
    s14 = R0.y * s6;
    s13 = R0.z * s6;
    s8 = s8 + s2 * ip.x;
    s14 = s14 + s2 * ip.y;
    s13 = s13 + s2 * ip.z;

L_249c40:   // corner contact: within the radius, and not too far below the face
    s1 = s8 * s8;
    s1 = s1 + s14 * s14;
    s1 = s1 + s13 * s13;
    s2 = std::sqrt(s1);
    if (s2 >= s0) {
        kind = 0;
        return 0;
    }
    s0 = s2 * 0.70710677f;   // 0x3f3504e6
    if (!(s4 >= s0)) {
        kind = 0;
        return 0;
    }
    s0 = s7 - s1;
    depth = s0;
    goto L_249b3c;
    (void)s10;
    (void)s11;
    (void)s12;
}

// ---- Octree --------------------------------------------------------------------------------------------

// ARM VCVT.S32.F32: round towards zero, saturating; NaN converts to 0.
inline int32_t truncateToInt32(float v) {
    if (std::isnan(v)) {
        return 0;
    }
    if (v >= 2147483648.0f) {
        return INT32_MAX;
    }
    if (v <= -2147483648.0f) {
        return INT32_MIN;
    }
    return int32_t(v);
}

// 0x28ac80: integer cell range of an axis-aligned box (lo = min corner, hi = max corner).
// lo clamps at 0, hi at ~mask. Returns 1 if the range is non-empty.
inline int cellRange(const Model& m, const Vec3& lo, const Vec3& hi, int32_t outLo[3], int32_t outHi[3]) {
    const float a[3] = {lo.x, lo.y, lo.z};
    const float b[3] = {hi.x, hi.y, hi.z};
    const float o[3] = {m.areaMin.x, m.areaMin.y, m.areaMin.z};
    for (int k = 0; k < 3; k++) {
        outLo[k] = truncateToInt32(a[k] - o[k]);
    }
    for (int k = 0; k < 3; k++) {
        outHi[k] = truncateToInt32(b[k] - o[k]);
    }
    for (int k = 0; k < 3; k++) {
        if (outLo[k] < 0) {
            outLo[k] = 0;
        }
    }
    for (int k = 0; k < 3; k++) {
        int32_t limit = int32_t(~m.mask[k]);
        if (limit < outHi[k]) {
            outHi[k] = limit;
        }
    }
    return (outHi[0] >= outLo[0] && outHi[1] >= outLo[1] && outLo[2] <= outHi[2]) ? 1 : 0;
}

// 0x28abcc: the octree leaf for cell (x, y, z). Returns the byte offset (within the model) of the
// leaf's prism list and the final shift (the leaf's cell size is 1 << shift).
inline uint32_t octreeLeaf(const Model& m, const uint32_t c[3], uint32_t& shiftOut) {
    auto word = [&](uint32_t offset) {
        int32_t v;
        memcpy(&v, m.file.data() + m.base + offset, 4);
        return v;
    };
    uint32_t sh = m.shift;
    uint32_t index = ((c[2] >> sh) << (m.shiftZ & 0xff)) | ((c[1] >> sh) << (m.shiftY & 0xff)) | (c[0] >> sh);
    index <<= 2;
    shiftOut = sh;
    if (m.shiftZ == 0xffffffffu && m.shiftY == 0xffffffffu) {
        index = 0;
    }
    uint32_t node = m.octreeOffset;
    int32_t v = word(node + index);
    // Positive entries point to eight children; a set top bit marks a leaf.
    while (v >= 0) {
        node += uint32_t(v);
        sh = shiftOut - 1;
        shiftOut = sh;
        uint32_t child = (((c[2] >> sh) & 1u) << 2) | (((c[1] >> sh) & 1u) << 1) | ((c[0] >> sh) & 1u);
        v = word(node + child * 4);
    }
    return node + (uint32_t(v) & 0x7fffffffu);
}

// ---- Sphere query ----------------------------------------------------------------------------------------

// Result ring of 0x249c8c (param_4): {model, prism, depth, kind}.
struct Hit {
    int model;
    int prism;
    float depth;
    uint8_t kind;
};

struct HitRing {
    std::vector<Hit> entries;
    int capacity = 0;
    int start = 0;
    int count = 0;

    explicit HitRing(int cap = 0) : entries(cap), capacity(cap) {}

    const Hit& at(int i) const {
        int k = start + i;
        if (k >= capacity) {
            k -= capacity;
        }
        return entries[k];
    }
};

// 0x249c8c: sphere (centre p, radius r) against all models. Appends up to maxHits new contacts
// (prisms already in the ring are skipped) and returns the number appended. onTest (validation
// only) receives each prism tested, in order. The cell walk visits each octree leaf once per row
// (it remembers the last leaf with prisms to skip repeats, as the original does).
inline uint32_t sphereQuery(const std::vector<const Model*>& models, const Vec3& p, float r, uint32_t maxHits,
                            HitRing& ring,
                            void (*onTest)(void*, int prism, int ret, float depth, uint8_t kind) = nullptr,
                            void* ctx = nullptr) {
    float depth = 0.0f;   // local_b8 / local_b4 (output slots, reused across prisms)
    uint8_t kind = 0;
    const Vec3 hi{p.x + r, p.y + r, p.z + r};
    const Vec3 lo{p.x - r, p.y - r, p.z - r};
    uint32_t added = 0;
    uint32_t lastLeaf = 0;
    bool haveLast = false;
    uint32_t bestLeaf = 0;
    for (size_t modelIndex = 0; modelIndex < models.size(); modelIndex++) {
        const Model& m = *models[modelIndex];
        int32_t cellLo[3], cellHi[3];
        if (!cellRange(m, lo, hi, cellLo, cellHi)) {
            continue;
        }
        uint32_t z = uint32_t(cellLo[2]);
        do {
            uint32_t y = uint32_t(cellLo[1]);
            int32_t stepZ = 1000000;   // 0xf4240
            do {
                int32_t bestX = 0;
                uint32_t x = uint32_t(cellLo[0]);
                int32_t stepY = 1000000;
                do {
                    uint32_t c[3] = {x, y, z};
                    uint32_t sh;
                    uint32_t leaf = octreeLeaf(m, c, sh);
                    int32_t size = int32_t(1u << (sh & 0xff));
                    uint32_t sizeMask = uint32_t(size - 1);
                    uint32_t xInCell = x & sizeMask;
                    int32_t remainingZ = size - int32_t(z & sizeMask);
                    int32_t remainingY = size - int32_t(sizeMask & y);
                    if (remainingZ < stepZ) {
                        stepZ = remainingZ;
                    }
                    if (remainingY < stepY) {
                        stepY = remainingY;
                    }
                    uint16_t first;
                    memcpy(&first, m.file.data() + m.base + leaf + 2, 2);
                    if (bestX < remainingY && first != 0) {
                        bestLeaf = leaf;
                        bestX = remainingY;
                    }
                    if (!haveLast || leaf != lastLeaf) {
                        for (uint32_t li = leaf + 2;; li += 2) {
                            uint16_t index;
                            memcpy(&index, m.file.data() + m.base + li, 2);
                            if (index == 0xffff) {
                                break;
                            }
                            const Prism& prism = m.prisms.at(index);
                            if (!(0.0f < prism.length)) {
                                continue;
                            }
                            int k = 0;
                            for (; k != ring.count; k++) {
                                const Hit& h = ring.at(k);
                                if (h.model == int(modelIndex) && h.prism == int(index)) {
                                    break;
                                }
                            }
                            if (k != ring.count) {
                                continue;   // already reported
                            }
                            int hit = prismSphere(m, prism, p, r, depth, kind);
                            if (onTest) {
                                onTest(ctx, int(index), hit, depth, kind);
                            }
                            if (!hit) {
                                continue;
                            }
                            if (!(added < maxHits)) {
                                continue;
                            }
                            if (ring.count < ring.capacity) {
                                int slot = ring.count + ring.start;
                                ring.count++;
                                if (ring.capacity <= slot) {
                                    slot -= ring.capacity;
                                }
                                ring.entries[slot] = Hit{int(modelIndex), int(index), depth, kind};
                            }
                            added++;
                        }
                    }
                    x = x + uint32_t(size - int32_t(xInCell));
                } while (x <= uint32_t(cellHi[0]));
                y = y + uint32_t(stepY);
                lastLeaf = bestLeaf;
                haveLast = (bestLeaf != 0);
            } while (y <= uint32_t(cellHi[1]));
            z = z + uint32_t(stepZ);
        } while (z <= uint32_t(cellHi[2]));
    }
    return added;
}

// ---- Segment queries -------------------------------------------------------------------------------------

// 0x3f1958: segment p -> p + dir against one prism (dir is the full, unnormalised segment).
// On contact writes t (the fraction of dir) and the contact class (1 face; 2/3/4 near edge
// e1/e2/e3; 5/6/7 near corners), with a tolerance of 0.01 against the edge planes.
inline int segmentPrism(const Model& m, const Prism& prism, const Vec3& p, const Vec3& dir, float& t, uint8_t& kind) {
    const Vec3& P = m.positions.at(prism.pos);
    const Vec3& N = m.normals.at(prism.face);
    const float zero = 0.0f;   // 0x3f1b98
    Vec3 d{p.x - P.x, p.y - P.y, p.z - P.z};
    float s1 = d.x * N.x;
    s1 = s1 + d.y * N.y;
    s1 = s1 + d.z * N.z;
    if (!(s1 > zero)) {   // the start is behind the face (bls)
        kind = 0;
        return 0;
    }
    float s0 = N.x * dir.x;
    s0 = s0 + N.y * dir.y;
    s0 = s0 + N.z * dir.z;
    if (s1 + s0 > zero) {   // the end is still in front of the face
        kind = 0;
        return 0;
    }
    float tt = s1 / -s0;
    Vec3 q{d.x + dir.x * tt, d.y + dir.y * tt, d.z + dir.z * tt};
    auto bits = [](float f) {
        int32_t b;
        memcpy(&b, &f, 4);
        return b;
    };
    const int32_t toleranceBits = 0x3c23d70a;   // 0.01f compared as signed bits
    auto edge = [&](const Vec3& E, int& near) -> bool {
        float v = q.x * E.x;
        v = v + q.y * E.y;
        v = v + q.z * E.z;
        if (bits(v) > toleranceBits) {
            return false;
        }
        if (!(zero > v) && toleranceBits >= bits(v)) {
            near = 1;
        }
        return true;
    };
    int near1 = 0;
    int near2 = 0;
    int near3 = 0;
    if (!edge(m.normals.at(prism.e1), near1) || !edge(m.normals.at(prism.e2), near2)) {
        kind = 0;
        return 0;
    }
    const Vec3& E3 = m.normals.at(prism.e3);
    float v3 = q.x * E3.x;
    v3 = v3 + q.y * E3.y;
    v3 = v3 + q.z * E3.z;
    float limit = prism.length + 0.01f;
    if (!(limit >= v3)) {   // bhs (an unordered compare passes)
        kind = 0;
        return 0;
    }
    t = tt;
    if (v3 >= zero && toleranceBits >= bits(v3)) {
        near3 = 1;
    }
    if (near1) {
        kind = near2 ? (near3 ? 1 : 5) : (near3 ? 7 : 2);
    } else if (near2) {
        kind = near3 ? 6 : 3;
    } else {
        kind = near3 ? 4 : 1;
    }
    return 1;
}

// 0x3f1ba4: march the segment p -> p + dir through each model's octree (a 3D DDA over the cells).
// With a ring, it appends contacts (up to maxHits overall) and returns the nearest prism; without a
// ring, it stops each model at the first cell containing a contact. Returns the nearest prism index
// (or -1) and its model through nearestModel.
struct SegmentHit {
    int model;
    int prism;
    float t;
    uint8_t kind;
};

inline int segmentQuery(const std::vector<const Model*>& models, const Vec3& p, const Vec3& dir,
                        std::vector<SegmentHit>* ring, int ringCapacity, uint32_t maxHits, uint32_t* outCount,
                        int* nearestModel = nullptr,
                        void (*onTest)(void*, int prism, int ret, float t, uint8_t kind) = nullptr,
                        void* ctx = nullptr) {
    const float z0 = 0.0f;
    const float tiny = 1e-6f;
    const float stepEps = 0.001f;
    const float far = 1e9f;
    if (dir.x == z0 && dir.y == z0 && dir.z == z0) {
        return -1;
    }
    // Unit direction and length (s23).
    Vec3 n = dir;
    float l2 = n.x * n.x;
    l2 = l2 + n.y * n.y;
    l2 = l2 + n.z * n.z;
    float length = std::sqrt(l2);
    if (tiny > l2) {
        n = {z0, z0, z0};
    } else {
        float L = n.x * n.x;
        L = L + n.y * n.y;
        L = L + n.z * n.z;
        float r = std::sqrt(L);
        if (r > 0.0f) {
            float k = 1.0f / r;
            n = {n.x * k, n.y * k, n.z * k};
        }
    }
    {
        float L = n.x * n.x;
        L = L + n.y * n.y;
        L = L + n.z * n.z;
        if (!(L >= tiny)) {
            return -1;
        }
    }
    uint32_t hits = 0;
    int best = -1;
    int bestModel = -1;
    for (size_t modelIndex = 0; modelIndex < models.size(); modelIndex++) {
        const Model& m = *models[modelIndex];
        float t = z0;   // s18 distance travelled
        float rel[3] = {p.x - m.areaMin.x, p.y - m.areaMin.y, p.z - m.areaMin.z};
        int32_t cell[3] = {truncateToInt32(rel[0]), truncateToInt32(rel[1]), truncateToInt32(rel[2])};
        float current[3];
        auto inside = [&]() {
            return !(uint32_t(cell[0]) & m.mask[0]) && !(uint32_t(cell[1]) & m.mask[1]) &&
                   !(uint32_t(cell[2]) & m.mask[2]);
        };

        // Enter the octree's box (the start may be outside of it).
        bool entered = false;
        if (inside()) {
            current[0] = rel[0];
            current[1] = rel[1];
            current[2] = rel[2];
            entered = true;
        } else {
            const float nd[3] = {n.x, n.y, n.z};
            for (int a = 0; a < 3 && !entered; a++) {
                if (nd[a] == z0) {
                    continue;
                }
                float bound = nd[a] > z0 ? z0 : float(~m.mask[a]);
                t = (bound - rel[a]) / nd[a];
                if (t < z0 || t > length) {
                    continue;
                }
                current[0] = rel[0] + n.x * t;
                current[1] = rel[1] + n.y * t;
                current[2] = rel[2] + n.z * t;
                cell[0] = truncateToInt32(current[0]);
                cell[1] = truncateToInt32(current[1]);
                cell[2] = truncateToInt32(current[2]);
                if (inside()) {
                    entered = true;
                }
            }
        }
        if (!entered) {
            continue;
        }

        // Distances to the next cell boundary along each axis, by direction sign.
        int32_t positive[3], negative[3];
        int32_t* selected[3];
        int32_t sign[3];
        const float nd[3] = {n.x, n.y, n.z};
        for (int a = 0; a < 3; a++) {
            if (nd[a] < z0) {
                selected[a] = &negative[a];
                sign[a] = -1;
            } else {
                selected[a] = &positive[a];
                sign[a] = 1;
            }
        }
        bool stopModel = false;
        while (true) {
            uint32_t c[3] = {uint32_t(cell[0]), uint32_t(cell[1]), uint32_t(cell[2])};
            uint32_t sh;
            uint32_t leaf = octreeLeaf(m, c, sh);
            int32_t size = int32_t(1u << (sh & 0xff));
            int32_t sizeMask = size - 1;
            for (int a = 0; a < 3; a++) {
                positive[a] = size - (cell[a] & sizeMask);
                negative[a] = -(cell[a] & sizeMask);
            }
            for (int a = 0; a < 3; a++) {
                if (*selected[a] == 0) {
                    *selected[a] = sign[a];
                }
            }
            // Test the leaf's prisms.
            float bestT = 1.0f;   // s19
            for (uint32_t li = leaf + 2;; li += 2) {
                uint16_t index;
                memcpy(&index, m.file.data() + m.base + li, 2);
                if (index == 0xffff) {
                    break;
                }
                const Prism& prism = m.prisms.at(index);
                if (!(prism.length > z0)) {
                    continue;
                }
                float hitT = z0;
                uint8_t kind = 0;
                int hit = segmentPrism(m, prism, p, dir, hitT, kind);
                if (onTest) {
                    onTest(ctx, int(index), hit, hitT, kind);
                }
                if (!hit) {
                    continue;
                }
                if (ring) {
                    if (int(ring->size()) < ringCapacity) {
                        ring->push_back(SegmentHit{int(modelIndex), int(index), hitT, kind});
                    }
                    hits++;
                    if (hitT < bestT) {
                        bestT = hitT;
                        best = index;
                        bestModel = int(modelIndex);
                    }
                    if (hits == maxHits) {
                        if (outCount) {
                            *outCount = hits;
                        }
                        if (nearestModel) {
                            *nearestModel = bestModel;
                        }
                        return best;
                    }
                } else if (hitT < bestT) {
                    bestT = hitT;
                    best = index;
                    bestModel = int(modelIndex);
                }
            }
            if (!ring && best >= 0) {
                break;
            }
            // Step to the nearest cell boundary.
            float stepTo[3];
            for (int a = 0; a < 3; a++) {
                float absolute = nd[a];
                if (absolute <= z0) {
                    absolute = -absolute;
                }
                stepTo[a] = (absolute < stepEps) ? far : float(*selected[a]) / nd[a];
            }
            float step = stepTo[0];
            if (step > stepTo[1]) {
                step = stepTo[1];
            }
            if (step > stepTo[2]) {
                step = stepTo[2];
            }
            if (length - t <= step) {
                break;
            }
            current[0] = current[0] + n.x * step;
            current[1] = current[1] + n.y * step;
            current[2] = current[2] + n.z * step;
            t = t + step;
            cell[0] = truncateToInt32(current[0]);
            cell[1] = truncateToInt32(current[1]);
            cell[2] = truncateToInt32(current[2]);
            if (!inside()) {
                break;
            }
            if (!(t < length)) {
                break;
            }
        }
        (void)stopModel;
    }
    if (outCount) {
        *outCount = hits;
    }
    if (nearestModel) {
        *nearestModel = bestModel;
    }
    return best;
}

}   // namespace captain_toad::RecoveredKcl
