#include "physics/rigid/PrimitiveContacts.hpp"

#include <cmath>

namespace beacon::physics::rigid {

using math::Vec3;

namespace {

Vec3 column(const Mat3& m, int c) {
    return c == 0 ? Vec3{m.r[0].x, m.r[1].x, m.r[2].x}
                  : c == 1 ? Vec3{m.r[0].y, m.r[1].y, m.r[2].y} : Vec3{m.r[0].z, m.r[1].z, m.r[2].z};
}

// World vector into a shape's frame (transpose of its rotation).
Vec3 toLocal(const Pose& p, const Vec3& v) { return apply(transpose(p.rotation), v); }

float safeLength(const Vec3& v) {
    const float l2 = math::dot(v, v);
    return l2 > 1.17549435e-38f ? std::sqrt(l2) : 0.0f;
}

float clampTo(float v, float half) { return -half > v ? -half : v > half ? half : v; }

}   // namespace

ConvexContact sphereSphere(float radiusA, const Pose& a, float radiusB, const Pose& b) {
    const Vec3 d = b.translation - a.translation;
    const float l2 = math::dot(d, d);
    const float len = std::sqrt(l2);
    ConvexContact c;
    c.distance = len - radiusA - radiusB;
    c.normal = l2 > 1e-30f ? d * (1.0f / len) : Vec3{0, 0, 1};
    c.pointA = toLocal(a, c.normal * radiusA);
    c.pointB = toLocal(b, c.normal * -radiusB);
    return c;
}

ConvexContact boxSphere(const Vec3& half, const Pose& a, float radius, const Pose& b) {
    const Vec3 p = toLocal(a, b.translation - a.translation);
    const float pv[3] = {p.x, p.y, p.z}, hv[3] = {half.x, half.y, half.z};
    // The face: the axis with the largest gap |p| - h (later axes win only when larger).
    float gap[3], sign[3];
    for (int k = 0; k < 3; ++k) {
        gap[k] = std::fabs(pv[k]) - hv[k] - radius;
        sign[k] = std::signbit(pv[k]) ? -1.0f : 1.0f;
    }
    int i = gap[1] > gap[0] ? 1 : 0;
    if (gap[2] > std::max(gap[1], gap[0])) {
        i = 2;
    }
    const int j = (i + 1) % 3, k = (i + 2) % 3;
    // Closest point: on the face's plane, the other two coordinates clamped to the box.
    float cp[3];
    cp[i] = hv[i] * sign[i];
    cp[j] = clampTo(pv[j], hv[j]);
    cp[k] = clampTo(pv[k], hv[k]);
    const float inside = sign[i] * (pv[i] - sign[i] * hv[i]);   // < 0: centre inside along the face axis
    const Vec3 v{pv[0] - cp[0], pv[1] - cp[1], pv[2] - cp[2]};
    const float l2 = math::dot(v, v);
    const float len = std::sqrt(l2);
    Vec3 nLocal;
    if (inside < 0 || l2 < 1e-30f) {
        float f[3] = {0, 0, 0};
        f[i] = sign[i];
        nLocal = {f[0], f[1], f[2]};
    } else {
        nLocal = v * (1.0f / len);
    }
    ConvexContact c;
    c.normal = apply(a.rotation, nLocal);
    c.pointA = {cp[0], cp[1], cp[2]};
    c.pointB = toLocal(b, c.normal * -radius);
    c.distance = (inside < 0 ? -len : len) - radius;
    return c;
}

ConvexContact capsuleSphere(float halfLength, float radiusA, const Pose& a, float radiusB, const Pose& b) {
    const Vec3 axis = column(a.rotation, 0);
    const Vec3 d = b.translation - a.translation;
    const float t = clampTo(math::dot(axis, d), halfLength);
    const Vec3 onAxis = axis * t;
    const Vec3 v = d - onAxis;
    const float l2 = math::dot(v, v);
    ConvexContact c;
    c.distance = safeLength(v) - radiusA - radiusB;
    c.normal = l2 > 1e-6f ? v * (1.0f / std::sqrt(l2)) : Vec3{1, 0, 0};
    c.pointA = toLocal(a, onAxis + c.normal * radiusA);
    c.pointB = toLocal(b, c.normal * -radiusB);
    return c;
}

ConvexContact capsuleCapsule(float halfLengthA, float radiusA, const Pose& a, float halfLengthB, float radiusB,
                             const Pose& b) {
    const Vec3 ua = column(a.rotation, 0), ub = column(b.rotation, 0);
    const Vec3 d = b.translation - a.translation;
    const float ab = math::dot(ua, ub), ad = math::dot(ua, d), bd = math::dot(ub, d);
    // Closest points of the two segments: s along A, t along B.
    const float den = 1.0f - ab * ab;
    float s = den != 0 ? clampTo((ad - ab * bd) / den, halfLengthA) : 0.0f;
    float t = ab * s - bd;
    if (-halfLengthB > t) {
        t = -halfLengthB;
        s = clampTo(ab * t + ad, halfLengthA);
    } else if (t > halfLengthB) {
        t = halfLengthB;
        s = clampTo(t * ab + ad, halfLengthA);
    }
    const Vec3 pa = ua * s, pb = ub * t;
    const Vec3 v = pb + (d - pa);
    const float l2 = math::dot(v, v);
    ConvexContact c;
    c.distance = safeLength(v) - radiusA - radiusB;
    c.normal = l2 > 1e-6f ? v * (1.0f / std::sqrt(l2)) : Vec3{1, 0, 0};
    c.pointA = toLocal(a, pa + c.normal * radiusA);
    c.pointB = toLocal(b, pb - c.normal * radiusB);
    return c;
}

namespace {

constexpr float kVoronoiTolerance = -1e-5f;   // bevels Voronoi planes when features are parallel

struct V3 {
    float v[3];
    float& operator[](int i) { return v[i]; }
    float operator[](int i) const { return v[i]; }
};

V3 toV3(const Vec3& a) { return {{a.x, a.y, a.z}}; }
Vec3 toVec(const V3& a) { return {a[0], a[1], a[2]}; }
float dot3(const V3& a, const V3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
float lengthSqr3(const V3& a) { return dot3(a, a); }

// Closest-feature search in the face frame (z = face normal) shared by the box-capsule tests.
struct FaceSearch {
    V3 halfA, offset, direction, signs, scales;
    float halfB = 0;
    // Results.
    float minDistSqr = 0;
    V3 closest{};
    V3 pointA{};
    float paramB = 0;
    bool done = false;

    // Capsule end (signB scaleB) against the face.
    float endpointTest(float& t0, float& t1, V3& pts, float signB, float scaleB) {
        V3 e;
        for (int k = 0; k < 3; ++k) {
            e[k] = offset[k] + direction[k] * scaleB;
        }
        t0 = clampTo(e[0], halfA[0]);
        t1 = clampTo(e[1], halfA[1]);
        e[0] -= t0;
        e[1] -= t1;
        pts = e;
        done = -signB * dot3(pts, direction) >= kVoronoiTolerance;
        return lengthSqr3(pts);
    }

    void endpointTests(float signB, float scaleB, bool first) {
        float t0, t1;
        V3 pts;
        float d = endpointTest(t0, t1, pts, signB, scaleB);
        if (first || d < minDistSqr) {
            set(d, pts, t0, t1, scaleB);
        }
        if (done) {
            return;
        }
        d = endpointTest(t0, t1, pts, -signB, -scaleB);
        if (d < minDistSqr) {
            set(d, pts, t0, t1, -scaleB);
        }
    }

    void set(float d, const V3& pts, float t0, float t1, float b) {
        minDistSqr = d;
        closest = pts;
        pointA[0] = t0;
        pointA[1] = t1;
        paramB = b;
    }

    // Box edge (centre offset along ac, direction ad) against the capsule's segment.
    float edgeTest(int ac, int ad, float& tA, float& tB, V3& pts) {
        pts = offset;
        pts[ac] -= scales[ac];
        const float dirAd = direction[ad], ptsAd = pts[ad];
        const float dirDotPts = dot3(direction, pts);
        const float denom = 1.0f - dirAd * dirAd;
        tA = denom == 0.0f ? 0.0f : clampTo((ptsAd - dirDotPts * dirAd) / denom, halfA[ad]);
        tB = tA * dirAd - dirDotPts;
        if (tB < -halfB) {
            tB = -halfB;
            tA = clampTo(tB * dirAd + ptsAd, halfA[ad]);
        } else if (tB > halfB) {
            tB = halfB;
            tA = clampTo(tB * dirAd + ptsAd, halfA[ad]);
        }
        for (int k = 0; k < 3; ++k) {
            pts[k] += direction[k] * tB;
        }
        const float c_ac = pts[ac] * signs[ac], c_2 = pts[2] * signs[2];
        done = c_ac >= kVoronoiTolerance * c_2 && c_2 >= kVoronoiTolerance * c_ac;
        pts[ad] -= tA;
        return lengthSqr3(pts);
    }

    void edgeTests(bool first) {
        float tA, tB;
        V3 pts;
        auto setEdge = [&](float d, int ac, int ad) {
            minDistSqr = d;
            closest = pts;
            pointA[ac] = scales[ac];
            pointA[ad] = tA;
            paramB = tB;
        };
        float d = edgeTest(0, 1, tA, tB, pts);
        if (first || d < minDistSqr) {
            setEdge(d, 0, 1);
        }
        if (done) {
            return;
        }
        signs[0] = -signs[0];
        scales[0] = -scales[0];
        d = edgeTest(0, 1, tA, tB, pts);
        if (d < minDistSqr) {
            setEdge(d, 0, 1);
        }
        if (done) {
            return;
        }
        d = edgeTest(1, 0, tA, tB, pts);
        if (d < minDistSqr) {
            setEdge(d, 1, 0);
        }
        if (done) {
            return;
        }
        signs[1] = -signs[1];
        scales[1] = -scales[1];
        d = edgeTest(1, 0, tA, tB, pts);
        if (d < minDistSqr) {
            setEdge(d, 1, 0);
        }
    }
};

}   // namespace

ConvexContact boxCapsule(const Vec3& half, const Pose& a, float halfLength, float radius, const Pose& b) {
    const Mat3 aInv = transpose(a.rotation);
    const Vec3 dirB = column(b.rotation, 0);
    const V3 dir = toV3(apply(aInv, dirB));
    const V3 offset = toV3(apply(aInv, b.translation - a.translation));
    const V3 h = toV3(half);
    ConvexContact c;

    // Separating axes: the box's faces, then the box axes crossed with the capsule's axis; the
    // largest gap wins (a gap beyond the threshold ends the test: the threshold is 1e38 here).
    bool crossAxis = false;
    int faceDim = 0, edgeDim = 0;
    V3 axis{{1, 0, 0}};
    float maxGap = 0;
    for (int k = 0; k < 3; ++k) {
        const float gap = std::fabs(offset[k]) - h[k] - std::fabs(dir[k]) * halfLength;
        if (k == 0 || gap > maxGap) {
            maxGap = gap;
            faceDim = k;
            axis = V3{{0, 0, 0}};
            axis[k] = 1;
        }
    }
    // Columns of [-dir]x (cross products of the box axes with the capsule axis).
    const V3 crossCols[3] = {{{0, -dir[2], dir[1]}}, {{dir[2], 0, -dir[0]}}, {{-dir[1], dir[0], 0}}};
    // The library forms the gaps from the transposed matrix, i.e. with [dir]x's columns negated.
    for (int k = 0; k < 3; ++k) {
        const V3& col = crossCols[k];
        const float lsqr = lengthSqr3(col);
        if (lsqr > 1e-30f) {
            const float proj = std::fabs(dot3(col, offset));
            const float projHalf = std::fabs(col[0]) * h[0] + std::fabs(col[1]) * h[1] + std::fabs(col[2]) * h[2];
            const float lRecip = 1.0f / std::sqrt(lsqr);
            const float gap = (proj - projHalf) * lRecip;
            if (gap > maxGap) {
                maxGap = gap;
                crossAxis = true;
                edgeDim = k;
                axis = V3{{-col[0] * lRecip, -col[1] * lRecip, -col[2] * lRecip}};
            }
        }
    }
    if (dot3(axis, offset) < 0.0f) {
        for (float& x : axis.v) {
            x = -x;
        }
    }
    // Face frame: z along the face normal (and y along the box edge for a cross axis).
    int dim[3];
    if (crossAxis) {
        const float ax = std::fabs(axis[0]), ay = std::fabs(axis[1]), az = std::fabs(axis[2]);
        dim[1] = edgeDim;
        if (edgeDim == 0) {
            dim[0] = ay > az ? 2 : 1;
            dim[2] = ay > az ? 1 : 2;
        } else if (edgeDim == 1) {
            dim[0] = az > ax ? 0 : 2;
            dim[2] = az > ax ? 2 : 0;
        } else {
            dim[0] = ax > ay ? 1 : 0;
            dim[2] = ax > ay ? 0 : 1;
        }
    } else {
        dim[2] = faceDim;
        dim[0] = (faceDim + 1) % 3;
        dim[1] = (faceDim + 2) % 3;
    }
    auto perm = [&](const V3& v) { return V3{{v[dim[0]], v[dim[1]], v[dim[2]]}}; };
    FaceSearch f;
    f.offset = perm(offset);
    f.halfA = perm(h);
    const V3 axisPerm = perm(axis);
    for (int k = 0; k < 3; ++k) {
        f.signs[k] = std::copysign(1.0f, axisPerm[k]);
        f.scales[k] = f.signs[k] * f.halfA[k];
    }
    f.direction = perm(dir);
    f.halfB = halfLength;
    const float signB = -dot3(dir, axis) > 0.0f ? 1.0f : -1.0f;
    const float scaleB = signB * halfLength;
    f.offset[2] -= f.scales[2];
    if (maxGap < 0.0f) {
        for (int k = 0; k < 3; ++k) {
            f.offset[k] -= axisPerm[k] * maxGap * 1.01f;
        }
    }
    f.pointA[2] = f.scales[2];
    f.scales[2] = 0.0f;
    if (crossAxis) {
        f.edgeTests(true);
        if (!f.done) {
            f.endpointTests(signB, scaleB, false);
        }
    } else {
        f.endpointTests(signB, scaleB, true);
        if (!f.done) {
            f.edgeTests(false);
        }
    }
    auto unperm = [&](const V3& v) {
        V3 r{};
        for (int k = 0; k < 3; ++k) {
            r[dim[k]] = v[k];
        }
        return r;
    };
    const bool centreInside = f.signs[2] * f.closest[2] < 0.0f;
    Vec3 nLocal;
    if (centreInside || f.minDistSqr < 1e-30f) {
        nLocal = toVec(axis);
    } else {
        nLocal = toVec(unperm(f.closest)) * (1.0f / std::sqrt(f.minDistSqr));
    }
    c.normal = apply(a.rotation, nLocal);
    c.pointA = toVec(unperm(f.pointA));
    c.pointB = apply(transpose(b.rotation), dirB * f.paramB - c.normal * radius);
    const float len = std::sqrt(f.minDistSqr);
    c.distance = (centreInside ? -len : len) - radius;
    return c;
}

namespace {

// 3x3 matrix by [row][column] for the box-box test.
struct M3 {
    float m[3][3];
    V3 col(int c) const { return {{m[0][c], m[1][c], m[2][c]}}; }
    V3 operator*(const V3& v) const {
        V3 r{};
        for (int i = 0; i < 3; ++i) {
            r[i] = m[i][0] * v[0] + m[i][1] * v[1] + m[i][2] * v[2];
        }
        return r;
    }
    M3 transposed() const {
        M3 t{};
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                t.m[i][j] = m[j][i];
            }
        }
        return t;
    }
};

M3 toM3(const Mat3& a) {
    return {{{a.r[0].x, a.r[0].y, a.r[0].z}, {a.r[1].x, a.r[1].y, a.r[1].z}, {a.r[2].x, a.r[2].y, a.r[2].z}}};
}

V3 add(const V3& a, const V3& b) { return {{a[0] + b[0], a[1] + b[1], a[2] + b[2]}}; }
V3 scale(const V3& a, float s) { return {{a[0] * s, a[1] * s, a[2] * s}}; }
V3 mulElem(const V3& a, const V3& b) { return {{a[0] * b[0], a[1] * b[1], a[2] * b[2]}}; }
float sq(float x) { return x * x; }

// Closest features of two faces in their face frames (z = face normal).
struct BoxFaces {
    V3 hA, hB, faceAB, faceBA, signsA, signsB, scalesA, scalesB;
    M3 ab, ba;   // B's face axes in A's face frame, and the reverse
    float minDistSqr = 0;
    V3 pointA{}, pointB{};
    bool done = false;

