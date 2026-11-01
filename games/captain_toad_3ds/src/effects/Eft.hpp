// =============================================================================================
// Eft.hpp - the particle-effect runtime
// =============================================================================================
//
// Every sparkle, dust puff, confetti burst and swirl in the opening is drawn by a small particle
// engine that the game links into its ARM11 program (its resource files carry the tag "3DSEft").
// This file is a translation of that engine's calculation side: how emitters spawn particles,
// how particles move, fade, scale, spin and change colour, and what each particle hands to the
// GPU every frame. The drawing itself (shaders, textures, blending) lives in EftRenderer.
//
// How the engine is organised
// ---------------------------
//   * An EMITTER SET is one named effect, for example a goal sparkle. The game creates a set at a
//     world matrix ("create effect X here") and may move, recolour, fade out or kill it later.
//   * A set contains up to 16 EMITTERS. Each emitter is one layer of the effect, for example the
//     glow, the stars and the falling dust of a sparkle. Emitters are described by a large binary
//     record in the effect resource (EmitterData below, about 0x740 bytes). Almost every number
//     that shapes an effect lives in that record; the code reads it by byte offset, and the
//     comments quote those offsets (data +0x2c4 = emission rate and so on).
//   * Each frame an emitter decides how many PARTICLES to spawn, places them inside its emission
//     VOLUME (a point, circle, sphere, cylinder or box) and gives them an initial velocity, life
//     time, colour, scale and rotation, mostly randomised within ranges from the data record.
//   * Each frame every live particle is then advanced: velocity is added to the position, gravity
//     and air resistance change the velocity, alpha and scale follow three-phase "in / hold /
//     out" animations, colours follow three-key animations, and texture coordinates scroll,
//     scale, rotate or step through sprite-sheet patterns. Particles whose age reaches their life
//     are freed.
//   * "Complex" emitters add optional extras: flicker, force fields (random kicks, spin around an
//     axis) and CHILD particles, which a parent particle emits along its path.
//
// Randomness
// ----------
// Effects look random but are fully deterministic, which is what lets this port reproduce the
// original frame for frame:
//   * A global xorshift128 generator (static state at 0x546988, initial words {1, 0x6c078967,
//     0x714acb41, 0x48077044}) is drawn once per emitter-set creation and once per emitter whose
//     data seed (+8) is 0.
//   * Each emitter owns a linear congruential generator (emitter +0x8c, x = x * 0x41c64e6d + 12345)
//     that every random particle property draws from, in a fixed order. Reordering any draw would
//     change every particle that follows, so the translation keeps the original draw order.
//   * Each emitter also owns two 9-bit indices (+0x88 / +0x8a) that walk through two 512-entry
//     tables of precomputed random vectors (unit vectors and vectors in a cube).
//
// Sources and conventions
// -----------------------
//   * Translated from the decompiled functions (Ghidra exports in evidence/analysis/eft-d and
//     eft-e; literal pools via tools/eft_literals.py). Function addresses are quoted as 0x......
//   * Floating-point expressions keep the original operation order, and the build uses
//     -ffp-contract=off, so results match the original bit for bit.
//   * Resource data is the ROM's EffectData.ptcl (SPBD, version 0x33). The runtime tables are the
//     code.bin sine/cosine table plus the random-vector and flicker tables, which the importer
//     generates exactly as the original initialisers 0x34479c / 0x347f88 do.
//
// Object sizes (from the system initialiser 0x351e3c)
// ---------------------------------------------------
//   emitter set 0x28c bytes, emitter 0x298 bytes, particle 0x168 bytes plus a 0x10-byte alpha
//   animation block and a 0x18-byte scale animation block.
//
// Frame flow
// ----------
//   0x352cd4 -> 0x353814, for each draw group, for each emitter set in creation order, for each
//   emitter from the last to the first:
//     1. emitter calc (vtable +8: 0x34ff3c), which spawns particles through the volume table
//        0x4f904c;
//     2. particle calc fused with drawing (vtable +0x10: 0x34f720 simple / 0x3509c0 complex),
//        which also frees the particles whose age has reached their life.
// =============================================================================================
#pragma once
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <list>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <set>
#include <vector>

namespace captain_toad::RecoveredEft {

// =============================================================================================
// Math helpers (exact translations; operand order preserved)
// =============================================================================================

struct V3 {
    float x = 0;
    float y = 0;
    float z = 0;
};

// Row-major 3x4 matrix: three rows of (rotation/scale x3, translation). The translation sits in
// elements [3], [7] and [11].
using Mtx34 = std::array<float, 12>;

inline Mtx34 identity() {
    return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
}

// 0x2974c8: transform a point, out = m * v.
inline V3 mtxApply(const Mtx34& m, const V3& v) {
    return {m[3] + m[0] * v.x + m[1] * v.y + m[2] * v.z, m[7] + m[4] * v.x + m[5] * v.y + m[6] * v.z,
            m[11] + m[8] * v.x + m[9] * v.y + m[10] * v.z};
}

// 0x2e163c: matrix product, out = a * b.
inline Mtx34 mtxMul(const Mtx34& a, const Mtx34& b) {
    Mtx34 o;
    for (int r = 0; r < 3; r++) {
        const float a0 = a[r * 4];
        const float a1 = a[r * 4 + 1];
        const float a2 = a[r * 4 + 2];
        const float a3 = a[r * 4 + 3];
        o[r * 4 + 0] = b[0] * a0 + b[4] * a1 + b[8] * a2;
        o[r * 4 + 1] = b[1] * a0 + b[5] * a1 + b[9] * a2;
        o[r * 4 + 2] = b[2] * a0 + b[6] * a1 + b[10] * a2;
        o[r * 4 + 3] = a3 + b[3] * a0 + b[7] * a1 + b[11] * a2;
    }
    return o;
}

// 0x2ea260: inverse of an affine matrix (cofactors over the determinant, then the translation
// moved through the inverted 3x3). Returns false and leaves `o` untouched when singular.
inline bool mtxInverse(Mtx34& o, const Mtx34& p) {
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
        return false;
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
    o = {r00, r01, r02, (-(r00 * tx) - r01 * ty) - r02 * tz, r10, r11, r12, (-(r10 * tx) - r11 * ty) - r12 * tz,
         r20, r21, r22, (-(r20 * tx) - r21 * ty) - r22 * tz};
    return true;
}

// 0x2fd3a4: normalise in place (a zero-length vector is left unchanged).
inline void normalize(V3& v) {
    const float length = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (length <= 0.0f) {
        return;
    }
    const float k = 1.0f / length;
    v.x *= k;
    v.y *= k;
    v.z *= k;
}

// The ARM instruction VCVT.F32.U32 converts under the FPSCR rounding mode. The traced game runs
// with round-to-nearest, which is what C++ does; the switch exists to model the other mode.
inline bool& roundTowardZero() {
    static bool rz = false;
    return rz;
}

// u32 -> float conversion as the hardware does it.
inline float u2f(uint32_t x) {
    if (!roundTowardZero() || x < (1u << 24)) {
        return float(x);
    }
    const int bits = 32 - __builtin_clz(x);
    const int shift = bits - 24;
    return float((x >> shift) << shift);
}

inline float bitsToFloat(uint32_t u) {
    float f;
    memcpy(&f, &u, 4);
    return f;
}

// The original often compares floats through their bit patterns (integer compares on the raw
// register). For non-negative floats this orders the same way as the values; for example
// floatBits(f) > 0x3f800000 means "f > 1.0".
inline int32_t floatBits(float f) {
    int32_t i;
    memcpy(&i, &f, 4);
    return i;
}

// =============================================================================================
// Lookup tables
// =============================================================================================

struct Tables {
    // 257 entries of {sin, delta sin, cos, delta cos}: a full turn in 256 steps plus a guard
    // entry, linearly interpolated between steps (code.bin 0x499efc).
    std::vector<float> sinCos;
    // 512 random unit vectors (heap table read through *0x4f91a8).
    std::vector<V3> randomUnit;
    // 512 random vectors in the cube [-1, 1]^3 (heap table read through *0x4f91a4).
    std::vector<V3> randomCube;
    // 128 flicker factors, 0.5 + 0.5 * sin(i / 128 * 2pi) (heap table read through *0x4f9048).
    std::vector<float> flicker;
    // Evenly spread point sets on the unit sphere for the "sphere, equal division" volume
    // (code.bin, pointers 0x4f9088[0..7]: 2, 3, 4, 6, 8, 12, 20 and 32 points).
    std::vector<std::vector<V3>> divide;
    // The second set for volume type 6 (code.bin, pointers 0x4f90a8[0..62]: 2 to 64 points;
    // empty in packages exported before it).
    std::vector<std::vector<V3>> divideMany;

    // Sine and cosine of an angle in radians. The angle is first turned into a 32-bit binary
    // angle (a full turn = 2^32, via 0x2ff634: float -> int64, low word); the top 8 bits select
    // the table step and the low 24 bits interpolate within it.
    void sinCosRad(float rad, float& s, float& c) const {
        const uint32_t u = uint32_t(int64_t(rad * 683565248.0f));
        const float* t = &sinCos[(u >> 24) * 4];
        const float f = float(u & 0xffffff) * 5.9604645e-08f;
        s = t[0] + t[1] * f;
        c = t[2] + t[3] * f;
    }
};

// =============================================================================================
// Resource (EffectData.ptcl)
// =============================================================================================

// A view of one emitter's data record inside the resource. The accessors read a value of the
// given type at a byte offset; the offsets are the ones the original code uses.
struct EmitterData {
    const uint8_t* p = nullptr;

    float f(int o) const {
        float v;
        memcpy(&v, p + o, 4);
        return v;
    }
    int32_t i(int o) const {
        int32_t v;
        memcpy(&v, p + o, 4);
        return v;
    }
    uint32_t u(int o) const {
        uint32_t v;
        memcpy(&v, p + o, 4);
        return v;
    }
    uint16_t h(int o) const {
        uint16_t v;
        memcpy(&v, p + o, 2);
        return v;
    }
    int16_t sh(int o) const {
        int16_t v;
        memcpy(&v, p + o, 2);
        return v;
    }
    uint8_t b(int o) const { return p[o]; }

    explicit operator bool() const { return p != nullptr; }

    // Emitter kind: 0 simple, 1 complex (complex emitters have flicker, fields and children).
    int kind() const { return i(0); }

    bool operator==(const EmitterData& o) const { return p == o.p; }
};

// The parsed effect resource: a list of named emitter sets, each a list of emitter records.
//
// File layout (SPBD): +8 set count, +0x10 name table, +0x14 texture table, +0x18 texture size;
// set headers start at 0x48, 0x1c bytes each (+8 name, +0x10 emitter count, +0x14 emitter table);
// each emitter table entry is 8 bytes with the record offset first. An emitter's name is at
// record +0x38.
struct Resource {
    struct Set {
        std::string name;
        std::vector<EmitterData> emitters;
        std::vector<std::string> emitterNames;
    };

    std::vector<uint8_t> data;
    std::vector<Set> sets;
    std::map<std::string, int> setIndex;
    uint32_t nameTable = 0;
    uint32_t textureTable = 0;
    uint32_t textureSize = 0;

    std::string name(uint32_t off) const {
        return std::string(reinterpret_cast<const char*>(data.data() + nameTable + off));
    }

    void load(std::vector<uint8_t> bytes) {
        data = std::move(bytes);
        if (data.size() < 0x48 || memcmp(data.data(), "SPBD", 4)) {
            throw std::runtime_error("not an SPBD effect resource");
        }
        auto u32 = [&](size_t o) {
            uint32_t v;
            memcpy(&v, data.data() + o, 4);
            return v;
        };
        const uint32_t count = u32(8);
        nameTable = u32(0x10);
        textureTable = u32(0x14);
        textureSize = u32(0x18);
        auto nameAt = [&](uint32_t off) {
            const char* s = reinterpret_cast<const char*>(data.data() + nameTable + off);
            return std::string(s);
        };
        for (uint32_t s = 0; s < count; s++) {
            const size_t header = 0x48 + 0x1c * size_t(s);
            Set set;
            set.name = nameAt(u32(header + 8));
            const uint32_t emitterCount = u32(header + 0x10);
            const uint32_t table = u32(header + 0x14);
            for (uint32_t k = 0; k < emitterCount; k++) {
                EmitterData d{data.data() + u32(table + 8 * k)};
                set.emitters.push_back(d);
                set.emitterNames.push_back(nameAt(d.u(0x38)));
            }
            setIndex[set.name] = int(sets.size());
            sets.push_back(std::move(set));
        }
    }
};

// =============================================================================================
// Runtime instances
// =============================================================================================

struct Emitter;
struct EmitterSet;

// Per-particle alpha animation (side block via system +0x334). Alpha changes by inRate per frame
// until inFrame, holds, then changes by outRate per frame after outFrame.
struct AlphaAnim {
    float inRate = 0;
    float outRate = 0;
    int32_t inFrame = 0;
    int32_t outFrame = 0;
};

// Per-particle scale animation (side block via system +0x338), the same three phases as alpha
// with separate X and Y rates.
struct ScaleAnim {
    float inX = 0;
    float inY = 0;
    float outX = 0;
    float outY = 0;
    int32_t inFrame = 0;
    int32_t outFrame = 0;
};

// One particle (0x168 bytes in the original). The comments give the byte offset and, in
// brackets, the float index (byte offset / 4) as it appears in the decompiled code.
// A stripe (type 5: a ribbon along the particle's recent path): the history ring the particle calc
// writes once per frame (0x35038c) and the stripe draw reads (0x34c5f4). Allocated per particle at
// its creation (0x3536a0, 0x5880-byte objects); fields as the original's offsets.
struct Stripe {
    struct Entry {            // 0x58 bytes from +0xc
        V3 pos;               // +0x0c world (or, in local mode, emitter-space) position
        float width = 0;      // +0x18 particle scale x * data +0x21c
        float mtx[12] = {};   // +0x1c the emitter matrix (identity in local mode)
        V3 side;              // +0x4c the ribbon's side direction
        V3 dir;               // +0x58 smoothed travel direction
    };
    int tail = 0;          // +0x04
    int head = 0;          // +0x08
    int count = 0;         // +0x580c entries in use
    int frames = 0;        // +0x5818 records so far
    V3 dir;                // +0x584c
    V3 smooth1, smooth2;   // +0x5858, +0x5864 interpolated positions
    std::array<Entry, 256> entries;
};

struct Particle {
    bool used = false;                // pool slot in use (+0x144 data pointer != 0)
    float age = 0;                    // +0x00 [0]    frames lived, fractional
    int32_t ageInt = 0;               // +0x04 [1]    frames lived, whole
    int32_t life = 0;                 // +0x08 [2]    life in frames (-1 / 0x7fffffff: infinite)
    V3 pos;                           // +0x0c [3..5] position in emitter space
    V3 posDelta;                      // +0x18 [6..8] smoothed movement (for stretching / direction)
    V3 vel;                           // +0x24 [9..b] velocity per frame
    V3 worldPos;                      // +0x30 [c..e]
    V3 worldDir;                      // +0x3c [f..11] movement direction in world space
    V3 rot;                           // +0x48 [12..14] rotation (radians)
    V3 rotVel;                        // +0x54 [15..17] rotation speed
    float alpha = 0;                  // +0x60 [18]
    AlphaAnim alphaAnim;              // via +0x64 [19]
    float scaleX = 0;                 // +0x68 [1a]
    float scaleY = 0;                 // +0x6c [1b]
    ScaleAnim scaleAnim;              // via +0x70 [1c]
    float tex0Rot = 0;                // +0x74 [1d] texture 0 rotation
    float tex0PatU = 0;               // +0x78 [1e] texture 0 sprite-sheet cell offset
    float tex0PatV = 0;               // +0x7c [1f]
    float tex0ScrollU = 0;            // +0x80 [20] texture 0 scroll
    float tex0ScrollV = 0;            // +0x84 [21]
    float tex0ScaleU = 0;             // +0x88 [22] texture 0 scale
    float tex0ScaleV = 0;             // +0x8c [23]
    float tex0Flip = 0;               // +0x90 [24] 1: mirror U, 2: mirror V, 3: both
    float tex1Rot = 0;                // +0x94 [25] the same for texture 1
    float tex1PatU = 0;               // [26]
    float tex1PatV = 0;               // [27]
    float tex1ScrollU = 0;            // [28]
    float tex1ScrollV = 0;            // [29]
    float tex1ScaleU = 0;             // [2a]
    float tex1ScaleV = 0;             // [2b]
    float tex1Flip = 0;               // [2c]
    float alphaScale = 1;             // +0xb4 [2d] flicker factor on alpha
    float scaleScale = 1;             // +0xb8 [2e] flicker factor on scale
    float color0[4] = {};             // +0xbc [2f..32] colour channel 0
    float color1[4] = {};             // +0xcc [33..36] colour channel 1
    Mtx34 mtxRT{};                    // +0xdc [37..42]  copies of the emitter matrices, used when
    Mtx34 mtxSRT{};                   // +0x10c [43..4e] the particle does not follow its emitter
    const Mtx34* pRT = nullptr;       // +0x13c [4f] -> emitter RT (+0x28) or the copy
    const Mtx34* pSRT = nullptr;      // +0x140 [50] -> emitter SRT (+0x58) or the copy
    EmitterData data;                 // +0x144 [51]
    Particle* prev = nullptr;         // +0x148 [52]
    Particle* next = nullptr;         // +0x14c [53]
    Emitter* emitter = nullptr;       // +0x150 [54]
    std::shared_ptr<Stripe> stripe;   // +0x154 [55] (type 5 emitters)
    int32_t type = 0;                 // +0x158 [56] (2: child particle)
    uint32_t seed = 0;                // +0x15c [57] per-particle random value
    float childTimer = 0;             // +0x160 [58] frames since the last child emission
    float childLast = -1;             // +0x164 [59] whole age at the last child emission
};

// Per-emitter controls inside the set (set +0x50 + i * 0x14): the game can slow an emitter
// down, change its spawn interval or particle life, or hide it.
struct EmitterCtrl {
    float frameRate = 1;
    float intervalScale = 1;
    float lifeScale = 1;
    uint32_t visible = 0x3f;
    Emitter* emitter = nullptr;
};

// One emitter (0x298 bytes in the original).
struct Emitter {
    bool alive = false;            // +0xd8 calc object != 0
    float frame = 0;               // +0x00 [0] frames since creation
    float intervalTimer = 0;       // +0x04 [1] frames since the last timed emission
    uint32_t flags = 0;            // +0x08 [2] 1/2: no random translate/rotate, 8: emitted once, 0x10: calculated once
    int32_t particleCount = 0;     // +0x10
    int32_t childCount = 0;        // +0x14
    int32_t group = 0;             // +0x18 draw group
    EmitterSet* set = nullptr;     // +0x1c [7]
    EmitterCtrl* ctrl = nullptr;   // +0x20 [8]
    int32_t createId = 0;          // +0x24 [9]
    Mtx34 mtxRT{};                 // +0x28 [10..21] = set RT  * local RT  (no scale)
    Mtx34 mtxSRT{};                // +0x58 [22..33] = set SRT * local SRT (with scale)
    uint16_t rndA = 0;             // +0x88 index into the random cube table
    uint16_t rndB = 0;             // +0x8a index into the random unit-vector table
    uint32_t lcg = 0;              // +0x8c [0x23] the emitter's random generator
    int32_t interval = 0;          // +0x90 [0x24] frames between timed emissions
    float fade = 1;                // +0x94 [0x25] 1 while alive, falls to 0 on fade-out
    V3 prevPos;                    // +0x98 [0x26..0x28] world position last frame
    float distanceAccum = 0;       // +0xa4 [0x29] distance travelled, for distance emission
    V3 rndScale;                   // +0xa8 random offsets added to the local transform
    V3 rndRot;                     // +0xb4
    V3 rndTrans;                   // +0xc0
    int32_t followType = 0;        // +0xcc [0x33] 0: particles follow the emitter, 1: not, 2: position only
    EmitterData data;              // +0xdc [0x37]
    Particle* head = nullptr;      // +0xe0 [0x38] particle list
    Particle* childHead = nullptr;   // +0xe4 [0x39] child particle list
    Particle* tail = nullptr;        // +0xe8 [0x3a]
    Particle* childTail = nullptr;   // +0xec [0x3b]
    float emitRate = 0;              // +0x16c [0x5b] particles per frame
    float life = 0;                  // +0x170 [0x5c] base particle life
    V3 scale;                        // +0x174 [0x5d..0x5f] local transform
    V3 rotate;                       // +0x180 [0x60..0x62]
    V3 translate;                    // +0x18c [0x63..0x65]
    V3 color0;                       // +0x198 [0x66..0x68] emitter colour channel 0
    float alpha = 0;                 // +0x1a4 [0x69]
    float allDirVel = 0;             // +0x1a8 [0x6a] outward speed from the volume
    float dirVel = 0;                // +0x1ac [0x6b] speed along the emitter direction
    float particleScaleX = 1;        // +0x1b0
    float particleScaleY = 1;        // +0x1b4
    V3 color1;                       // +0x1b8 emitter colour channel 1
    V3 volumeScale;                  // +0x1c4 [0x71..0x73]
    Mtx34 localRT{};                 // +0x1d0 [0x74..]
    Mtx34 localSRT{};                // +0x200 [0x80..]
    float emitAccum = 0;             // +0x230 [0x8c] fractional particles carried over
    uint32_t flags188 = 0;           // +0x238 copy of data +0x188 (calc switches)
    int32_t drawn = 0;               // +0x284 particles drawn this frame
    uint8_t calcRot = 0;             // +0x290..+0x292 precomputed calc modes
    uint8_t calcTex = 0;
    uint8_t calcVel = 0;
    uint8_t worldDirOn = 0;    // +0x293 compute the world movement direction
    uint8_t alphaAnimOn = 0;   // +0x294 run the alpha animation
    uint8_t scaleAnimOn = 0;   // +0x295 run the scale animation
    // Shader binding (0x34e108): which colour channel is drawn (data +0x170 when data +0x180) and
    // which one feeds the TEV constant colour (data +0x174 when data +0x180).
    int32_t colorIndex = 0;   // +0x264
    int32_t konstIndex = 0;   // +0x268

