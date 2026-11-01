// Recovered player collider (original game, ARM11 main code), transcribed from the original
// instructions with the original operation order (tools/arm_disasm.py listings; addresses in
// docs/player-movement-notes.md). Compile with -ffp-contract=off.
//
// How the player collides with the level, per tick (tick 0x182350 -> moveAndCollide 0x2c5470):
//   - The movement for this tick (velocity plus extra terms) is split into sub-steps of at most
//     27 units so that the player cannot pass through thin walls.
//   - Each sub-step (0x2c4e8c) moves the body, then asks the level:
//       * Ground: a segment straight down from 42% of the body height finds the floor (plus three
//         probes around the feet, 120 degrees apart, and one ahead while dashing). Floors closer
//         than a "snap allowance" pull the player down onto them, so walking down slopes and small
//         steps keeps him on the ground. The distances of all probes are combined (resolve) into one
//         vertical correction.
//       * Walls: a sphere at the lower body and one at the chest (lowered while crouching) push the
//         player out of walls and ceilings. Every contact is also sorted into lists (front, back,
//         left, right, ceiling), which the action machine reads ("is there a wall in front?").
//     The pushes are combined per axis (largest positive plus largest negative) and applied.
//   - Afterwards, cutVelocity (0x18262c) removes the velocity components that run into active
//     contacts (no more falling while on the ground, no more pushing into a wall).
//
// Scope: the paths the opening executes. Engine containers are reduced to vectors; branches that
// depend on systems not present in the opening (moving collision parts, restricted planes, the
// 0x182e14 actor-collision pass for climbing) are gated and fail loudly if reached.
#pragma once

#include <algorithm>
#include <array>
#include <string>

#include "game/Collision.hpp"
#include "game/Math.hpp"

namespace captain_toad::RecoveredPlayerCollider {

using captain_toad::RecoveredVec::V3;
using namespace captain_toad::RecoveredVec;

// A float's bit pattern (unsigned and signed); the original compares some floats this way.
inline uint32_t fb(float f) {
    return captain_toad::RecoveredMath::bitsf(f);
}
inline int32_t sfb(float f) {
    return int32_t(fb(f));
}

// The player's physics state (**(player+0xd0)); offsets of the original.
struct Body {
    V3 pos;                                                   // +0x00
    V3 front;                                                 // +0x0c facing direction
    V3 up;                                                    // +0x18
    V3 vel;                                                   // +0x24 velocity (units per tick)
    float mtx30[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};   // +0x30 3x4; reset by 0x2a21d0 to the constant
                                                              // at 0x550630 (identity, as observed)
    V3 gravity;                                               // +0x60
    V3 groundUp;                                              // +0x6c up direction of the ground the player stands on
    float scale = 1;                                          // +0x78
    float tilt = 0;   // +0x7c walk forward tilt (radians), written by 0x1f4ee4 and cleared on walk exit
};

// Player parameters read by the collider (PlayerConst defaults, evidence/player/params.json).
struct Params {
    float gravity = 6.0f;
    float tall = 160.0f;   // body height
    float legRadiusScale = 0.75f;
    float chestRadius = 50.0f;   // radius of both body spheres
    float cutVelLimit = 0.0f;
    float cutVelRate = 0.5f;
    float normalSpeedMaxGround = 16.6f;
    float dashSpeedMax = 18.0f;
    float dashCheckRadius = 87.5f;
};

// A contact list (vtable 0x480574): keeps the offer with the largest value (0x38d16c).
struct ContactList {
    bool valid = false;
    V3 vec;   // the contact normal
    float value = 0;
    int part = -1;
    int codeA = 0;   // ground: the prism's attribute (material)
    int codeB = 0;
    int codeC = 0;

    void clear() { valid = false; }   // 0x38d160

    void offer(const V3& v, float s, int p, int a, int b, int c) {   // 0x38d16c
        if (!valid || value < s) {
            vec = v;
            value = s;
            part = p;
            codeA = a;
            codeB = b;
            codeC = c;
            valid = true;
        }
    }
};

// Up to five probe distances with their parts (0x2c4410 / 0x1832b4 arrays: +0x00 dist[5],
// +0x14 parts[5], +0x28 count), and the resolver 0x2c41b8 that combines them.
struct ProbeArray {
    float dist[5]{};
    int part[5]{};
    uint32_t count = 0;

    void add(float d, int p) {
        dist[count] = d;
        part[count] = p;
        count++;
    }

