#include "physics/rigid/RigidBody.hpp"

#include "math/Vector.hpp"

#include <cmath>
#include <cstring>

namespace beacon::physics::rigid {

using math::Quat;
using math::Vec3;

Mat3 rotationMatrix(const Quat& q) {
    const float x = q.x, y = q.y, z = q.z, w = q.w;
    Mat3 m;
    m.r[0] = {1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y)};
    m.r[1] = {2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x)};
    m.r[2] = {2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)};
    return m;
}

Mat3 transpose(const Mat3& m) {
    Mat3 t;
    t.r[0] = {m.r[0].x, m.r[1].x, m.r[2].x};
    t.r[1] = {m.r[0].y, m.r[1].y, m.r[2].y};
    t.r[2] = {m.r[0].z, m.r[1].z, m.r[2].z};
    return t;
}

Vec3 apply(const Mat3& m, const Vec3& v) {
    return {math::dot(m.r[0], v), math::dot(m.r[1], v), math::dot(m.r[2], v)};
}

Mat3 multiply(const Mat3& a, const Mat3& b) {
    const Mat3 bt = transpose(b);
    Mat3 m;
    for (int i = 0; i < 3; ++i) {
        m.r[i] = {math::dot(a.r[i], bt.r[0]), math::dot(a.r[i], bt.r[1]), math::dot(a.r[i], bt.r[2])};
    }
    return m;
}

Mat3 inverse(const Mat3& m) {
    // Adjugate from row cross products, over the determinant.
    const Vec3 c0 = math::cross(m.r[1], m.r[2]);
    const Vec3 c1 = math::cross(m.r[2], m.r[0]);
    const Vec3 c2 = math::cross(m.r[0], m.r[1]);
    const float det = math::dot(m.r[0], c0);
    Mat3 inv;
    if (det == 0) {
        inv.r[0] = inv.r[1] = inv.r[2] = {};
        return inv;
    }
    const float k = 1.0f / det;
    inv.r[0] = {c0.x * k, c1.x * k, c2.x * k};
    inv.r[1] = {c0.y * k, c1.y * k, c2.y * k};
    inv.r[2] = {c0.z * k, c1.z * k, c2.z * k};
    return inv;
}

Mat3 RigidBody::inertiaInv() const {
    if (mass == 0) {
        Mat3 z;
        z.r[0] = z.r[1] = z.r[2] = {};
        return z;
    }
    return inverse(inertia);
}

namespace {

// Scales v back to `limit` when longer (the library's reciprocal square root, refined twice:
// taken as exact here).
Vec3 limitLength(const Vec3& v, float limit) {
    const float l2 = math::dot(v, v);
    if (l2 <= limit * limit) {
        return v;
    }
    return v * (limit / std::sqrt(l2));
}

bool skipsForces(const RigidState& s) {
    // Mask 0x15: fixed, keyframe, trigger.
    return s.sleeping || s.motion == MotionType::Fixed || s.motion == MotionType::Keyframe ||
           s.motion == MotionType::Trigger;
}

bool skipsIntegration(const RigidState& s) {
    // Mask 0x11: fixed, trigger.
    return s.sleeping || s.motion == MotionType::Fixed || s.motion == MotionType::Trigger;
}

Quat mul(const Quat& a, const Quat& b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

}   // namespace

void applyExternalForce(RigidState& s, const RigidBody& b, const Vec3& force, const Vec3& torque, float dt) {
    if (skipsForces(s)) {
        return;
    }
    const Mat3 r = rotationMatrix(s.orientation);
    const Mat3 rt = transpose(r);
    const Mat3 worldInertia = multiply(multiply(r, b.inertia), rt);
    const Mat3 worldInertiaInv = multiply(multiply(r, b.inertiaInv()), rt);
    Vec3 v = s.linearVelocity + force * (b.massInv() * dt);
    const Vec3 momentum = apply(worldInertia, s.angularVelocity) + torque * dt;
    Vec3 w = apply(worldInertiaInv, momentum);
    s.linearVelocity = limitLength(v, s.maxLinearVelocity);
    s.angularVelocity = limitLength(w, s.maxAngularVelocity);
}

void sinCos(float x, float& s, float& c) {
    // Quadrant q = round-toward-zero(x * 2/pi + sign(x) / 2); r = x - q * (pi/2) in two parts.
    const float qf = std::trunc(x * 0.63661975f + std::copysign(0.5f, x));
    const int q = int(qf);
    float r = x - qf * 1.57079625f;
    r = r - qf * 7.54978942e-8f;
    const float r2 = r * r;
    const float sinP = r + r * r2 * (-0.16666652f + r2 * (0.0083320755f + r2 * -0.00019507270f));
    const float cosP = 1.0f + r2 * (-0.49999902f + r2 * (0.041656695f + r2 * -0.0013602249f));
    // sin: quadrants 0..3 -> sinP, cosP, -sinP, -cosP; cos: cosP, -sinP, -cosP, sinP.
    switch (q & 3) {
    case 0: s = sinP, c = cosP; break;
    case 1: s = cosP, c = -sinP; break;
    case 2: s = -sinP, c = -cosP; break;
    default: s = -cosP, c = sinP; break;
    }
}

void integrate(RigidState& s, float dt) {
    if (skipsIntegration(s)) {
        return;
    }
    s.position = s.position + s.linearVelocity * dt;
    const Vec3 w = s.angularVelocity;
    const float speed = math::length(w);
    Quat q = s.orientation;
    if (speed > 1e-6f) {
        float sn = 0, cs = 1;
        sinCos(speed * dt * 0.5f, sn, cs);
        const float k = sn / speed;
        Quat d{w.x * k, w.y * k, w.z * k, cs};
        if (math::dot(w, Vec3{d.x, d.y, d.z}) < 0) {
            d = {-d.x, -d.y, -d.z, -d.w};
        }
        q = mul(d, q);
    } else {
        const Quat dq = mul(Quat{w.x, w.y, w.z, 0}, q);
        q = {q.x + dq.x * 0.5f * dt, q.y + dq.y * 0.5f * dt, q.z + dq.z * 0.5f * dt, q.w + dq.w * 0.5f * dt};
    }
    const float l = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    s.orientation = {q.x / l, q.y / l, q.z / l, q.w / l};
}

}   // namespace beacon::physics::rigid