    // Draw setup of the current frame (0x278e3c, computed before the fused particle calc+draw).
    bool drawThisFrame = false;
    float shaderColor[4] = {};   // vertex-shader uniform c82
    uint32_t konst = 0;          // TEV constant colour r | g << 8 | b << 16 | a << 24 (0x27a574)

    // Child pass (complex emitters with data +0x548 bit 0): material binding of the child block
    // (0x34e108 with data +0x70c: +0x26c colour channel = data +0x72c, +0x270 constant channel =
    // data +0x730) and its draw setup (0x278e3c, param_4 == 1).
    int32_t childColorIndex = 0;
    int32_t childKonstIndex = 0;
    bool childDrawThisFrame = false;
    float childShaderColor[4] = {};
    uint32_t childKonst = 0;
};

// One emitter set (0x28c bytes in the original): the effect as the game sees it. The scale,
// colour and velocity fields are multipliers the game can change while the effect plays.
struct EmitterSet {
    bool alive = false;
    int32_t emitterAlive = 0;
    int32_t emitterCount = 0;
    int32_t createId = 0;
    std::vector<Emitter*> emitters;     // +0x10 + i * 4
    std::array<EmitterCtrl, 16> ctrl;   // +0x50 + i * 0x14
    int32_t resId = 0;                  // +0x190
    int32_t setIndex = 0;               // +0x194
    int32_t group = 0;                  // +0x19c
    Mtx34 mtxSRT = identity();          // +0x1a0 world matrix with scale
    Mtx34 mtxRT = identity();           // +0x1d0 world matrix without scale
    float scale[3] = {1, 1, 1};         // +0x200
    float baseScale[2] = {1, 1};        // +0x20c (1 from the create)
    float particleScale[2] = {1, 1};    // +0x214
    float effectiveScale[2] = {1, 1};   // +0x21c = +0x20c * matrix scale (0x282854)
    float volumeScale[3] = {1, 1, 1};   // +0x224
    float color[4] = {1, 1, 1, 1};      // +0x230
    float allDirVelScale = 1;           // +0x240
    float dirVelScale = 1;              // +0x244
    float randomVelScale = 1;           // +0x248
    V3 addVel;                          // +0x24c velocity added to every new particle
    V3 dir;                             // +0x258 direction override (when useDir)
    int32_t startFrame = 0;             // +0x264
    V3 rotAdd;                          // +0x268 rotation added to every new particle
    bool fadeOut = false;               // +0x284 stop emitting and fade out
    bool useDir = false;                // +0x285
    bool stopCalc = false;              // +0x286 freeze
    bool stopDraw = false;              // +0x287 hide
    bool infinite = false;              // +0x288 has an emitter with infinite life
};

// A particle's per-draw uniform block: what 0x34aa00 / 0x34b494 upload as c1..c10 of the
// particle shaders (traced in gpu-trace-confetti-a). Vectors are in shader order (x, y, z, w).
struct DrawBlock {
    float color[4];      // c+0: rgb of the active colour channel, alpha * alphaScale
    float scaleRot[4];   // c+1: (scaleX * kx * scaleScale, scaleY * ky * scaleScale (+ stretch), tex0 rot, tex1 rot)
    float tex0[4];       // c+2: (patU + scrollU, patV - scrollV, scaleU, scaleV)
    float tex1[4];       // c+3
    float pos[4];        // c+4: world position, tex0 flip
    float dir[4];        // c+5: world velocity (when enabled; otherwise stale), tex1 flip
    float rot[4];        // c+6: rotation (x, y, z), unused
    Mtx34 mtx;           // c+7..c+9: written by the draw functions (0x4f8540 table)
    const Particle* particle;
    const Emitter* emitter;
    bool child = false;    // drawn with the emitter's child material, shader and uniforms
    bool stripe = false;   // the emitter's stripes (0x34c5f4: all its particles with a history)
};

// =============================================================================================
// The effect system
// =============================================================================================

struct System {
    const Resource* res = nullptr;
    const Tables* tab = nullptr;
    uint32_t xs[4] = {1, 0x6c078967u, 0x714acb41u, 0x48077044u};   // xorshift128 state (0x546988)
    int32_t createCounter = 0;                                     // +0x490
    std::list<EmitterSet> sets;   // pool (the order of the group lists is kept separately)
    std::vector<std::unique_ptr<Emitter>> emitterPool;
    std::vector<std::unique_ptr<Particle>> particlePool;
    std::map<int, std::vector<EmitterSet*>> groups;   // +0x120 + group * 4 list, in creation order
    int colorIndex = 0;                               // draw state 0x4f8560[3]: active colour channel
    float globalScale[2] = {1.0f, 1.0f};              // 0x4f8584 (traced at the checkpoint)
    // Stage effect colours (+0x4fc, +0x50c): set by 0x223f54 from the stage's 3DSEft.byml
    // StageEffectColor (parsed by 0x2e5460); negative or absent values become 1.
    float envColor0[4] = {1, 1, 1, 1};
    float envColor1[4] = {1, 1, 1, 1};
    std::vector<DrawBlock>* out = nullptr;   // per-frame draw output (optional)

    // Global xorshift128 step.
    uint32_t xorshift() {
        uint32_t t = xs[0];
        xs[0] = xs[1];
        t ^= t << 11;
        xs[1] = xs[2];
        const uint32_t w = xs[3];
        xs[2] = w;
        t = w ^ (w >> 19) ^ t ^ (t >> 8);
        xs[3] = t;
        return t;
    }

    // Per-emitter LCG step.
    static uint32_t lcgNext(uint32_t x) { return x * 0x41c64e6du + 0x3039u; }

    // Random float in [0, 1): (float)x * 2^-32, then step the LCG.
    static float lcgUnit(uint32_t& x) {
        const float f = u2f(x) * 2.3283064e-10f;
        x = lcgNext(x);
        return f;
    }

    // Pools: reuse the first free slot, grow when none is free.
    Particle* allocParticle() {
        for (auto& p : particlePool) {
            if (!p->used) {
                *p = Particle{};
                p->used = true;
                return p.get();
            }
        }
        particlePool.push_back(std::make_unique<Particle>());
        particlePool.back()->used = true;
        return particlePool.back().get();
    }

    Emitter* allocEmitter() {
        for (auto& e : emitterPool) {
            if (!e->alive) {
                *e = Emitter{};
                e->alive = true;
                return e.get();
            }
        }
        emitterPool.push_back(std::make_unique<Emitter>());
        emitterPool.back()->alive = true;
        return emitterPool.back().get();
    }

    // Game-facing API.
    EmitterSet* create(int setIndex, const Mtx34& mtx, int group, uint32_t emitterMask = 0xffffffffu);
    void kill(EmitterSet* s);
    void calcGroup(int group, float frameRate = 1.0f);
    void setVolumeScale(EmitterSet& s, const V3& scale);   // 0x32c27c
    void setEmitRatio(EmitterSet& s, float ratio);         // 0x32c200

    // Emitter level.
    void initEmitter(Emitter& e, EmitterData d, uint32_t setRandom);   // 0x352d78 + 0x34f3fc + 0x34ed28
    void emitterLocalMatrix(Emitter& e);                               // 0x2795f0 (random part) + matrices
    void calcEmitter(Emitter& e, float frameRate);                     // 0x34ff3c
    Particle* emitVolume(Emitter& e);                                  // volume table 0x4f904c
    void distanceEmit(Emitter& e);                                     // 0x34f98c

    // Child particles.
    void childTrigger(Emitter& e, Particle& p, float frameRate);   // 0x35038c (child part)
    void stripeRecord(Emitter& e, Particle& p, float frameRate);   // 0x35038c (stripe part)
    void stripeDrain(Emitter& e, Particle& p);                     // 0x3509c0 (stripe part)
    void emitChildren(Emitter& e, const Particle& parent);         // 0x350e38
    void calcChild(Particle& p, float frameRate);                  // 0x34a380
    void freeChild(Particle& p);                                   // 0x27a944