    // 0x2c41b8 (s0 = reference). partMoving[p] stands for (*part)+0x134 != 0.
    //   - If the probes hit different parts and one of them is static: the largest distance.
    //   - Otherwise: the mean of the distances within 20 of the reference (or of all of them).
    // The loops keep the original's unrolled order (two accumulators), which affects rounding.
    float resolve(float ref, const std::vector<bool>& partMoving) const {
        const uint32_t n = count;
        if (n == 0) {
            return 0.0f;
        }
        bool allSame = true;
        if (n > 1) {
            const int p0 = part[0];
            uint32_t lead = ((~n) & 1u) + 1u;   // 2 for an even count, 1 for an odd one
            for (uint32_t i = 1; i < lead; i++) {
                if (part[i] != p0) {
                    allSame = false;
                    break;
                }
            }
            for (uint32_t i = lead; allSame && i < n; i += 2) {
                if (!(part[i] == p0 && part[i + 1] == p0)) {
                    allSame = false;
                }
            }
        }
        if (!allSame) {
            for (uint32_t i = 0; i < n; i++) {
                if (!partMoving.at(part[i])) {
                    // The maximum (0x2c432c..).
                    float m = dist[0];
                    if (n <= 1) {
                        return m;
                    }
                    uint32_t i2 = 0;
                    if ((n & 1u) == 0) {
                        if (dist[1] > m) {
                            m = dist[1];
                        }
                        i2 = 1;
                    }
                    for (uint32_t k = (n - 1) >> 1; k; k--) {
                        if (dist[i2 + 1] > m) {
                            m = dist[i2 + 1];
                        }
                        if (dist[i2 + 2] > m) {
                            m = dist[i2 + 2];
                        }
                        i2 += 2;
                    }
                    return m;
                }
            }
        }
        // The mean of the distances within 20 of the reference (two accumulators, original order).
        const int32_t limit = 0x41a00000;   // 20.0f
        auto near = [&](float d) {
            float a = d - ref;
            if (0.0f > a) {
                a = ref - d;
            }
            return sfb(a) < limit;
        };
        float s0 = 0.0f;
        float s2 = 0.0f;
        uint32_t c0 = 0;
        uint32_t c1 = 0;
        uint32_t i = 0;
        if (n & 1u) {
            if (near(dist[0])) {
                s0 = dist[0] + s0;
                c0 = 1;
            }
            i = 1;
        }
        for (uint32_t k = n >> 1; k; k--) {
            if (near(dist[i])) {
                s0 = dist[i] + s0;
                c0++;
            }
            if (near(dist[i + 1])) {
                s2 = dist[i + 1] + s2;
                c1++;
            }
            i += 2;
        }
        uint32_t total = c0 + c1;
        float sum = s0 + s2;
        if (total == 0) {
            // The mean of all of them (0x2c43a0).
            float a = 0.0f;
            float b = 0.0f;
            uint32_t j = 0;
            if (n & 1u) {
                a = dist[0] + a;
                j = 1;
            }
            for (uint32_t k = n >> 1; k; k--) {
                b = dist[j + 1] + b;
                a = dist[j] + a;
                j += 2;
            }
            sum = a + b;
            total = n;
        }
        float inverse = 1.0f / float(total);
        return inverse * sum;
    }
};

// Results of the ground checker (+0x1c, 0x20e590 -> 0x29bf98) and of the body-sphere checker
// (+0x14, 0x2146d8 -> 0x1be0a4).
struct HitResults {
    std::vector<captain_toad::RecoveredCollision::HitInfo> hits;
    void clear() { hits.clear(); }
    uint32_t count() const { return uint32_t(hits.size()); }
};

struct Collider {
    Body* body = nullptr;
    Params params;
    std::vector<captain_toad::RecoveredCollision::Part> parts;
    std::vector<bool>
        partMoving;   // (*part)+0x134 != 0 (and +0x118 == -1); true for the opening's terrain part (0x17f, -1)
    std::vector<bool> partFollow;   // ground the player follows (0x1e589c); the terrain returns zero deltas
    HitResults ground;              // +0x1c
    HitResults spheres;             // +0x14

    // Contact lists: +0x44 ground, +0x48 ceiling, +0x4c wall in front, +0x50 wall behind,
    // +0x54 / +0x58 walls at the sides, +0x5c front (second list).
    std::array<ContactList, 7> lists;
    std::array<std::vector<int>, 5> partsLists;   // +0xb0..+0xc0 (0x2c4734 unique add)
    bool flagC8 = false;                          // +0xc8
    bool flagC9 = false;                          // +0xc9
    bool enableSideProbes = true;                 // +0x118
    bool enableActorPass = true;                  // +0x119
    bool flag24 = false;                          // +0x24
    uint32_t counter28 = 0;
    uint32_t counter34 = 0;
    uint32_t counter38 = 0;
    uint32_t counter3c = 0;
    V3 extra68;   // +0x68 extra movement terms of this tick
    V3 extra74;   // +0x74
    V3 pushA;     // +0xcc combined push of the last sub-step
    V3 pushB;     // +0xd8 the same, moving parts only

    bool dashHeld = false;   // input +0xd6 (via +0x20 vtable +0x50)
    std::string animation;   // current player animation ("ClimbMove" gates two passes)

    // -(groundUp . v) in the original's VNMLA form, for v - groundUp * (groundUp . v).
    float groundUpDot(const V3& v) const {
        float k = body->groundUp.x * v.x;
        k = k + body->groundUp.y * v.y;
        return -k - body->groundUp.z * v.z;
    }
    float upDotNeg(const V3& v) const {
        float k = body->up.x * v.x;
        k = k + body->up.y * v.y;
        return -k - body->up.z * v.z;
    }

    ContactList& groundList() { return lists[0]; }

    // Ground offers carry the hit prism's attribute as code A; the original passes the attribute's
    // MaterialCode (and WallCode, FloorCode) read in 0x1832b4 / 0x2c4410 (traced: sound-material-a).
    // The footstep sounds and effects use it (grass, stone, soil).
    int attributeOf(const captain_toad::RecoveredCollision::HitInfo& h) const {
        return h.part >= 0 ? int(parts[size_t(h.part)].kcl->prisms.at(size_t(h.prism)).attribute) : -1;
    }
    int groundAttribute() const { return lists[0].valid ? lists[0].codeA : -1; }
    int groundPart() const { return lists[0].valid ? lists[0].part : -1; }
    bool onGround() const { return lists[0].valid; }   // vtable +0x28
    bool ceiling() const { return lists[1].valid; }    // vtable +0x34 (list +0x48)

    void addUniquePart(int which, int part) {   // 0x2c4734 (capacity not modelled)
        auto& list = partsLists.at(which);
        for (int p : list) {
            if (p == part) {
                return;
            }
        }
        list.push_back(part);
    }

