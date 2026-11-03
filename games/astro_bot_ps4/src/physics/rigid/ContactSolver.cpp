#include "physics/rigid/ContactSolver.hpp"

#include "math/Vector.hpp"

#include <algorithm>
#include <cmath>

namespace beacon::physics::rigid {

using math::Quat;
using math::Vec3;

namespace {

constexpr float kRegularisation = 0.001f;   // added to every row's effective mass (0x1328a88)
constexpr float kSlop = 0.001f;             // penetration allowed before the bias acts (0x1328a88)
constexpr float kBiasSoftness = 0.05f;      // bias x |d| / (|d| + 0.05) (0x1328a84)
constexpr float kRestitutionSpeed = -0.5f;  // restitution only below this normal speed (0x1328a90)

Vec3 rotate(const Quat& q, const Vec3& v) {
    const Vec3 u{q.x, q.y, q.z};
    const Vec3 t = math::cross(u, v) * 2.0f;
    return v + t * q.w + math::cross(u, t);
}

Mat3 crossMatrix(const Vec3& v) {
    Mat3 m;
    m.r[0] = {0, -v.z, v.y};
    m.r[1] = {v.z, 0, -v.x};
    m.r[2] = {-v.y, v.x, 0};
    return m;
}

Mat3 scaled(const Mat3& m, float s) {
    Mat3 r;
    for (int i = 0; i < 3; ++i) {
        r.r[i] = m.r[i] * s;
    }
    return r;
}

Mat3 add(const Mat3& a, const Mat3& b) {
    Mat3 r;
    for (int i = 0; i < 3; ++i) {
        r.r[i] = a.r[i] + b.r[i];
    }
    return r;
}

bool dynamic(MotionType m) { return m == MotionType::Active || m == MotionType::OneWay; }

// The two sides' inverse masses with the one-way rule applied.
void effectiveMasses(const SolverBody& sa, const SolverBody& sb, float& ma, Mat3& ia, float& mb, Mat3& ib) {
    ma = sa.massInv;
    ia = sa.inertiaInv;
    mb = sb.massInv;
    ib = sb.inertiaInv;
    const Mat3 zero{{{0, 0, 0}, {0, 0, 0}, {0, 0, 0}}};
    if (sa.motion == MotionType::OneWay) {
        mb = 0;
        ib = zero;
    }
    if (sb.motion == MotionType::OneWay) {
        ma = 0;
        ia = zero;
    }
}

void applyImpulse(float impulse, const Vec3& n, const Vec3& rA, const Vec3& rB, float ma, const Mat3& ia, float mb,
                  const Mat3& ib, SolverBody& sa, SolverBody& sb) {
    sa.deltaLinearVelocity += n * (impulse * ma);
    sa.deltaAngularVelocity += apply(ia, math::cross(rA, n)) * impulse;
    sb.deltaLinearVelocity -= n * (impulse * mb);
    sb.deltaAngularVelocity -= apply(ib, math::cross(rB, n)) * impulse;
}

void solveRow(ConstraintRow& row, const Vec3& rA, const Vec3& rB, float ma, const Mat3& ia, float mb, const Mat3& ib,
              SolverBody& sa, SolverBody& sb) {
    const Vec3 dvA = sa.deltaLinearVelocity + math::cross(sa.deltaAngularVelocity, rA);
    const Vec3 dvB = sb.deltaLinearVelocity + math::cross(sb.deltaAngularVelocity, rB);
    float delta = row.rhs - row.jacDiagInv * math::dot(row.normal, dvA - dvB);
    const float old = row.accumImpulse;
    row.accumImpulse = std::max(row.lowerLimit, std::min(old + delta, row.upperLimit));
    delta = row.accumImpulse - old;
    applyImpulse(delta, row.normal, rA, rB, ma, ia, mb, ib, sa, sb);
}

}   // namespace

void planeSpace(const Vec3& n, Vec3& t1, Vec3& t2) {
    if (std::fabs(n.z) > 0.707f) {
        const float a = n.y * n.y + n.z * n.z;
        const float k = 1.0f / std::sqrt(a);
        t1 = {0, -n.z * k, n.y * k};
        t2 = {a * k, -n.x * t1.z, n.x * t1.y};
    } else {
        const float a = n.x * n.x + n.y * n.y;
        const float k = 1.0f / std::sqrt(a);
        t1 = {-n.y * k, n.x * k, 0};
        t2 = {-n.z * t1.y, n.z * t1.x, a * k};
    }
}

SolverBody makeSolverBody(const RigidState& s, const RigidBody& b) {
    SolverBody sb;
    sb.orientation = s.orientation;
    sb.motion = s.motion;
    if (dynamic(s.motion)) {
        const Mat3 r = rotationMatrix(s.orientation);
        sb.massInv = b.massInv();
        sb.inertiaInv = multiply(multiply(r, b.inertiaInv()), transpose(r));
    } else {
        sb.massInv = 0;
        sb.inertiaInv = Mat3{{{0, 0, 0}, {0, 0, 0}, {0, 0, 0}}};
    }
    return sb;
}

void setupContactPoint(ContactPoint& cp, float restitution, const RigidState& a, const RigidState& b,
                       const SolverBody& sa, const SolverBody& sb, const ContactSolverParams& p) {
    const Vec3 rA = rotate(sa.orientation, cp.localPointA);
    const Vec3 rB = rotate(sb.orientation, cp.localPointB);
    float ma, mb;
    Mat3 ia, ib;
    effectiveMasses(sa, sb, ma, ia, mb, ib);
    const Mat3 cA = crossMatrix(rA), cB = crossMatrix(rB);
    Mat3 k = scaled(Mat3{}, ma + mb);
    k = add(k, scaled(multiply(multiply(cA, ia), cA), -1));
    k = add(k, scaled(multiply(multiply(cB, ib), cB), -1));

    const Vec3 vA = a.linearVelocity + math::cross(a.angularVelocity, rA);
    const Vec3 vB = b.linearVelocity + math::cross(b.angularVelocity, rB);
    const Vec3 vAB = vA - vB;
    const Vec3 n = cp.rows[0].normal;
    Vec3 t1, t2;
    planeSpace(n, t1, t2);

    const float depth = cp.distance;
    // Position term: zero in the game (bias 0 at the call); kept for completeness.
    const float bias = p.separateBias * (std::fabs(depth) / (std::fabs(depth) + kBiasSoftness));
    const float vn = math::dot(vAB, n);
    const float e = vn < kRestitutionSpeed ? 1.0f + restitution : 1.0f;

    ConstraintRow& r0 = cp.rows[0];
    r0.jacDiagInv = 1.0f / (math::dot(apply(k, n), n) + kRegularisation);
    r0.rhs = (-(e * vn) - std::min(0.0f, depth + kSlop) * bias / p.timeStep) * r0.jacDiagInv;
    r0.lowerLimit = 0;
    r0.upperLimit = 1e38f;

    ConstraintRow& r1 = cp.rows[1];
    r1.normal = t1;
    r1.jacDiagInv = 1.0f / (math::dot(apply(k, t1), t1) + kRegularisation);
    r1.rhs = -math::dot(vAB, t1) * r1.jacDiagInv;
    r1.lowerLimit = 0;
    r1.upperLimit = 1e38f;

    ConstraintRow& r2 = cp.rows[2];
    r2.normal = t2;
    r2.jacDiagInv = 1.0f / (math::dot(apply(k, t2), t2) + kRegularisation);
    r2.rhs = -math::dot(vAB, t2) * r2.jacDiagInv;
    r2.lowerLimit = 0;
    r2.upperLimit = 1e38f;
}

void warmStartContactPoint(const ContactPoint& cp, SolverBody& sa, SolverBody& sb) {
    const Vec3 rA = rotate(sa.orientation, cp.localPointA);
    const Vec3 rB = rotate(sb.orientation, cp.localPointB);
    float ma, mb;
    Mat3 ia, ib;
    effectiveMasses(sa, sb, ma, ia, mb, ib);
    for (const ConstraintRow& row : cp.rows) {
        applyImpulse(row.accumImpulse, row.normal, rA, rB, ma, ia, mb, ib, sa, sb);
    }
}

void solveContactPoint(ContactPoint& cp, SolverBody& sa, SolverBody& sb, float friction) {
    const Vec3 rA = rotate(sa.orientation, cp.localPointA);
    const Vec3 rB = rotate(sb.orientation, cp.localPointB);
    float ma, mb;
    Mat3 ia, ib;
    effectiveMasses(sa, sb, ma, ia, mb, ib);
    solveRow(cp.rows[0], rA, rB, ma, ia, mb, ib, sa, sb);
    const float limit = friction * std::fabs(cp.rows[0].accumImpulse);
    for (int i = 1; i < 3; ++i) {
        cp.rows[i].lowerLimit = -limit;
        cp.rows[i].upperLimit = limit;
        solveRow(cp.rows[i], rA, rB, ma, ia, mb, ib, sa, sb);
    }
}

void setupSweptPoint(ConstraintRow& row, float distance, const Vec3& normal, const Vec3& localA, const Vec3& localB,
                     float timeStep, const RigidState& a, const RigidState& b, const SolverBody& sa,
                     const SolverBody& sb) {
    const Vec3 rA = rotate(sa.orientation, localA);
    const Vec3 rB = rotate(sb.orientation, localB);
    float ma, mb;
    Mat3 ia, ib;
    effectiveMasses(sa, sb, ma, ia, mb, ib);
    const Mat3 cA = crossMatrix(rA), cB = crossMatrix(rB);
    Mat3 k = scaled(Mat3{}, ma + mb);
    k = add(k, scaled(multiply(multiply(cA, ia), cA), -1));
    k = add(k, scaled(multiply(multiply(cB, ib), cB), -1));
    const Vec3 vA = a.linearVelocity + math::cross(a.angularVelocity, rA);
    const Vec3 vB = b.linearVelocity + math::cross(b.angularVelocity, rB);
    row.normal = normal;
    row.jacDiagInv = 1.0f / math::dot(apply(k, normal), normal);
    row.rhs = -(distance / timeStep + math::dot(normal, vA - vB)) * row.jacDiagInv;
    row.lowerLimit = 0;
    row.upperLimit = 1e38f;
}

void solveSweptPoint(ConstraintRow& row, const Vec3& localA, const Vec3& localB, SolverBody& sa, SolverBody& sb) {
    const Vec3 rA = rotate(sa.orientation, localA);
    const Vec3 rB = rotate(sb.orientation, localB);
    float ma, mb;
    Mat3 ia, ib;
    effectiveMasses(sa, sb, ma, ia, mb, ib);
    solveRow(row, rA, rB, ma, ia, mb, ib, sa, sb);
}

void setupRolling(RollingRow& row, float limit, const RigidState& a, const RigidState& b, const SolverBody& sa,
                  const SolverBody& sb) {
    const Vec3 w = a.angularVelocity - b.angularVelocity;
    const float w2 = math::dot(w, w);
    if (!(w2 > 1e-8f)) {
        row.direction = {};
        row.accumImpulse = 0;
        return;
    }
    const Vec3 d = w * (1.0f / std::sqrt(w2));
    const float k = math::dot(d, apply(add(sa.inertiaInv, sb.inertiaInv), d));
    row.direction = d;
    row.jacDiagInv = std::fabs(k) > 1e-8f ? 1.0f / k : 0.0f;
    row.rhs = -math::dot(d, w) * row.jacDiagInv;
    row.lowerLimit = -limit;
    row.upperLimit = limit;
}

void warmStartRolling(const RollingRow& row, SolverBody& sa, SolverBody& sb) {
    const Vec3& d = row.direction;
    if (d.x == 0 || d.y == 0 || d.z == 0) {
        return;
    }
    sa.deltaAngularVelocity += apply(sa.inertiaInv, d) * row.accumImpulse;
    sb.deltaAngularVelocity -= apply(sb.inertiaInv, d) * row.accumImpulse;
}

void solveRolling(RollingRow& row, SolverBody& sa, SolverBody& sb) {
    const Vec3& d = row.direction;
    if (d.x == 0 || d.y == 0 || d.z == 0) {
        return;
    }
    const float old = row.accumImpulse;
    const float wanted = old + (row.rhs - math::dot(d, sa.deltaAngularVelocity - sb.deltaAngularVelocity) * row.jacDiagInv);
    row.accumImpulse = std::max(row.lowerLimit, std::min(wanted, row.upperLimit));
    const float x = row.accumImpulse - old;
    sa.deltaAngularVelocity += apply(sa.inertiaInv, d) * x;
    sb.deltaAngularVelocity -= apply(sb.inertiaInv, d) * x;
}

void correctContactPosition(const ContactPoint& cp, RigidState& a, RigidState& b, const SolverBody& sa,
                            const SolverBody& sb, float bias) {
    const Vec3 rA = rotate(a.orientation, cp.localPointA);
    const Vec3 rB = rotate(b.orientation, cp.localPointB);
    float ma, mb;
    Mat3 ia, ib;
    effectiveMasses(sa, sb, ma, ia, mb, ib);
    const Mat3 cA = crossMatrix(rA), cB = crossMatrix(rB);
    Mat3 k = scaled(Mat3{}, ma + mb);
    k = add(k, scaled(multiply(multiply(cA, ia), cA), -1));
    k = add(k, scaled(multiply(multiply(cB, ib), cB), -1));
    const Vec3 n = cp.rows[0].normal;
    const float d = math::dot((a.position + rA) - (b.position + rB), n);
    const float strength = bias * (std::fabs(d) / (std::fabs(d) + kBiasSoftness));
    const float impulse = -(strength * std::min(0.0f, d + kSlop)) / math::dot(apply(k, n), n);
    a.position += n * (impulse * ma);
    b.position -= n * (impulse * mb);
    auto rotateBy = [](RigidState& s, const Vec3& w) {
        const Quat q = s.orientation;
        // (w, 0) q / 2 added to q.
        const Quat dq{w.x * q.w + w.y * q.z - w.z * q.y, w.y * q.w + w.z * q.x - w.x * q.z,
                      w.z * q.w + w.x * q.y - w.y * q.x, -(w.x * q.x + w.y * q.y + w.z * q.z)};
        Quat r{q.x + dq.x * 0.5f, q.y + dq.y * 0.5f, q.z + dq.z * 0.5f, q.w + dq.w * 0.5f};
        const float l = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z + r.w * r.w);
        s.orientation = {r.x / l, r.y / l, r.z / l, r.w / l};
    };
    rotateBy(a, apply(ia, math::cross(rA, n)) * impulse);
    rotateBy(b, apply(ib, math::cross(rB, n)) * -impulse);
}

}   // namespace beacon::physics::rigid
