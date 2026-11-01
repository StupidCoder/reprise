// =============================================================================================
// Presentation.cpp - drawing between the game's ticks (enhancement, see Presentation.hpp)
// =============================================================================================
#include "session/Presentation.hpp"

#include <cmath>

namespace captain_toad::session {

namespace {

struct Trs {
    float t[3];
    float s[3];
    float q[4];   // x, y, z, w
};

// A rotation matrix (columns already divided by their scale) to a quaternion.
void toQuat(const float r[9], float q[4]) {
    const float trace = r[0] + r[4] + r[8];
    if (trace > 0) {
        const float s = std::sqrt(trace + 1.0f) * 2;
        q[3] = 0.25f * s;
        q[0] = (r[7] - r[5]) / s;
        q[1] = (r[2] - r[6]) / s;
        q[2] = (r[3] - r[1]) / s;
    } else if (r[0] > r[4] && r[0] > r[8]) {
        const float s = std::sqrt(1.0f + r[0] - r[4] - r[8]) * 2;
        q[3] = (r[7] - r[5]) / s;
        q[0] = 0.25f * s;
        q[1] = (r[1] + r[3]) / s;
        q[2] = (r[2] + r[6]) / s;
    } else if (r[4] > r[8]) {
        const float s = std::sqrt(1.0f + r[4] - r[0] - r[8]) * 2;
        q[3] = (r[2] - r[6]) / s;
        q[0] = (r[1] + r[3]) / s;
        q[1] = 0.25f * s;
        q[2] = (r[5] + r[7]) / s;
    } else {
        const float s = std::sqrt(1.0f + r[8] - r[0] - r[4]) * 2;
        q[3] = (r[3] - r[1]) / s;
        q[0] = (r[2] + r[6]) / s;
        q[1] = (r[5] + r[7]) / s;
        q[2] = 0.25f * s;
    }
}

Trs decompose(const float* m) {   // row-major 3x4
    Trs d;
    float r[9];
    for (int c = 0; c < 3; c++) {
        d.t[c] = m[c * 4 + 3];
        const float s = std::sqrt(m[c] * m[c] + m[4 + c] * m[4 + c] + m[8 + c] * m[8 + c]);
        d.s[c] = s;
        for (int row = 0; row < 3; row++) {
            r[row * 3 + c] = s > 1e-12f ? m[row * 4 + c] / s : (row == c ? 1.0f : 0.0f);
        }
    }
    // A mirrored matrix: flip one axis so the rest is a rotation.
    const float det =
        r[0] * (r[4] * r[8] - r[5] * r[7]) - r[1] * (r[3] * r[8] - r[5] * r[6]) + r[2] * (r[3] * r[7] - r[4] * r[6]);
    if (det < 0) {
        d.s[0] = -d.s[0];
        r[0] = -r[0];
        r[3] = -r[3];
        r[6] = -r[6];
    }
    toQuat(r, d.q);
    return d;
}

void compose(const Trs& d, float* m) {
    const float x = d.q[0], y = d.q[1], z = d.q[2], w = d.q[3];
    const float r[9] = {1 - 2 * (y * y + z * z), 2 * (x * y - z * w),     2 * (x * z + y * w),
                        2 * (x * y + z * w),     1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
                        2 * (x * z - y * w),     2 * (y * z + x * w),     1 - 2 * (x * x + y * y)};
    for (int row = 0; row < 3; row++) {
        for (int c = 0; c < 3; c++) {
            m[row * 4 + c] = r[row * 3 + c] * d.s[c];
        }
        m[row * 4 + 3] = d.t[row];
    }
}

void blend(const float* a, const float* b, float t, float* out) {
    if (t <= 0) {
        std::copy(a, a + 12, out);
        return;
    }
    if (t >= 1) {
        std::copy(b, b + 12, out);
        return;
    }
    const Trs x = decompose(a), y = decompose(b);
    Trs o;
    for (int k = 0; k < 3; k++) {
        o.t[k] = x.t[k] + (y.t[k] - x.t[k]) * t;
        o.s[k] = x.s[k] + (y.s[k] - x.s[k]) * t;
    }
    float dot = 0;
    for (int k = 0; k < 4; k++) {
        dot += x.q[k] * y.q[k];
    }
    const float sign = dot < 0 ? -1.0f : 1.0f;
    float n = 0;
    for (int k = 0; k < 4; k++) {
        o.q[k] = x.q[k] * (1 - t) + y.q[k] * sign * t;
        n += o.q[k] * o.q[k];
    }
    n = std::sqrt(n);
    for (int k = 0; k < 4; k++) {
        o.q[k] /= n > 0 ? n : 1.0f;
    }
    compose(o, out);
}

}   // namespace

void blendRows34(const float* a, const float* b, float t, float* out) {
    blend(a, b, t, out);
}

Affine blendAffine(const Affine& a, const Affine& b, float t) {
    Affine o;
    blend(a.m.data(), b.m.data(), t, o.m.data());
    return o;
}

captain_toad::RecoveredVec::M34 blendM34(const captain_toad::RecoveredVec::M34& a,
                                         const captain_toad::RecoveredVec::M34& b, float t) {
    captain_toad::RecoveredVec::M34 o;
    blend(a.m, b.m, t, o.m);
    return o;
}

captain_toad::RecoveredCamera::Pose blendPose(const captain_toad::RecoveredCamera::Pose& a,
                                              const captain_toad::RecoveredCamera::Pose& b, float t) {
    auto mix = [&](const captain_toad::RecoveredVec::V3& p, const captain_toad::RecoveredVec::V3& q) {
        return captain_toad::RecoveredVec::V3{p.x + (q.x - p.x) * t, p.y + (q.y - p.y) * t, p.z + (q.z - p.z) * t};
    };
    captain_toad::RecoveredCamera::Pose o;
    o.eye = mix(a.eye, b.eye);
    o.target = mix(a.target, b.target);
    o.up = mix(a.up, b.up);
    captain_toad::RecoveredVec::normalize(o.up);
    o.fovy = a.fovy + (b.fovy - a.fovy) * t;
    return o;
}

}   // namespace captain_toad::session
