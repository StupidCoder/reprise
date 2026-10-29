#include "ui/vhs.hpp"
#include "ui/shaders/vhs_frag.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace ui {

namespace {

// A hash of a few integers to [0, 1).
float hash(std::uint32_t a, std::uint32_t b = 0, std::uint32_t c = 0) {
    std::uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u ^ (c + 0x165667B1u) * 0xC2B2AE3Du;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return float(h >> 8) / float(1u << 24);
}

// Smoothly varying noise over time (frames), -1 to 1.
float wander(long frame, float rate, std::uint32_t seed) {
    const float t = float(frame) * rate;
    const auto i = std::uint32_t(std::int64_t(std::floor(t)));
    const float f = t - std::floor(t), s = f * f * (3 - 2 * f);
    return (hash(i, seed) * (1 - s) + hash(i + 1, seed) * s) * 2 - 1;
}

struct Strength {
    float wear;      // master amount
    float tear;      // chance of a tear in a time block
    float hold;      // chance of the vertical hold slipping
    float fade;      // chance of the colour fading out
    float dropouts;  // streaks per frame
};

constexpr std::array<Strength, vhs_levels> strengths{{
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    {0.35f, 0.0f, 0.0f, 0.0f, 0.6f},  // light: a played tape
    {0.65f, 0.06f, 0.0f, 0.0f, 1.5f}, // medium: a worn one
    {1.0f, 0.16f, 0.02f, 0.05f, 3.0f}, // heavy
    {1.45f, 0.32f, 0.07f, 0.14f, 6.0f}, // broken
}};

} // namespace

VhsFrame vhs_frame(long frame, int level, int sw, int sh) {
    level = std::clamp(level, 0, vhs_levels - 1);
    const auto& k = strengths[std::size_t(level)];
    VhsFrame t;
    const float a = t.wear = k.wear;
    t.dropouts = k.dropouts;
    // Luma with a little ringing, chroma smeared over a few pixels and arriving late.
    t.chroma_radius = 1 + int(std::lround(2.5f * a));
    t.chroma_delay = 0.8f + 1.6f * a;

    // The whole picture's state this frame.
    const auto f = t.frame = std::uint32_t(frame);
    t.time = float(frame);
    t.slip = (hash(f / 6, 31) - 0.5f) * 0.6f * a; // the odd whole-picture slip
    if (k.hold > 0 && hash(f / 40, 41) < k.hold) { // the vertical hold letting go
        const float phase = float(f % 40) / 40.0f;
        t.roll = std::sin(phase * 3.14159f) * float(sh) * (0.15f + 0.35f * hash(f / 40, 43));
    }
    const bool faded = k.fade > 0 && hash(f / 25, 51) < k.fade;
    t.saturation = (faded ? 0.15f : 1.0f) * (1.0f - 0.12f * a) * (1.0f + 0.08f * wander(frame, 0.05f, 53));
    t.brightness = 1.0f + 0.03f * a * wander(frame, 0.3f, 55);
    // A tear: in some time blocks a band is yanked sideways and full of static.
    if (k.tear > 0 && hash(f / 11, 61) < k.tear) {
        t.tear_centre = hash(f / 11, 63) * float(sh);
        t.tear_half = float(sh) * (0.02f + 0.08f * hash(f / 11, 65));
        t.tear_shift = (hash(f / 11, 67) - 0.5f) * float(sw) * 0.25f * a;
    }
    t.head_lines = 3 + int(2 * a); // head-switching noise at the bottom
    t.track_top = float(sh) * (1.0f - 0.10f * std::min(1.0f, a));
    t.bar_at = std::fmod(float(frame) * 0.4f, float(sh) * 1.6f) - float(sh) * 0.3f;
    return t;
}

