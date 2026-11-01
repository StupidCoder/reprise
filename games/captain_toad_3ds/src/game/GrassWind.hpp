// Recovered GrassControl wind: the gentle swaying of the tall grass tuft in the opening
// (original game, ARM11 main code; Ghidra exports in evidence/analysis/grass-control-{a..e}).
//
// How the original animates grass: instead of simulating wind every frame, the GrassControl object
// precomputes a short loop of 90 frames at stage start. Each frame stores, for every joint of the
// grass blade's bone chain, a sideways (x, z) offset from a 4-dimensional fractal noise ("value
// noise" summed over octaves) sampled along time. During play it steps through the loop once per
// game frame; when the loop wraps, it crossfades from the last to the first frame so the seam is
// invisible. Joints higher up the blade move more (offset scaled by (j/n)^4).
//
// Validation: 120 consecutive traced frames of the grass tip joints are reproduced to 4e-4 (the
// rounding of the comparison's bind values), including the loop crossfade
// (tools/grass_wind_model.py uses the same arithmetic).
//   0x452ca8 create   frames (+0xcc) = 90, bounds min = FLT_MAX / max = -FLT_MAX, time (+0x9c) = 0
//   0x167438 init     reads BendRate, SlideMax, BaseWindX, BaseWindZ, TimeShift from the map; precomputes
//   0x1670ec          one buffer frame (time += 0.009; 4-octave fractal value noise)
//   0x16768c update   index = (index + 1) % frames; at a wrap, hold and crossfade 0.09 per frame
//   0x2d18dc fractal noise, 0x26da3c smoothed 4D noise, 0x26d970 hash, 0x26d9f0 cosine interpolation
// Float operations keep the instruction order; compile with -ffp-contract=off.
// cosf: the host's libm instead of the original library routine at 0x2f7264 (no difference observed).
#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

namespace captain_toad::RecoveredGrassWind {

// 0x26d970: integer lattice hash -> pseudo-random value in [-1, 1].
inline float hash4(int32_t x, int32_t y, int32_t z, int32_t w) {
    uint32_t n = uint32_t(x) + (uint32_t(y) * 64u - uint32_t(y) * 7u) + uint32_t(z) * 3249u + uint32_t(w) * 185193u;
    n = (n << 13) ^ n;
    uint32_t m = n * (15731u * (n * n) + 789221u) + 1376312589u;
    m &= 0x7fffffffu;
    return 1.0f - float(m) * 9.313225746154785e-10f;
}

// 0x26da3c: smoothed lattice value: the centre / 4 + the 8 axial neighbours / 16 + the 16 diagonal
// corners / 64 (the call order is kept because the sums round in that order).
inline float smooth(int32_t x, int32_t y, int32_t z, int32_t w) {
    float centre = hash4(x, y, z, w) * 0.25f;
    float axial = hash4(x - 1, y, z, w);
    axial += hash4(x + 1, y, z, w);
    axial += hash4(x, y - 1, z, w);
    axial += hash4(x, y + 1, z, w);
    axial += hash4(x, y, z - 1, w);
    axial += hash4(x, y, z + 1, w);
    axial += hash4(x, y, z, w - 1);
    axial += hash4(x, y, z, w + 1);
    axial = axial * 0.0625f;
    float corners = hash4(x - 1, y - 1, z - 1, w - 1);
    const int order[15][4] = {{1, -1, -1, -1}, {-1, 1, -1, -1}, {1, 1, -1, -1},  {-1, -1, 1, -1}, {1, -1, 1, -1},
                              {-1, 1, 1, -1},  {1, 1, 1, -1},   {-1, -1, -1, 1}, {1, -1, -1, 1},  {-1, 1, -1, 1},
                              {1, 1, -1, 1},   {-1, -1, 1, 1},  {1, -1, 1, 1},   {-1, 1, 1, 1},   {1, 1, 1, 1}};
    for (const auto& o : order) {
        corners += hash4(x + o[0], y + o[1], z + o[2], w + o[3]);
    }
    return (centre + axial) + corners * 0.015625f;
}

// 0x26d9f0: cosine interpolation between a and b at fraction x.
inline float cosineLerp(float x, float a, float b) {
    float f = (1.0f - cosf(x * 3.1415927410125732f)) * 0.5f;
    float r = (1.0f - f) * a;
    return r + b * f;
}

// Floor as the original compiles it: truncate, and for negative inputs subtract one from the
// integer part and add one to the fraction.
inline void floorQuirk(float v, int32_t& i, float& fraction) {
    i = int32_t(v);
    fraction = v - float(i);
    if (!(v >= 0.0f)) {
        i -= 1;
        fraction = fraction + 1.0f;
    }
}

// 0x2d18dc: fractal value noise. Parameters {octaves, frequency, amplitude, persistence}; the loop
// runs octaves - 1 times (so 3 octaves are summed for the default 4, as in the original).
inline float fractal(float x, float y, float z, float w, int octaves = 4, float frequency = 1.0f,
                     float amplitude = 1.0f, float persistence = 0.5f) {
    float total = 0.0f;
    float freq = frequency;
    float amp = amplitude;
    for (int octave = 0; octave < octaves - 1; octave++) {
        int32_t ix, iy, iz, iw;
        float fx, fy, fz, fw;
        floorQuirk(x * freq, ix, fx);
        floorQuirk(y * freq, iy, fy);
        floorQuirk(z * freq, iz, fz);
        floorQuirk(w * freq, iw, fw);
        // The 16 corners of the 4D lattice cell, then interpolate along x, y, z and w in turn.
        float v[16];
        for (int c = 0; c < 16; c++) {
            v[c] = smooth(ix + (c & 1), iy + ((c >> 1) & 1), iz + ((c >> 2) & 1), iw + ((c >> 3) & 1));
        }
        float a[8], b[4], d[2];
        for (int c = 0; c < 8; c++) {
            a[c] = cosineLerp(fx, v[2 * c], v[2 * c + 1]);
        }
        for (int c = 0; c < 4; c++) {
            b[c] = cosineLerp(fy, a[2 * c], a[2 * c + 1]);
        }
        for (int c = 0; c < 2; c++) {
            d[c] = cosineLerp(fz, b[2 * c], b[2 * c + 1]);
        }
        float n = cosineLerp(fw, d[0], d[1]);
        total = total + n * amp;   // one VMLA
        amp = persistence * amp;
        freq = freq * 2.0f;
    }
    return total;
}

struct Control {
    // Map parameters (constructor defaults of 0x452ca8; the opening's map overrides them).
    float bendRate = 1;
    float slideMax = 50;
    float windX = 0;
    float windZ = 20;
    float timeShift = 2;
    int frames = 90;