    // Particle level.
    void initParticle(Emitter& e, Particle& p);                               // 0x27b628
    void calcSimple(Particle& p, float frameRate);                            // 0x34aa00
    void calcComplex(Particle& p, float frameRate);                           // 0x34b494
    void colorAnim(Particle& p, int channel);                                 // 0x351880
    void texPattern(float& u, float& v, const Particle& p, int animOffset);   // 0x27a664
    void freeParticle(Particle& p);                                           // 0x27a948
    void link(Emitter& e, Particle& p);
};

// =============================================================================================
// Implementation
// =============================================================================================

// High word of (int32)b * (uint32)a. With a random 32-bit `a` this is "a random integer below b"
// (the original uses smull/umull pairs for it).
inline int32_t mulHi(uint32_t a, int32_t b) {
    return int32_t((int64_t(b) * int64_t(uint64_t(a))) >> 32);
}

// Signed division by 100 through the 0x51eb851f reciprocal, as compiled. The effect data stores
// many timings as percentages of a particle's life.
inline int32_t div100(int32_t x) {
    const int32_t h = int32_t((int64_t(0x51eb851f) * int64_t(x)) >> 32);
    return (h >> 5) - (h >> 31);
}

// Rotate v by the rotation that takes +Y onto `axis` (the quaternion form used by the volume
// and cone code). Lets a hemisphere or cone that is authored around +Y point anywhere.
inline V3 rotateYTo(const V3& axis, const V3& v) {
    const float zero = 0.0f;
    const float one = 1.0f;
    const float ax = axis.x;
    const float ay = axis.y;
    const float az = axis.z;
    const float d = zero * ax + one * ay + zero * az + one;   // 1 + dot(Y, axis)

    // Quaternion (qx, qy, qz, qw); the decompiled names were fVar27, fVar21, fVar19, fVar23.
    float qx = zero;
    float qy = zero;
    float qz = zero;
    float qw = one;
    float w = zero;
    if (floatBits(d) > 0x34000000) {
        w = std::sqrt(d * 2.0f);
        const float k = one / w;
        qy = (zero * ax - zero * az) * k;
        qz = (zero * ay - one * ax) * k;
        qx = (one * az - zero * ay) * k;
        qw = w * 0.5f;
    } else {
        // axis is -Y: half a turn about X.
        qx = one;
        qy = zero;
        qz = zero;
        qw = zero;
    }

    // Matrix from the quaternion (0x345068: fVar22 = 2 / |q|^2).
    const float n = 2.0f / (qx * qx + qy * qy + qz * qz + qw * qw);
    const float yn = qy * n;
    const float zn = qz * n;
    const float xxn = qx * qx * n;
    const float wx = qw * qx * n;
    const float m01 = qx * yn - qw * zn;
    const float m10 = qx * yn + qw * zn;
    const float m02 = qx * zn + qw * yn;
    const float m20 = qx * zn - qw * yn;
    const float m12 = qy * zn - wx;
    const float m21 = qy * zn + wx;
    const float m00 = one - (qy * yn + qz * zn);
    const float m11 = one - (xxn + qz * zn);
    const float m22 = one - (xxn + qy * yn);
    Mtx34 m = {m00, m01, m02, zero, m10, m11, m12, zero, m20, m21, m22, zero};
    return mtxApply(m, v);
}

// Local emitter matrices from scale / rotate (XYZ Euler, radians) / translate
// (0x34ed28 tail / 0x2795f0 tail). `srt` includes the scale, `rt` does not.
inline void buildLocal(const Tables& t, const V3& sc, const V3& r, const V3& tr, Mtx34& srt, Mtx34& rt) {
    float sx, cx, sy, cy, sz, cz;
    t.sinCosRad(r.x, sx, cx);
    t.sinCosRad(r.y, sy, cy);
    t.sinCosRad(r.z, sz, cz);

    srt[0] = cy * cz * sc.x;
    srt[1] = sc.y * (sx * sy * cz - cx * sz);
    srt[2] = (sx * sz + cx * cz * sy) * sc.z;
    srt[3] = tr.x;
    srt[4] = cy * sz * sc.x;
    srt[5] = sc.y * (cx * cz + sx * sy * sz);
    srt[6] = (sy * cx * sz - sx * cz) * sc.z;
    srt[7] = tr.y;
    srt[8] = -(sc.x * sy);
    srt[9] = sx * cy * sc.y;
    srt[10] = cx * cy * sc.z;
    srt[11] = tr.z;

    rt[0] = cy * cz;
    rt[1] = sx * sy * cz - cx * sz;
    rt[2] = sx * sz + cx * cz * sy;
    rt[3] = tr.x;
    rt[4] = cy * sz;
    rt[5] = cx * cz + sx * sy * sz;
    rt[6] = sy * cx * sz - sx * cz;
    rt[7] = tr.y;
    rt[8] = -sy;
    rt[9] = sx * cy;
    rt[10] = cx * cy;
    rt[11] = tr.z;
}

// Insert a new particle into its emitter's list. Data +4 bit 0x400 selects append (new particles
// drawn last, on top) instead of the default prepend.
inline void System::link(Emitter& e, Particle& p) {
    if ((e.data.u(4) & 0x400) == 0) {
        // Prepend.
        Particle* oldHead = e.head;
        e.head = &p;
        if (oldHead) {
            oldHead->prev = &p;
            p.next = oldHead;
        }
        p.prev = nullptr;
        if (!oldHead) {
            p.next = nullptr;
        }
        if (!e.tail) {
            e.tail = &p;
        }
    } else {
        // Append.
        Particle* oldTail = e.tail;
        e.tail = &p;
        if (!oldTail) {
            p.prev = nullptr;
            p.next = nullptr;
        } else {
            oldTail->next = &p;
            p.prev = oldTail;
            p.next = nullptr;
        }
        if (!e.head) {
            e.head = &p;
        }
    }
    e.particleCount++;
}

// 0x27a948: unlink a (non-child) particle and return it to the pool.
inline void System::freeParticle(Particle& p) {
    Emitter& e = *p.emitter;
    e.particleCount--;
    if (p.prev && p.next) {
        p.next->prev = p.prev;
        p.prev->next = p.next;
    } else if (e.head == &p) {
        e.head = p.next;
        if (p.next) {
            p.next->prev = p.prev;
        } else {
            e.tail = nullptr;
        }
    } else if (e.tail == &p) {
        e.tail = p.prev;
        if (p.prev) {
            p.prev->next = p.next;
        } else {
            e.head = nullptr;
        }
    }
    p.used = false;
}

// ---------------------------------------------------------------------------------------------
// Creating and killing effects
// ---------------------------------------------------------------------------------------------

// 0x35332c (CreateEmitterSetID), with 0x352d78 per emitter.
//
// Creates emitter set `setIndex` at world matrix `mtx` in draw group `group`. `emitterMask`
// selects which of the set's emitters are created (bit i = emitter i). The emitters are created
// from the last to the first, which matters because each creation may draw from the global
// random generator.
inline EmitterSet* System::create(int setIndex, const Mtx34& mtx, int group, uint32_t emitterMask) {
    const auto& resourceSet = res->sets.at(size_t(setIndex));
    sets.emplace_back();
    EmitterSet& s = sets.back();
    s.alive = true;
    s.mtxSRT = mtx;
    s.mtxRT = mtx;
    s.createId = createCounter;
    s.resId = 0;
    s.setIndex = setIndex;
    s.group = group;
    groups[group].push_back(&s);

    // One global random draw per set; emitters with seed mode -1 share it.
    const uint32_t setRandom = xorshift();

    const int emitterTotal = int(resourceSet.emitters.size());
    s.emitters.clear();
    for (int i = emitterTotal - 1; i >= 0; i--) {
        if ((emitterMask & (1u << i)) == 0) {
            continue;
        }
        Emitter* e = allocEmitter();
        s.emitters.push_back(e);
        EmitterCtrl& c = s.ctrl[size_t(i)];
        c = EmitterCtrl{};
        c.emitter = e;
        e->ctrl = &c;
        e->set = &s;
        e->group = group;
        initEmitter(*e, resourceSet.emitters[size_t(i)], setRandom);
        if (resourceSet.emitters[size_t(i)].i(0x31c) == 0x7fffffff) {
            s.infinite = true;
        }
    }
    s.emitterAlive = int(s.emitters.size());
    s.emitterCount = s.emitterAlive;
    createCounter++;
    return &s;
}

// Remove a set immediately, with all its particles (no fade-out).
inline void System::kill(EmitterSet* s) {
    for (Emitter* e : s->emitters) {
        if (!(e && e->alive && e->set == s)) {
            continue;
        }
        for (Particle* p = e->head; p;) {
            Particle* next = p->next;
            p->used = false;
            p = next;
        }
        for (Particle* p = e->childHead; p;) {
            Particle* next = p->next;
            p->used = false;
            p = next;
        }
        e->childHead = nullptr;
        e->childTail = nullptr;
        e->alive = false;
        e->head = nullptr;
        e->tail = nullptr;
    }
    s->alive = false;
    auto& groupList = groups[s->group];
    for (size_t i = 0; i < groupList.size(); i++) {
        if (groupList[i] == s) {
            groupList.erase(groupList.begin() + long(i));
            break;
        }
    }
}

// 0x352d78 (seed, createId) + 0x34f3fc (InitEmitter, including 0x2795f0) + 0x34ed28 (resource
// copy): set up a fresh emitter from its data record.
inline void System::initEmitter(Emitter& e, EmitterData d, uint32_t setRandom) {
    // Seed of the emitter's LCG (data +8): -1 share the set's draw, 0 draw a fresh global value,
    // anything else is a fixed seed (the same effect then looks identical every time).
    const int32_t seedMode = d.i(8);
    uint32_t seed;
    if (seedMode == -1) {
        seed = setRandom;
    } else if (seedMode == 0) {
        seed = xorshift();
    } else {
        seed = uint32_t(seedMode);
    }
    e.rndA = uint16_t(seed);
    e.rndB = uint16_t(seed >> 16);
    e.lcg = seed;
    e.createId = createCounter;

    // 0x34f3fc
    e.data = d;
    e.frame = 0;
    e.intervalTimer = 0;
    e.particleCount = 0;
    e.childCount = 0;
    e.flags = 0;
    {
        // Emission interval: base (+0x2d0) minus a random part below +0x2d4.
        const uint32_t x = e.lcg;
        e.lcg = lcgNext(x);
        e.interval = d.i(0x2d0) - mulHi(x, d.i(0x2d4));
    }
    e.fade = 1.0f;
    e.followType = d.i(0x190);
    e.head = nullptr;
    e.childHead = nullptr;
    e.tail = nullptr;
    e.childTail = nullptr;
    e.emitAccum = 0;
    e.localRT = identity();
    e.localSRT = identity();
    e.distanceAccum = 0;
    emitterLocalMatrix(e);   // 0x2795f0 (random offsets + matrices)
    e.prevPos = {e.mtxRT[3], e.mtxRT[7], e.mtxRT[11]};

    // 0x34ed28: copy the animatable values out of the resource.
    e.followType = d.i(0x190);
    e.emitRate = d.f(0x2c4);
    e.life = float(d.i(0x31c));
    e.allDirVel = d.f(0x2d8);
    e.dirVel = d.f(0x2dc);
    e.alpha = d.f(0x2b0);
    e.color0 = {d.f(0x290), d.f(0x294), d.f(0x298)};
    e.color1 = {d.f(0x2a0), d.f(0x2a4), d.f(0x2a8)};
    if (d.u(0xc) & 0x10000) {
        // 0x353110: the stage effect colours (system +0x4fc / +0x50c) replace the emitter
        // colours, so one effect can be tinted to match each stage.
        e.color0 = {envColor0[0], envColor0[1], envColor0[2]};
        e.color1 = {envColor1[0], envColor1[1], envColor1[2]};
    }
    e.volumeScale = {d.f(0x284), d.f(0x288), d.f(0x28c)};
    e.particleScaleX = 1.0f;
    e.particleScaleY = 1.0f;
    e.scale = {d.f(0x208) + e.rndScale.x, d.f(0x20c) + e.rndScale.y, d.f(0x210) + e.rndScale.z};
    e.rotate = {d.f(0x214) + e.rndRot.x, d.f(0x218) + e.rndRot.y, d.f(0x21c) + e.rndRot.z};
    e.translate = {d.f(0x220) + e.rndTrans.x, d.f(0x224) + e.rndTrans.y, d.f(0x228) + e.rndTrans.z};
    e.flags188 = d.u(0x188);
    // Shader binding (0x352d78 -> 0x34e108; draw state 0x4f8560[3] via 0x278e3c).
    e.colorIndex = d.b(0x180) ? d.i(0x170) : 0;
    e.konstIndex = d.b(0x180) ? d.i(0x174) : 0;
    if (d.kind() == 1 && (d.u(0x548) & 1)) {
        e.childColorIndex = d.i(0x72c);
        e.childKonstIndex = d.i(0x730);
    }
    buildLocal(*tab, e.scale, e.rotate, e.translate, e.localSRT, e.localRT);

    // Calc mode bytes (0x34ed28 tail): world direction, alpha and scale animation switches.
    e.worldDirOn = (d.b(0x188) & 0x10) != 0;
    e.alphaAnimOn = (d.b(0x188) & 0x80) != 0;
    e.scaleAnimOn = (d.b(0x188) & 0x40) != 0;
}

// 0x2795f0: random rotate / translate offsets of the emitter's local transform (ranges data
// +0x22c and +0x238; emitter flag bits 2 / 1 suppress them), then the local matrices.
inline void System::emitterLocalMatrix(Emitter& e) {
    const EmitterData& d = e.data;
    e.rndScale = {0, 0, 0};

    if ((e.flags & 2) == 0) {
        float r[3];
        for (int k = 0; k < 3; k++) {
            const float f = u2f(e.lcg);
            e.lcg = lcgNext(e.lcg);
            r[k] = (f * 2.3283064e-10f * 2.0f - 1.0f) * d.f(0x22c + 4 * k);   // uniform in [-range, range]
        }
        e.rndRot = {r[0], r[1], r[2]};
    } else {
        e.rndRot = {0, 0, 0};
    }

    if ((e.flags & 1) == 0) {
        float r[3];
        for (int k = 0; k < 3; k++) {
            const float f = u2f(e.lcg);
            e.lcg = lcgNext(e.lcg);
            r[k] = (f * 2.3283064e-10f * 2.0f - 1.0f) * d.f(0x238 + 4 * k);
        }
        e.rndTrans = {r[0], r[1], r[2]};
    } else {
        e.rndTrans = {0, 0, 0};
    }

    const V3 sc{d.f(0x208) + 0.0f, d.f(0x20c) + 0.0f, d.f(0x210) + 0.0f};
    const V3 rt{d.f(0x214) + e.rndRot.x, d.f(0x218) + e.rndRot.y, d.f(0x21c) + e.rndRot.z};
    const V3 tr{d.f(0x220) + e.rndTrans.x, d.f(0x224) + e.rndTrans.y, d.f(0x228) + e.rndTrans.z};
    buildLocal(*tab, sc, rt, tr, e.localSRT, e.localRT);
}

// 0x32c27c (the keeper's 0x223880; EffectObjAreaCube's appear 0x24fb9c with its placement
// scale): the set's volume scale (+0x224), and every live emitter with a local translation
// (|t|^2 >= 1e-6) rebuilds its local matrices with that translation scaled.
inline void System::setVolumeScale(EmitterSet& s, const V3& scale) {
    s.volumeScale[0] = scale.x;
    s.volumeScale[1] = scale.y;
    s.volumeScale[2] = scale.z;
    for (Emitter* e : s.emitters) {
        if (!e || !e->alive || e->set != &s) {
            continue;
        }
        const EmitterData& d = e->data;
        V3 tr{d.f(0x220) + e->rndTrans.x, d.f(0x224) + e->rndTrans.y, d.f(0x228) + e->rndTrans.z};
        if (tr.x * tr.x + tr.y * tr.y + tr.z * tr.z < 1.0000001e-06f) {
            continue;
        }
        tr = {tr.x * scale.x, tr.y * scale.y, tr.z * scale.z};
        const V3 sc{d.f(0x208) + e->rndScale.x, d.f(0x20c) + e->rndScale.y, d.f(0x210) + e->rndScale.z};
        const V3 rt{d.f(0x214) + e->rndRot.x, d.f(0x218) + e->rndRot.y, d.f(0x21c) + e->rndRot.z};
        buildLocal(*tab, sc, rt, tr, e->localSRT, e->localRT);
    }
}

// 0x32c200 (the keeper's 0x223804; EffectObjAreaCube's EffectEmitRatio): every emitter's
// emission-rate multiplier (set +0x50 + i * 0x14, used by emissionCount).
inline void System::setEmitRatio(EmitterSet& s, float ratio) {
    for (size_t i = 0; i < s.emitters.size() && i < s.ctrl.size(); i++) {
        s.ctrl[i].frameRate = ratio;
    }
}

// ---------------------------------------------------------------------------------------------
// Emitter calc
// ---------------------------------------------------------------------------------------------

// 0x34ff3c (CalcEmitter, simple and complex emitters).
//
// Per frame: update the fade (in while alive, out after the game asked the set to fade), place
// the emitter in the world, and emit when the frame lies in the emission window [+0x2c8, +0x2cc).
// Emission is either timed (every `interval` frames, a burst of emitRate particles) or by
// distance travelled (data +0x181). An emitter whose window has passed dies once its particles
// are gone.
inline void System::calcEmitter(Emitter& e, float frameRate) {
    const EmitterData& d = e.data;
    EmitterSet& s = *e.set;

    bool doEmit;
    if (!s.fadeOut) {
        float f = e.fade + d.f(0x53c);
        doEmit = true;
        if (floatBits(f) > 0x3f800000) {
            f = 1.0f;
        }
        e.fade = f;
    } else {
        const float f = e.fade - d.f(0x53c);
        doEmit = d.b(0x17d) == 0;   // +0x17d: keep emitting while fading out
        e.fade = f;
        if (!(f >= 0.0f) || f == 0.0f) {
            e.alive = false;   // 0x2a0b74: the emitter ends
            return;
        }
    }

    e.mtxRT = mtxMul(s.mtxRT, e.localRT);
    e.mtxSRT = mtxMul(s.mtxSRT, e.localSRT);

    // Traced (gpu-trace gotv, GoalItemGot/ptcl inheriting emitter movement): on the first calc the
    // previous position equals the current one (no inherited jump from the creation matrix or from
    // stale emitter memory); the mechanism that resets it is not recovered.
    if (e.frame == 0.0f) {
        e.prevPos = {e.mtxRT[3], e.mtxRT[7], e.mtxRT[11]};
    }

    const float start = float(s.startFrame);
    if (int32_t(e.frame - start) < d.i(0x2cc) && doEmit) {
        if (d.i(0x2c8) <= int32_t(e.frame - start)) {
            if (d.b(0x181) == 0) {
                // Timed emission.
                const bool first = (e.flags & 8) == 0;
                const int32_t scaledInterval = e.interval * int32_t(e.ctrl->intervalScale);
                bool redraw = scaledInterval == 0 || first;
                bool emit = true;
                if (scaledInterval != 0) {
                    const float intervalFrames = float(scaledInterval);
                    redraw = (intervalFrames <= e.intervalTimer) || redraw;
                    if (intervalFrames <= e.intervalTimer) {
                        e.intervalTimer = 0.0f;
                    } else {
                        e.intervalTimer = e.intervalTimer + frameRate;
                        if (!first) {
                            emit = false;
                        }
                    }
                }
                if (emit) {
                    emitVolume(e);
                    if (d.b(0x183) != 0) {
                        // +0x183: re-randomise the local transform on every emission.
                        emitterLocalMatrix(e);
                        e.mtxRT = mtxMul(s.mtxRT, e.localRT);
                        e.mtxSRT = mtxMul(s.mtxSRT, e.localSRT);
                    }
                    if (redraw) {
                        // Next interval: base + random below +0x2d4.
                        const uint32_t x = e.lcg;
                        e.lcg = lcgNext(x);
                        e.interval = mulHi(x, d.i(0x2d4)) + d.i(0x2d0);
                    }
                }
            } else {
                distanceEmit(e);   // 0x34f98c
            }
        }
    } else if (!e.head && !e.childHead) {
        e.alive = false;
        return;
    }

    e.frame = e.frame + frameRate;
    e.flags |= 0x10;
    e.prevPos = {e.mtxRT[3], e.mtxRT[7], e.mtxRT[11]};
}

// 0x34f98c: distance emission (data +0x181), used for trails.
//
// Before the emitter's first full calc (flags bit 0x10) there is one plain emission. Afterwards
// there is one emission per +0x2b4 units travelled since the previous position, each from the
// position interpolated along the travelled segment. The travelled distance counts as zero below
// +0x2c0, and is then clamped to [+0x2bc, +0x2b8].
inline void System::distanceEmit(Emitter& e) {
    const EmitterData& d = e.data;
    if ((e.flags & 0x10) == 0) {
        emitVolume(e);
        return;
    }
    const float cx = e.mtxRT[3];
    const float cy = e.mtxRT[7];
    const float cz = e.mtxRT[11];
    const float px = e.prevPos.x;
    const float py = e.prevPos.y;
    const float pz = e.prevPos.z;
    float dx = px - cx;
    float dy = py - cy;
    float dz = pz - cz;
    float lengthSq = dx * dx;
    lengthSq = lengthSq + dy * dy;
    lengthSq = lengthSq + dz * dz;
    float dist = std::sqrt(lengthSq);

    if (d.f(0x2c0) > dist) {
        dist = 0.0f;
    }
    if (dist == 0.0f) {
        dist = d.f(0x2bc);
    } else if (d.f(0x2bc) > dist) {
        dist = d.f(0x2bc);
    } else if (!(d.f(0x2b8) >= dist)) {
        dist = d.f(0x2b8);
    }

    float acc = e.distanceAccum + dist;
    const float perUnit = d.f(0x2b4) != 0.0f ? 1.0f / d.f(0x2b4) : 0.0f;
    const int count = int(acc * perUnit);
    const float invDist = dist != 0.0f ? 1.0f / dist : 0.0f;
    for (int i = 0; i < count; i++) {
        acc = acc - d.f(0x2b4);
        const float t = acc * invDist;
        const float w = 1.0f - t;
        const float x = cx * w + px * t;
        const float y = cy * w + py * t;
        const float z = cz * w + pz * t;
        // Emit from the interpolated point, then restore the current position.
        e.mtxRT[3] = x;
        e.mtxSRT[3] = x;
        e.mtxRT[7] = y;
        e.mtxSRT[7] = y;
        e.mtxRT[11] = z;
        e.mtxSRT[11] = z;
        emitVolume(e);
        e.mtxRT[3] = cx;
        e.mtxRT[7] = cy;
        e.mtxRT[11] = cz;
        e.mtxSRT[3] = cx;
        e.mtxSRT[7] = cy;
        e.mtxSRT[11] = cz;
    }
    e.distanceAccum = acc;
}

// ---------------------------------------------------------------------------------------------
// Emission volumes
// ---------------------------------------------------------------------------------------------

// Common prologue of the volume functions: how many particles to emit this frame. The rate is
// accumulated so fractional rates emit every few frames; the very first frame always emits at
// least one particle when the rate is non-zero. Distance emission always emits one.
inline int emissionCount(Emitter& e) {
    const float rate = e.ctrl->frameRate;
    float acc = e.emitAccum + e.emitRate * rate;
    e.emitAccum = acc;
    if (e.frame == 0.0f && floatBits(acc) < 0x3f800000 && e.emitRate * rate != 0.0f) {
        e.emitAccum = 1.0f;
    }
    int count = int(e.emitAccum);
    if (e.data.b(0x181) != 0) {
        count = 1;
    }
    e.emitAccum = e.emitAccum - float(count);
    return count;
}

// XZ diffusion (data +0x310): an extra horizontal push away from the emitter's vertical axis,
// shared by the volume functions. A particle on the axis gets a random horizontal direction.
inline void xzDiffusion(Emitter& e, Particle& p) {
    const float k = e.data.f(0x310);
    if (k == 0.0f) {
        return;
    }
    float x = p.pos.x;
    float z = p.pos.z;
    const float zero = 0.0f;
    if (floatBits(x * x + zero * zero + z * z) < 0x800001) {
        const float f0 = u2f(e.lcg);
        const uint32_t l1 = System::lcgNext(e.lcg);
        const float f1 = float(l1);
        e.lcg = System::lcgNext(l1);
        x = f0 * 2.3283064e-10f * 2.0f - 1.0f;
        z = f1 * 2.3283064e-10f * 2.0f - 1.0f;
    }
    const float lengthSq = x * x + zero * zero + z * z;
    float nx = zero;
    float ny = zero;
    float nz = zero;
    if (lengthSq != zero) {
        const float r = 1.0f / std::sqrt(lengthSq);
        ny = zero * r;
        nx = x * r;
        nz = z * r;
    }
    p.vel.x = p.vel.x + nx * k;
    p.vel.y = p.vel.y + ny * k;
    p.vel.z = p.vel.z + nz * k;
}

// Spawn this frame's particles in the emitter's volume (volume table 0x4f904c; all 15 types).
//
// Common volume parameters: radii +0x254 / +0x258 / +0x25c (times the set and emitter volume
// scales), arc start +0x260 and arc length +0x264 (binary angles, a full turn = 2^32), +0x17f
// "random arc start", +0x26c hollowness of the filled shapes, +0x182 / +0x270..+0x27c latitude
// limit and axis of the spheres. Each particle starts at a point of the volume, with an outward
// velocity of allDirVel in the direction of that point.
//
// Constants: 2.3283064e-10 = 2^-32 (random u32 -> [0, 1)); 1.4629181e-09 = 2pi / 2^32 (binary
// angle -> radians); 683565248 = 2^32 / 2pi; 5.9604645e-08 = 2^-24.
inline Particle* System::emitVolume(Emitter& e) {
    const EmitterData& d = e.data;
    EmitterSet& s = *e.set;
    const int type = d.i(0x250);
    // Stripe particles (types 5 / 6, e.g. the protagonist's fall trail PlayerBlur) are not ported:
    // such an emitter emits nothing (reported once) instead of stopping the game.
    if (d.i(0x328) == 6) {   // the connected stripe (0x34d250) is not ported
        static bool reported = false;
        if (!reported) {
            reported = true;
            std::fprintf(stderr, "Eft: connected stripes (type 6) not ported (emitter skipped)\n");
        }
        return nullptr;
    }
    const int count = emissionCount(e);
    if (count == 0) {
        return nullptr;
    }
    const float allDir = e.allDirVel * s.allDirVelScale;
    Particle* first = nullptr;
    auto make = [&]() {
        Particle* p = allocParticle();
        p->data = d;
        if (!first) {
            first = p;
        }
        return p;
    };

    switch (type) {
    case 0: {
        // 0x34495c point: every particle starts at the centre and flies in a random direction.
        for (int i = 0; i < count; i++) {
            Particle& p = *make();
            p.pos = {0, 0, 0};
            const V3& r = tab->randomUnit[e.rndB & 0x1ff];
            e.rndB++;
            p.vel = {r.x * allDir, r.y * allDir, r.z * allDir};
            xzDiffusion(e, p);
            initParticle(e, p);
        }
        break;
    }
    case 1: {
        // 0x344ce4 circle (the rim of an ellipse in the XZ plane), random angle within the arc.
        const float len = float(d.u(0x264)) * 1.4629181e-09f;
        float start = float(d.i(0x260)) * 1.4629181e-09f;
        const float rx = d.f(0x254) * s.volumeScale[0] * e.volumeScale.x;
        const float rz = d.f(0x25c) * s.volumeScale[2] * e.volumeScale.y;
        if (d.b(0x17f)) {
            const float f = u2f(e.lcg);
            e.lcg = lcgNext(e.lcg);
            start = f * 2.3283064e-10f * 3.1415927f * 2.0f;
        }
        for (int i = 0; i < count; i++) {
            Particle& p = *make();
            const float f = u2f(e.lcg);
            e.lcg = lcgNext(e.lcg);
            float sn, cs;
            tab->sinCosRad(start + f * 2.3283064e-10f * len, sn, cs);
            p.pos = {sn * rx, 0.0f, cs * rz};
            p.vel = {sn * allDir, 0.0f, cs * allDir};
            xzDiffusion(e, p);
            initParticle(e, p);
        }
        break;
    }
    case 4: {
        // 0x345068 sphere (surface). Either a random longitude within the arc and a uniform
        // height, or (+0x182) a cap around an axis down to latitude +0x270.
        const float radX = d.f(0x254) * s.volumeScale[0] * e.volumeScale.x;
        const float radY = d.f(0x258) * s.volumeScale[1] * e.volumeScale.y;
        const float radZ = d.f(0x25c) * s.volumeScale[2] * e.volumeScale.z;
        float len = 0.0f;
        float start = 0.0f;
        if (d.b(0x182) == 0) {
            len = float(d.u(0x264)) * 1.4629181e-09f;
            if (d.b(0x17f) == 0) {
                start = float(d.i(0x260)) * 1.4629181e-09f;
            } else {
                const float f = u2f(e.lcg);
                e.lcg = lcgNext(e.lcg);
                start = f * 2.3283064e-10f * 3.1415927f * 2.0f;
            }
        }
        for (int i = 0; i < count; i++) {
            Particle& p = *make();
            float sn, cs;
            {
                const float f = u2f(e.lcg);
                e.lcg = lcgNext(e.lcg);
                if (d.b(0x182) == 0) {
                    tab->sinCosRad(start + f * 2.3283064e-10f * len, sn, cs);
                } else {
                    tab->sinCosRad(f * 2.3283064e-10f * 3.1415927f * 2.0f, sn, cs);
                }
            }
            float y;
            if (d.b(0x182) == 0) {
                const float f = u2f(e.lcg);
                e.lcg = lcgNext(e.lcg);
                y = f * 2.3283064e-10f * 2.0f - 1.0f;
            } else {
                // y uniform in [cos(+0x270), 1] (the cosine through the table, inline).
                const uint32_t u = uint32_t(int64_t(d.f(0x270) * 683565248.0f));
                const float* t = &tab->sinCos[(u >> 24) * 4];
                const float frac = float(u & 0xffffff);
                const float f = u2f(e.lcg);
                e.lcg = lcgNext(e.lcg);
                y = 1.0f - (1.0f - (t[2] + t[3] * frac * 5.9604645e-08f)) * f * 2.3283064e-10f;
            }
            const float q = 1.0f - y * y;
            const float r = (q >= 0.0f && q != 0.0f) ? std::sqrt(q) : 0.0f;
            V3 v{r * sn, y, r * cs};
            if (d.b(0x182) != 0 && !(d.f(0x274) == 0.0f && d.i(0x278) == 0x3f800000 && d.f(0x27c) == 0.0f)) {
                v = rotateYTo({d.f(0x274), d.f(0x278), d.f(0x27c)}, v);
            }
            p.pos = {v.x * radX, v.y * radY, v.z * radZ};
            p.vel = {v.x * allDir, v.y * allDir, v.z * allDir};
            xzDiffusion(e, p);
            initParticle(e, p);
        }
        break;
    }
    case 2: {
        // 0x3476a0 circle, equal division: the particles are spread evenly along the arc
        // (a full circle when the arc length is -1), optionally jittered by +0x268 degrees.
        const float rx = d.f(0x254) * s.volumeScale[0] * e.volumeScale.x;
        const float rz = d.f(0x25c) * s.volumeScale[2] * e.volumeScale.y;
        uint32_t angle = d.u(0x260);
        if (d.b(0x17f)) {
            angle = e.lcg;
            e.lcg = lcgNext(e.lcg);
        }
        uint32_t step = 0;
        if (count >= 2) {
            step = d.i(0x264) == -1 ? 0xffffffffu / uint32_t(count) : d.u(0x264) / uint32_t(count - 1);
        }
        for (int i = 0; i < count; i++) {
            Particle& p = *make();
            float a = float(int32_t(angle)) * 1.4629181e-09f;
            if (d.f(0x268) != 0.0f) {
                const float f = u2f(e.lcg);
                e.lcg = lcgNext(e.lcg);
                a = a + d.f(0x268) * (f * 2.3283064e-10f - 0.5f) * 0.011111111f * 3.1415927f;
            }
            float sn, cs;
            tab->sinCosRad(a, sn, cs);
            angle = angle + step;
            p.pos = {sn * rx, 0.0f, cs * rz};
            p.vel = {sn * allDir, 0.0f, cs * allDir};
            xzDiffusion(e, p);
            initParticle(e, p);
        }
        break;
    }
    case 3: {
        // 0x346504 filled circle (a disc, or a ring when the hollow ratio +0x26c is set). The
        // square root of a uniform value spreads points evenly over the area.
        const float len = float(d.u(0x264)) * 1.4629181e-09f;
        float start = float(d.i(0x260)) * 1.4629181e-09f;
        const float rx = d.f(0x254) * s.volumeScale[0] * e.volumeScale.x;
        const float rz = d.f(0x25c) * s.volumeScale[2] * e.volumeScale.y;
        if (d.b(0x17f)) {
            const float f = u2f(e.lcg);
            e.lcg = lcgNext(e.lcg);
            start = f * 2.3283064e-10f * 3.1415927f * 2.0f;
        }
        const float hollow = 1.0f - d.f(0x26c);
        for (int i = 0; i < count; i++) {
            Particle& p = *make();
            const float f0 = u2f(e.lcg);
            const uint32_t l1 = lcgNext(e.lcg);
            float sn, cs;
            tab->sinCosRad(start + f0 * 2.3283064e-10f * len, sn, cs);
            const float f1 = u2f(l1);
            e.lcg = lcgNext(l1);
            const float q = f1 * 2.3283064e-10f + hollow * hollow * (1.0f - f1 * 2.3283064e-10f);
            const float r = (q >= 0.0f && q != 0.0f) ? std::sqrt(q) : 0.0f;
            const float x = r * sn;
            const float z = r * cs;
            p.pos = {x * rx, 0.0f, z * rz};
            p.vel = {x * allDir, 0.0f, z * allDir};
            xzDiffusion(e, p);
            initParticle(e, p);
        }
        break;
    }
    case 5: {
        // 0x347a80 sphere, equal division: one particle per point of a precomputed evenly
        // spread point set (+0x17e selects the set), optionally only the points above latitude
        // +0x270 and rotated onto an axis.
        const std::vector<V3>& pointSet = tab->divide.at(d.b(0x17e));
        const float radX = d.f(0x254) * s.volumeScale[0] * e.volumeScale.x;
        const float radY = d.f(0x258) * s.volumeScale[1] * e.volumeScale.y;
        const float radZ = d.f(0x25c) * s.volumeScale[2] * e.volumeScale.z;
        const bool axis = d.b(0x182) != 0 && !(d.f(0x274) == 0.0f && d.i(0x278) == 0x3f800000 && d.f(0x27c) == 0.0f);
        for (int i = 0; i < count && size_t(i) < pointSet.size(); i++) {
            V3 v = pointSet[size_t(i)];
            if (d.b(0x182) != 0 && !(floatBits(d.f(0x270)) > 0x40490e38)) {
                const uint32_t u = uint32_t(int64_t(d.f(0x270) * 683565248.0f));
                const float* t = &tab->sinCos[(u >> 24) * 4];
                const float c = t[2] + t[3] * float(u & 0xffffff) * 5.9604645e-08f;
                if (!(v.y > c)) {
                    continue;
                }
            }
            Particle& p = *make();
            if (axis) {
                v = rotateYTo({d.f(0x274), d.f(0x278), d.f(0x27c)}, v);
            }
            p.pos = {v.x * radX, v.y * radY, v.z * radZ};
            p.vel = {v.x * allDir, v.y * allDir, v.z * allDir};
            xzDiffusion(e, p);
            initParticle(e, p);
        }
        break;
    }
    case 6: {
        // 0x348058 sphere, equal division (second table): as type 5, with the point set
        // 0x4f90a8[+0x17e + 2] (sets of 2 to 64 points, so +0x17e = n - 4).
        const size_t set = size_t(d.b(0x17e)) + 2;
        if (set >= tab->divideMany.size()) {
            static bool reported = false;
            if (!reported) {
                reported = true;
                std::fprintf(stderr, "Eft: volume type 6 needs the second divide table (re-import)\n");
            }
            return nullptr;
        }
        const std::vector<V3>& pointSet = tab->divideMany[set];
        const float radX = d.f(0x254) * s.volumeScale[0] * e.volumeScale.x;
        const float radY = d.f(0x258) * s.volumeScale[1] * e.volumeScale.y;
        const float radZ = d.f(0x25c) * s.volumeScale[2] * e.volumeScale.z;
        const bool axis = d.b(0x182) != 0 && !(d.f(0x274) == 0.0f && d.i(0x278) == 0x3f800000 && d.f(0x27c) == 0.0f);
        for (int i = 0; i < count && size_t(i) < pointSet.size(); i++) {
            V3 v = pointSet[size_t(i)];
            if (d.b(0x182) != 0 && !(floatBits(d.f(0x270)) > 0x40490e38)) {
                const uint32_t u = uint32_t(int64_t(d.f(0x270) * 683565248.0f));
                const float* t = &tab->sinCos[(u >> 24) * 4];
                const float c = t[2] + t[3] * float(u & 0xffffff) * 5.9604645e-08f;
                if (!(v.y > c)) {
                    continue;
                }
            }
            Particle& p = *make();
            if (axis) {
                v = rotateYTo({d.f(0x274), d.f(0x278), d.f(0x27c)}, v);
            }
            p.pos = {v.x * radX, v.y * radY, v.z * radZ};
            p.vel = {v.x * allDir, v.y * allDir, v.z * allDir};
            xzDiffusion(e, p);
            initParticle(e, p);
        }
        break;
    }
    case 7: {
        // 0x3468d8 filled sphere: as the sphere, with a random radius factor R in
        // [1 - hollow, 1] (+0x26c).
        const float radX = d.f(0x254) * s.volumeScale[0] * e.volumeScale.x;
        const float radY = d.f(0x258) * s.volumeScale[1] * e.volumeScale.y;
        const float radZ = d.f(0x25c) * s.volumeScale[2] * e.volumeScale.z;
        float len = 0.0f;
        float start = 0.0f;
        if (d.b(0x182) == 0) {
            len = float(d.u(0x264)) * 1.4629181e-09f;
            if (d.b(0x17f) == 0) {
                start = float(d.i(0x260)) * 1.4629181e-09f;
            } else {
                const float f = u2f(e.lcg);
                e.lcg = lcgNext(e.lcg);
                start = f * 2.3283064e-10f * 3.1415927f * 2.0f;
            }
        }
        for (int i = 0; i < count; i++) {
            Particle& p = *make();
            float sn, cs;
            {
                const float f = u2f(e.lcg);
                e.lcg = lcgNext(e.lcg);
                if (d.b(0x182) == 0) {
                    tab->sinCosRad(start + f * 2.3283064e-10f * len, sn, cs);
                } else {
                    tab->sinCosRad(f * 2.3283064e-10f * 3.1415927f * 2.0f, sn, cs);
                }
            }
            float y;
            if (d.b(0x182) == 0) {
                const float f = u2f(e.lcg);
                e.lcg = lcgNext(e.lcg);
                y = f * 2.3283064e-10f * 2.0f - 1.0f;
            } else {
                const uint32_t u = uint32_t(int64_t(d.f(0x270) * 683565248.0f));
                const float* t = &tab->sinCos[(u >> 24) * 4];
                const float f = u2f(e.lcg);
                e.lcg = lcgNext(e.lcg);
                y = 1.0f - (1.0f - (t[2] + t[3] * float(u & 0xffffff) * 5.9604645e-08f)) * f * 2.3283064e-10f;
            }
            const float q = 1.0f - y * y;
            const float r = (q >= 0.0f && q != 0.0f) ? std::sqrt(q) : 0.0f;
            float g = u2f(e.lcg) * 2.3283064e-10f;
            e.lcg = lcgNext(e.lcg);
            const float sg = (g >= 0.0f && g != 0.0f) ? std::sqrt(g) : 0.0f;
            const float R = (1.0f + sg * d.f(0x26c)) - d.f(0x26c);
            V3 v{r * sn, y, r * cs};
            if (d.b(0x182) != 0 && !(d.f(0x274) == 0.0f && d.i(0x278) == 0x3f800000 && d.f(0x27c) == 0.0f)) {
                v = rotateYTo({d.f(0x274), d.f(0x278), d.f(0x27c)}, v);
            }
            p.pos = {v.x * radX * R, v.y * radY * R, v.z * radZ * R};
            p.vel = {v.x * allDir, v.y * allDir, v.z * allDir};
            xzDiffusion(e, p);
            initParticle(e, p);
        }
        break;
    }
    case 9: {
        // 0x346fd4 filled cylinder: a filled circle (hollow +0x26c) with a random height in
        // [-ry, ry]. The velocity stays horizontal.
        const float len = float(d.u(0x264)) * 1.4629181e-09f;
        float start = float(d.i(0x260)) * 1.4629181e-09f;
        const float rx = d.f(0x254) * s.volumeScale[0] * e.volumeScale.x;
        const float ry = d.f(0x258) * s.volumeScale[1] * e.volumeScale.y;
        const float rz = d.f(0x25c) * s.volumeScale[2] * e.volumeScale.z;
        if (d.b(0x17f)) {
            const float f = u2f(e.lcg);
            e.lcg = lcgNext(e.lcg);
            start = f * 2.3283064e-10f * 3.1415927f * 2.0f;
        }
        const float hollow = 1.0f - d.f(0x26c);
        for (int i = 0; i < count; i++) {
            Particle& p = *make();
            const float f0 = u2f(e.lcg);
            const uint32_t l1 = lcgNext(e.lcg);
            float sn, cs;
            tab->sinCosRad(start + f0 * 2.3283064e-10f * len, sn, cs);
            const float f1 = u2f(l1);
            const uint32_t l2 = lcgNext(l1);
            const float q = f1 * 2.3283064e-10f + hollow * hollow * (1.0f - f1 * 2.3283064e-10f);
            const float r = (q >= 0.0f && q != 0.0f) ? std::sqrt(q) : 0.0f;
            const float f2 = u2f(l2);
            e.lcg = lcgNext(l2);
            p.vel = {sn * allDir, 0.0f, cs * allDir};
            p.pos = {sn * r * rx, (f2 * 2.3283064e-10f * 2.0f - 1.0f) * ry, cs * r * rz};
            xzDiffusion(e, p);
            initParticle(e, p);
        }
        break;
    }
    case 8: {
        // 0x345d34 cylinder (the mantle): a circle with a random height in [-ry, ry].
        const float len = float(d.u(0x264)) * 1.4629181e-09f;
        float start = float(d.i(0x260)) * 1.4629181e-09f;
        const float rx = d.f(0x254) * s.volumeScale[0] * e.volumeScale.x;
        const float ry = d.f(0x258) * s.volumeScale[1] * e.volumeScale.y;
        const float rz = d.f(0x25c) * s.volumeScale[2] * e.volumeScale.z;
        if (d.b(0x17f)) {
            const float f = u2f(e.lcg) * 2.3283064e-10f;
            e.lcg = lcgNext(e.lcg);
            start = f * 3.1415927f * 2.0f;
        }
        for (int i = 0; i < count; i++) {
            Particle& p = *make();
            const float f0 = u2f(e.lcg);
            const uint32_t l1 = lcgNext(e.lcg);
            float sn, cs;
            tab->sinCosRad(start + f0 * 2.3283064e-10f * len, sn, cs);
            const float f1 = u2f(l1);
            e.lcg = lcgNext(l1);
            p.pos = {sn * rx, (f1 * 2.3283064e-10f * 2.0f - 1.0f) * ry, cs * rz};
            p.vel = {sn * allDir, 0.0f, cs * allDir};
            xzDiffusion(e, p);
            initParticle(e, p);
        }
        break;
    }
    case 11: {
        // 0x345710 box. Without a hollow ratio (+0x26c) a point anywhere in the box; with one, a
        // point on the shell between the inner cube (side h) and the unit cube: one of three
        // slabs is picked in proportion to its volume, filled in the positive octant, then
        // each axis is mirrored at random.
        const float rx = d.f(0x254) * s.volumeScale[0] * e.volumeScale.x;
        const float ry = d.f(0x258) * s.volumeScale[1] * e.volumeScale.y;
        const float rz = d.f(0x25c) * s.volumeScale[2] * e.volumeScale.z;
        auto unit = [&]() {
            const float f = u2f(e.lcg);
            e.lcg = lcgNext(e.lcg);
            return f;
        };
        for (int i = 0; i < count; i++) {
            Particle& p = *make();
            float h = 1.0f - d.f(0x26c);
            if (h == 1.0f) {
                h = 0.999f;
            }
            float x, y;
            if (h == 0.0f) {
                const float f0 = unit();
                const float f1 = unit();
                const float f2 = unit();
                x = f0 * 2.3283064e-10f * 2.0f - 1.0f;
                y = f1 * 2.3283064e-10f * 2.0f - 1.0f;
                p.pos.z = (f2 * 2.3283064e-10f * 2.0f - 1.0f) * rz;
            } else {
                const float a = 1.0f - h;
                const float f0 = unit();
                const float t = (1.0f - h * h * h) * f0 * 2.3283064e-10f;
                if (a <= t) {
                    const float b = a + a * h;
                    const float f1 = unit();
                    const float f2 = unit();
                    const float f3 = unit();
                    if (b <= t) {
                        p.pos = {f1 * 2.3283064e-10f * h, f2 * 2.3283064e-10f * h, h + f3 * 2.3283064e-10f * a};
                    } else {
                        p.pos = {h + f1 * 2.3283064e-10f * a, f2 * 2.3283064e-10f * h, f3 * 2.3283064e-10f};
                    }
                } else {
                    const float f1 = unit();
                    const float f2 = unit();
                    const float f3 = unit();
                    p.pos = {f1 * 2.3283064e-10f, h + f2 * 2.3283064e-10f * a, f3 * 2.3283064e-10f};
                }
                if (floatBits(unit() * 2.3283064e-10f) < 0x3f000000) {
                    p.pos.x = -p.pos.x;
                }
                if (floatBits(unit() * 2.3283064e-10f) < 0x3f000000) {
                    p.pos.y = -p.pos.y;
                }
                float z = p.pos.z;
                if (floatBits(unit() * 2.3283064e-10f) < 0x3f000000) {
                    z = -z;
                }
                x = p.pos.x;
                y = p.pos.y;
                p.pos.z = z * rz;
            }
            p.pos.x = x * rx;
            p.pos.y = y * ry;
            float nx = 0;
            float ny = 0;
            float nz = 0;
            if (!(p.pos.x == 0.0f && p.pos.y == 0.0f && p.pos.z == 0.0f)) {
                const float r = 1.0f / std::sqrt(p.pos.x * p.pos.x + p.pos.y * p.pos.y + p.pos.z * p.pos.z);
                nx = p.pos.x * r;
                ny = p.pos.y * r;
                nz = p.pos.z * r;
            }
            p.vel = {nx * allDir, ny * allDir, nz * allDir};
            xzDiffusion(e, p);
            initParticle(e, p);
        }
        break;
    }
    case 14: {
        // 0x346100 rectangle (the rim of a rectangle in the XZ plane). Per particle four LCG
        // values: the first picks an edge pair (below 0x7fffffff: the edges at z = +-rz), the
        // second its side, the third (x edges) or fourth (z edges) the place along it.
        const float rx = d.f(0x254) * s.volumeScale[0] * e.volumeScale.x;
        const float rz = d.f(0x25c) * s.volumeScale[2] * e.volumeScale.y;
        for (int i = 0; i < count; i++) {
            Particle& p = *make();
            const uint32_t l0 = e.lcg;
            const uint32_t l1 = lcgNext(l0);
            const uint32_t l2 = lcgNext(l1);
            const uint32_t l3 = lcgNext(l2);
            e.lcg = lcgNext(l3);
            if (l0 < 0x7fffffffu) {
                p.pos = {rx * (u2f(l2) * 2.3283064e-10f * 2.0f - 1.0f), 0.0f, l1 < 0x7fffffffu ? rz : -rz};
            } else {
                p.pos = {l1 < 0x7fffffffu ? rx : -rx, 0.0f, rz * (u2f(l3) * 2.3283064e-10f * 2.0f - 1.0f)};
            }
            float nx = 0, nz = 0;
            if (!(p.pos.x == 0.0f && p.pos.z == 0.0f)) {
                const float r = 1.0f / std::sqrt(p.pos.x * p.pos.x + p.pos.y * p.pos.y + p.pos.z * p.pos.z);
                nx = p.pos.x * r;
                nz = p.pos.z * r;
            }
            p.vel = {nx * allDir, 0.0f, nz * allDir};
            xzDiffusion(e, p);
            initParticle(e, p);
        }
        break;
    }
    case 12: {
        // 0x349f70 line along Z: length l (+0x25c), shifted back by (l + l * +0x280) / 2; one LCG
        // draw per particle; the velocity is allDirVel along +Z (not along the position).
        const float l = d.f(0x25c) * s.volumeScale[2] * e.volumeScale.x;
        const float back = l + d.f(0x280) * l;
        for (int i = 0; i < count; i++) {
            Particle& p = *make();
            const float f = u2f(e.lcg);
            e.lcg = lcgNext(e.lcg);
            p.pos = {0.0f, 0.0f, f * 2.3283064e-10f * l - back * 0.5f};
            p.vel = {0.0f, 0.0f, allDir};
            xzDiffusion(e, p);
            initParticle(e, p);
        }
        break;
    }
    case 10: {
        // 0x349b3c box surface: per particle five LCG values. The first picks a face pair by
        // thirds (below 0x55555555 the faces z = +-rz, below 0xaaaaaaaa y = +-ry, else x = +-rx),
        // the second its side (below 0x7fffffff the positive one), the last three a place in
        // [-1, 1) on the x, y and z axes (the face's own axis ignores its value). The velocity
        // points outward from the centre.
        const float rx = d.f(0x254) * s.volumeScale[0] * e.volumeScale.x;
        const float ry = d.f(0x258) * s.volumeScale[1] * e.volumeScale.y;
        const float rz = d.f(0x25c) * s.volumeScale[2] * e.volumeScale.z;
        for (int i = 0; i < count; i++) {
            Particle& p = *make();
            const uint32_t l0 = e.lcg;
            const uint32_t l1 = lcgNext(l0);
            const uint32_t l2 = lcgNext(l1);
            const uint32_t l3 = lcgNext(l2);
            const uint32_t l4 = lcgNext(l3);
            e.lcg = lcgNext(l4);
            const float a = u2f(l2) * 2.3283064e-10f * 2.0f - 1.0f;
            const float b = u2f(l3) * 2.3283064e-10f * 2.0f - 1.0f;
            const float c = u2f(l4) * 2.3283064e-10f * 2.0f - 1.0f;
            const bool positive = l1 < 0x7fffffffu;
            if (l0 < 0x55555555u) {
                p.pos = {a * rx, b * ry, positive ? rz : -rz};
            } else if (l0 < 0xaaaaaaaau) {
                p.pos = {a * rx, positive ? ry : -ry, c * rz};
            } else {
                p.pos = {positive ? rx : -rx, b * ry, c * rz};
            }
            float nx = 0, ny = 0, nz = 0;
            if (!(p.pos.x == 0.0f && p.pos.y == 0.0f && p.pos.z == 0.0f)) {
                const float r = 1.0f / std::sqrt(p.pos.x * p.pos.x + p.pos.y * p.pos.y + p.pos.z * p.pos.z);
                nx = p.pos.x * r;
                ny = p.pos.y * r;
                nz = p.pos.z * r;
            }
            p.vel = {nx * allDir, ny * allDir, nz * allDir};
            xzDiffusion(e, p);
            initParticle(e, p);
        }
        break;
    }
    case 13: {
        // 0x3473e8 line along Z, equal division: as the line (type 12), but the particles sit
        // at t = 0, 1 / (n - 1), ..., 1 of its length (one particle: t = 0.5); no LCG draw.
        const float l = d.f(0x25c) * s.volumeScale[2] * e.volumeScale.x;
        const float back = l + d.f(0x280) * l;
        float t = count == 1 ? 0.5f : 0.0f;
        const float step = count == 1 ? 0.0f : 1.0f / float(count - 1);
        for (int i = 0; i < count; i++) {
            Particle& p = *make();
            p.pos = {0.0f, 0.0f, t * l - back * 0.5f};
            t = t + step;
            p.vel = {0.0f, 0.0f, allDir};
            xzDiffusion(e, p);
            initParticle(e, p);
        }
        break;
    }
    default: {
        // The volume function table at 0x4f904c has 15 entries (0..14), all ported.
        static std::set<int> reported;
        if (reported.insert(type).second) {
            std::fprintf(stderr, "Eft: unknown volume type %d (emitter skipped)\n", type);
        }
        return nullptr;
    }
    }
    e.flags |= 8;
    return first;
}

// ---------------------------------------------------------------------------------------------
// Particle initialisation
// ---------------------------------------------------------------------------------------------

// 0x27b628: give a freshly placed particle its velocity, life, scale, rotation, colours and
// texture state. The volume code has already set its position and outward velocity.
//
// Every random draw below comes from the emitter's LCG (or its table indices) in exactly the
// original order; the comments name what each draw decides.
inline void System::initParticle(Emitter& e, Particle& p) {
    const EmitterData& d = e.data;
    EmitterSet& s = *e.set;

    // Random speed factor: 1 minus up to +0x2e0.
    float randomVel;
    {
        const float f = u2f(e.lcg);
        e.lcg = lcgNext(e.lcg);
        randomVel = 1.0f - d.f(0x2e0) * s.randomVelScale * f * 2.3283064e-10f;
    }
    const float dirVel = e.dirVel * s.dirVelScale;

    // Particles that follow the emitter (follow type 0) read its matrices every frame; the others
    // keep a copy of the matrices at birth and stay where they were emitted.
    if (e.followType == 0) {
        p.pRT = &e.mtxRT;
        p.pSRT = &e.mtxSRT;
    } else {
        p.mtxRT = e.mtxRT;
        p.mtxSRT = e.mtxSRT;
        p.pRT = &p.mtxRT;
        p.pSRT = &p.mtxSRT;
    }

    // Random position offset (+0x314): a random unit vector times the offset.
    if (d.f(0x314) != 0.0f) {
        const V3& r = tab->randomUnit[e.rndB & 0x1ff];
        e.rndB++;
        const float k = d.f(0x314);
        p.pos.x = p.pos.x + r.x * k;
        p.pos.y = p.pos.y + r.y * k;
        p.pos.z = p.pos.z + r.z * k;
    }

    // Directional velocity: along the emitter direction (+0x2e4), spread inside a cone of
    // +0x2f0 degrees, or along the set's direction override.
    if (!s.useDir) {
        if (d.f(0x2f0) == 0.0f) {
            p.vel.x = (p.vel.x + d.f(0x2e4) * dirVel) * randomVel;
            p.vel.y = (p.vel.y + d.f(0x2e8) * dirVel) * randomVel;
            p.vel.z = (p.vel.z + d.f(0x2ec) * dirVel) * randomVel;
        } else {
            // 0x27b7e0..0x27ba9c: a random direction in the cone of +0x2f0 degrees around the Y axis
            // (cos uniform in [1 - spread/90, 1], azimuth uniform), rotated onto the emitter direction
            // (+0x2e4) by the quaternion taking Y to it (180 degrees about X when opposite).
            const uint32_t l0 = e.lcg;
            const uint32_t l1 = lcgNext(l0);
            float c = 1.0f - d.f(0x2f0) * 0.011111111f;
            const uint32_t u = uint32_t(int64_t(u2f(l0) * 2.3283064e-10f * 2.0f * 3.1415927f * 683565248.0f));
            const float fr = u2f(u & 0xffffff) * 5.9604645e-08f;
            const float* t = &tab->sinCos[(u >> 24) * 4];
            const float sn = t[0] + t[1] * fr;
            const float cs = t[2] + t[3] * fr;
            e.lcg = lcgNext(l1);
            const float f1 = u2f(l1) * 2.3283064e-10f;
            const float y = c + f1 * (1.0f - c);
            const float q2 = 1.0f - y * y;
            const float r = q2 > 0.0f ? std::sqrt(q2) : 0.0f;
            const V3 v{r * cs, y, r * sn};

            // Normalised emitter direction.
            const float dx = d.f(0x2e4);
            const float dy = d.f(0x2e8);
            const float dz = d.f(0x2ec);
            const float l2 = dx * dx + dy * dy + dz * dz;
            float nx = 0;
            float ny = 0;
            float nz = 0;
            if (l2 != 0.0f) {
                const float k = 1.0f / std::sqrt(l2);
                nx = dx * k;
                ny = dy * k;
                nz = dz * k;
            }

            // Quaternion from Y to the direction: (cross(Y, n), 1 + dot(Y, n)), normalised.
            const float zero = 0.0f;
            const float one = 1.0f;
            const float cx = one * nz - zero * ny;
            const float cy = zero * nx - zero * nz;
            const float cz = zero * ny - one * nx;
            const float dot = ((zero * nx + one * ny) + zero * nz) + one;
            float qx = 1;
            float qy = 0;
            float qz = 0;
            float qw = 0;
            if (floatBits(dot) > 0x34000000) {
                const float s2 = std::sqrt(dot * 2.0f);
                qw = s2 * 0.5f;
                const float k = one / s2;
                qx = cx * k;
                qy = cy * k;
                qz = cz * k;
            }
            const float n2 = 2.0f / (((qx * qx + qy * qy) + qz * qz) + qw * qw);
            const float xs = qx * n2;
            const float ys = qy * n2;
            const float zs = qz * n2;
            const float xx = qx * xs;
            const float yy = qy * ys;
            const float zz = qz * zs;
            const float wz = qw * zs;
            const float xy = qx * ys;
            const float wx = qw * xs;
            const float yz = qy * zs;
            const float wy = qw * ys;
            const float xz = qx * zs;
            const Mtx34 m{one - (yy + zz), xy - wz, xz + wy, 0.0f,    xy + wz,         one - (xx + zz),
                          yz - wx,         0.0f,    xz - wy, yz + wx, one - (xx + yy), 0.0f};
            const V3 w = mtxApply(m, v);
            p.vel.x = (p.vel.x + w.x * dirVel) * randomVel;
            p.vel.y = (p.vel.y + w.y * dirVel) * randomVel;
            p.vel.z = (p.vel.z + w.z * dirVel) * randomVel;
        }
    } else {
        p.vel.x = (p.vel.x + s.dir.x * dirVel) * randomVel;
        p.vel.y = (p.vel.y + s.dir.y * dirVel) * randomVel;
        p.vel.z = (p.vel.z + s.dir.z * dirVel) * randomVel;
    }

    // Random velocity jitter per axis (+0x2f4..+0x2fc) from the random cube table.
    {
        const V3& r = tab->randomCube[e.rndA & 0x1ff];
        e.rndA++;
        const float vy = p.vel.y + r.y * d.f(0x2f8);
        p.vel.x = p.vel.x + r.x * d.f(0x2f4);
        p.vel.y = vy;
        p.vel.z = p.vel.z + r.z * d.f(0x2fc);
    }

    // Inherit a fraction (+0x318) of the emitter's own movement since last frame, so particles
    // trail behind a moving emitter instead of being left behind at rest.
    if (d.f(0x318) != 0.0f) {
        const V3 cur{e.mtxRT[3], e.mtxRT[7], e.mtxRT[11]};
        Mtx34 inv;
        mtxInverse(inv, e.followType == 0 ? e.mtxSRT : p.mtxSRT);
        const V3 a = mtxApply(inv, cur);
        mtxInverse(inv, e.followType == 0 ? e.mtxSRT : p.mtxSRT);
        const V3 b = mtxApply(inv, e.prevPos);
        const float k = d.f(0x318);
        const float vy = p.vel.y + (a.y - b.y) * k;
        p.vel.x = p.vel.x + (a.x - b.x) * k;
        p.vel.y = vy;
        p.vel.z = p.vel.z + (a.z - b.z) * k;
    }

    // The set's added velocity, through the set's rotation rows.
    {
        const Mtx34& m = s.mtxRT;
        const float ay = s.addVel.y;
        const float az = s.addVel.z;
        const float ax = s.addVel.x;
        const float vx = ay * m[4] + az * m[8] + p.vel.x + ax * m[0];
        const float vz = ay * m[6] + az * m[10] + p.vel.z + ax * m[2];
        const float vy = ay * m[5] + az * m[9] + p.vel.y + ax * m[1];
        p.ageInt = 0;
        p.age = 0.0f;
        p.vel = {vx, vy, vz};
        p.posDelta = p.vel;
    }

    // Seed, and random texture mirroring (each texture may flip U and/or V by coin toss).
    p.seed = e.lcg;
    const uint32_t l1 = lcgNext(e.lcg);
    const uint32_t l2 = lcgNext(l1);
    const float f1 = u2f(l1);
    const float f2 = u2f(l2);
    e.lcg = lcgNext(l2);
    p.tex0Flip = 0;
    p.tex1Flip = 0;
    if (d.b(0x33e) == 1 && floatBits(f1 * 2.3283064e-10f) > 0x3f000000) {
        p.tex0Flip = 1.0f;
    }
    if (d.b(0x33f) == 1 && floatBits(f2 * 2.3283064e-10f) > 0x3f000000) {
        p.tex0Flip = p.tex0Flip + 2.0f;
    }
    if (d.b(0x3b6) == 1 && floatBits(f1 * 2.3283064e-10f) > 0x3f000000) {
        p.tex1Flip = 1.0f;
    }
    if (d.b(0x3b7) == 1 && floatBits(f2 * 2.3283064e-10f) > 0x3f000000) {
        p.tex1Flip = p.tex1Flip + 2.0f;
    }

    // Life: the emitter's life minus a random part below +0x320, times the controller's scale.
    if (d.i(0x31c) == 0x7fffffff) {
        p.life = 0x7fffffff;
    } else {
        const uint32_t x = e.lcg;
        e.lcg = lcgNext(x);
        const float r = float(mulHi(x, d.i(0x320)));
        p.life = int32_t((e.life - r) * e.ctrl->lifeScale);
    }

    // Scale (randomly reduced by up to +0x4e0), and the alpha / scale animations. Their phase
    // boundaries are stored as percentages of the life (+0x4c8 / +0x4cc for alpha, +0x4d8 /
    // +0x4dc for scale); the rates are chosen so that the values reach their targets exactly at
    // the phase boundaries.
    float sx, sy;
    {
        const float f = u2f(e.lcg);
        e.lcg = lcgNext(e.lcg);
        const float k = 1.0f - d.f(0x4e0) * f * 2.3283064e-10f;
        sx = s.particleScale[0] * k;
        sy = s.particleScale[1] * k;
    }
    const int32_t lastFrame = p.life - 1;
    if (lastFrame == 0) {
        // A one-frame particle: no animation.
        p.alphaAnim = {0.0f, 0.0f, -1, 0x7fffffff};
        p.alpha = d.f(0x4bc);
        p.scaleAnim = {0.0f, 0.0f, 0.0f, 0.0f, -1, 0x7fffffff};
        p.scaleX = d.f(0x4ec) * d.f(0x4e4) * sx;
        p.scaleY = d.f(0x4f0) * d.f(0x4e8) * sy;
    } else {
        AlphaAnim& a = p.alphaAnim;
        a.inFrame = div100(d.i(0x4c8) * lastFrame);
        a.outFrame = div100(d.i(0x4cc) * lastFrame);
        a.inRate = a.inFrame == 0 ? 0.0f : d.f(0x4c0) / float(a.inFrame);
        a.outRate = d.i(0x4cc) == 100 ? 0.0f : d.f(0x4c4) / float(lastFrame - a.outFrame);

        ScaleAnim& c = p.scaleAnim;
        c.inFrame = div100(d.i(0x4d8) * lastFrame);
        c.outFrame = div100(d.i(0x4dc) * lastFrame);
        const float inFrames = float(c.inFrame);
        const float outFrames = float(lastFrame - c.outFrame);
        const float ix = d.f(0x4f4) * (1.0f / inFrames);
        const float iy = d.f(0x4f8) * (1.0f / inFrames);
        const float ox = d.f(0x4fc) * (1.0f / outFrames);
        const float oy = d.f(0x500) * (1.0f / outFrames);

        // The first calc adds one step, so the start values are one step back.
        p.alpha = d.f(0x4bc) - a.inRate;
        // Association as compiled (0x27c188): data * (value * random scale).
        c.inX = d.f(0x4e4) * (ix * sx);
        c.inY = d.f(0x4e8) * (iy * sy);
        c.outX = d.f(0x4e4) * (ox * sx);
        c.outY = d.f(0x4e8) * (oy * sy);
        p.scaleX = d.f(0x4e4) * ((d.f(0x4ec) - ix) * sx);
        p.scaleY = d.f(0x4e8) * ((d.f(0x4f0) - iy) * sy);
    }

    // Rotation and rotation speed: base + set addition + random range (6 draws), then optional
    // random sign flips per axis (+0x184..+0x186), so some particles spin the other way.
    {
        float r[6];
        for (int k = 0; k < 6; k++) {
            r[k] = u2f(e.lcg);
            e.lcg = lcgNext(e.lcg);
        }
        const float c = 2.3283064e-10f;
        const float rx = d.f(0x508) + s.rotAdd.x + d.f(0x514) * r[0] * c;
        const float ry = d.f(0x50c) + s.rotAdd.y + d.f(0x518) * r[1] * c;
        const float rz = d.f(0x510) + s.rotAdd.z + d.f(0x51c) * r[2] * c;
        const float vx = d.f(0x520) + d.f(0x52c) * r[3] * c;
        const float vy = d.f(0x524) + d.f(0x530) * r[4] * c;
        const float vz = d.f(0x528) + d.f(0x534) * r[5] * c;
        p.rot = {rx, ry, rz};
        p.rotVel = {vx, vy, vz};
        if (d.b(0x184)) {
            const float f = u2f(e.lcg);
            e.lcg = lcgNext(e.lcg);
            if (floatBits(f * c) > 0x3f000000) {
                p.rot.x = -rx;
                p.rotVel.x = -vx;
            }
        }
        if (d.b(0x185)) {
            const float f = u2f(e.lcg);
            e.lcg = lcgNext(e.lcg);
            if (floatBits(f * c) > 0x3f000000) {
                p.rot.y = -ry;
                p.rotVel.y = -vy;
            }
        }
        if (d.b(0x186)) {
            const float f = u2f(e.lcg);
            e.lcg = lcgNext(e.lcg);
            if (floatBits(f * c) > 0x3f000000) {
                p.rot.z = -rz;
                p.rotVel.z = -vz;
            }
        }
    }

    // Colours: fixed, or (mode 1 at +0x428 / +0x42c) one of the three colour keys picked by
    // seed % 3, so a burst can mix three colours.
    {
        const uint32_t pick = p.seed % 3u;
        const int c0 = d.i(0x428) == 1 ? 0x430 + int(pick) * 0x10 : 0x430;
        const int c1 = d.i(0x42c) == 1 ? 0x460 + int(pick) * 0x10 : 0x460;
        p.color0[0] = d.f(c0);
        p.color0[1] = d.f(c0 + 4);
        p.color0[2] = d.f(c0 + 8);
        p.color1[0] = d.f(c1);
        p.color1[1] = d.f(c1 + 4);
        p.color1[2] = d.f(c1 + 8);
    }

    // Texture scroll / scale / rotation start values (10 draws, base +- range).
    {
        float r[10];
        for (int k = 0; k < 10; k++) {
            r[k] = u2f(e.lcg);
            e.lcg = lcgNext(e.lcg);
        }
        const float c = 4.656613e-10f;   // 2^-31: random in [0, 2)
        auto rnd = [&](int base, int range, float x) {
            return (d.f(base) + d.f(range)) - x * d.f(range) * c;
        };
        p.tex0ScrollU = rnd(0x37c, 0x384, r[0]);
        p.tex0ScrollV = rnd(0x380, 0x388, r[1]);
        p.tex0ScaleU = rnd(0x394, 0x39c, r[2]);
        p.tex0ScaleV = rnd(0x398, 0x3a0, r[3]);
        p.tex0Rot = rnd(0x3a8, 0x3ac, r[4]);
        p.tex1ScrollU = rnd(0x3f4, 0x3fc, r[5]);
        p.tex1ScrollV = rnd(0x3f8, 0x400, r[6]);
        p.tex1ScaleU = rnd(0x40c, 0x414, r[7]);
        p.tex1ScaleV = rnd(0x410, 0x418, r[8]);
        p.tex1Rot = rnd(0x420, 0x424, r[9]);
    }

    // Texture pattern start cells: a random cell of the sprite sheet when it has two or more.
    auto pattern = [&](int countOff, int colsOff, int sizeU, int sizeV, float& u, float& v) {
        const uint8_t cellCount = d.b(countOff);
        if (cellCount < 2) {
            u = 0.0f;
            v = 0.0f;
            return;
        }
        const uint32_t x = e.lcg;
        e.lcg = lcgNext(x);
        const uint32_t idx = uint32_t((uint64_t(cellCount) * uint64_t(x)) >> 32);
        const uint32_t cols = d.b(colsOff);
        const uint32_t row = cols ? idx / cols : 0;
        const uint32_t col = cols ? idx % cols : idx;
        u = d.f(sizeU) * float(int32_t(col));
        v = d.f(sizeV) * float(int32_t(row));
    };
    pattern(0x33d, 0x33b, 0x368, 0x36c, p.tex0PatU, p.tex0PatV);
    pattern(0x3b5, 0x3b3, 0x3e0, 0x3e4, p.tex1PatU, p.tex1PatV);

    if (d.i(0x328) == 5) {
        p.stripe = std::make_shared<Stripe>();   // 0x3536a0
    } else {
        p.stripe.reset();
    }
    p.emitter = &e;
    p.childTimer = 1000000.0f;
    p.childLast = bitsToFloat(0xffffffffu);
    p.alphaScale = 1.0f;
    p.scaleScale = 1.0f;
    link(e, p);
}

// ---------------------------------------------------------------------------------------------
// Particle animation
// ---------------------------------------------------------------------------------------------

// 0x351880: three-key colour animation of channel `ch`. The particle's life is split into
// `repeat` periods (+0x4a8); within a period the colour holds key 0 until `in`, blends to key 1
// until `peak`, blends to key 2 until `end`, then holds key 2. With +0x4b0 each particle starts
// at a random point of the cycle.
inline void System::colorAnim(Particle& p, int ch) {
    const EmitterData& d = p.data;
    const uint32_t repeat = d.u(0x4a8 + ch * 4);
    int32_t period = repeat ? int32_t(uint32_t(p.life) / repeat) : 0;
    if (period == 0) {
        period = p.life;
    }
    int32_t age = p.ageInt;
    if (d.i(0x4b0 + ch * 4) != 0) {
        age = age + int32_t(p.seed >> 6);
    }
    const int32_t t = period ? int32_t(uint32_t(age) % uint32_t(period)) : 0;
    const int base = 0x430 + ch * 0x30;
    float* out = ch == 0 ? p.color0 : p.color1;

    const int32_t in = div100(d.i(0x490 + ch * 4) * period);
    if (t < in) {
        out[0] = d.f(base);
        out[1] = d.f(base + 4);
        out[2] = d.f(base + 8);
        return;
    }
    const int32_t peak = div100(d.i(0x498 + ch * 4) * period);
    if (t < peak) {
        const float k = float(t - in) / float(peak - in);
        out[0] = d.f(base) + (d.f(base + 0x10) - d.f(base)) * k;
        out[1] = d.f(base + 4) + (d.f(base + 0x14) - d.f(base + 4)) * k;
        out[2] = d.f(base + 8) + (d.f(base + 0x18) - d.f(base + 8)) * k;
        return;
    }
    const int32_t end = div100(d.i(0x4a0 + ch * 4) * period);
    if (end <= t) {
        out[0] = d.f(base + 0x20);
        out[1] = d.f(base + 0x24);
        out[2] = d.f(base + 0x28);
        return;
    }
    const float k = float(t - peak) / float(end - peak);
    out[0] = d.f(base + 0x10) + (d.f(base + 0x20) - d.f(base + 0x10)) * k;
    out[1] = d.f(base + 0x14) + (d.f(base + 0x24) - d.f(base + 0x14)) * k;
    out[2] = d.f(base + 0x18) + (d.f(base + 0x28) - d.f(base + 0x18)) * k;
}

// 0x27a664: texture pattern (sprite-sheet) animation. The animation block at data + off holds:
// +1 random start, +2 clamp at the last cell, +3 columns, +8 frames per cell (0: spread the
// cells over the whole life), +10 cell count, +0xc the cell sequence, +0x30 / +0x34 cell size.
inline void System::texPattern(float& u, float& v, const Particle& p, int off) {
    const EmitterData& d = p.data;
    const int32_t step = d.sh(off + 8);
    const uint32_t cellCount = uint32_t(int32_t(d.sh(off + 10)));
    int32_t a = p.ageInt;
    int32_t div = step;
    if (step == 0) {
        a = a * int32_t(cellCount);
        div = p.life;
    }
    uint32_t idx = div ? uint32_t(a) / uint32_t(div) : 0;
    if (d.b(off + 2) == 0 || idx < cellCount) {
        if (step != 0 && d.b(off + 2) == 0 && d.b(off + 1) != 0) {
            idx = idx + p.seed;
        }
        const uint32_t r = cellCount ? idx % cellCount : 0;
        const uint32_t cell = d.b(off + 0xc + int(r));
        const uint32_t cols = d.b(off + 3);
        const uint32_t row = cols ? cell / cols : 0;
        const uint32_t col = cols ? cell % cols : cell;
        u = d.f(off + 0x30) * float(int32_t(col));
        v = d.f(off + 0x34) * float(int32_t(row));
    }
}

// Shared tail of both particle calcs: advance the two texture channels (pattern, scroll, scale,
// rotation; switches in data +0x188) and write them to the draw block.
inline void texChannels(System& sys, Particle& p, float fr, DrawBlock& o) {
    const EmitterData& d = p.data;
    const uint32_t f = d.u(0x188);

    if (f & 0x2000) {
        sys.texPattern(p.tex0PatU, p.tex0PatV, p, 0x338);
    }
    if (f & 0x400) {
        p.tex0ScaleU = p.tex0ScaleU + d.f(0x38c) * fr;
        p.tex0ScaleV = p.tex0ScaleV + d.f(0x390) * fr;
        p.tex0Rot = p.tex0Rot + d.f(0x3a4) * fr;
        p.tex0ScrollU = p.tex0ScrollU + d.f(0x374) * fr;
        p.tex0ScrollV = p.tex0ScrollV + d.f(0x378) * fr;
    }
    o.scaleRot[2] = p.tex0Rot;
    o.tex0[0] = p.tex0PatU + p.tex0ScrollU;
    o.tex0[1] = p.tex0PatV - p.tex0ScrollV;
    o.tex0[2] = p.tex0ScaleU;
    o.tex0[3] = p.tex0ScaleV;
    o.pos[3] = p.tex0Flip;

    if (f & 0x4000) {
        sys.texPattern(p.tex1PatU, p.tex1PatV, p, 0x3b0);
    }
    if (f & 0x800) {
        p.tex1ScrollU = p.tex1ScrollU + d.f(0x3ec) * fr;
        p.tex1ScrollV = p.tex1ScrollV + d.f(0x3f0) * fr;
        p.tex1ScaleU = p.tex1ScaleU + d.f(0x404) * fr;
        p.tex1ScaleV = p.tex1ScaleV + d.f(0x408) * fr;
        p.tex1Rot = p.tex1Rot + d.f(0x41c) * fr;
    }
    o.scaleRot[3] = p.tex1Rot;
    o.tex1[0] = p.tex1PatU + p.tex1ScrollU;
    o.tex1[1] = p.tex1PatV - p.tex1ScrollV;
    o.tex1[2] = p.tex1ScaleU;
    o.tex1[3] = p.tex1ScaleV;
    o.dir[3] = p.tex1Flip;
}

// Alpha animation step: fade in until inFrame, hold, fade out after outFrame.
inline void alphaStep(Particle& p, float fr) {
    const AlphaAnim& a = p.alphaAnim;
    if (a.inFrame < p.ageInt) {
        if (a.outFrame < p.ageInt) {
            p.alpha = p.alpha + a.outRate * fr;
        }
    } else {
        p.alpha = p.alpha + a.inRate * fr;
    }
}

// Scale animation step: the same three phases as alpha.
inline void scaleStep(Particle& p, float fr) {
    const ScaleAnim& c = p.scaleAnim;
    if (c.inFrame < p.ageInt) {
        if (c.outFrame < p.ageInt) {
            const float y = c.outY;
            p.scaleX = p.scaleX + c.outX * fr;
            p.scaleY = p.scaleY + y * fr;
        }
    } else {
        const float y = c.inY;
        p.scaleX = p.scaleX + c.inX * fr;
        p.scaleY = p.scaleY + y * fr;
    }
}

inline float absBits(float f) {
    return std::fabs(f);
}

// 0x34bc94 -> 0x27a710: the stored world direction and the particle's RT matrix complete the
// block (m[9] is nudged by 1e-4 when the velocity equals it, avoiding a degenerate basis).
inline void drawBillboard(const Particle& p, DrawBlock& o) {
    o.dir[0] = p.worldDir.x;
    o.dir[1] = p.worldDir.y;
    o.dir[2] = p.worldDir.z;
    o.mtx = *p.pRT;
    if (p.vel.x == (*p.pRT)[1] && p.vel.z == (*p.pRT)[9]) {
        o.mtx[9] = o.mtx[9] + 9.9999997e-05f;
    }
}

// |x| > 1e-4, as a bit compare.
inline bool moved(float x) {
    return floatBits(std::fabs(x)) > 0x38d1b717;
}

// 0x34aa00: calc of a simple particle, and its draw block.
//
// Order of work: follow the emitter (follow type 2: position only), update the movement
// direction, move by the velocity, transform to world space, alpha and colour animation, scale
// (with optional stretching along the movement, +0x504), texture channels, rotation with
// damping (+0x538), gravity (+0x304..+0x30c, in emitter space with +0x17b), air resistance
// (+0x300), and age.
//
// `fr` is the frame rate (1 = one 60 Hz frame); the damping factors are blended towards 1 for
// partial frames.
inline void System::calcSimple(Particle& p, float fr) {
    DrawBlock o{};
    o.particle = &p;
    o.emitter = p.emitter;
    const Emitter& e = *p.emitter;
    const EmitterData& d = p.data;
    const float inv = 1.0f - fr;

    if (e.followType == 2) {
        p.mtxSRT[3] = e.mtxSRT[3];
        p.mtxSRT[7] = e.mtxSRT[7];
        p.mtxSRT[11] = e.mtxSRT[11];
        p.mtxRT[3] = e.mtxRT[3];
        p.mtxRT[7] = e.mtxRT[7];
        p.mtxRT[11] = e.mtxRT[11];
    }
    const Mtx34& m = *p.pSRT;

    if (e.worldDirOn) {
        if (moved(p.vel.x) || moved(p.vel.y) || moved(p.vel.z)) {
            p.posDelta.x = (p.vel.x - p.posDelta.x) + p.posDelta.x;
            p.posDelta.y = (p.vel.y - p.posDelta.y) + p.posDelta.y;
            p.posDelta.z = (p.vel.z - p.posDelta.z) + p.posDelta.z;
        }
        const V3& v = p.posDelta;
        p.worldDir = {m[0] * v.x + m[1] * v.y + m[2] * v.z, m[4] * v.x + m[5] * v.y + m[6] * v.z,
                      m[8] * v.x + m[9] * v.y + m[10] * v.z};
        o.dir[0] = p.worldDir.x;
        o.dir[1] = p.worldDir.y;
        o.dir[2] = p.worldDir.z;
    }

    const float pz = p.pos.z + p.vel.z * fr;
    const float px = p.pos.x + p.vel.x * fr;
    const float py = p.pos.y + p.vel.y * fr;
    p.pos = {px, py, pz};
    // World position; association as compiled (0x34abf4..0x34ac30).
    o.pos[2] = ((m[9] * py + m[10] * pz) + (m[11] + m[8] * px)) + 0.0f;
    o.pos[1] = ((m[5] * py + m[6] * pz) + (m[7] + m[4] * px)) + 0.0f;
    o.pos[0] = ((m[1] * py + m[2] * pz) + (m[3] + m[0] * px)) + 0.0f;

    if (e.alphaAnimOn) {
        alphaStep(p, fr);
    }
    o.color[3] = p.alpha * p.alphaScale;

    const int ci = e.colorIndex;
    if (d.u(0x188) & (ci == 0 ? 0x100u : 0x200u)) {
        colorAnim(p, ci);
    }
    const float* c = ci == 0 ? p.color0 : p.color1;
    o.color[0] = c[0];
    o.color[1] = c[1];
    o.color[2] = c[2];

    if (e.scaleAnimOn) {
        scaleStep(p, fr);
    }
    float sy = p.scaleY * globalScale[1] * p.scaleScale;
    if (0.0f < d.f(0x504)) {
        // Stretch along the movement: speed plus smoothed movement, times +0x504.
        sy = sy + (std::sqrt(p.vel.x * p.vel.x + p.vel.y * p.vel.y + p.vel.z * p.vel.z) +
                   std::sqrt(p.posDelta.x * p.posDelta.x + p.posDelta.y * p.posDelta.y + p.posDelta.z * p.posDelta.z)) *
                      d.f(0x504);
    }
    o.scaleRot[1] = sy;
    o.scaleRot[0] = p.scaleX * globalScale[0] * p.scaleScale;

    texChannels(*this, p, fr, o);

    const float rz = p.rot.z + p.rotVel.z * fr;
    const float rx = p.rot.x + p.rotVel.x * fr;
    const float ry = p.rot.y + p.rotVel.y * fr;
    const float rotDamping = d.f(0x538) + (1.0f - d.f(0x538)) * inv;
    p.rot = {rx, ry, rz};
    o.rot[2] = rz;
    o.rot[1] = ry;
    o.rot[0] = rx;
    p.rotVel = {p.rotVel.x * rotDamping, p.rotVel.y * rotDamping, p.rotVel.z * rotDamping};

    float vy, vz;
    if (d.b(0x17b) == 0) {
        vy = p.vel.y + d.f(0x308) * fr;
        vz = p.vel.z + d.f(0x30c) * fr;
        p.vel.x = p.vel.x + d.f(0x304) * fr;
    } else {
        // Gravity in emitter space.
        const float gy = d.f(0x308) * fr;
        const float gx = d.f(0x304) * fr;
        const float gz = d.f(0x30c) * fr;
        const Mtx34& r = *p.pRT;
        vy = p.vel.y + gx * r[1] + r[5] * gy + gz * r[9];
        vz = p.vel.z + gx * r[2] + r[6] * gy + gz * r[10];
        p.vel.x = p.vel.x + gx * r[0] + r[4] * gy + gz * r[8];
    }

    const float age0 = p.age;
    const float air = d.f(0x300) + (1.0f - d.f(0x300)) * inv;
    p.age = age0 + fr;
    p.vel = {p.vel.x * air, vy * air, vz * air};
    p.ageInt = int32_t(age0 + fr);

    drawBillboard(p, o);
    if (out) {
        out->push_back(o);
    }
}

// ---------------------------------------------------------------------------------------------
// Fields (complex emitters): forces applied to particles after the basic motion.
// ---------------------------------------------------------------------------------------------

// 0x27b590: random field. Every `interval` frames add a vector from the random cube table,
// scaled per axis: particles wander.
inline const uint8_t* fieldRandom(System& sys, Emitter& e, Particle& p, const uint8_t* f) {
    uint32_t interval;
    memcpy(&interval, f, 4);
    if (interval && uint32_t(p.ageInt) % interval == 0) {
        const V3& r = sys.tab->randomCube[e.rndA & 0x1ff];
        e.rndA++;
        float k[3];
        memcpy(k, f + 4, 12);
        const float vy = p.vel.y + r.y * k[1];
        const float vz = p.vel.z + r.z * k[2];
        p.vel.x = p.vel.x + r.x * k[0];
        p.vel.y = vy;
        p.vel.z = vz;
    }
    return f + 16;
}

// 0x27b1dc: spin field. The block holds an angle per frame (binary angle, 2^32 = full turn), an
// axis (0 X, 1 Y, 2 Z of the emitter) and a diffusion speed. The particle position rotates about
// the axis and optionally drifts outward: particles swirl.
inline const uint8_t* fieldSpin(System& sys, Particle& p, const uint8_t* f, float fr) {
    int32_t angle;
    int32_t axis;
    float diffusion;
    memcpy(&angle, f, 4);
    memcpy(&axis, f + 4, 4);
    memcpy(&diffusion, f + 8, 4);
    const uint32_t u = uint32_t(int32_t(float(angle) * fr));
    const float* t = &sys.tab->sinCos[(u >> 24) * 4];
    const float fx = u2f(u & 0xffffff) * 5.9604645e-08f;
    const float sn = t[0] + t[1] * fx;
    const float cs = t[2] + t[3] * fx;

    // a' = a cos + b sin, b' = b cos - a sin, then push outward by `diffusion` per frame.
    auto rotate = [&](float& a, float& b) {
        const float na = a * cs + b * sn;
        const float nb = b * cs - a * sn;
        a = na;
        b = nb;
        if (diffusion == 0.0f) {
            return;
        }
        const float lengthSq = na * na + nb * nb;
        if (!(lengthSq > 0.0f)) {
            return;
        }
        const float k = (1.0f / std::sqrt(lengthSq)) * fr * diffusion;
        a = na + na * k;
        b = nb + nb * k;
    };
    if (axis == 0) {
        rotate(p.pos.y, p.pos.z);
    } else if (axis == 1) {
        rotate(p.pos.z, p.pos.x);
    } else if (axis == 2) {
        rotate(p.pos.x, p.pos.y);
    }
    return f + 12;
}

// 0x27affc: collision field. Block: kind (u16: 0 the particle dies, 1 it bounces), frame (u16:
// 0 the plane is world Y in the particle's own space, 1 world Y while the particle lives in the
// emitter's space), the plane's Y and the bounce factor. Below the plane a dying particle's age
// jumps to its life; a bouncing one is put on the plane and its Y velocity reversed and scaled.
inline const uint8_t* fieldCollision(Emitter& e, Particle& p, const uint8_t* f) {
    int16_t kind, frame;
    float y, bounce;
    memcpy(&kind, f, 2);
    memcpy(&frame, f + 2, 2);
    memcpy(&y, f + 4, 4);
    memcpy(&bounce, f + 8, 4);
    auto die = [&] {
        p.ageInt = p.life;
        p.age = float(p.life);
    };
    if (frame == 0) {
        if (kind == 0) {
            if (!(y <= p.pos.y)) {
                p.pos.y = y;
                die();
            }
        } else if (kind == 1 && p.pos.y < y) {
            p.pos.y = y;
            p.vel.y = -(p.vel.y * bounce);
        }
        return f + 12;
    }
    const Mtx34& m = e.followType == 0 ? e.mtxSRT : p.mtxSRT;
    V3 w = mtxApply(m, p.pos);
    if (kind == 0) {
        if (!(y <= w.y)) {
            die();
        }
    } else if (kind == 1 && w.y < y) {
        w.y = y + 0.0001f;   // literal 0x27b1d8
        const float vx = m[0] * p.vel.x + m[1] * p.vel.y + m[2] * p.vel.z;
        const float vz = m[8] * p.vel.x + m[9] * p.vel.y + m[10] * p.vel.z;
        const float vy = -((m[4] * p.vel.x + m[5] * p.vel.y + m[6] * p.vel.z) * bounce);
        Mtx34 inv = m;
        mtxInverse(inv, m);
        p.pos = mtxApply(inv, w);
        p.vel.x = inv[0] * vx + inv[1] * vy + inv[2] * vz;
        p.vel.y = inv[4] * vx + inv[5] * vy + inv[6] * vz;
        p.vel.z = inv[8] * vx + inv[9] * vy + inv[10] * vz;
    }
    return f + 12;
}

// 0x27aeb8: convergence field. Block: mode (0 a point in the particle's space, else relative to
// the emitter's position), the point and a strength. The particle moves that fraction of the way
// to the point per frame.
inline const uint8_t* fieldConvergence(Emitter& e, Particle& p, const uint8_t* f, float fr) {
    int32_t mode;
    float t[3], k;
    memcpy(&mode, f, 4);
    memcpy(t, f + 4, 12);
    memcpy(&k, f + 16, 4);
    V3 base{0, 0, 0};
    if (mode != 0) {
        const V3 at{e.mtxRT[3], e.mtxRT[7], e.mtxRT[11]};
        const Mtx34& m = e.followType == 0 ? e.mtxSRT : p.mtxSRT;
        Mtx34 inv = m;
        mtxInverse(inv, m);
        base = mtxApply(inv, at);
    }
    p.pos.x = p.pos.x + k * ((base.x + t[0]) - p.pos.x) * fr;
    p.pos.y = p.pos.y + k * ((base.y + t[1]) - p.pos.y) * fr;
    p.pos.z = p.pos.z + k * ((base.z + t[2]) - p.pos.z) * fr;
    return f + 20;
}

// 0x27ae00: position field. Block: a movement per frame and a flag; with the flag set the
// movement is turned by the particle's RT matrix (transposed: its columns).
inline const uint8_t* fieldPosition(Particle& p, const uint8_t* f, float fr) {
    float a[3];
    memcpy(a, f, 12);
    if (f[12] == 0) {
        p.pos.x = p.pos.x + a[0] * fr;
        p.pos.y = p.pos.y + a[1] * fr;
        p.pos.z = p.pos.z + a[2] * fr;
    } else {
        const Mtx34& m = *p.pRT;
        const float x = a[0] * fr, y = a[1] * fr, z = a[2] * fr;
        p.pos.x = p.pos.x + x * m[0] + m[4] * y + z * m[8];
        p.pos.y = p.pos.y + x * m[1] + m[5] * y + z * m[9];
        p.pos.z = p.pos.z + x * m[2] + m[6] * y + z * m[10];
    }
    return f + 16;
}

// 0x34b494: calc of a complex particle, and its draw block.
//
// The same ingredients as the simple calc in a different order (air resistance and gravity
// first, then flicker, the animations, the texture channels and the fields), followed by the
// child particle trigger.
inline void System::calcComplex(Particle& p, float fr) {
    DrawBlock o{};
    o.particle = &p;
    o.emitter = p.emitter;
    Emitter& e = *p.emitter;
    const EmitterData& d = p.data;
    const float inv = 1.0f - fr;

    if (e.followType == 2) {
        p.mtxSRT[11] = e.mtxSRT[11];
        p.mtxSRT[3] = e.mtxSRT[3];
        p.mtxRT[3] = e.mtxRT[3];
        p.mtxSRT[7] = e.mtxSRT[7];
        p.mtxRT[11] = e.mtxRT[11];
        p.mtxRT[7] = e.mtxRT[7];
    }

    const V3 old = p.pos;
    const V3 v0 = p.vel;
    const float air = d.f(0x300) + (1.0f - d.f(0x300)) * inv;
    float vx = v0.x * air;
    float vy = v0.y * air;
    float vz = v0.z * air;
    p.pos = {p.pos.x + v0.x * fr, p.pos.y + v0.y * fr, p.pos.z + v0.z * fr};
    if (d.b(0x17b) == 0) {
        vx = vx + d.f(0x304) * fr;
        vy = vy + d.f(0x308) * fr;
        vz = vz + d.f(0x30c) * fr;
    } else {
        const float gy = d.f(0x308) * fr;
        const float gx = d.f(0x304) * fr;
        const float gz = d.f(0x30c) * fr;
        const Mtx34& r = *p.pRT;
        vx = vx + gx * r[0] + r[4] * gy + gz * r[8];
        vy = vy + gx * r[1] + r[5] * gy + gz * r[9];
        vz = vz + gx * r[2] + r[6] * gy + gz * r[10];
    }
    p.vel = {vx, vy, vz};

    if (d.b(0x54e) & 1) {
        // 0x346488: flicker. The block at +0x558 holds amplitude, rate and a seed multiplier
        // (int); a per-particle position in the 128-entry sine table makes neighbours twinkle
        // out of step. Bits 2 / 4 of +0x54e apply it to alpha / scale.
        const int blockOffset = d.h(0x558);
        const float amp = d.f(blockOffset);
        const float rate = d.f(blockOffset + 4);
        const int32_t mul = d.i(blockOffset + 8);
        const int32_t idx = (int32_t(p.seed) * mul + int32_t(float(p.ageInt) * rate)) & 0x7f;
        const float k = 1.0f - tab->flicker[size_t(idx)] * amp;
        if (d.h(0x54e) & 2) {
            p.alphaScale = k;
        }
        if (d.h(0x54e) & 4) {
            p.scaleScale = k;
        }
    }

    if (e.alphaAnimOn) {
        alphaStep(p, fr);
    }
    o.color[3] = p.alpha * p.alphaScale;

    if (e.scaleAnimOn) {
        scaleStep(p, fr);
    }
    float sy = p.scaleY * globalScale[1] * p.scaleScale;
    if (0.0f < d.f(0x504)) {
        sy = sy + (std::sqrt(p.vel.x * p.vel.x + p.vel.y * p.vel.y + p.vel.z * p.vel.z) +
                   std::sqrt(p.posDelta.x * p.posDelta.x + p.posDelta.y * p.posDelta.y + p.posDelta.z * p.posDelta.z)) *
                      d.f(0x504);
    }
    o.scaleRot[1] = sy;
    o.scaleRot[0] = p.scaleX * globalScale[0] * p.scaleScale;

    {
        const float rz = p.rot.z + p.rotVel.z * fr;
        const float ry = p.rot.y + p.rotVel.y * fr;
        const float rx = p.rot.x + p.rotVel.x * fr;
        const float rotDamping = d.f(0x538) + (1.0f - d.f(0x538)) * inv;
        p.rot = {rx, ry, rz};
        o.rot[0] = rx;
        o.rot[1] = ry;
        o.rot[2] = rz;
        p.rotVel = {p.rotVel.x * rotDamping, p.rotVel.y * rotDamping, p.rotVel.z * rotDamping};
    }

    const int ci = e.colorIndex;
    if (d.u(0x188) & (ci == 0 ? 0x100u : 0x200u)) {
        colorAnim(p, ci);
    }
    const float* c = ci == 0 ? p.color0 : p.color1;
    o.color[0] = c[0];
    o.color[1] = c[1];
    o.color[2] = c[2];

    texChannels(*this, p, fr, o);

    // Fields (+0x54c bits, blocks from +0x556 in bit order).
    const uint16_t fields = d.h(0x54c);
    if (fields) {
        const uint8_t* f = d.p + d.h(0x556);
        if (fields & 1) {
            f = fieldRandom(*this, e, p, f);
        }
        if (fields & 2) {
            throw std::runtime_error("Eft: magnet field (0x27b3d0) not ported");
        }
        if (fields & 4) {
            f = fieldSpin(*this, p, f, fr);
        }
        if (fields & 8) {
            f = fieldCollision(e, p, f);
        }
        if (fields & 0x10) {
            f = fieldConvergence(e, p, f, fr);
        }
        if (fields & 0x20) {
            f = fieldPosition(p, f, fr);
        }
        if (fields & ~0x3du) {
            throw std::runtime_error("Eft: unknown particle field bits");
        }
    }

    // Smoothed movement from the actual position change (fields included).
    const float dx = p.pos.x - old.x;
    const float dy = p.pos.y - old.y;
    const float dz = p.pos.z - old.z;
    if (moved(dx) || moved(dy) || moved(dz)) {
        p.posDelta.x = (dx - p.posDelta.x) + p.posDelta.x;
        p.posDelta.y = (dy - p.posDelta.y) + p.posDelta.y;
        p.posDelta.z = (dz - p.posDelta.z) + p.posDelta.z;
    }

    const Mtx34& m = *p.pSRT;
    const float px = p.pos.x;
    const float py = p.pos.y;
    const float pz = p.pos.z;
    // World position, as compiled (0x34bb88..).
    o.pos[2] = ((m[9] * py + m[10] * pz) + (m[11] + m[8] * px)) + 0.0f;
    o.pos[1] = ((m[5] * py + m[6] * pz) + (m[7] + m[4] * px)) + 0.0f;
    o.pos[0] = ((m[1] * py + m[2] * pz) + (m[3] + m[0] * px)) + 0.0f;
    p.worldPos = {o.pos[0], o.pos[1], o.pos[2]};   // +0x30 (read by the stripe history, 0x35038c)
    if (e.worldDirOn) {
        const V3& v = p.posDelta;
        p.worldDir = {m[0] * v.x + m[1] * v.y + m[2] * v.z, m[4] * v.x + m[5] * v.y + m[6] * v.z,
                      m[8] * v.x + m[9] * v.y + m[10] * v.z};
        o.dir[0] = p.worldDir.x;
        o.dir[1] = p.worldDir.y;
        o.dir[2] = p.worldDir.z;
    }

    const float a = p.age + fr;
    p.age = a;
    p.ageInt = int32_t(a);

    drawBillboard(p, o);
    if (out && !p.stripe) {   // stripe emitters draw their ribbons instead (0x34c5f4)
        out->push_back(o);
    }
    childTrigger(e, p, fr);
    stripeRecord(e, p, fr);
}

// ---------------------------------------------------------------------------------------------
// Child particles
// ---------------------------------------------------------------------------------------------
//
// Complex emitters with data +0x548 bit 0 let each particle emit smaller "child" particles as it
// flies, for example a spark leaving a trail of glitter. Children have their own, simpler
// parameter block and calc, and are drawn in a separate pass with their own material.
//
// Child data block: +0x560 count per emission, +0x564 start (percent of the parent's life),
// +0x568 life, +0x56c interval, +0x570 calc modes (copied to emitter +0x23c by 0x34ed28:
// 1 air resistance, 2 gravity, 4 rotation, 8 rotation damping, 0x10 world direction, 0x40 scale
// animation, 0x2000 texture pattern), velocities +0x574..+0x588, colours +0x630 / +0x63c, alpha
// +0x64c..+0x654, scale +0x658..+0x664, rotation +0x668..+0x6a2, animation timing
// +0x6b8..+0x6c8, texture cells +0x6cc..+0x6d4, gravity +0x6ac..+0x6b4, damping +0x69c / +0x708.
//
// Flags +0x548: 2 inherit colour 0, 4 inherit alpha, 8 inherit scale, 0x10 inherit rotation,
// 0x20 inherit velocity, 0x40 emitter matrices, 0x800 fields, 0x8000 inherit colour 1.

// 0x35038c (stripe part): once per frame step the particle's history gets a new entry at the head:
// the position (world position, or with data +0x550 bit 0 the emitter-space position and an
// identity matrix), smoothed over the two previous entries by the stripe's interpolation factor
// (+0x20, below 1; 0.7 for the middle entry), the width (particle scale x times the set's effective
// scale, set +0x21c), the emitter matrix, the smoothed travel
// direction (+0x24) and the side direction (option 2: the matrix's Y axis; otherwise Y x dir).
// Stripe data at data + (u16)data +0x55a: +0 option, +0x10 history length, +0x20 interpolation,
// +0x24 direction smoothing.
inline void System::stripeRecord(Emitter& e, Particle& p, float fr) {
    if (!p.stripe || int(fr) == 0) {
        return;
    }
    const EmitterData& d = p.data;
    const int so = d.h(0x55a);
    const int histLen = std::max(1, std::min(256, d.i(so + 0x10)));
    Stripe& s = *p.stripe;
    Stripe::Entry& cur = s.entries[size_t(s.head)];
    if ((d.u(0x550) & 1) == 0) {
        const float interp = d.f(so + 0x20);
        const bool simple = s.count <= 2 || histLen <= 3 || !(interp < 1.0f) || s.head == s.tail;
        if (simple) {
            cur.pos = p.worldPos;
            s.smooth2 = p.worldPos;
            s.smooth1 = p.worldPos;
        } else {
            int i1 = s.head - 1;
            if (i1 < 0) {
                i1 = histLen - 1;
            }
            int i0 = i1 - 1;
            if (i0 < 0) {
                i0 = histLen - 1;
            }
            s.smooth1.x = s.smooth1.x + (p.worldPos.x - s.smooth1.x) * interp;
            s.smooth1.y = s.smooth1.y + (p.worldPos.y - s.smooth1.y) * interp;
            s.smooth1.z = s.smooth1.z + (p.worldPos.z - s.smooth1.z) * interp;
            s.smooth2.x = s.smooth2.x + (s.smooth1.x - s.smooth2.x) * interp;
            s.smooth2.y = s.smooth2.y + (s.smooth1.y - s.smooth2.y) * interp;
            s.smooth2.z = s.smooth2.z + (s.smooth1.z - s.smooth2.z) * interp;
            s.entries[size_t(i0)].pos = s.smooth2;
            const float k = 0.7f;   // 0x350724
            s.entries[size_t(i1)].pos = {s.smooth2.x + (p.worldPos.x - s.smooth2.x) * k,
                                         s.smooth2.y + (p.worldPos.y - s.smooth2.y) * k,
                                         s.smooth2.z + (p.worldPos.z - s.smooth2.z) * k};
            cur.pos = p.worldPos;
        }
        for (int k = 0; k < 12; k++) {
            cur.mtx[k] = e.mtxSRT[size_t(k)];
        }
    } else {
        cur.pos = p.pos;
        const float id[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
        for (int k = 0; k < 12; k++) {
            cur.mtx[k] = id[k];
        }
    }
    cur.width = p.scaleX * e.set->effectiveScale[0];   // emitter +0x1c -> set +0x21c
    if (s.head != s.tail) {
        int prev = s.head - 1;
        if (prev < 0) {
            prev = histLen - 1;
        }
        const V3 a = cur.pos, b = s.entries[size_t(prev)].pos;
        V3 delta{a.x - b.x, a.y - b.y, a.z - b.z};
        if (s.count < 2) {
            s.dir = delta;
        } else {
            normalize(delta);
            const float k = d.f(so + 0x24);
            s.dir = {s.dir.x + (delta.x - s.dir.x) * k, s.dir.y + (delta.y - s.dir.y) * k,
                     s.dir.z + (delta.z - s.dir.z) * k};
        }
        normalize(s.dir);
        cur.dir = s.dir;
        const V3 y{cur.mtx[1], cur.mtx[5], cur.mtx[9]};
        if (d.i(so) == 2) {
            cur.side = y;
        } else {
            V3 c{y.y * s.dir.z - y.z * s.dir.y, y.z * s.dir.x - y.x * s.dir.z, y.x * s.dir.y - y.y * s.dir.x};
            normalize(c);
            cur.side = c;
        }
    }
    s.head++;
    if (s.head >= histLen) {
        s.head = 0;
    }
    if (s.head == s.tail) {
        s.tail++;
        if (s.tail >= histLen) {
            s.tail = 0;
        }
    }
    s.count = std::min(s.count + 1, histLen);
    s.frames++;
}

// 0x3509c0: a stripe particle past its life drops one history entry per frame from the tail while
// its texture channels keep scrolling; it is freed once the history is empty.
inline void System::stripeDrain(Emitter& e, Particle& p) {
    const EmitterData& d = p.data;
    Stripe& s = *p.stripe;
    const int histLen = std::max(1, std::min(256, d.i(d.h(0x55a) + 0x10)));
    s.tail++;
    if (s.tail >= histLen) {
        s.tail = 0;
    }
    s.count--;
    p.tex0ScrollU = p.tex0ScrollU + d.f(0x374);
    p.tex0ScrollV = p.tex0ScrollV + d.f(0x378);
    p.tex0ScaleU = p.tex0ScaleU + d.f(0x38c);
    p.tex0ScaleV = p.tex0ScaleV + d.f(0x390);
    p.tex0Rot = p.tex0Rot + d.f(0x3a4);
    p.tex1ScrollU = p.tex1ScrollU + d.f(0x3ec);
    p.tex1ScrollV = p.tex1ScrollV + d.f(0x3f0);
    p.tex1ScaleU = p.tex1ScaleU + d.f(0x404);
    p.tex1ScaleV = p.tex1ScaleV + d.f(0x408);
    p.tex1Rot = p.tex1Rot + d.f(0x41c);
    s.frames++;
    (void)e;
}

// 0x35038c (child part): after a parent's calc, once its age passes the start fraction of its
// life, emit every +0x56c frames (the timer starts at 1e6, so the first emission is immediate).
inline void System::childTrigger(Emitter& e, Particle& p, float fr) {
    const EmitterData& d = e.data;
    if ((d.b(0x548) & 1) == 0) {
        return;
    }
    const int32_t before = int32_t(p.age - fr);
    const int32_t start = int32_t((int64_t(p.life - 1) * int64_t(d.i(0x564))) / 100);
    if (start > before) {
        return;
    }
    const float fb = float(before);
    if (p.childTimer < float(d.i(0x56c)) || p.childLast == fb) {
        p.childTimer = p.childTimer + fr;
    } else {
        p.childLast = fb;
        p.childTimer = 0.0f;
        emitChildren(e, p);
    }
}

// 0x350e38: emit the child particles of one parent. The LCG draws follow the original order;
// each child also draws the global xorshift once for its seed slot before the LCG value
// replaces it.
inline void System::emitChildren(Emitter& e, const Particle& parent) {
    const EmitterData& d = e.data;
    const uint32_t flags = d.u(0x548);
    auto unit = [&]() {
        const float f = u2f(e.lcg);
        e.lcg = lcgNext(e.lcg);
        return f;
    };

    for (int k = 0; k < d.i(0x560); k++) {
        Particle& c = *allocParticle();
        c.life = d.i(0x568);
        c.data = d;
        c.emitter = &e;
        c.type = 2;

        // Velocity: zero, or a fraction (+0x574) of the parent's movement.
        if ((flags & 0x20) == 0) {
            c.vel = {0, 0, 0};
        } else {
            const float r = d.f(0x574);
            c.vel = {parent.posDelta.x * r, parent.posDelta.y * r, parent.posDelta.z * r};
        }

        const float alpha = (flags & 4) ? parent.alpha * parent.alphaScale : d.f(0x64c);

        // Scale: own, or the parent's times +0x658; randomly reduced by up to +0x664.
        if ((flags & 8) == 0) {
            const float sc = 1.0f - d.f(0x664) * unit() * 2.3283064e-10f;
            c.scaleX = d.f(0x65c) * sc;
            c.scaleY = d.f(0x660) * sc;
        } else {
            const float sc = 1.0f - d.f(0x664) * unit() * 2.3283064e-10f;
            c.scaleX = parent.scaleX * d.f(0x658) * parent.scaleScale * sc * e.particleScaleX;
            c.scaleY = parent.scaleY * d.f(0x658) * parent.scaleScale * sc * e.particleScaleY;
        }

        // Rotation: own random, or inherited (all axes or one, +0x668).
        if ((flags & 0x10) == 0) {
            const float rx = d.f(0x66c) + d.f(0x678) * unit() * 2.3283064e-10f;
            const float ry = d.f(0x670) + d.f(0x67c) * unit() * 2.3283064e-10f;
            const float rz = d.f(0x674) + d.f(0x680) * unit() * 2.3283064e-10f;
            c.rot = {rx, ry, rz};
        } else {
            switch (d.i(0x668)) {
            case 1:
                c.rot = {parent.rot.x, 0.0f, 0.0f};
                break;
            case 2:
                c.rot = {0.0f, parent.rot.y, 0.0f};
                break;
            case 3:
                c.rot = {0.0f, 0.0f, parent.rot.z};
                break;
            default:
                c.rot = parent.rot;
            }
        }

        // Colours: own, or inherited per channel.
        if ((flags & 2) == 0) {
            c.color0[0] = d.f(0x630);
            c.color0[1] = d.f(0x634);
            c.color0[2] = d.f(0x638);
            c.color0[3] = 1.0f;
        } else {
            for (int i = 0; i < 4; i++) {
                c.color0[i] = parent.color0[i];
            }
        }
        if ((flags & 0x8000) == 0) {
            c.color1[0] = d.f(0x63c);
            c.color1[1] = d.f(0x640);
            c.color1[2] = d.f(0x644);
            c.color1[3] = 1.0f;
        } else {
            for (int i = 0; i < 4; i++) {
                c.color1[i] = parent.color1[i];
            }
        }

        // Rotation speed, and random sign flips.
        {
            const float vx = d.f(0x684) + d.f(0x690) * unit() * 2.3283064e-10f;
            const float vy = d.f(0x688) + d.f(0x694) * unit() * 2.3283064e-10f;
            const float vz = d.f(0x68c) + d.f(0x698) * unit() * 2.3283064e-10f;
            c.rotVel = {vx, vy, vz};
        }
        auto flip = [&](int o) {
            return d.b(o) != 0 && floatBits(unit() * 2.3283064e-10f) > 0x3f000000;
        };
        if ((flags & 0x10) == 0) {
            if (flip(0x6a0)) {
                c.rot.x = -c.rot.x;
                c.rotVel.x = -c.rotVel.x;
            }
            if (flip(0x6a1)) {
                c.rot.y = -c.rot.y;
                c.rotVel.y = -c.rotVel.y;
            }
            if (flip(0x6a2)) {
                c.rot.z = -c.rot.z;
                c.rotVel.z = -c.rotVel.z;
            }
        } else {
            if (flip(0x6a0)) {
                c.rotVel.x = -c.rotVel.x;
            }
            if (flip(0x6a1)) {
                c.rotVel.y = -c.rotVel.y;
            }
            if (flip(0x6a2)) {
                c.rotVel.z = -c.rotVel.z;
            }
        }

        c.tex0ScrollU = 0.0f;
        c.tex0ScrollV = 0.0f;
        c.tex0Rot = 0.0f;
        c.tex0ScaleU = 1.0f;
        c.tex0ScaleV = 1.0f;

        // Alpha: fade in from +0x654 over +0x6bc frames, fade out to +0x650 after +0x6b8.
        if (d.i(0x6bc) == 0) {
            c.alphaAnim.inRate = 0.0f;
            c.alpha = alpha;
        } else {
            c.alphaAnim.inRate = (alpha - d.f(0x654)) / float(d.i(0x6bc));
            c.alpha = d.f(0x654);
        }
        {
            const float n = float(c.life - d.i(0x6b8));
            const float k = n == 0.0f ? 0.0f : 1.0f / n;
            c.alphaAnim.outRate = (d.f(0x650) - alpha) * k;
            // Scale: towards factor +0x6c4 / +0x6c8 after frame +0x6c0.
            const float n2 = float(c.life - d.i(0x6c0));
            const float k2 = n2 == 0.0f ? 0.0f : 1.0f / n2;
            c.scaleAnim.inX = c.scaleX * (d.f(0x6c4) - 1.0f) * k2;
            c.scaleAnim.inY = c.scaleY * (d.f(0x6c8) - 1.0f) * k2;
        }
        c.alphaScale = 1.0f;
        c.scaleScale = 1.0f;

        // Random texture cell.
        if (d.h(0x6cc) < 2) {
            c.tex0PatU = 0.0f;
            c.tex0PatV = 0.0f;
        } else {
            const uint32_t x = e.lcg;
            e.lcg = lcgNext(x);
            const uint32_t idx = uint32_t((uint64_t(d.h(0x6cc)) * uint64_t(x)) >> 32);
            const uint32_t div = d.b(0x6ce) ? d.b(0x6ce) : 1;
            c.tex0PatU = d.f(0x6d0) * float(int32_t(idx % div));
            c.tex0PatV = d.f(0x6d4) * float(int32_t(idx / div));
        }

        // Random velocity (all-direction +0x578, per-axis +0x57c..+0x584) and start position:
        // the parent's position plus a random offset (+0x588), one velocity step back.
        {
            const V3& u = tab->randomUnit[e.rndB & 0x1ff];
            e.rndB++;
            const V3& q = tab->randomCube[e.rndA & 0x1ff];
            e.rndA++;
            const float a = d.f(0x578);
            const float off = d.f(0x588);
            c.vel.x = u.x * a + q.x * d.f(0x57c) + c.vel.x;
            c.vel.y = u.y * a + q.y * d.f(0x580) + c.vel.y;
            c.vel.z = u.z * a + q.z * d.f(0x584) + c.vel.z;
            c.posDelta = c.vel;
            c.pos = {(parent.pos.x + u.x * off) - c.vel.x, (parent.pos.y + u.y * off) - c.vel.y,
                     (parent.pos.z + u.z * off) - c.vel.z};
        }
        c.ageInt = 0;
        c.age = 0.0f;
        c.seed = xorshift();

        // Random texture mirroring.
        {
            const float f0 = u2f(e.lcg);
            const uint32_t l1 = lcgNext(e.lcg);
            e.lcg = lcgNext(l1);
            const float f1 = u2f(l1);
            c.tex0Flip = 0.0f;
            if (d.b(0x6fd) == 1 && floatBits(f0 * 2.3283064e-10f) > 0x3f000000) {
                c.tex0Flip = 1.0f;
            }
            if (d.b(0x6fe) == 1 && floatBits(f1 * 2.3283064e-10f) > 0x3f000000) {
                c.tex0Flip = c.tex0Flip + 2.0f;
            }
        }

        // Matrices: the emitter's (following it), or a copy of the parent's.
        if ((flags & 0x40) == 0) {
            if (e.followType == 0) {
                c.mtxRT = e.mtxRT;
                c.mtxSRT = e.mtxSRT;
                c.pRT = &e.mtxRT;
                c.pSRT = &e.mtxSRT;
            } else {
                c.mtxRT = parent.mtxRT;
                c.mtxSRT = parent.mtxSRT;
                c.pRT = &parent.mtxRT;
                c.pSRT = &parent.mtxSRT;
            }
        } else {
            c.pRT = &e.mtxRT;
            c.pSRT = &e.mtxSRT;
        }
        c.seed = e.lcg;
        e.lcg = lcgNext(e.lcg);

        // Prepend to the child list.
        Particle* oldHead = e.childHead;
        if (!oldHead) {
            e.childHead = &c;
            c.next = nullptr;
        } else {
            oldHead->prev = &c;
            c.next = oldHead;
            e.childHead = &c;
        }
        c.prev = nullptr;
        if (!e.childTail) {
            e.childTail = &c;
        }
        e.childCount++;
    }
}

// 0x27a944: a child particle leaves the child list.
inline void System::freeChild(Particle& p) {
    Emitter& e = *p.emitter;
    e.childCount--;
    if (p.prev && p.next) {
        p.next->prev = p.prev;
        p.prev->next = p.next;
    } else if (e.childHead == &p) {
        e.childHead = p.next;
        if (p.next) {
            p.next->prev = p.prev;
        } else {
            e.childTail = nullptr;
        }
    } else if (e.childTail == &p) {
        e.childTail = p.prev;
        if (p.prev) {
            p.prev->next = p.next;
        } else {
            e.childHead = nullptr;
        }
    }
    p.used = false;
}

// 0x34a380: calc of a child particle, and its 7-vector uniform block (no RT matrix rows).
inline void System::calcChild(Particle& p, float fr) {
    DrawBlock o{};
    o.particle = &p;
    o.emitter = p.emitter;
    o.child = true;
    Emitter& e = *p.emitter;
    const EmitterData& d = p.data;
    const uint32_t modes = d.u(0x570);

    p.pos = {p.pos.x + p.vel.x * fr, p.pos.y + p.vel.y * fr, p.pos.z + p.vel.z * fr};
    if (modes & 1) {
        // Air resistance.
        const float k = d.f(0x708) + (1.0f - d.f(0x708)) * (1.0f - fr);
        p.vel = {p.vel.x * k, p.vel.y * k, p.vel.z * k};
    }
    if (modes & 2) {
        // Gravity (in emitter space with +0x17b).
        if (d.b(0x17b) == 0) {
            p.vel.x = p.vel.x + d.f(0x6ac) * fr;
            p.vel.y = p.vel.y + d.f(0x6b0) * fr;
            p.vel.z = p.vel.z + d.f(0x6b4) * fr;
        } else {
            const Mtx34& r = *p.pRT;
            const float gy = d.f(0x6b0) * fr;
            const float gx = d.f(0x6ac) * fr;
            const float gz = d.f(0x6b4) * fr;
            p.vel.z = p.vel.z + gx * r[2] + r[6] * gy + gz * r[10];
            p.vel.x = p.vel.x + gx * r[0] + r[4] * gy + gz * r[8];
            p.vel.y = p.vel.y + gx * r[1] + r[5] * gy + gz * r[9];
        }
    }

    if (p.ageInt < d.i(0x6bc)) {
        p.alpha = p.alpha + p.alphaAnim.inRate * fr;
    } else if (d.i(0x6b8) <= p.ageInt) {
        p.alpha = p.alpha + p.alphaAnim.outRate * fr;
    }
    o.color[3] = p.alpha * p.alphaScale;
    o.scaleRot[3] = p.tex1Rot;
    o.scaleRot[2] = p.tex0Rot;

    if ((modes & 0x40) && d.i(0x6c0) <= p.ageInt) {
        const float y = p.scaleAnim.inY;
        p.scaleX = p.scaleX + p.scaleAnim.inX * fr;
        p.scaleY = p.scaleY + y * fr;
    }
    o.scaleRot[1] = p.scaleY * globalScale[1];
    o.scaleRot[0] = p.scaleX * globalScale[0];

    if (modes & 4) {
        p.rot = {p.rot.x + p.rotVel.x * fr, p.rot.y + p.rotVel.y * fr, p.rot.z + p.rotVel.z * fr};
    }
    if (modes & 8) {
        const float k = d.f(0x69c) + (1.0f - d.f(0x69c)) * (1.0f - fr);
        p.rotVel = {p.rotVel.x * k, p.rotVel.y * k, p.rotVel.z * k};
    }
    o.rot[0] = p.rot.x;
    o.rot[1] = p.rot.y;
    o.rot[2] = p.rot.z;

    const float* c = e.childColorIndex == 0 ? p.color0 : p.color1;
    o.color[0] = c[0];
    o.color[1] = c[1];
    o.color[2] = c[2];

    if (modes & 0x2000) {
        throw std::runtime_error("Eft: child texture pattern animation not ported");
    }
    o.tex0[3] = p.tex0ScaleV;
    o.tex0[2] = p.tex0ScaleU;
    o.tex0[1] = p.tex0PatV - p.tex0ScrollV;
    o.tex0[0] = p.tex0PatU + p.tex0ScrollU;
    o.pos[3] = p.tex0Flip;
    if (d.u(0x548) & 0x800) {
        throw std::runtime_error("Eft: child fields not ported");
    }

    const Mtx34& m = p.mtxSRT;
    const float px = p.pos.x;
    const float py = p.pos.y;
    const float pz = p.pos.z;
    o.pos[2] = (((m[9] * py + m[10] * pz) + m[11]) + m[8] * px) + 0.0f;
    o.pos[1] = (((m[5] * py + m[6] * pz) + m[7]) + m[4] * px) + 0.0f;
    o.pos[0] = (((m[1] * py + m[2] * pz) + m[3]) + m[0] * px) + 0.0f;
    o.dir[3] = 0.0f;

    if (modes & 0x10) {
        if (moved(p.vel.x) || moved(p.vel.y) || moved(p.vel.z)) {
            p.posDelta.x = (p.vel.x - p.posDelta.x) + p.posDelta.x;
            p.posDelta.y = (p.vel.y - p.posDelta.y) + p.posDelta.y;
            p.posDelta.z = (p.vel.z - p.posDelta.z) + p.posDelta.z;
        }
        const Mtx34& s = *p.pSRT;
        const V3& v = p.posDelta;
        p.worldDir = {s[0] * v.x + s[1] * v.y + s[2] * v.z, s[4] * v.x + s[5] * v.y + s[6] * v.z,
                      s[8] * v.x + s[9] * v.y + s[10] * v.z};
        o.dir[0] = p.worldDir.x;
        o.dir[1] = p.worldDir.y;
        o.dir[2] = p.worldDir.z;
    }

    const float a = p.age + fr;
    p.age = a;
    p.ageInt = int32_t(a);
    if (out) {
        out->push_back(o);
    }
}

// ---------------------------------------------------------------------------------------------
// Draw setup
// ---------------------------------------------------------------------------------------------

// Float colour -> byte: clamp above 1 (integer compare of the bits), times 255, truncate.
inline uint32_t colorByte(float f) {
    if (0x3f800000 < floatBits(f)) {
        f = 1.0f;
    }
    const float x = f * 255.0f;
    return x <= 0.0f ? 0u : uint32_t(x) & 0xff;
}

// 0x278e3c with param_4 == 1: uniforms of the child pass. c82 comes from the colour channel
// +0x26c; the TEV constant from the child colour 1 block (data +0x63c), or from the head child
// when inherited (+0x548 bit 0x8000).
inline void drawSetupChild(Emitter& e) {
    const EmitterSet& s = *e.set;
    const EmitterData& d = e.data;
    e.childDrawThisFrame = !s.stopDraw && e.ctrl && e.ctrl->visible != 0 && e.childHead;
    if (!e.childDrawThisFrame) {
        return;
    }
    const V3& ec = e.childColorIndex == 0 ? e.color0 : e.color1;
    e.childShaderColor[0] = (ec.x * 1.0f) * s.color[0];
    e.childShaderColor[1] = (ec.y * 1.0f) * s.color[1];
    e.childShaderColor[2] = (ec.z * 1.0f) * s.color[2];
    e.childShaderColor[3] = ((s.color[3] * e.fade) * e.alpha) * 1.0f;

    const V3& kc = e.childKonstIndex == 0 ? e.color0 : e.color1;
    float r = s.color[0] * kc.x;
    float g = s.color[1] * kc.y;
    float b = s.color[2] * kc.z;
    float a = e.fade * e.alpha;
    if ((d.u(0x548) & 0x8000) == 0) {
        r = d.f(0x63c) * r;
        g = d.f(0x640) * g;
        b = d.f(0x644) * b;
    } else {
        const float* hc = e.childKonstIndex == 0 ? e.childHead->color0 : e.childHead->color1;
        r = hc[0] * r;
        g = hc[1] * g;
        b = hc[2] * b;
        a = e.childHead->alpha * a;
    }
    e.childKonst = colorByte(r) | colorByte(g) << 8 | colorByte(b) << 16 | colorByte(a) << 24;
}

// 0x278e3c, non-child path (param_4 == 0): whether the emitter draws this frame, the vertex-shader
// colour uniform c82 and the TEV constant colour (0x27a574). It reads the head particle before
// this frame's particle calc (0x34f720 calls it ahead of the fused calc+draw loop). The global
// colour multipliers (system +0x4ec.., data +0xc bit 0x40000) are 1 in the opening.
inline void drawSetup(Emitter& e) {
    const EmitterSet& s = *e.set;
    const EmitterData& d = e.data;
    e.drawThisFrame = !s.stopDraw && e.ctrl && e.ctrl->visible != 0 && d.b(0x180) != 0 && e.head && d.b(0x15f) == 0;
    if (!e.drawThisFrame) {
        return;
    }
    const Particle& h = *e.head;
    const V3& ec = e.colorIndex == 0 ? e.color0 : e.color1;
    e.shaderColor[0] = (ec.x * 1.0f) * s.color[0];
    e.shaderColor[1] = (ec.y * 1.0f) * s.color[1];
    e.shaderColor[2] = (ec.z * 1.0f) * s.color[2];
    e.shaderColor[3] = ((s.color[3] * e.fade) * e.alpha) * 1.0f;

    const V3& kc = e.konstIndex == 0 ? e.color0 : e.color1;
    const float* hc = e.konstIndex == 0 ? h.color0 : h.color1;
    const uint32_t r = colorByte((hc[0] * s.color[0]) * kc.x);
    const uint32_t g = colorByte((hc[1] * s.color[1]) * kc.y);
    const uint32_t b = colorByte((hc[2] * s.color[2]) * kc.z);
    const uint32_t a = colorByte(h.alpha * (e.fade * e.alpha));
    e.konst = r | g << 8 | b << 16 | a << 24;
}

// ---------------------------------------------------------------------------------------------
// Frame entry point
// ---------------------------------------------------------------------------------------------

// 0x353814 for every set of a draw group (0x352cd4), calc pass (param_4 == 0). Calculates every
// emitter of every set in the group, then its particles (dead ones are freed), then the child
// particles; a set whose emitters have all ended is removed.
inline void System::calcGroup(int group, float frameRate) {
    auto it = groups.find(group);
    if (it == groups.end()) {
        return;
    }
    std::vector<EmitterSet*> list = it->second;
    for (EmitterSet* s : list) {
        if (!s->alive) {
            continue;
        }
        for (int i = int(s->emitters.size()) - 1; i >= 0; i--) {
            Emitter* e = s->emitters[size_t(i)];
            // A finished emitter's pool slot can be taken by a later set; the original checks the
            // emitter's set creation id (emitter +0x24 against set +0xc), here its owner.
            if (!e || !e->alive || e->set != s) {
                continue;
            }
            if (!s->stopCalc && frameRate > 0.0f) {
                calcEmitter(*e, frameRate);
                if (!e->alive) {
                    continue;
                }
            }
            e->drawn = 0;
            drawSetup(*e);

            const bool complex = e->data.kind() == 1;
            for (Particle* p = e->head; p;) {
                Particle* next = p->next;
                if (p->ageInt < p->life) {
                    if (complex) {
                        calcComplex(*p, frameRate);
                    } else {
                        calcSimple(*p, frameRate);
                    }
                    e->drawn++;
                } else if (p->stripe && p->stripe->tail != p->stripe->head) {
                    stripeDrain(*e, *p);   // 0x3509c0: the history drains after the particle's life
                } else {
                    freeParticle(*p);
                }
                p = next;
            }
            if (out && complex && e->data.i(0x328) == 5 && e->head) {
                DrawBlock o{};
                o.emitter = e;
                o.stripe = true;
                out->push_back(o);
            }

            // 0x353814 -> vtable +0x14 (0x350c8c): the child pass of complex emitters.
            if (complex && (e->data.u(0x548) & 1)) {
                drawSetupChild(*e);
                for (Particle* c = e->childHead; c;) {
                    Particle* next = c->next;
                    if (c->ageInt < c->life) {
                        if (e->data.u(0x548) & 0x40) {
                            c->mtxSRT = e->mtxSRT;
                            c->mtxRT = e->mtxRT;
                        }
                        calcChild(*c, frameRate);
                    } else {
                        freeChild(*c);
                    }
                    c = next;
                }
            }
        }

        int alive = 0;
        for (Emitter* e : s->emitters) {
            if (e && e->alive && e->set == s) {
                alive++;
            }
        }
        if (alive == 0) {
            kill(s);
        }
    }
}

}   // namespace captain_toad::RecoveredEft
