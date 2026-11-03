// Contact constraints of the game's physics library (findings in .analysis/docs/physics.md,
// "Contact solver"). Sequential impulses: each contact point has a normal row and two friction
// rows; the solver accumulates velocity changes in per-body solver records and adds them to the
// bodies' velocities after the iterations.
#pragma once

#include "physics/rigid/ContactManifold.hpp"
#include "physics/rigid/RigidBody.hpp"

namespace beacon::physics::rigid {

// A body as the solver sees it (0x70 bytes in the library).
struct SolverBody {
    math::Vec3 deltaLinearVelocity, deltaAngularVelocity;
    math::Quat orientation;
    Mat3 inertiaInv;   // world space
    float massInv = 0;
    MotionType motion = MotionType::Fixed;
};

// Solver record of a body (set-up 0xdd2bb0 per body): dynamic bodies (active, one-way) get their
// inverse mass and world inverse inertia, the others none.
SolverBody makeSolverBody(const RigidState& s, const RigidBody& b);

struct ContactSolverParams {
    float timeStep = 1.0f / 60.0f;
    // The velocity set-up runs with 0 (0xdd2bb0 passes 0); kept for the published behaviour.
    float separateBias = 0;
    // Position correction strength (world +0x2764, world creation default 0.8).
    float positionBias = 0.8f;
};

// Contact point set-up (0xe68f10, game version). For the normal row:
//   K = (mA + mB) I - [rA]x IA [rA]x - [rB]x IB [rB]x   (one-way bodies: the other side's mass off)
//   jacDiagInv = 1 / (n.K.n + 0.001)
//   rhs = (-e' v.n - min(0, d + 0.001) bias |d| / (|d| + 0.05) / dt) jacDiagInv,
//   e' = 1 + restitution when the bodies approach faster than 0.5 along n, else 1;
// friction rows along the plane space of n: rhs = -(v.t) / (t.K.t + 0.001); limits [0, 1e38).
void setupContactPoint(ContactPoint& cp, float restitution, const RigidState& a, const RigidState& b,
                       const SolverBody& sa, const SolverBody& sb, const ContactSolverParams& p);

// Warm start: the accumulated impulses of all three rows applied again.
void warmStartContactPoint(const ContactPoint& cp, SolverBody& sa, SolverBody& sb);

// One iteration on a point (0xe69730): the normal row, then the friction rows limited to
// +-friction |normal impulse|.
void solveContactPoint(ContactPoint& cp, SolverBody& sa, SolverBody& sb, float friction);

// Rolling friction of a contact pair: one angular row against the relative spin, set up once
// per pair after its points (0xe6add0) and solved after its points in every iteration (0xe6af50).
// Set-up: w = wA - wB (the bodies' angular velocities); when |w|^2 > 1e-8 the row turns along
// d = w / |w| with k = d.(IA^-1 + IB^-1) d (the solver copies, priority scaling included, no
// one-way rule), jacDiagInv = 1 / k (0 when |k| <= 1e-8), rhs = -(d.w) jacDiagInv, limits
// -+ limit; else d = 0 and the accumulated impulse 0 (the rest stays as it was). The accumulated
// impulse is kept across steps and applied as a warm start after the contact points' (inline in
// 0xdd2bb0). Warm start and solve are skipped unless all three components of d are non-zero.
// Solve: impulse change x = clamp(old + rhs - jacDiagInv d.(dwA - dwB)) - old, then
// dwA += IA^-1 d x, dwB -= IB^-1 d x (the warm start: x = the accumulated impulse).
struct RollingRow {
    math::Vec3 direction;
    float rhs = 0, jacDiagInv = 0, lowerLimit = 0, upperLimit = 0, accumImpulse = 0;
};

void setupRolling(RollingRow& row, float limit, const RigidState& a, const RigidState& b, const SolverBody& sa,
                  const SolverBody& sb);
void warmStartRolling(const RollingRow& row, SolverBody& sa, SolverBody& sb);
void solveRolling(RollingRow& row, SolverBody& sa, SolverBody& sb);

// A point of a pair's swept (continuous-collision) list: one row along the normal, no friction,
// set up only in steps where one of the two bodies carries the swept flag (else the list is
// dropped). Set-up 0xe6a570: the normal row with the full effective mass (no 0.001 term; one-way
// rule as for points), rhs = -(d / dt + n.vAB) jacDiagInv, limits [0, 1e38] - the bodies may
// close the gap d within the step but not pass it. The accumulated impulse starts at 0 (the list
// is rebuilt every detection; no warm start). Solve 0xe6aa90: the usual clamped row.
void setupSweptPoint(ConstraintRow& row, float distance, const math::Vec3& normal, const math::Vec3& localA,
                     const math::Vec3& localB, float timeStep, const RigidState& a, const RigidState& b,
                     const SolverBody& sa, const SolverBody& sb);
void solveSweptPoint(ConstraintRow& row, const math::Vec3& localA, const math::Vec3& localB, SolverBody& sa,
                     SolverBody& sb);

// Position correction of one point after integration (0xe69e00): the bodies are moved apart
// along the normal by p = -bias |d| / (|d| + 0.05) min(0, d + 0.001) / (n.K.n) (d from the
// current poses), linearly by p / m and turned by 1/2 (I^-1 (r x n) p, 0) q (normalised).
void correctContactPosition(const ContactPoint& cp, RigidState& a, RigidState& b, const SolverBody& sa,
                            const SolverBody& sb, float bias);

// Tangent directions of a normal (the library's plane space: threshold 1/sqrt 2 on |n.z|).
void planeSpace(const math::Vec3& n, math::Vec3& t1, math::Vec3& t2);

}   // namespace beacon::physics::rigid
