// =============================================================================================
// EffectKeeper.hpp - how actors attach particle effects to themselves
// =============================================================================================
//
// The particle engine (Eft.hpp) only knows emitter sets and world matrices. The game layer
// between an actor and the engine is the "effect keeper": every actor has one, configured from
// the actor's entry in EffectData/EffectYaml.szs (one BYML per actor). Each entry says which
// emitter sets to create for a named action (for example "Land" or "Goal"), where to place them
// (the actor's position, one of its skeleton joints, or the camera), with which offset, rotation
// and scale, and whether the effect should keep following the actor after it was created.
//
// Translated functions:
//   0x235394  entry parser (flag defaults: without "EmitTrans" the follow/emit flags derive from
//             FollowPos / FollowMtx / EmitIgnoreRotate / EmitIgnoreScale)
//   0x22b4c4  create (0x35332c) with the source matrix, or identity at the source position
//   0x282c98  placement right after creation (position 0x28293c, matrix 0x36f8a8, scale)
//   0x32ccc0  per-frame follow update (lr of the set-matrix writes in evidence/runs/eft-follow-b)
//   0x282854  set matrix + scale store: SRT = RT * diag(scale), +0x200 scale, +0x21c / +0x220
// Helpers: 0x27321c (columns normalised), 0x2f2074 (inverse; camera matrix from the view),
// 0x2edc80 (rotation from front + up), 0x2f1bd8 (Euler Z*Y*X), 0x2f22e4 (multiply).
//
// Sources (per keeper): a position vector, an optional matrix (the JointName joint, or the
// actor's base matrix), and the camera view matrix. Set +0x20c / +0x210 are initialised to 1 by
// the create and never changed in the opening; they enter the scale of the translation-only
// follow branch.
// =============================================================================================
#pragma once
#include "effects/Eft.hpp"
#include <string>
#include <vector>

namespace captain_toad::RecoveredEffectKeeper {

using captain_toad::RecoveredEft::Mtx34;
using captain_toad::RecoveredEft::V3;

// An action of an entry: the effect is created when the actor enters the action, between its
// start and end frame (-1: no limit); `keep` leaves the effect alive after the action ends.
struct Action {
    std::string name;
    int start = -1;
    int end = -1;
    bool keep = false;
};

// One parsed EffectYaml entry. The comments give the field offsets in the original entry object.
struct Entry {
    std::string name;
    std::string joint;   // JointName
    std::string category;
    std::vector<std::string> resources;                                   // ResourceList entries without MaterialType
    std::vector<std::pair<std::string, std::string>> materialResources;   // (MaterialType, ResourceName)
    std::vector<Action> actions;
    std::vector<std::string> reactions;   // HitReactionList: hit reactions that create the set

    V3 posOffset;                // +0x14 offset from the source position
    bool hasPosOffset = false;   // +0x59
    V3 rotate;                   // +0x20 extra rotation (degrees)
    bool hasRotate = false;      // +0x5a
    float scale = 1;             // +0x2c
    float particleScale = 1;     // +0x30
    float emitRatio = 1;         // +0x34

    bool billboard = false;          // +0x48 face the camera
    bool camera = false;             // +0x49 placed relative to the camera
    bool deleteAtClipping = false;   // +0x4a
    bool ignoreRotate = false;       // +0x4b
    bool ignoreScale = false;        // +0x4c
    bool followPos = false;          // +0x4d
    bool followMtx = false;          // +0x4e
    bool emitRotate = false;         // +0x50 take the source rotation at creation
    bool emitScale = false;          // +0x51 take the source scale at creation
    bool followTrans = false;        // +0x52 keep following the source position
    bool followRotate = false;       // +0x53 keep following the source rotation
    bool followScale = false;        // +0x54 keep following the source scale
    bool needProgramInfo = false;    // +0x55
    bool directPos = false;          // +0x58 use the position vector even when a matrix exists

    // Draw group of the category (observed in the create calls of evidence/runs/eft-create-b:
    // player effects 2, 2D effects 3, others 0). The category names are data literals.
    int group() const {
        if (category == "エフェクト（プレイヤー）") {
            return 2;
        }
        if (category == "エフェクト（２Ｄ）") {
            return 3;
        }
        return 0;
    }