    // ---- Ground ---------------------------------------------------------------------------------------

    // 0x20e590: run the ground checker on the segment p -> p + dir; the "PlayerOnly" filter passes
    // the terrain.
    void runGround(const V3& p, const V3& dir) {
        ground.clear();
        captain_toad::RecoveredCollision::segmentCheck(parts, p, dir, ground.hits);
    }

    // 0x2c47b8: index of the ground hit nearest to a.
    uint32_t nearestGround(const V3& a) const {
        uint32_t best = 0;
        V3 d = sub(ground.hits.at(0).contact, a);
        float s0 = d.x * d.x;
        s0 = s0 + d.y * d.y;
        s0 = s0 + d.z * d.z;
        float bestDistance = std::sqrt(s0);
        for (uint32_t i = 1; i < ground.count(); i++) {
            V3 e = sub(ground.hits[i].contact, a);
            float s = e.x * e.x;
            s = s + e.y * e.y;
            s = s + e.z * e.z;
            float l = std::sqrt(s);
            if (l < bestDistance) {
                best = i;
                bestDistance = l;
            }
        }
        return best;
    }

    // 0x2c4410: one side probe from `start` along `dir`; adds to the probe arrays.
    // probeLen = |dir|, allowance = the snap allowance (60 * scale).
    int sideProbe(const V3& start, const V3& dir, const V3& a, ProbeArray& arr1, ProbeArray& arr2, float probeLen,
                  float allowance) {
        runGround(start, dir);
        if (ground.count() == 0) {
            return 0;
        }
        uint32_t i = nearestGround(start);
        const V3& n = ground.hits[i].faceNormal;
        float g = body->groundUp.x * n.x;
        g = g + body->groundUp.y * n.y;
        g = g + body->groundUp.z * n.z;
        if (sfb(g) < 0x3f000000) {
            return 0;   // steeper than 60 degrees: not ground
        }
        // Distance along the probe, measured in the plane of the hit (r1 = a (sb), r2 = point).
        V3 d = sub(a, ground.hits[i].contact);
        float s16 = n.x * d.x;
        s16 = s16 + n.y * d.y;
        s16 = s16 + n.z * d.z;
        V3 e = add(d, dir);
        float s2 = n.x * e.x;
        s2 = s2 + n.y * e.y;
        s2 = s2 + n.z * e.z;
        float s0 = -s2 * probeLen;   // VNMUL s0, s2, s18
        s2 = s16 - s2;
        float s1 = s0 / s2;
        float sc = body->scale;
        float ten = 10.0f;
        float s3 = s1;
        s3 = s3 - sc * ten;
        int result;
        if (0.0f >= s3) {
            result = 0;
        } else {
            float allow = allowance;
            allow = allow + sc * ten;
            float distance = s1 - allow;
            result = 1;
            int part = ground.hits[i].part;
            arr1.add(distance, part);
            if (part >= 0 && partMoving.at(part) /* && +0x118 == -1 */) {
                arr2.add(distance, part);
            }
            float value = probeLen - distance;
            lists[0].offer(n, value, part, attributeOf(ground.hits[i]), 0, 0);
        }
        addUniquePart(0, ground.hits[i].part);
        return result;
    }