platform::Image vhs(const platform::Image& in, long frame, int level) {
    level = std::clamp(level, 0, vhs_levels - 1);
    if (level == 0 || in.width <= 0 || in.height <= 0) {
        return in;
    }
    const int sw = in.width, sh = in.height;
    const VhsFrame t = vhs_frame(frame, level, sw, sh);
    const float a = t.wear;
    const int w = sw * 2, h = sh * 2;
    platform::Image out(w, h);

    // The source as luma and chroma (YIQ), line by line: luma with a little
    // ringing, chroma smeared over a few pixels and arriving late.
    const int chroma_radius = t.chroma_radius;
    const float chroma_delay = t.chroma_delay;
    std::vector<float> Y(std::size_t(sw * sh)), I(Y.size()), Q(Y.size());
    std::vector<float> line_y(static_cast<std::size_t>(sw)), sum_i(static_cast<std::size_t>(sw) + 1), sum_q(sum_i.size());
    for (int y = 0; y < sh; ++y) {
        const auto* src = in.pixels.data() + std::size_t(y * sw);
        float* ly = Y.data() + std::size_t(y * sw);
        float* li = I.data() + std::size_t(y * sw);
        float* lq = Q.data() + std::size_t(y * sw);
        sum_i[0] = sum_q[0] = 0;
        for (int x = 0; x < sw; ++x) {
            const float r = src[x].r / 255.0f, g = src[x].g / 255.0f, b = src[x].b / 255.0f;
            line_y[std::size_t(x)] = 0.299f * r + 0.587f * g + 0.114f * b;
            sum_i[std::size_t(x + 1)] = sum_i[std::size_t(x)] + (0.596f * r - 0.274f * g - 0.322f * b);
            sum_q[std::size_t(x + 1)] = sum_q[std::size_t(x)] + (0.211f * r - 0.523f * g + 0.312f * b);
        }
        for (int x = 0; x < sw; ++x) {
            const float left = line_y[std::size_t(std::max(0, x - 1))];
            const float right = line_y[std::size_t(std::min(sw - 1, x + 1))];
            const float centre = line_y[std::size_t(x)];
            const float soft = (left + 2 * centre + right) * 0.25f;
            ly[x] = centre + (centre - soft) * 0.6f * a; // the sharpening's overshoot
            const int l = std::max(0, x - chroma_radius), r = std::min(sw, x + chroma_radius + 1);
            li[x] = (sum_i[std::size_t(r)] - sum_i[std::size_t(l)]) / float(r - l);
            lq[x] = (sum_q[std::size_t(r)] - sum_q[std::size_t(l)]) / float(r - l);
        }
    }

    const auto f = t.frame;
    const float slip = t.slip, roll = t.roll, saturation = t.saturation, brightness = t.brightness;
    const float tear_centre = t.tear_centre, tear_half = t.tear_half, tear_shift = t.tear_shift;
    const int head_lines = t.head_lines;
    const float track_top = t.track_top;

    // Vignette, separable.
    std::vector<float> vig_x(static_cast<std::size_t>(w)), vig_y(static_cast<std::size_t>(h));
    for (int x = 0; x < w; ++x) {
        const float u = (x + 0.5f) / float(w);
        vig_x[std::size_t(x)] = std::pow(std::clamp(16 * u * (1 - u), 0.0f, 1.0f), 0.18f * std::min(1.0f, a));
    }
    for (int y = 0; y < h; ++y) {
        const float v = (y + 0.5f) / float(h);
        vig_y[std::size_t(y)] = std::pow(std::clamp(16 * v * (1 - v), 0.0f, 1.0f), 0.18f * std::min(1.0f, a));
    }
    const float bar_at = t.bar_at;

    for (int oy = 0; oy < h; ++oy) {
        auto* row = out.pixels.data() + std::size_t(oy * w);
        float syf = oy * 0.5f + roll;
        syf = std::fmod(syf, float(sh));
        if (syf < 0) {
            syf += float(sh);
        }
        const int sy = std::min(sh - 1, int(syf));
        // The tape's faults are its own lines' (two for each line of the
        // picture), not the game's. Where this one sits sideways (source
        // pixels):
        const auto line = std::uint32_t(oy);
        float jitter = slip + (hash(line, f / 2, 71) - 0.5f) * 0.9f * a;
        bool noisy_line = false;
        if (float(sy) > track_top) {
            // The tracking band: a wave and noise, worse towards the bottom.
            const float depth = (float(sy) - track_top) / (float(sh) - track_top);
            jitter += std::sin(float(sy) * 0.9f + float(frame) * 0.35f) * 3.0f * a * depth;
            jitter += (hash(line, f, 73) - 0.5f) * 4.0f * a * depth;
        }
        if (sy >= sh - head_lines) {
            jitter += 6.0f + 10.0f * a * hash(line, f, 75); // head switching
            noisy_line = true;
        }
        float static_amount = 0;
        if (tear_centre >= 0) {
            const float d = std::abs(float(sy) - tear_centre);
            if (d < tear_half) {
                const float band = 1.0f - d / tear_half;
                jitter += tear_shift * band + (hash(line / 2, f, 77) - 0.5f) * 8.0f * band;
                static_amount = std::min(0.75f, band * 0.9f);
            }
        }
        // A dropout: a thin streak, one tape line high, where the tape lost
        // its coating; it flares and fades along the line.
        float streak_from = -1, streak_length = 1, streak_peak = 0;
        if (hash(line, f, 79) < t.dropouts / float(h)) {
            streak_from = hash(line, f, 81) * float(w);
            streak_length = 4.0f + hash(line, f, 83) * 36.0f;
            streak_peak = 0.55f + 0.4f * hash(line, f, 85);
        }
        const float scan = (oy & 1) ? 1.0f - 0.22f * std::min(1.0f, 0.5f + a) : 1.0f;
        const float bar = std::max(0.0f, 1.0f - std::abs(float(sy) - bar_at) / (float(sh) * 0.06f));
        const float lift = bar * 0.05f * a;
        const float* ly = Y.data() + std::size_t(sy * sw);
        const float* li = I.data() + std::size_t(sy * sw);
        const float* lq = Q.data() + std::size_t(sy * sw);
        const float vy = vig_y[std::size_t(oy)] * scan * brightness;
        for (int ox = 0; ox < w; ++ox) {
            const float sx = ox * 0.5f - jitter;
            const float cx = sx - chroma_delay;
            float yv, iv, qv;
            {
                const float fx = std::clamp(sx, 0.0f, float(sw - 1));
                const int x0 = int(fx), x1 = std::min(sw - 1, x0 + 1);
                const float t = fx - float(x0);
                yv = ly[x0] + (ly[x1] - ly[x0]) * t;
                const int c0 = std::clamp(int(cx), 0, sw - 1);
                iv = li[c0] * saturation;
                qv = lq[c0] * saturation;
            }
            // Grain, stretched along the line as tape noise is.
            const float grain = (hash(std::uint32_t(ox / 3), line, f) - 0.5f) * 0.12f * a;
            yv += grain + lift;
            if (noisy_line) {
                yv = yv * 0.6f + hash(std::uint32_t(ox), std::uint32_t(oy), f) * 0.4f;
            }
            if (static_amount > 0) {
                const float s = hash(std::uint32_t(ox / 2), std::uint32_t(oy), f * 7);
                yv += (s - yv) * static_amount;
                iv *= 1 - static_amount;
                qv *= 1 - static_amount;
            }
            if (const float u = (float(ox) - streak_from) / streak_length; u >= 0 && u < 1) {
                // A sharp start and a tail.
                const float shape = std::min(1.0f, u * 6.0f) * (1.0f - u) * (1.0f - u);
                yv += (1.0f - yv) * streak_peak * shape;
                iv *= 1 - shape;
                qv *= 1 - shape;
            }
            float r = yv + 0.956f * iv + 0.621f * qv;
            float g = yv - 0.272f * iv - 0.647f * qv;
            float b = yv - 1.106f * iv + 1.703f * qv;
            const float v = vy * vig_x[std::size_t(ox)];
            r = std::clamp(r * v, 0.0f, 1.0f);
            g = std::clamp(g * v, 0.0f, 1.0f);
            b = std::clamp(b * v, 0.0f, 1.0f);
            row[ox] = {std::uint8_t(r * 255.0f + 0.5f), std::uint8_t(g * 255.0f + 0.5f),
                       std::uint8_t(b * 255.0f + 0.5f), 255};
        }
    }
    return out;
}