    static bool voronoi3(const V3& f) {
        return f[0] >= kVoronoiTolerance * f[2] && f[1] >= kVoronoiTolerance * f[0] &&
               f[2] >= kVoronoiTolerance * f[1];
    }

    // A corner of B (scalesB x, y on its face) against A's face; the mirrored test with A, B swapped.
    float vertexFaceTest(float& t0, float& t1, const V3& h, const V3& offTo, const V3& offFrom, const M3& to,
                         const M3& from, const V3& signs, const V3& scales) {
        const V3 corner = add(add(offTo, scale(to.col(0), scales[0])), scale(to.col(1), scales[1]));
        t0 = clampTo(corner[0], h[0]);
        t1 = clampTo(corner[1], h[1]);
        V3 face = add(add(offFrom, scale(from.col(0), t0)), scale(from.col(1), t1));
        for (int k = 0; k < 3; ++k) {
            face[k] = (face[k] - scales[k]) * signs[k];
        }
        done = voronoi3(face);
        return sq(corner[0] - t0) + sq(corner[1] - t1) + sq(corner[2]);
    }

    void vertexFaceTests(bool bOnA, bool first) {
        V3& signs = bOnA ? signsB : signsA;
        V3& scales = bOnA ? scalesB : scalesA;
        auto test = [&](bool isFirst) {
            float t0, t1;
            const float d = bOnA ? vertexFaceTest(t0, t1, hA, faceAB, faceBA, ab, ba, signsB, scalesB)
                                 : vertexFaceTest(t0, t1, hB, faceBA, faceAB, ba, ab, signsA, scalesA);
            if (isFirst || d < minDistSqr) {
                minDistSqr = d;
                V3& onFace = bOnA ? pointA : pointB;
                V3& corner = bOnA ? pointB : pointA;
                onFace[0] = t0;
                onFace[1] = t1;
                corner[0] = scales[0];
                corner[1] = scales[1];
            }
        };
        test(first);
        if (done) {
            return;
        }
        signs[0] = -signs[0], scales[0] = -scales[0];
        test(false);
        if (done) {
            return;
        }
        signs[1] = -signs[1], scales[1] = -scales[1];
        test(false);
        if (done) {
            return;
        }
        signs[0] = -signs[0], scales[0] = -scales[0];
        test(false);
    }