    // 0x1832b4: ground contact. Outputs (as the original's out-parameters): up * resolve(arr1),
    // up * resolve(arr2) (if > 0), and the box sums of the slope pushes.
    bool groundContact(V3& outUp1, V3& outUp2, V3& outBox1, V3& outBox2, const V3& a, const V3& dir, float allowance) {
        ProbeArray arr1, arr2;
        V3 b1min{}, b1max{}, b2min{}, b2max{};
        const float zero = 0.0f;
        float s16 = zero;
        float probeLengthSquared = dir.x * dir.x;
        probeLengthSquared = probeLengthSquared + dir.y * dir.y;
        probeLengthSquared = probeLengthSquared + dir.z * dir.z;
        float probeLen = std::sqrt(probeLengthSquared);

        // The central probe.
        runGround(a, dir);
        if (ground.count() != 0) {
            uint32_t best = nearestGround(a);
            const V3 n = ground.hits[best].faceNormal;
            float g = body->groundUp.x * n.x;
            g = g + body->groundUp.y * n.y;
            g = g + body->groundUp.z * n.z;
            if (sfb(g) >= 0x3f000000) {
                V3 end = add(a, dir);
                V3 d = sub(end, ground.hits[best].contact);
                float l2 = d.x * d.x;
                l2 = l2 + d.y * d.y;
                l2 = l2 + d.z * d.z;
                s16 = std::sqrt(l2);
                float sc = body->scale;
                float ten = 10.0f;
                float s2 = s16;
                s2 = s2 - sc * ten;
                if (!(zero >= s2)) {
                    float allow = allowance;
                    allow = allow + sc * ten;
                    s16 = s16 - allow;
                    int part = ground.hits[best].part;
                    arr1.add(s16, part);
                    bool moving = part >= 0 && partMoving.at(part);
                    if (moving) {
                        arr2.add(s16, part);
                    }
                    float value = probeLen - s16;
                    lists[0].offer(n, value, part, attributeOf(ground.hits[best]), 0, 0);
                    // Slope push: the horizontal part of the normal (relative to the body's up).
                    float k = body->up.x * n.x;
                    k = k + body->up.y * n.y;
                    k = -k - body->up.z * n.z;
                    V3 w = addScaled(body->up, n, k);
                    V3 push{};
                    float w2 = w.x * w.x;
                    w2 = w2 + w.y * w.y;
                    w2 = w2 + w.z * w.z;
                    if (!(w2 < captain_toad::RecoveredMath::fbits(0x358637be))) {   // 0x2dc0b8
                        normalize(w);
                        float c = n.x * body->up.x;
                        c = c + n.y * body->up.y;
                        c = c + n.z * body->up.z;
                        float sq = s16 * s16;
                        float cs = c * s16;
                        float r = sq;
                        r = r - cs * cs;
                        if (zero > r) {
                            r = zero;
                        }
                        float rt = std::sqrt(r);
                        if (rt > zero) {
                            float m = sq / rt;
                            const float cap = 40.0f;
                            if (cap < m) {
                                m = cap;
                            }
                            push = scale(w, m);
                        }
                    }
                    accumulate(b1min, b1max, push);
                    if (moving) {
                        accumulate(b2min, b2max, push);
                    }
                }
                addUniquePart(0, ground.hits[best].part);
            }
        }
        if (arr2.count != 0) {
            flagC9 = true;
        }

        if (enableSideProbes) {
            // Three probes around the feet, 120 degrees apart.
            float r = params.chestRadius * params.legRadiusScale;
            V3 base = a;
            V3 offset = scale(body->front, r);
            captain_toad::RecoveredMath::Quat q = captain_toad::RecoveredMath::fromAxisAngle(body->up, 2.0943951f);
            captain_toad::RecoveredVec::M34 rotation = captain_toad::RecoveredMath::toMatrix(q);
            for (int k = 0; k < 3; k++) {
                V3 p = add(base, offset);
                p = add(p, V3{0, 0, 0});
                sideProbe(p, dir, a, arr1, arr2, probeLen, allowance);
                offset = mulPoint(rotation, offset);
            }
            // Dash probe, behind the feet: gated by the area "PlayerColliderBDashSupportOffArea" (+0x40,
            // not present in the opening map) and the input's dash-held flag (+0x20 -> byte +0xd6).
            float k = groundUpDot(body->vel);
            V3 horizontal = addScaled(body->groundUp, body->vel, k);
            float h2 = horizontal.x * horizontal.x;
            h2 = h2 + horizontal.y * horizontal.y;
            h2 = h2 + horizontal.z * horizontal.z;
            float horizontalSpeed = std::sqrt(h2);
            if (dashHeld && params.normalSpeedMaxGround < horizontalSpeed) {
                float t = (horizontalSpeed - params.normalSpeedMaxGround) /
                          (params.dashSpeedMax - params.normalSpeedMaxGround);
                if (t < 0.0f) {
                    t = 0.0f;
                } else if (t > 1.0f) {
                    t = 1.0f;
                }
                float distance = body->scale * params.dashCheckRadius;   // 0x1d8334
                distance = distance * t;
                V3 o = scale(body->front, distance);
                V3 start = sub(a, o);
                sideProbe(start, dir, a, arr1, arr2, probeLen, allowance);
            }
        }
        outUp1 = body->up;
        outUp1 = scale(outUp1, arr1.resolve(s16, partMoving));
        float r2 = arr2.resolve(s16, partMoving);
        if (r2 > zero) {
            outUp2 = scale(body->up, r2);
        }
        outBox1 = add(b1min, b1max);
        outBox2 = add(b2min, b2max);
        return arr1.count != 0;
    }

    // ---- Walls --------------------------------------------------------------------------------------

    // Wall-average accumulator (+0xc4; cleared by 0x1c8394, updated by 0x2c4b70).
    struct WallAverage {
        bool valid = false;
        V3 sumP, sumN;
        uint32_t count = 0;
        V3 avgP, avgN;
    } wallAverage;

    // 0x2c4b70: walls in front of the player whose normal is nearly horizontal (|g| <= cos 85
    // degrees) and that face the player (horizontal normal . front <= -0.5) are averaged.
    void accumulateWall(const V3& p, const V3& n, bool codeIsNoAction) {
        if (codeIsNoAction) {
            return;
        }
        V3 d = sub(p, body->pos);
        float s1 = d.x * body->front.x;
        s1 = s1 + d.y * body->front.y;
        s1 = s1 + d.z * body->front.z;
        if (s1 <= 0.0f) {
            return;
        }
        float g = n.x * body->groundUp.x;
        g = g + n.y * body->groundUp.y;
        g = g + n.z * body->groundUp.z;
        if (sfb(g) > 0x3db27eb6) {
            return;
        }
        if (fb(g) > 0xbdb27eb6u) {
            return;
        }
        float k = body->up.x * n.x;
        k = k + body->up.y * n.y;
        k = -k - body->up.z * n.z;
        V3 h = addScaled(body->up, n, k);
        float h2 = h.x * h.x;
        h2 = h2 + h.y * h.y;
        h2 = h2 + h.z * h.z;
        if (captain_toad::RecoveredMath::fbits(0x358637be) > h2) {
            return;
        }
        normalize(h);
        float f = h.x * body->front.x;
        f = f + h.y * body->front.y;
        f = f + h.z * body->front.z;
        if (fb(f) < 0xbf000000u) {
            return;
        }
        auto& average = wallAverage;
        if (!average.valid) {
            average.sumP = p;
            average.sumN = n;
            average.valid = true;
        } else {
            average.sumP = add(average.sumP, p);
            average.sumN = add(average.sumN, n);
        }
        average.count++;
        float inverse = 1.0f / float(average.count);
        average.avgP = scale(average.sumP, inverse);
        average.avgN = scale(average.sumN, inverse);
        normalize(average.avgN);
    }