    // buffer[frame][joint] = (x, z) offset of chain joint 1..n-1 from the chain root (single chain).
    std::vector<std::vector<std::pair<float, float>>> buffer;
    int index = 0;           // +0xc8 current buffer frame
    bool blending = false;   // +0xc4 crossfading after a wrap
    float blend = 0;         // +0x9c (the time field, reused as the blend weight)

    // 0x167438 / 0x1670ec: precompute the loop. Bounds (0xa0..0xac) are the min/max of the chain
    // roots' x/z. With several chains each chain's normalised root position shifts its noise; the
    // opening's grass has one chain, so the extent is zero and the shift is 0.
    void precompute(int joints, float rootX, float rootZ, float minX, float minZ, float maxX, float maxZ) {
        buffer.assign(frames, {});
        float time = 0.0f;
        float extentZ = maxZ - minZ;
        float extentX = maxX - minX;
        for (int k = 0; k < frames; k++) {
            time = time + 0.009f;
            float px = std::fabs(extentX) < 0.001f ? 0.0f : ((rootX - minX) * 0.5f) / extentX;
            float pz = std::fabs(extentZ) < 0.001f ? 0.0f : ((rootZ - minZ) * 0.5f) / extentZ;
            // The noise is sampled along the wind direction, scrolling with time.
            float dx = windX;
            float dz = windZ;
            if (dx * dx + dz * dz >= 1.0000001111620804e-06f) {
                float length = sqrtf(dx * dx + dz * dz);
                if (length > 0.0f) {
                    float inverse = 1.0f / length;
                    dx = dx * inverse;
                    dz = dz * inverse;
                }
            } else {
                dx = 0.0f;
                dz = 0.0f;
            }
            float s = -(time * timeShift);
            dx = dx * s;
            dz = dz * s;
            float nx = fractal(dx + px, dz + pz, 0.0f, time) * (slideMax * bendRate);
            float nz = fractal(dx + px, dz + pz, 100.0f, time) * (slideMax * bendRate);
            // Higher joints sway more: (j+1)/n to the 4th power.
            float inverseCount = 1.0f / float(joints);
            for (int j = 1; j < joints; j++) {
                float q = float(j + 1) * inverseCount;
                q = q * q;
                q = q * q;
                buffer[k].push_back({(windX + nx) * q, (windZ + nz) * q});
            }
        }
    }

    // 0x16768c, once per 30 Hz game frame: writes the chain joints' x/z offsets (out[j-1]).
    void update(std::vector<std::pair<float, float>>& out) {
        int previous = index;
        bool wasBlending = blending;
        index = (index + 1) % frames;
        blending = index < previous;   // the loop wrapped
        if (blending) {
            float t = wasBlending ? blend : 0.0f;
            t = t + 0.09f;
            if (t > 1.0f) {
                t = 1.0f;   // the original compares the float bit patterns as signed ints (same for t >= 0)
            }
            blend = t;
        }
        const auto& target = buffer[index];
        out.resize(target.size());
        for (size_t j = 0; j < target.size(); j++) {
            if (!blending) {
                out[j] = target[j];
                continue;
            }
            const auto& from = buffer[previous][j];
            if (blend == 1.0f) {
                blending = false;
            }
            out[j] = {from.first + blend * (target[j].first - from.first),
                      from.second + blend * (target[j].second - from.second)};
        }
        if (blending) {
            index = previous;   // hold the last frame while crossfading
        }
    }
};

}   // namespace captain_toad::RecoveredGrassWind
