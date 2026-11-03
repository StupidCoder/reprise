// Rigid bodies of the game's physics library (Physics Effects as linked in the game; findings in
// .analysis/docs/physics.md). Units are the library's: world units times the world scale
// (positions and velocities; orientations and angular velocities are unscaled).
//
// Per update a body goes through: the external force (gravity) with the velocity limits
// (applyExternalForce, 0xd9a050), contacts and constraints (solver, not ported yet), then the
// position and orientation update (integrate, 0xd99cb0).
#pragma once

#include "math/Transform.hpp"

#include <cstdint>

namespace beacon::physics::rigid {

// Body motion types (state +0x01).
enum class MotionType : std::uint8_t { Fixed = 0, Active = 1, Keyframe = 2, OneWay = 3, Trigger = 4 };

// 3x3 matrix by rows (the library keeps its inertia as three 16-byte rows).
struct Mat3 {
    math::Vec3 r[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
};

Mat3 rotationMatrix(const math::Quat& q);
Mat3 multiply(const Mat3& a, const Mat3& b);
Mat3 transpose(const Mat3& m);
Mat3 inverse(const Mat3& m);   // zero matrix when singular
math::Vec3 apply(const Mat3& m, const math::Vec3& v);

// The moving part of a body (the library's 0xa0-byte state).
struct RigidState {
    bool useSleep = true;                     // +0x00 bit 0 (may be put to sleep)
    bool sleeping = false;                    // +0x00 bit 1
    bool swept = false;                       // +0x00 bit 5: swept contacts this step (set by
                                              // detection, cleared after integration)
    MotionType motion = MotionType::Active;   // +0x01
    std::uint16_t sleepCount = 0;             // +0x02
    std::uint32_t filterSelf = 0xffffffffu, filterTarget = 0xffffffffu;   // +0x08 / +0x0c
    // +0x10 / +0x12: groups (0xffff none). Two bodies sharing a group never pair; a query skips
    // bodies in its groups.
    std::uint16_t group[2] = {0xffff, 0xffff};
    float linearDamping = 1, angularDamping = 0.99f;   // +0x18 / +0x1c: scale v / w after the solver
    float maxLinearVelocity = 200, maxAngularVelocity = 314.159265f;   // +0x20, +0x24
    math::Vec3 position;                      // +0x40
    math::Quat orientation;                   // +0x50
    math::Vec3 linearVelocity;                // +0x60
    math::Vec3 angularVelocity;               // +0x70
};

// Mass properties and surface of a body (the library's 0x50-byte body).
struct RigidBody {
    Mat3 inertia;         // +0x00, body space
    float mass = 1;       // +0x30
    float restitution = 0, friction = 0.6f;
    // +0x3c: rolling friction (pair coefficient = the two bodies' mean, the limit of the pair's
    // rolling row). Body records start at 0 (0xcb5880 and the other creators); the physical
    // block sets 0.03 (0x778290), one other object a tuning value (0x8a5c70), through 0xca6350.
    float rollingFriction = 0;
    float massInv() const { return mass == 0 ? 0 : 1.0f / mass; }   // 0xd99710
    Mat3 inertiaInv() const;                                        // 0xd99730 (zero when massless)
};

// Velocity change from a force and torque over dt (0xd9a050). Not for fixed, keyframe or trigger
// bodies, nor sleeping ones. v += F / m dt; w = Iw^-1 (Iw w + T dt) with the world inertia
// Iw = R I R^T (no gyroscopic term, Euler: unlike the published version's Runge-Kutta); then each
// velocity is scaled back to its limit when longer. No damping here.
void applyExternalForce(RigidState& s, const RigidBody& b, const math::Vec3& force, const math::Vec3& torque,
                        float dt);

// Position and orientation update (0xd99cb0). Not for fixed or trigger bodies, nor sleeping ones.
// p += v dt; when |w| > 1e-6 the orientation turns by the exact rotation of |w| dt about w
// (the library's own sine / cosine), else q += (w, 0) q dt / 2; then it is normalised.
void integrate(RigidState& s, float dt);

// The library's single-precision sine and cosine (range reduction by pi/2 in two parts, then
// odd / even polynomials; constants at 0x13200a0..0x1320150).
void sinCos(float x, float& s, float& c);

}   // namespace beacon::physics::rigid