    // 0x2c4dc8: a thin wall, i.e. some surface crosses the segment p - 5n -> p + 5n.
    bool thinWall(const V3& p, const V3& n) {
        V3 dir = scale(n, 10.0f);
        V3 back = scale(n, 5.0f);
        V3 start = sub(p, back);
        std::vector<captain_toad::RecoveredCollision::HitInfo> hits;
        return captain_toad::RecoveredCollision::segmentCheck(parts, start, dir, hits) != 0;
    }

    // 0x2c48a8: sort a wall contact into the front / back / side lists.
    void classifyWall(const V3& p, const V3& n, int part, float depth, const V3& side) {
        float k = groundUpDot(n);
        V3 h = addScaled(body->groundUp, n, k);
        normalize(h);
        auto isOpposed = [](const V3& a, const V3& b) {   // 0x2c4178: a.b <= -0.7071 (on the bits)
            float d = a.x * b.x;
            d = d + a.y * b.y;
            d = d + a.z * b.z;
            return uint64_t(fb(d)) + 0x40cafb0du >= 0x100000000ull;
        };
        auto isAligned = [](const V3& a, const V3& b) {   // 0x1d82f4: a.b >= 0.7071 (signed bits)
            float d = a.x * b.x;
            d = d + a.y * b.y;
            d = d + a.z * b.z;
            return int64_t(sfb(d)) + int64_t(int32_t(0xc0cafb0du)) >= 0;
        };
        auto frontOfPlayer = [&]() {
            V3 d = sub(p, body->pos);
            float f = d.x * body->front.x;
            f = f + d.y * body->front.y;
            f = f + d.z * body->front.z;
            return f > 0.0f;
        };
        if (isOpposed(h, body->front)) {
            // A wall in front.
            if (thinWall(p, n)) {
                return;
            }
            lists[2].offer(n, depth, part, 0, 0, 0);   // +0x4c
            lists[6].offer(n, depth, part, 0, 0, 0);   // +0x5c
            addUniquePart(1, part);                    // +0xb4
            addUniquePart(3, part);                    // +0xbc
            return;
        }
        if (isAligned(h, body->front)) {
            // A wall behind.
            lists[3].offer(n, depth, part, 0, 0, 0);   // +0x50
            addUniquePart(4, part);                    // +0xc0
            return;
        }
        // A wall at one side.
        float sideDot = side.x * h.x;
        sideDot = sideDot + side.y * h.y;
        sideDot = sideDot + side.z * h.z;
        if (sideDot > 0.0f) {
            if (thinWall(p, n)) {
                return;
            }
            lists[4].offer(n, depth, part, 0, 0, 0);   // +0x54
            addUniquePart(frontOfPlayer() ? 3 : 4, part);
        } else {
            if (thinWall(p, n)) {
                return;
            }
            lists[5].offer(n, depth, part, 0, 0, 0);   // +0x58
            addUniquePart(frontOfPlayer() ? 3 : 4, part);
        }
    }

    // 0x1849e8: record one body-sphere wall contact.
    void recordWall(uint32_t i, const V3& side) {
        const auto& hit = spheres.hits.at(i);
        accumulateWall(hit.contact, hit.faceNormal, false);
        classifyWall(hit.contact, hit.faceNormal, hit.part, hit.depth, side);
    }

    // The body-sphere check (+0x14 -> 0x2146d8); its direction argument is the zero vector.
    void runSpheres(const V3& centre, float radius) {
        spheres.clear();
        captain_toad::RecoveredCollision::sphereCheck(parts, centre, radius, V3{0, 0, 0}, spheres.hits);
    }

    bool partMovingFixed(int part) const { return part >= 0 && partMoving.at(part); /* && +0x118 == -1 */ }

    // 0x183a48 (with the floor flag 0): wall push-outs of the lower body sphere.
    void lowerSphere(V3& out1, V3& out2, const V3& centre, const V3& side) {
        float radius = params.chestRadius;   // 0x2c470c
        runSpheres(centre, radius);
        V3 b1min{}, b1max{}, b2min{}, b2max{};
        for (uint32_t i = 0; i < spheres.count(); i++) {
            const auto& hit = spheres.hits[i];
            const V3& n = hit.faceNormal;
            float g = body->groundUp.x * n.x;
            g = g + body->groundUp.y * n.y;
            float g2 = g;
            g2 = g2 + body->groundUp.z * n.z;
            if (sfb(g2) >= 0x3f000000) {
                continue;   // floor-like: handled only with the floor flag
            }
            g = g + body->groundUp.z * n.z;
            if (fb(g) > 0xbf5db3d7u) {
                continue;   // ceiling-like
            }
            // 0x182c24: wall push (the direction +0x7c is zero for these checks).
            float dd = hit.projected.x * n.x;
            dd = dd + hit.projected.y * n.y;
            dd = dd + hit.projected.z * n.z;
            V3 push = scale(n, hit.depth);
            if (dd > 0.0f) {
                push = add(push, hit.projected);
            }
            accumulate(b1min, b1max, push);
            if (partMovingFixed(hit.part)) {
                accumulate(b2min, b2max, push);
            }
            recordWall(i, side);
        }
        out1 = add(b1min, b1max);
        out2 = add(b2max, b2min);
    }