platform::ScreenEffect vhs_effect(long frame, int level, int width, int height) {
    static const platform::FragmentShader shader{"vhs", shaders::vhs_spirv, shaders::vhs_msl};
    level = std::clamp(level, 0, vhs_levels - 1);
    const VhsFrame t = vhs_frame(frame, level, width, height);
    // The shader's uniform block (std140: five vec4 and one uvec4).
    const std::array<float, 20> values{
        float(width), float(height), t.wear, t.dropouts,
        t.slip, t.roll, t.saturation, t.brightness,
        t.tear_centre, t.tear_half, t.tear_shift, t.track_top,
        t.bar_at, t.chroma_delay, float(t.head_lines), float(t.chroma_radius),
        t.time, 0, 0, 0};
    const std::array<std::uint32_t, 4> clock{t.frame, 0, 0, 0};
    platform::ScreenEffect effect;
    effect.shader = &shader;
    effect.uniforms.resize(sizeof(values) + sizeof(clock));
    std::memcpy(effect.uniforms.data(), values.data(), sizeof(values));
    std::memcpy(effect.uniforms.data() + sizeof(values), clock.data(), sizeof(clock));
    effect.cpu = [frame, level](const platform::Image& picture) { return vhs(picture, frame, level); };
    return effect;
}

} // namespace ui
