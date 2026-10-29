// The VHS look (shared/ui/vhs.cpp) as a fragment shader for SDL's GPU renderer: the same tape,
// drawn while the game's frame is composed instead of on the CPU. The frame-wide state comes
// from ui::vhs_frame() as uniforms; everything per line and per pixel is worked out here the
// way ui::vhs() does it, over the fixed 720 x 480 virtual video grid.
//
// Built into SPIR-V (Vulkan) and MSL (Metal) by tools/build_shaders.py; the results are in
// vhs_frag.hpp. SDL's conventions: the drawn texture at sampler slot 0 (set 2), the uniforms
// at slot 0 (set 3), the vertex colour and texture coordinate at locations 0 and 1.
#version 450

layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 2, binding = 0) uniform sampler2D u_texture;

layout(set = 3, binding = 0, std140) uniform Tape {
    vec4 size_wear;  // virtual signal width, height, wear, dropouts per frame
    vec4 picture;    // slip, roll, saturation, brightness
    vec4 tear;       // centre (< 0: none), half height, shift, top of the tracking band
    vec4 misc;       // rolling bar line, chroma delay, head-switching lines, chroma radius
    vec4 clock;      // the frame number as a float
    uvec4 frame;     // the frame number
};

// A hash of three integers to [0, 1) (ui/vhs.cpp's hash).
float hash(uint a, uint b, uint c) {
    uint h = (a * 0x9E3779B1u) ^ ((b + 0x7F4A7C15u) * 0x85EBCA77u) ^ ((c + 0x165667B1u) * 0xC2B2AE3Du);
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return float(h >> 8) / 16777216.0;
}

vec3 source(int x, int y) {
    // Identical centre-based virtual-signal sampling to the CPU implementation.
    const ivec2 extent = textureSize(u_texture, 0);
    const ivec2 signal = ivec2(size_wear.xy);
    const ivec2 pixel = min(extent - 1, ((ivec2(x, y) * 2 + 1) * extent) / (signal * 2));
    return texelFetch(u_texture, pixel, 0).rgb;
}

float luma(vec3 c) {
    return 0.299 * c.r + 0.587 * c.g + 0.114 * c.b;
}

// The tape's luma at a source pixel: sharpened, with a little ringing.
float tape_luma(int x, int y, int sw, float a) {
    const float left = luma(source(max(0, x - 1), y));
    const float right = luma(source(min(sw - 1, x + 1), y));
    const float centre = luma(source(x, y));
    const float soft = (left + 2.0 * centre + right) * 0.25;
    return centre + (centre - soft) * 0.6 * a;
}

// The tape's chroma (I, Q) at a source pixel: the average over the smear.
vec2 tape_chroma(int x, int y, int sw, int radius) {
    const int l = max(0, x - radius), r = min(sw, x + radius + 1);
    vec2 sum = vec2(0.0);
    for (int i = l; i < r; ++i) {
        const vec3 c = source(i, y);
        sum += vec2(0.596 * c.r - 0.274 * c.g - 0.322 * c.b, 0.211 * c.r - 0.523 * c.g + 0.312 * c.b);
    }
    return sum / float(r - l);
}

float vignette(float u, float a) {
    return pow(clamp(16.0 * u * (1.0 - u), 0.0, 1.0), 0.18 * min(1.0, a));
}