    // 0x183e44: the upper (chest) sphere: walls and ceilings.
    void upperSphere(V3& out1, V3& out2, V3& out3, V3& out4, const V3& centre, const V3& side) {
        runSpheres(centre, params.chestRadius);
        // Stack slots sp+0x44/0x64, 0x70/0x7c, 0x88/0x94, 0xa0/0xac.
        V3 a1{}, a2{}, b1{}, b2{}, c1{}, c2{}, d1{}, d2{};
        const float zero = 0.0f;
        for (uint32_t i = 0; i < spheres.count(); i++) {
            const auto& hit = spheres.hits[i];
            const V3& n = hit.faceNormal;
            float nd = n.x * hit.projected.x;
            nd = nd + n.y * hit.projected.y;
            nd = nd + n.z * hit.projected.z;
            V3 directionPush = nd > zero ? hit.projected : V3{0, 0, 0};
            float crouch = float(counter28);
            if (crouch < zero) {
                crouch = zero;
            } else if (crouch > 1.0f) {
                crouch = 1.0f;
            }
            float g = body->groundUp.x * n.x;
            g = g + body->groundUp.y * n.y;
            float gz = g + body->groundUp.z * n.z;
            bool wall;
            if (flag24 || fb(crouch) == 0x3f800000u) {
                wall = sfb(gz) < 0x3f000000 && fb(gz) <= 0xbf5db3d7u;
            } else {
                wall = !(int64_t(sfb(gz)) + int64_t(int32_t(0xc0cafb0du)) >= 0) && fb(gz) <= 0xbf5db3d7u;
            }
            if (wall) {
                V3 push = add(scale(n, hit.depth), directionPush);
                accumulate(a1, a2, push);
                if (partMovingFixed(hit.part)) {
                    accumulate(b1, b2, push);
                }
                accumulateWall(hit.contact, n, false);
                classifyWall(hit.contact, n, hit.part, hit.depth, side);
                float f = n.x * body->front.x;
                f = f + n.y * body->front.y;
                f = f + n.z * body->front.z;
                if (uint64_t(fb(f)) + 0x40cafb0du >= 0x100000000ull) {
                    flagC8 = true;
                }
                continue;
            }
            float gc = body->groundUp.x * n.x;
            gc = gc + body->groundUp.y * n.y;
            gc = gc + body->groundUp.z * n.z;
            if (fb(gc) <= 0xbf5db3d7u) {
                continue;   // floors are not handled here
            }
            // A ceiling.
            float depth = hit.depth;
            V3 push = add(scale(n, depth), directionPush);
            accumulate(a1, a2, push);
            bool moving = partMovingFixed(hit.part);
            if (moving) {
                accumulate(b1, b2, push);
            }
            lists[1].offer(n, depth, hit.part, 0, 0, 0);   // +0x48
            addUniquePart(2, hit.part);                    // +0xb8
            V3 dp = sub(hit.contact, body->pos);
            float f = dp.x * body->front.x;
            f = f + dp.y * body->front.y;
            f = f + dp.z * body->front.z;
            addUniquePart(f > zero ? 3 : 4, hit.part);
            // Sideways slide away from a sloped ceiling.
            float k = upDotNeg(n);
            V3 w = addScaled(body->up, n, k);
            float w2 = w.x * w.x;
            w2 = w2 + w.y * w.y;
            w2 = w2 + w.z * w.z;
            if (w2 < captain_toad::RecoveredMath::fbits(0x358637be)) {
                w = V3{0, 0, 0};
            } else {
                normalize(w);
                float c = n.x * body->up.x;
                c = c + n.y * body->up.y;
                c = c + n.z * body->up.z;
                float cd = c * depth;
                float sq = cd * cd;
                float r = -sq + depth * depth;
                if (zero > r) {
                    r = zero;
                }
                float rt = std::sqrt(r);
                if (rt > zero) {
                    float m = sq / rt;
                    m = m + rt;
                    float cap = 40.0f;
                    if (cap >= m) {
                        cap = m;
                    }
                    w = scale(w, cap);
                } else {
                    w = V3{0, 0, 0};
                }
            }
            accumulate(c1, c2, w);
            if (moving) {
                accumulate(d1, d2, w);
            }
        }
        out1 = add(a1, a2);
        out2 = add(b1, b2);
        out3 = add(c1, c2);
        out4 = add(d1, d2);
    }

    // 0x2c3f68: the upper sphere's centre (moved down while crouching: counter28 and the
    // +0x38/+0x3c ratio).
    V3 upperCentre(const V3& pos) {
        float tall = params.tall;
        float chest = params.chestRadius;
        V3 base = add(scale(body->up, tall - chest), pos);   // 0x1d835c
        float t = float(counter28);
        if (t < 0.0f) {
            t = 0.0f;
        } else if (t > 1.0f) {
            t = 1.0f;
        }
        if (flag24) {
            t = 1.0f;
        }
        float s17 = t;
        if (counter38 != 0 && counter3c != 0) {
            float r = float(counter38) / float(counter3c);
            if (r < 0.0f) {
                r = 0.0f;
            } else if (r > 1.0f) {
                r = 1.0f;
            }
            if (s17 <= r) {
                s17 = r;
            }
        }
        if (s17 <= 0.0f) {
            return base;
        }
        V3 c = add(scale(body->up, chest), pos);   // 0x2c4128
        c = add(c, scale(scale(body->up, 10.0f), body->scale));
        V3 out = scale(base, 1.0f - s17);
        return add(out, scale(c, s17));
    }

    // 0x182e14: the actor-collision pass, active only while the "ClimbMove" animation plays.
    void climbPass(V3& out1, V3& out2) {
        out1 = V3{0, 0, 0};
        out2 = V3{0, 0, 0};
        if (enableActorPass && animation == "ClimbMove") {
            throw std::runtime_error("collider: ClimbMove pass (0x182e14) not ported");
        }
    }

    // ---- Movement -------------------------------------------------------------------------------------