    // 0x235394 flag defaults.
    void finish(bool emitTrans) {
        if (!emitTrans) {
            if (followPos) {
                followTrans = true;
            }
            if (followMtx) {
                followTrans = true;
                followRotate = true;
            }
            if (!ignoreRotate) {
                emitRotate = true;
            }
            if (!ignoreScale) {
                emitScale = true;
            }
        }
    }
};

// ---------------------------------------------------------------------------------------------
// Matrix helpers
// ---------------------------------------------------------------------------------------------

inline Mtx34 identity() {
    return captain_toad::RecoveredEft::identity();
}

inline void setTranslation(Mtx34& m, const V3& p) {
    m[3] = p.x;
    m[7] = p.y;
    m[11] = p.z;
}

inline V3 translation(const Mtx34& m) {
    return {m[3], m[7], m[11]};
}

// 0x27321c: rotation part with each column divided by its length (lengths below 0.001 are kept
// as they are). Removes scale from a joint matrix.
inline Mtx34 normalizeColumns(const Mtx34& m) {
    float l1 = std::sqrt(m[1] * m[1] + m[5] * m[5] + m[9] * m[9]);
    float l0 = std::sqrt(m[0] * m[0] + m[4] * m[4] + m[8] * m[8]);
    float l2 = std::sqrt(m[2] * m[2] + m[6] * m[6] + m[10] * m[10]);
    auto inv = [](float l) {
        const float a = l <= 0.0f ? -l : l;
        return 0.001f <= a ? 1.0f / l : l;
    };
    l0 = inv(l0);
    l1 = inv(l1);
    l2 = inv(l2);
    Mtx34 o = m;
    o[4] = o[4] * l0;
    o[8] = o[8] * l0;
    o[9] = o[9] * l1;
    o[2] = o[2] * l2;
    o[10] = o[10] * l2;
    o[5] = o[5] * l1;
    o[6] = o[6] * l2;
    o[0] = o[0] * l0;
    o[1] = o[1] * l1;
    return o;
}

// 0x2c2654: column lengths (the scale of a matrix).
inline V3 columnLengths(const Mtx34& m) {
    return {std::sqrt(m[0] * m[0] + m[4] * m[4] + m[8] * m[8]), std::sqrt(m[1] * m[1] + m[5] * m[5] + m[9] * m[9]),
            std::sqrt(m[2] * m[2] + m[6] * m[6] + m[10] * m[10])};
}

// 0x2f2074: affine inverse (identity when singular). Gives the camera's world matrix from its
// view matrix.
inline Mtx34 inverse(const Mtx34& p) {
    const float a = p[0];
    const float b = p[1];
    const float c = p[2];
    const float d = p[4];
    const float e = p[5];
    const float f = p[6];
    const float g = p[8];
    const float h = p[9];
    const float i = p[10];
    float det = (a * e * i - g * e * c) + (b * f * g - d * b * i) + (c * d * h - a * h * f);
    if (det == 0.0f) {
        return identity();
    }
    det = 1.0f / det;
    const float r00 = (e * i - h * f) * det;
    const float r01 = -((b * i - h * c) * det);
    const float r02 = (b * f - e * c) * det;
    const float r10 = -((d * i - g * f) * det);
    const float r11 = (a * i - g * c) * det;
    const float r12 = -((a * f - d * c) * det);
    const float r20 = (d * h - g * e) * det;
    const float r21 = -((a * h - g * b) * det);
    const float r22 = (a * e - d * b) * det;
    const float tx = p[3];
    const float ty = p[7];
    const float tz = p[11];
    return {r00, r01, r02, (-(r00 * tx) - r01 * ty) - r02 * tz, r10, r11, r12, (-(r10 * tx) - r11 * ty) - r12 * tz,
            r20, r21, r22, (-(r20 * tx) - r21 * ty) - r22 * tz};
}

inline V3 cross(const V3& a, const V3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

// 0x2edc80: rotation looking along `front`: columns x = |up x z|, y = z x x, z = |front|
// (the translation is untouched).
inline void lookRotation(Mtx34& m, V3 front, const V3& up) {
    captain_toad::RecoveredEft::normalize(front);
    V3 x = cross(up, front);
    captain_toad::RecoveredEft::normalize(x);
    const V3 y = cross(front, x);
    m[0] = x.x;
    m[4] = x.y;
    m[8] = x.z;
    m[1] = y.x;
    m[5] = y.y;
    m[9] = y.z;
    m[2] = front.x;
    m[6] = front.y;
    m[10] = front.z;
}

// 0x2f1bd8: Euler angles (radians) -> Rz * Ry * Rx.
inline Mtx34 euler(const V3& r) {
    const float sx = std::sin(r.x);
    const float sy = std::sin(r.y);
    const float sz = std::sin(r.z);
    const float cx = std::cos(r.x);
    const float cy = std::cos(r.y);
    const float cz = std::cos(r.z);
    return {cy * cz,
            sx * sy * cz - cx * sz,
            sx * sz + cx * cz * sy,
            0,
            cy * sz,
            cx * cz + sx * sy * sz,
            sy * cx * sz - sx * cz,
            0,
            -sy,
            sx * cy,
            cx * cy,
            0};
}

// 0x2fd284 with a zero translation: rotate a direction.
inline V3 rotate(const Mtx34& m, const V3& v) {
    return {m[0] * v.x + m[1] * v.y + m[2] * v.z, m[4] * v.x + m[5] * v.y + m[6] * v.z,
            m[8] * v.x + m[9] * v.y + m[10] * v.z};
}

// ---------------------------------------------------------------------------------------------
// Placement
// ---------------------------------------------------------------------------------------------

// What a keeper places its effects relative to.
struct Sources {
    V3 position;                     // keeper +0x14 (actor position)
    const Mtx34* matrix = nullptr;   // keeper +0x18 (joint or base matrix)
    Mtx34 view = identity();         // keeper +0x1c (camera view matrix)
};

// 0x28293c: emitter-set position. The source is the camera, the matrix translation or the
// position vector; the optional offset is rotated by the camera or the (optionally unscaled)
// source matrix.
inline V3 position(const Entry& en, const Sources& s, bool scaled) {
    V3 p;
    if (!en.camera) {
        p = (s.matrix && !en.directPos) ? translation(*s.matrix) : s.position;
    } else {
        p = translation(inverse(s.view));
    }
    if (!en.hasPosOffset) {
        return p;
    }
    Mtx34 r;
    if (en.camera) {
        r = inverse(s.view);
    } else if (!s.matrix) {
        r = identity();
    } else {
        r = scaled ? *s.matrix : normalizeColumns(*s.matrix);
    }
    const V3 o = rotate(r, en.posOffset);
    return {p.x + o.x, p.y + o.y, p.z + o.z};
}

// 0x36f8a8: emitter-set matrix at position p. Starts from the source rotation (when `rotated`),
// turns billboards towards the camera, aligns camera effects with the camera's view direction,
// then applies the entry's extra rotation.
inline Mtx34 matrix(const Entry& en, const V3& p, const Sources& s, bool rotated) {
    Mtx34 m = identity();
    if (s.matrix && rotated) {
        m = normalizeColumns(*s.matrix);
        setTranslation(m, {0, 0, 0});
    }
    auto fallback = [&]() {
        m = s.matrix ? *s.matrix : identity();
        setTranslation(m, {0, 0, 0});
    };
    if (en.billboard) {
        m = inverse(s.view);
        const V3 up{m[1], m[5], m[9]};
        V3 d{m[3] - p.x, m[7] - p.y, m[11] - p.z};
        if (1e-6f <= d.x * d.x + d.y * d.y + d.z * d.z) {
            lookRotation(m, d, up);
        } else {
            fallback();
        }
    }
    if (en.camera && rotated) {
        m = inverse(s.view);
        const V3 up{m[1], m[5], m[9]};
        V3 f{m[2], m[6], m[10]};
        if (1e-6f <= f.x * f.x + f.y * f.y + f.z * f.z) {
            lookRotation(m, f, up);
        } else {
            fallback();
        }
    }
    if (en.hasRotate) {
        const float k = 0.017453292f;   // degrees -> radians
        m = captain_toad::RecoveredEft::mtxMul(m, euler({en.rotate.x * k, en.rotate.y * k, en.rotate.z * k}));
    }
    setTranslation(m, p);
    return m;
}

// 0x282854: store a set's matrix and scale (SRT = RT * diag(scale)).
inline void setMatrix(captain_toad::RecoveredEft::EmitterSet& set, const Mtx34& m, const V3& s) {
    Mtx34 srt = m;
    srt[0] = m[0] * s.x;
    srt[4] = m[4] * s.x;
    srt[8] = m[8] * s.x;
    srt[1] = m[1] * s.y;
    srt[5] = m[5] * s.y;
    srt[9] = m[9] * s.y;
    srt[2] = m[2] * s.z;
    srt[6] = m[6] * s.z;
    srt[10] = m[10] * s.z;
    set.mtxSRT = srt;
    set.mtxRT = m;
    set.scale[0] = s.x;
    set.scale[1] = s.y;
    set.scale[2] = s.z;
    set.effectiveScale[0] = set.baseScale[0] * s.x;
    set.effectiveScale[1] = set.baseScale[1] * s.y;
}

// 0x22b4c4 + 0x282c98: create emitter set `setIndex` for an entry and place it. Entries without
// any placement option keep the plain creation matrix.
inline captain_toad::RecoveredEft::EmitterSet* create(captain_toad::RecoveredEft::System& sys, const Entry& en,
                                                      int setIndex, const Sources& s) {
    Mtx34 start = identity();
    if (s.matrix && !en.directPos) {
        start = *s.matrix;
    } else {
        setTranslation(start, s.position);
    }
    captain_toad::RecoveredEft::EmitterSet* set = sys.create(setIndex, start, en.group());
    if (!(en.billboard || en.camera || en.hasPosOffset || en.hasRotate || en.emitRotate || en.emitScale)) {
        return set;
    }
    const V3 p = position(en, s, en.emitScale);
    const Mtx34 m = matrix(en, p, s, en.emitRotate);
    V3 sc = (s.matrix && en.emitScale) ? columnLengths(*s.matrix) : V3{1, 1, 1};
    sc = {sc.x * en.scale, sc.y * en.scale, sc.z * en.scale};
    setMatrix(*set, m, sc);
    return set;
}

// 0x32ccc0: per-frame follow of one set. Without rotation following, billboarding or camera
// placement only the translation and/or scale are refreshed; otherwise the whole matrix is
// rebuilt.
inline void follow(captain_toad::RecoveredEft::EmitterSet& set, const Entry& en, const Sources& s) {
    if (!set.alive) {
        return;
    }
    const bool scaled = en.emitScale || en.followScale;
    if (!en.billboard && !en.camera && !en.followRotate) {
        if (!en.followTrans && !en.followScale) {
            return;
        }
        Mtx34 m = set.mtxRT;
        if (en.followTrans) {
            setTranslation(m, position(en, s, scaled));
        }
        V3 sc;
        if (en.followScale) {
            sc = s.matrix ? columnLengths(*s.matrix) : V3{1, 1, 1};
        } else {
            sc = {set.baseScale[0] / set.effectiveScale[0], set.baseScale[1] / set.effectiveScale[1], 1.0f};
        }
        setMatrix(set, m, {sc.x * en.scale, sc.y * en.scale, sc.z * en.scale});
        return;
    }
    const V3 p = en.followTrans ? position(en, s, scaled) : translation(set.mtxRT);
    const Mtx34 m = matrix(en, p, s, en.followRotate);
    const V3 sc = (en.followScale && s.matrix) ? columnLengths(*s.matrix) : V3{1, 1, 1};
    setMatrix(set, m, {sc.x * en.scale, sc.y * en.scale, sc.z * en.scale});
}

}   // namespace captain_toad::RecoveredEffectKeeper