void main() {
    const int sw = int(size_wear.x), sh = int(size_wear.y);
    const float a = size_wear.z;
    const int w = sw * 2, h = sh * 2;
    const uint f = frame.x;
    const int ox = clamp(int(v_uv.x * float(w)), 0, w - 1);
    const int oy = clamp(int(v_uv.y * float(h)), 0, h - 1);

    float syf = float(oy) * 0.5 + picture.y;
    syf = mod(syf, float(sh));
    const int sy = min(sh - 1, int(syf));
    // The tape's faults are its own lines' (two for each line of the picture), not the game's.
    const uint line = uint(oy);
    float jitter = picture.x + (hash(line, f / 2u, 71u) - 0.5) * 0.9 * a;
    bool noisy_line = false;
    const float track_top = tear.w;
    if (float(sy) > track_top) {
        // The tracking band: a wave and noise, worse towards the bottom.
        const float depth = (float(sy) - track_top) / (float(sh) - track_top);
        jitter += sin(float(sy) * 0.9 + clock.x * 0.35) * 3.0 * a * depth;
        jitter += (hash(line, f, 73u) - 0.5) * 4.0 * a * depth;
    }
    if (sy >= sh - int(misc.z)) {
        jitter += 6.0 + 10.0 * a * hash(line, f, 75u); // head switching
        noisy_line = true;
    }
    float static_amount = 0.0;
    if (tear.x >= 0.0) {
        const float d = abs(float(sy) - tear.x);
        if (d < tear.y) {
            const float band = 1.0 - d / tear.y;
            jitter += tear.z * band + (hash(line / 2u, f, 77u) - 0.5) * 8.0 * band;
            static_amount = min(0.75, band * 0.9);
        }
    }
    // A dropout: a thin streak, one tape line high, flaring and fading along the line.
    bool streak = false;
    float streak_from = 0.0, streak_length = 1.0, streak_peak = 0.0;
    if (hash(line, f, 79u) < size_wear.w / float(h)) {
        streak = true;
        streak_from = hash(line, f, 81u) * float(w);
        streak_length = 4.0 + hash(line, f, 83u) * 36.0;
        streak_peak = 0.55 + 0.4 * hash(line, f, 85u);
    }
    const float scan = (oy & 1) != 0 ? 1.0 - 0.22 * min(1.0, 0.5 + a) : 1.0;
    const float bar = max(0.0, 1.0 - abs(float(sy) - misc.x) / (float(sh) * 0.06));
    const float lift = bar * 0.05 * a;
    const float vy = vignette((float(oy) + 0.5) / float(h), a) * scan * picture.w;

    const float sx = float(ox) * 0.5 - jitter;
    const float cx = sx - misc.y;
    const float fx = clamp(sx, 0.0, float(sw - 1));
    const int x0 = int(fx), x1 = min(sw - 1, x0 + 1);
    const float t = fx - float(x0);
    const float y0 = tape_luma(x0, sy, sw, a);
    float yv = y0 + (tape_luma(x1, sy, sw, a) - y0) * t;
    const vec2 chroma = tape_chroma(clamp(int(cx), 0, sw - 1), sy, sw, int(misc.w)) * picture.z;
    float iv = chroma.x, qv = chroma.y;
    // Grain, stretched along the line as tape noise is.
    yv += (hash(uint(ox / 3), line, f) - 0.5) * 0.12 * a + lift;
    if (noisy_line) {
        yv = yv * 0.6 + hash(uint(ox), uint(oy), f) * 0.4;
    }
    if (static_amount > 0.0) {
        const float s = hash(uint(ox / 2), uint(oy), f * 7u);
        yv += (s - yv) * static_amount;
        iv *= 1.0 - static_amount;
        qv *= 1.0 - static_amount;
    }
    if (streak) {
        const float u = (float(ox) - streak_from) / streak_length;
        if (u >= 0.0 && u < 1.0) {
            const float shape = min(1.0, u * 6.0) * (1.0 - u) * (1.0 - u); // a sharp start and a tail
            yv += (1.0 - yv) * streak_peak * shape;
            iv *= 1.0 - shape;
            qv *= 1.0 - shape;
        }
    }
    const vec3 rgb = vec3(yv + 0.956 * iv + 0.621 * qv, yv - 0.272 * iv - 0.647 * qv, yv - 1.106 * iv + 1.703 * qv);
    const float v = vy * vignette((float(ox) + 0.5) / float(w), a);
    o_color = vec4(clamp(rgb * v, 0.0, 1.0), 1.0) * v_color;
}