    // 0x2c4e8c: one collision sub-step: pos += step, then the contact routines; writes pushA / pushB.
    void step(V3& pos, const V3& stepVector, const V3& probeDir, const V3& side, float allowance, bool skipGround,
              bool frontSlow) {
        (void)frontSlow;
        pos = add(pos, stepVector);
        V3 g1{}, g2{}, g3{}, g4{};
        if (!skipGround) {
            float s0 = params.tall * 0.42f;   // 0x2c53a4 * 0x3ed70a3d
            V3 a = add(scale(body->up, s0), pos);
            groundContact(g1, g2, g3, g4, a, probeDir, allowance);
        }
        V3 c1 = add(scale(body->up, params.chestRadius), pos);   // 0x2c470c, 0x2c4128
        V3 lift = scale(body->up, 10.0f);
        c1 = add(c1, scale(lift, body->scale));
        V3 l1{}, l2{};
        lowerSphere(l1, l2, c1, side);
        V3 c2 = upperCentre(pos);
        V3 dc = sub(c2, c1);
        float du = dc.x * body->up.x;
        du = du + dc.y * body->up.y;
        du = du + dc.z * body->up.z;
        if (du < 0.0f) {
            c2 = c1;
        }
        V3 u1{}, u2{}, u3{}, u4{};
        upperSphere(u1, u2, u3, u4, c2, side);
        V3 k1{}, k2{};
        climbPass(k1, k2);

        // Combine the pushes per axis: largest negative plus largest positive.
        V3 mn{}, mx{}, mn2{}, mx2{};
        accumulate(mn, mx, l1);
        accumulate(mn, mx, u1);
        accumulate(mn, mx, g1);
        accumulate(mn, mx, k1);
        accumulate(mn2, mx2, l2);
        accumulate(mn2, mx2, u2);
        accumulate(mn2, mx2, g2);
        accumulate(mn2, mx2, k2);
        if (ceiling() && onGround()) {
            accumulate(mn, mx, g3);
            accumulate(mn, mx, u3);
            accumulate(mn2, mx2, g4);
            accumulate(mn2, mx2, u4);
        }
        V3 A = add(mn, mx);
        V3 B = add(mn2, mx2);
        float b2 = B.x * B.x;
        b2 = b2 + B.y * B.y;
        b2 = b2 + B.z * B.z;
        if (!(b2 < captain_toad::RecoveredMath::fbits(0x2edbe6fe))) {
            float bl = std::sqrt(b2);
            V3 nb = scale(B, 1.0f / bl);
            float projA = nb.x * A.x;
            projA = projA + nb.y * A.y;
            projA = projA + nb.z * A.z;
            if (bl > projA) {
                A = sub(A, scale(nb, projA));
                A = add(A, B);
            }
        }
        pushA = A;
        pushB = B;
    }

    // 0x2c5868: clear the contact lists, parts lists, wall average and flags.
    void clearContacts() {
        for (auto& list : lists) {
            list.clear();
        }
        for (auto& list : partsLists) {
            list.clear();
        }
        wallAverage.valid = false;   // 0x1c8394 (the averages persist)
        wallAverage.count = 0;
        flagC8 = false;
        flagC9 = false;
    }

    // 0x2c5470: move by `move` in sub-steps of at most 27 units; allowance = snap allowance (60 * scale).
    void moveAndCollide(const V3& move, float allowance, bool skipGround) {
        if (counter28) {
            counter28--;
        }
        if (counter38) {
            counter38--;
        }
        V3 pos = body->pos;
        // front . velocity < 5 (signed bits) when standing on ground (the 0x2ed8b8 area check passes).
        bool frontSlow = false;
        {
            float d = body->front.x * body->vel.x;
            d = d + body->front.y * body->vel.y;
            d = d + body->front.z * body->vel.z;
            frontSlow = sfb(d) < 0x40a00000;
        }
        const float inverse27 = captain_toad::RecoveredMath::fbits(0x3d17b426);
        const float one = 1.0f;
        float moveLengthSquared = move.x * move.x;
        moveLengthSquared = moveLengthSquared + move.y * move.y;
        moveLengthSquared = moveLengthSquared + move.z * move.z;
        float moveLength = std::sqrt(moveLengthSquared);
        uint32_t n = uint32_t(moveLength * inverse27) + 1u;   // VCVT.U32.F32 truncates
        V3 stepVector = scale(move, one / float(n));
        // The ground probe points down by 42% of the height plus the allowance plus 10 * scale.
        V3 probeDir = body->up;
        float s0 = params.tall * 0.42f;
        s0 = s0 + allowance;
        s0 = -s0 + body->scale * -10.0f;   // VNMLS
        probeDir = scale(probeDir, s0);
        V3 side = cross(body->front, body->up);
        normalize(side);
        for (uint32_t i = 0; i < n; i++) {
            step(pos, stepVector, probeDir, side, allowance, skipGround, frontSlow);
            float a2 = pushA.x * pushA.x;
            a2 = a2 + pushA.y * pushA.y;
            a2 = a2 + pushA.z * pushA.z;
            if (sfb(a2) > 0x44364000) {   // |push|^2 > 729: apply a large push in sub-steps too
                float al = std::sqrt(a2);
                uint32_t m = uint32_t(al * inverse27) + 1u;
                V3 subStep = scale(pushA, one / float(m));
                for (uint32_t k = 0; k < m; k++) {
                    step(pos, subStep, probeDir, side, allowance, skipGround, frontSlow);
                    pos = add(pos, pushB);
                }
            } else {
                pos = add(pos, pushA);
            }
        }
        body->pos = pos;
        // 0x1848c4: "PlayerRestrictedPlane" areas are not present in the opening map.
    }