    // Edge of A (centre along ac, direction ad) against edge of B (bc, bd).
    float edgeTest(int ac, int ad, int bc, int bd, float& tA, float& tB) {
        V3 offAB = add(faceAB, scale(ab.col(bc), scalesB[bc]));
        offAB[ac] -= scalesA[ac];
        V3 offBA = add(faceBA, scale(ba.col(ac), scalesA[ac]));
        offBA[bc] -= scalesB[bc];
        const float dirDot = ab.col(bd)[ad];
        const float denom = 1.0f - dirDot * dirDot;
        const float offAd = offAB[ad], offBd = offBA[bd];
        tA = denom == 0.0f ? 0.0f : (offAd + offBd * dirDot) / denom;
        tA = clampTo(tA, hA[ad]);
        tB = tA * dirDot + offBd;
        if (tB < -hB[bd]) {
            tB = -hB[bd];
            tA = clampTo(tB * dirDot + offAd, hA[ad]);
        } else if (tB > hB[bd]) {
            tB = hB[bd];
            tA = clampTo(tB * dirDot + offAd, hA[ad]);
        }
        V3 eAB = mulElem(add(offAB, scale(ab.col(bd), tB)), signsA);
        V3 eBA = mulElem(add(offBA, scale(ba.col(ad), tA)), signsB);
        done = eAB[ac] >= kVoronoiTolerance * eAB[2] && eAB[2] >= kVoronoiTolerance * eAB[ac] &&
               eBA[bc] >= kVoronoiTolerance * eBA[2] && eBA[2] >= kVoronoiTolerance * eBA[bc];
        // As published (and as the game selects): tA comes off the sign-multiplied vector, so
        // the value is not the squared distance when signsA[ad] < 0; it only ranks the features.
        eAB[ad] -= tA;
        return dot3(eAB, eAB);
    }

    void edgeTests(bool first) {
        // (ac, ad, bc, bd) per group, and the sign flips between the four tests of a group.
        struct Step {
            char flipSide;   // 'a' or 'b' (before the test), 0: none
            int flipDim;
        };
        auto run = [&](int ac, int ad, int bc, int bd, const Step (&steps)[4], bool firstGroup) {
            for (int i = 0; i < 4; ++i) {
                if (steps[i].flipSide == 'a') {
                    signsA[steps[i].flipDim] = -signsA[steps[i].flipDim];
                    scalesA[steps[i].flipDim] = -scalesA[steps[i].flipDim];
                } else if (steps[i].flipSide == 'b') {
                    signsB[steps[i].flipDim] = -signsB[steps[i].flipDim];
                    scalesB[steps[i].flipDim] = -scalesB[steps[i].flipDim];
                }
                float tA, tB;
                const float d = edgeTest(ac, ad, bc, bd, tA, tB);
                if ((firstGroup && i == 0 && first) || d < minDistSqr) {
                    minDistSqr = d;
                    pointA[ac] = scalesA[ac];
                    pointA[ad] = tA;
                    pointB[bc] = scalesB[bc];
                    pointB[bd] = tB;
                }
                if (done) {
                    return true;
                }
            }
            return false;
        };
        if (run(0, 1, 0, 1, {{0, 0}, {'a', 0}, {'b', 0}, {'a', 0}}, true)) {
            return;
        }
        if (run(1, 0, 0, 1, {{'b', 0}, {'a', 1}, {'b', 0}, {'a', 1}}, false)) {
            return;
        }
        if (run(0, 1, 1, 0, {{'b', 0}, {'a', 0}, {'b', 1}, {'a', 0}}, false)) {
            return;
        }
        run(1, 0, 1, 0, {{'b', 1}, {'a', 1}, {'b', 1}, {'a', 1}}, false);
    }
};

void faceFrame(const V3& absAxis, int edgeDim, int (&dim)[3]) {
    dim[1] = edgeDim;
    if (edgeDim == 0) {
        dim[0] = absAxis[1] > absAxis[2] ? 2 : 1;
        dim[2] = absAxis[1] > absAxis[2] ? 1 : 2;
    } else if (edgeDim == 1) {
        dim[0] = absAxis[2] > absAxis[0] ? 0 : 2;
        dim[2] = absAxis[2] > absAxis[0] ? 2 : 0;
    } else {
        dim[0] = absAxis[0] > absAxis[1] ? 1 : 0;
        dim[2] = absAxis[0] > absAxis[1] ? 0 : 1;
    }
}

int bestFace(const V3& axis) {
    const float x = std::fabs(axis[0]), y = std::fabs(axis[1]), z = std::fabs(axis[2]);
    return x > y && x > z ? 0 : y > z ? 1 : 2;
}

}   // namespace