    // 0x182350: one collider tick on the ground (ground follow, list reset, move, extra terms cleared).
    void tick() {
        V3 follow{};
        if (onGround()) {
            // 0x1e589c follows moving ground parts; the opening's terrain part is static, for which the
            // follow routine returns zero deltas (traced: it exits after its two vtable checks).
            int groundPart = lists[0].part;
            if (groundPart >= 0 && groundPart < int(parts.size()) && parts[size_t(groundPart)].moved) {
                // The part's movement since the previous tick (its current matrix times the inverse
                // of the previous one) carries the player's position and front; the turned frame
                // is then turned back so its up is the player's up again (the second half of
                // 0x1e589c: 0x2f09ac / 0x2f06fc, then the front 0x2dc0a8 and the up 0x2dab6c), i.e.
                // the front keeps only its part across the up. Not ported: the follow deltas for the
                // inherited air velocity (+0x80, +0x8c).
                const auto& part = parts[size_t(groundPart)];
                const V3 local = captain_toad::RecoveredVec::mulPoint(part.previousInverse, body->pos);
                body->pos = captain_toad::RecoveredVec::mulPoint(part.world, local);
                V3 f = captain_toad::RecoveredVec::mulDir(
                    part.world, captain_toad::RecoveredVec::mulDir(part.previousInverse, body->front));
                const float along = captain_toad::RecoveredVec::dot(f, body->up);
                f = captain_toad::RecoveredVec::sub(f, captain_toad::RecoveredVec::scale(body->up, along));
                if (captain_toad::RecoveredVec::dot(f, f) >= 1.0e-6f) {
                    captain_toad::RecoveredVec::normalize(f);
                    body->front = f;
                }
            }
        }
        clearContacts();
        if (enableActorPass && animation == "ClimbMove") {
            throw std::runtime_error("collider: ClimbMove table (0x2c5754) not ported");
        }
        counter34 = 0;
        V3 move = add(body->vel, extra68);
        move = add(move, extra74);
        move = add(move, follow);
        moveAndCollide(move, body->scale * 60.0f, false);
        extra68 = V3{0, 0, 0};   // 0x2c5730
        extra74 = V3{0, 0, 0};
    }

    // 0x184b0c (vtable +0x08): the airborne tick. No ground snap (allowance 0). The inherited ground
    // velocity (+0x80, set by vtable +0x20 from the ground follow delta +0x8c and decayed by
    // FollowFront/BackDamper) is zero on the opening's static terrain.
    void airTick() {
        clearContacts();
        if (enableActorPass && animation == "ClimbMove") {
            throw std::runtime_error("collider: ClimbMove table (0x2c5754) not ported");
        }
        V3 move = add(body->vel, V3{0, 0, 0});
        move = add(move, extra68);
        move = add(move, extra74);
        moveAndCollide(move, 0.0f, false);
        extra68 = V3{0, 0, 0};   // 0x2c5730
        extra74 = V3{0, 0, 0};
    }

    // 0x18262c: cut the velocity components that run into active contacts (bits of `skip` = skip mask).
    void cutVelocity(uint32_t skip) {
        const float limit = params.cutVelLimit;
        const float rate = params.cutVelRate;
        V3& v = body->vel;
        auto cut = [&](const V3& axis, float d) {
            float k = axis.x * v.x;
            k = k + axis.y * v.y;
            k = -k - axis.z * v.z;
            v = addScaled(axis, v, k);
            V3 t = scale(axis, d);
            t = scale(t, rate);
            v = add(v, t);
        };
        auto dotVelocity = [&](const V3& a) {
            float d = a.x * v.x;
            d = d + a.y * v.y;
            d = d + a.z * v.z;
            return d;
        };
        float d = dotVelocity(body->groundUp);
        if (!(skip & 1) && onGround() && d < -limit) {
            cut(body->groundUp, d);   // falling into the ground
        }
        if (!(skip & 2) && ceiling() && d > limit) {
            cut(body->groundUp, d);   // rising into a ceiling
        }
        d = dotVelocity(body->front);
        if (!(skip & 4) && lists[2].valid && d > limit) {
            cut(body->front, d);   // into a wall in front
        }
        if (!(skip & 8) && lists[3].valid && d < -limit) {
            cut(body->front, d);   // into a wall behind
        }
        V3 side = cross(body->front, body->up);
        normalize(side);
        d = dotVelocity(side);
        if (!(skip & 0x20) && lists[5].valid && d > limit) {
            cut(side, d);
        }
        if (!(skip & 0x10) && lists[4].valid && d < -limit) {
            cut(side, d);
        }
        if (ceiling() && onGround()) {
            float k = body->up.x * pushB.x;
            k = k + body->up.y * pushB.y;
            k = -k - body->up.z * pushB.z;
            V3 w = addScaled(body->up, pushB, k);
            float w2 = w.x * w.x;
            w2 = w2 + w.y * w.y;
            w2 = w2 + w.z * w.z;
            if (!(w2 < captain_toad::RecoveredMath::fbits(0x358637be))) {
                normalize(w);
                float s = w.x * v.x;
                s = s + w.y * v.y;
                s = s + w.z * v.z;
                if (s < 0.0f) {
                    v = sub(v, scale(w, s));
                }
            }
        }
    }

    // 0x2c53dc: per-axis minimum and maximum.
    static void accumulate(V3& mn, V3& mx, const V3& v) {
        mn.x = (mn.x <= v.x) ? mn.x : v.x;
        mn.y = (mn.y <= v.y) ? mn.y : v.y;
        mn.z = (mn.z <= v.z) ? mn.z : v.z;
        mx.x = (v.x < mx.x) ? mx.x : v.x;
        mx.y = (v.y < mx.y) ? mx.y : v.y;
        mx.z = (v.z < mx.z) ? mx.z : v.z;
    }
};

}   // namespace captain_toad::RecoveredPlayerCollider