ConvexContact boxBox(const Vec3& halfA, const Pose& a, const Vec3& halfB, const Pose& b) {
    const V3 hA = toV3(halfA), hB = toV3(halfB);
    const M3 ab = toM3(multiply(transpose(a.rotation), b.rotation));
    const M3 ba = ab.transposed();
    const V3 offAB = toV3(apply(transpose(a.rotation), b.translation - a.translation));
    V3 offBA = ba * offAB;
    for (float& x : offBA.v) {
        x = -x;
    }
    M3 absAB{}, absBA{};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            absAB.m[i][j] = std::fabs(ab.m[i][j]);
            absBA.m[i][j] = std::fabs(ba.m[i][j]);
        }
    }
    enum { AxisA, AxisB, Cross } axisType = AxisA;
    V3 axisA{}, axisB{};
    int faceA = 0, faceB = 0, edgeA = 0, edgeB = 0;
    float maxGap = 0;
    const V3 projB = absAB * hB, projA = absBA * hA;
    for (int k = 0; k < 3; ++k) {
        const float gap = std::fabs(offAB[k]) - hA[k] - projB[k];
        if (k == 0 || gap > maxGap) {
            maxGap = gap, axisType = AxisA, faceA = k;
            axisA = V3{};
            axisA[k] = 1;
        }
    }
    for (int k = 0; k < 3; ++k) {
        const float gap = std::fabs(offBA[k]) - hB[k] - projA[k];
        if (gap > maxGap) {
            maxGap = gap, axisType = AxisB, faceB = k;
            axisB = V3{};
            axisB[k] = 1;
        }
    }
    // Cross axes (both absolute matrices padded by 1e-5 against zero cross products).
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            absAB.m[i][j] += 1e-5f;
            absBA.m[i][j] += 1e-5f;
        }
    }
    const V3 c0 = ba.col(0), c1 = ba.col(1), c2 = ba.col(2);
    const V3 lsq[3] = {add(mulElem(c2, c2), mulElem(c1, c1)), add(mulElem(c2, c2), mulElem(c0, c0)),
                       add(mulElem(c1, c1), mulElem(c0, c0))};
    const V3 projOff[3] = {add(scale(c1, offAB[2]), scale(c2, -offAB[1])),
                           add(scale(c2, offAB[0]), scale(c0, -offAB[2])),
                           add(scale(c0, offAB[1]), scale(c1, -offAB[0]))};
    const V3 aa0 = absBA.col(0), aa1 = absBA.col(1), aa2 = absBA.col(2);
    const V3 projAh[3] = {add(scale(aa1, hA[2]), scale(aa2, hA[1])), add(scale(aa2, hA[0]), scale(aa0, hA[2])),
                          add(scale(aa0, hA[1]), scale(aa1, hA[0]))};
    const V3 bb0 = absAB.col(0), bb1 = absAB.col(1), bb2 = absAB.col(2);
    const V3 projBh[3] = {add(scale(bb1, hB[2]), scale(bb2, hB[1])), add(scale(bb2, hB[0]), scale(bb0, hB[2])),
                          add(scale(bb0, hB[1]), scale(bb1, hB[0]))};
    for (int da = 0; da < 3; ++da) {
        for (int db = 0; db < 3; ++db) {
            const float lsqr = lsq[da][db];
            if (lsqr > 1e-30f) {
                const float lRecip = 1.0f / std::sqrt(lsqr);
                const float g = std::fabs(projOff[da][db]) - projAh[da][db] - projBh[db][da];
                const float gap = g * lRecip;
                if (gap > maxGap) {
                    maxGap = gap, axisType = Cross, edgeA = da, edgeB = db;
                    V3 e{};
                    e[da] = 1;
                    const V3 col = ab.col(db);
                    axisA = scale(V3{{e[1] * col[2] - e[2] * col[1], e[2] * col[0] - e[0] * col[2],
                                      e[0] * col[1] - e[1] * col[0]}},
                                  lRecip);
                }
            }
        }
    }
    auto negate = [](V3 v) {
        for (float& x : v.v) {
            x = -x;
        }
        return v;
    };
    int dimA[3], dimB[3];
    if (axisType == AxisA || axisType == Cross) {
        if (dot3(axisA, offAB) < 0.0f) {
            axisA = negate(axisA);
        }
        axisB = ba * negate(axisA);
        if (axisType == AxisA) {
            faceB = bestFace(axisB);
        }
    } else {
        if (dot3(axisB, offBA) < 0.0f) {
            axisB = negate(axisB);
        }
        axisA = ab * negate(axisB);
        faceA = bestFace(axisA);
    }
    if (axisType == Cross) {
        V3 absA{}, absB{};
        for (int k = 0; k < 3; ++k) {
            absA[k] = std::fabs(axisA[k]);
            absB[k] = std::fabs(axisB[k]);
        }
        faceFrame(absA, edgeA, dimA);
        faceFrame(absB, edgeB, dimB);
    } else {
        dimA[2] = faceA, dimA[0] = (faceA + 1) % 3, dimA[1] = (faceA + 2) % 3;
        dimB[2] = faceB, dimB[0] = (faceB + 1) % 3, dimB[1] = (faceB + 2) % 3;
    }
    auto permA = [&](const V3& v) { return V3{{v[dimA[0]], v[dimA[1]], v[dimA[2]]}}; };
    auto permB = [&](const V3& v) { return V3{{v[dimB[0]], v[dimB[1]], v[dimB[2]]}}; };
    BoxFaces f;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            f.ab.m[i][j] = ab.m[dimA[i]][dimB[j]];
        }
    }
    f.ba = f.ab.transposed();
    const V3 offABp = permA(offAB), offBAp = permB(offBA);
    f.hA = permA(hA);
    f.hB = permB(hB);
    const V3 axisAp = permA(axisA), axisBp = permB(axisB);
    for (int k = 0; k < 3; ++k) {
        f.signsA[k] = std::copysign(1.0f, axisAp[k]);
        f.signsB[k] = std::copysign(1.0f, axisBp[k]);
        f.scalesA[k] = f.signsA[k] * f.hA[k];
        f.scalesB[k] = f.signsB[k] * f.hB[k];
    }
    f.faceAB = add(offABp, scale(f.ab.col(2), f.scalesB[2]));
    f.faceAB[2] -= f.scalesA[2];
    f.faceBA = add(offBAp, scale(f.ba.col(2), f.scalesA[2]));
    f.faceBA[2] -= f.scalesB[2];
    if (maxGap < 0.0f) {
        f.faceAB = add(f.faceAB, scale(axisAp, -maxGap * 1.01f));
        f.faceBA = add(f.faceBA, scale(axisBp, -maxGap * 1.01f));
    }
    f.pointA[2] = f.scalesA[2];
    f.pointB[2] = f.scalesB[2];
    f.scalesA[2] = 0.0f;
    f.scalesB[2] = 0.0f;
    if (axisType == Cross) {
        f.edgeTests(true);
        if (!f.done) {
            f.vertexFaceTests(true, false);
            if (!f.done) {
                f.vertexFaceTests(false, false);
            }
        }
    } else if (axisType == AxisB) {
        f.vertexFaceTests(false, true);
        if (!f.done) {
            f.vertexFaceTests(true, false);
            if (!f.done) {
                f.edgeTests(false);
            }
        }
    } else {
        f.vertexFaceTests(true, true);
        if (!f.done) {
            f.vertexFaceTests(false, false);
            if (!f.done) {
                f.edgeTests(false);
            }
        }
    }
    auto unperm = [](const V3& v, const int (&dim)[3]) {
        V3 r{};
        for (int k = 0; k < 3; ++k) {
            r[dim[k]] = v[k];
        }
        return r;
    };
    ConvexContact c;
    c.pointA = toVec(unperm(f.pointA, dimA));
    c.pointB = toVec(unperm(f.pointB, dimB));
    c.normal = apply(a.rotation, toVec(axisA));
    // Overlapping: the largest gap; apart: the distance between the two closest points (not the
    // feature test's value, see edgeTest).
    c.distance = maxGap < 0.0f ? maxGap
                               : math::length((apply(b.rotation, c.pointB) + b.translation) -
                                              (apply(a.rotation, c.pointA) + a.translation));
    return c;
}

}   // namespace beacon::physics::rigid
