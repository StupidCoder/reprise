// A small software rasterizer for the 3DS adventures. See raster.hpp.
#include "graphics/ctr3d/raster.hpp"
#include <algorithm>
#include <cmath>

namespace picarat::ctr3d {

namespace {

int wrap(int i, int n, Texture::Wrap mode) {
    switch (mode) {
    case Texture::Wrap::Repeat:
        i %= n;
        return i < 0 ? i + n : i;
    case Texture::Wrap::Mirror: {
        const int period = 2 * n;
        int m = i % period;
        if (m < 0) {
            m += period;
        }
        return m < n ? m : period - 1 - m;
    }
    default:
        return std::clamp(i, 0, n - 1);
    }
}

// A clipped polygon vertex: clip position plus the interpolated attributes.
struct Clip {
    float p[4];
    float a[9]; // r g b a u v shade shade2 q
};

Clip lerp(const Clip& x, const Clip& y, float t) {
    Clip c;
    for (int i = 0; i < 4; ++i) {
        c.p[i] = x.p[i] + (y.p[i] - x.p[i]) * t;
    }
    for (int i = 0; i < 9; ++i) {
        c.a[i] = x.a[i] + (y.a[i] - x.a[i]) * t;
    }
    return c;
}

// Clips against w > epsilon and -w <= z (the near plane); x/y/far are left
// to the scissor and depth buffer.
int clipNear(const Clip in[3], Clip out[8]) {
    Clip a[8], b[8];
    int n = 3;
    std::copy(in, in + 3, a);
    auto pass = [&](auto distance) {
        int m = 0;
        for (int i = 0; i < n; ++i) {
            const Clip& p = a[i];
            const Clip& q = a[(i + 1) % n];
            const float dp = distance(p), dq = distance(q);
            if (dp >= 0) {
                b[m++] = p;
            }
            if ((dp >= 0) != (dq >= 0)) {
                b[m++] = lerp(p, q, dp / (dp - dq));
            }
        }
        n = m;
        std::copy(b, b + m, a);
    };
    pass([](const Clip& c) { return c.p[3] - 1e-5f; });
    if (n >= 3) {
        pass([](const Clip& c) { return c.p[2] + c.p[3]; });
    }
    std::copy(a, a + n, out);
    return n;
}

struct Screen {
    float x, y, z, invW;
    float a[9]; // attributes divided by w
};

void rasterize(Target& t, const DrawState& s, const Screen& v0, const Screen& v1,
               const Screen& v2) {
    const float area = (v1.x - v0.x) * (v2.y - v0.y) - (v2.x - v0.x) * (v1.y - v0.y);
    if (area == 0.0f || !std::isfinite(area)) {
        return;
    }
    // Screen y grows downwards, so counter-clockwise in NDC is negative here.
    const bool front = area < 0;
    if ((s.cull == DrawState::Cull::Back && !front) ||
        (s.cull == DrawState::Cull::Front && front)) {
        return;
    }
    const int x0 = std::max({s.scissor[0], 0, int(std::floor(std::min({v0.x, v1.x, v2.x})))});
    const int x1 =
        std::min({s.scissor[2], t.width, int(std::ceil(std::max({v0.x, v1.x, v2.x}))) + 1});
    const int y0 = std::max({s.scissor[1], 0, int(std::floor(std::min({v0.y, v1.y, v2.y})))});
    const int y1 =
        std::min({s.scissor[3], t.height, int(std::ceil(std::max({v0.y, v1.y, v2.y}))) + 1});
    if (x0 >= x1 || y0 >= y1) {
        return;
    }
    // Edge functions in 1/16 pixel fixed point for exact coverage.
    auto fx = [](float f) {
        return std::int64_t(std::lround(f * 16.0f));
    };
    const std::int64_t X[3] = {fx(v0.x), fx(v1.x), fx(v2.x)};
    const std::int64_t Y[3] = {fx(v0.y), fx(v1.y), fx(v2.y)};
    const std::int64_t twice = (X[1] - X[0]) * (Y[2] - Y[0]) - (X[2] - X[0]) * (Y[1] - Y[0]);
    if (twice == 0) {
        return;
    }
    const int sign = twice > 0 ? 1 : -1;

    struct Edge {
        std::int64_t a, b, c;
        bool topLeft;
    } e[3];

    for (int i = 0; i < 3; ++i) {
        const int j = (i + 1) % 3, k = (i + 2) % 3;
        // Edge opposite vertex k, from i to j; positive inside (after sign).
        e[k].a = (Y[i] - Y[j]) * sign;
        e[k].b = (X[j] - X[i]) * sign;
        e[k].c = (X[i] * Y[j] - X[j] * Y[i]) * sign;
        // Top edge: horizontal with the inside below; left edge: inside to the right.
        e[k].topLeft = (e[k].a == 0 && e[k].b > 0) || e[k].a > 0;
    }
    const float inv = 1.0f / float(twice * sign);
    const Screen* vs[3] = {&v0, &v1, &v2};
    for (int y = y0; y < y1; ++y) {
        const std::int64_t py = std::int64_t(y) * 16 + 8;
        for (int x = x0; x < x1; ++x) {
            const std::int64_t px = std::int64_t(x) * 16 + 8;
            std::int64_t w[3];
            bool inside = true;
            for (int i = 0; i < 3; ++i) {
                w[i] = e[i].a * px + e[i].b * py + e[i].c;
                if (w[i] < 0 || (w[i] == 0 && !e[i].topLeft)) {
                    inside = false;
                    break;
                }
            }
            if (!inside) {
                continue;
            }
            const float b0 = float(w[0]) * inv, b1 = float(w[1]) * inv, b2 = 1.0f - b0 - b1;
            const float z = b0 * vs[0]->z + b1 * vs[1]->z + b2 * vs[2]->z - s.depthOffset;
            const std::size_t at = std::size_t(y) * std::size_t(t.width) + std::size_t(x);
            if (s.depthTest && z > t.depth[at]) {
                continue;
            }
            const float iw = b0 * vs[0]->invW + b1 * vs[1]->invW + b2 * vs[2]->invW;
            const float pw = iw != 0 ? 1.0f / iw : 0.0f;
            float attr[9];
            for (int i = 0; i < 9; ++i) {
                attr[i] = (b0 * vs[0]->a[i] + b1 * vs[1]->a[i] + b2 * vs[2]->a[i]) * pw;
            }
            float c[4] = {attr[0], attr[1], attr[2], attr[3]};
            float tex[4] = {1, 1, 1, 1};
            if (s.texture) {
                // Projective coordinates: s / q, t / q per pixel.
                const float q = attr[8] != 0 ? attr[8] : 1.0f;
                s.texture->sample(attr[4] / q, attr[5] / q, tex);
            }
            if (s.fragmentLighting) {
                float d0 = 1;
                if (s.d0Table) {
                    const float scaled = std::clamp(attr[6], -1.0f, 1.0f) * 128.0f;
                    const int li = std::clamp(int(std::floor(scaled)), -128, 127);
                    const std::size_t e = std::size_t(li & 0xff);
                    d0 = s.d0Table[e] + s.d0Table[256 + e] * (scaled - float(li));
                }
                const float diffuse = std::max(0.0f, attr[7]);
                for (int i = 0; i < 3; ++i) {
                    const float lit =
                        s.litPrimary[i] + s.litDiffuse[i] * diffuse + s.litSpecular[i] * d0;
                    c[i] *= std::clamp(lit * tex[i], 0.0f, 1.0f);
                }
                c[3] *= tex[3];
            } else {
                for (int i = 0; i < 4; ++i) {
                    c[i] *= tex[i];
                }
            }
            for (int i = 0; i < 4; ++i) {
                c[i] = std::clamp(c[i] * s.colour[i], 0.0f, 1.0f);
            }
            if (c[3] <= s.alphaTest && !(s.alphaTest == 0 && c[3] > 0)) {
                continue;
            }
            platform::Color& d = t.colour[at];
            auto to8 = [](float f) {
                return std::uint8_t(std::lround(std::clamp(f, 0.0f, 1.0f) * 255.0f));
            };
            switch (s.blend) {
            case DrawState::Blend::None:
                d = {to8(c[0]), to8(c[1]), to8(c[2]), to8(c[3])};
                break;
            case DrawState::Blend::Alpha: {
                const float a = c[3];
                d.r = to8(c[0] * a + d.r / 255.0f * (1 - a));
                d.g = to8(c[1] * a + d.g / 255.0f * (1 - a));
                d.b = to8(c[2] * a + d.b / 255.0f * (1 - a));
                d.a = to8(a + d.a / 255.0f * (1 - a));
                break;
            }
            case DrawState::Blend::Multiply: {
                // The destination scaled by the source colour (alpha fades
                // the effect out).
                const float a = c[3];
                d.r = to8(d.r / 255.0f * (c[0] * a + 1 - a));
                d.g = to8(d.g / 255.0f * (c[1] * a + 1 - a));
                d.b = to8(d.b / 255.0f * (c[2] * a + 1 - a));
                break;
            }
            case DrawState::Blend::Add:
                d.r = to8(d.r / 255.0f + c[0] * c[3]);
                d.g = to8(d.g / 255.0f + c[1] * c[3]);
                d.b = to8(d.b / 255.0f + c[2] * c[3]);
                break;
            case DrawState::Blend::AddOne:
                d.r = to8(d.r / 255.0f + c[0]);
                d.g = to8(d.g / 255.0f + c[1]);
                d.b = to8(d.b / 255.0f + c[2]);
                break;
            case DrawState::Blend::Subtract:
                d.r = to8(d.r / 255.0f - c[0] * c[3]);
                d.g = to8(d.g / 255.0f - c[1] * c[3]);
                d.b = to8(d.b / 255.0f - c[2] * c[3]);
                break;
            }
            if (s.depthWrite) {
                t.depth[at] = z;
            }
        }
    }
}

} // namespace

void Texture::sample(float u, float v, float out[4]) const {
    if (width <= 0 || height <= 0) {
        out[0] = out[1] = out[2] = out[3] = 1;
        return;
    }
    auto fetch = [&](int x, int y, float w, float acc[4]) {
        if ((wrapS == Wrap::Border && (x < 0 || x >= width)) ||
            (wrapT == Wrap::Border && (y < 0 || y >= height))) {
            return; // the border colour: transparent
        }
        const platform::Color& c = pixels[std::size_t(wrap(y, height, wrapT)) * std::size_t(width) +
                                          std::size_t(wrap(x, width, wrapS))];
        acc[0] += c.r * w;
        acc[1] += c.g * w;
        acc[2] += c.b * w;
        acc[3] += c.a * w;
    };
    float acc[4] = {0, 0, 0, 0};
    const float fx = u * float(width), fy = v * float(height);
    if (!linear) {
        fetch(int(std::floor(fx)), int(std::floor(fy)), 1.0f, acc);
    } else {
        const float sx = fx - 0.5f, sy = fy - 0.5f;
        const int ix = int(std::floor(sx)), iy = int(std::floor(sy));
        const float tx = sx - float(ix), ty = sy - float(iy);
        fetch(ix, iy, (1 - tx) * (1 - ty), acc);
        fetch(ix + 1, iy, tx * (1 - ty), acc);
        fetch(ix, iy + 1, (1 - tx) * ty, acc);
        fetch(ix + 1, iy + 1, tx * ty, acc);
    }
    for (int i = 0; i < 4; ++i) {
        out[i] = acc[i] / 255.0f;
    }
}

void Target::resize(int w, int h) {
    width = w;
    height = h;
    colour.assign(std::size_t(w) * std::size_t(h), platform::Color{0, 0, 0, 255});
    depth.assign(std::size_t(w) * std::size_t(h), 1.0f);
}

void Target::clear(platform::Color c, float d) {
    std::fill(colour.begin(), colour.end(), c);
    std::fill(depth.begin(), depth.end(), d);
}

platform::Image Target::image() const {
    platform::Image out(width, height);
    out.pixels = colour;
    return out;
}

void drawTriangles(Target& target, const DrawState& state, std::span<const Vertex> vertices,
                   std::span<const std::uint16_t> indices) {
    for (std::size_t i = 0; i + 2 < indices.size(); i += 3) {
        Clip in[3];
        bool valid = true;
        for (int k = 0; k < 3; ++k) {
            const std::size_t idx = indices[i + std::size_t(k)];
            if (idx >= vertices.size()) {
                valid = false;
                break;
            }
            const Vertex& v = vertices[idx];
            in[k] = {{v.x, v.y, v.z, v.w}, {v.r, v.g, v.b, v.a, v.u, v.v, v.shade, v.shade2, v.q}};
        }
        if (!valid) {
            continue;
        }
        Clip poly[8];
        const int n = clipNear(in, poly);
        if (n < 3) {
            continue;
        }
        Screen s[8];
        for (int k = 0; k < n; ++k) {
            const float iw = 1.0f / poly[k].p[3];
            s[k].x = (poly[k].p[0] * iw * 0.5f + 0.5f) * float(target.width);
            s[k].y = (0.5f - poly[k].p[1] * iw * 0.5f) * float(target.height);
            s[k].z = poly[k].p[2] * iw;
            s[k].invW = iw;
            for (int a = 0; a < 9; ++a) {
                s[k].a[a] = poly[k].a[a] * iw;
            }
        }
        for (int k = 1; k + 1 < n; ++k) {
            rasterize(target, state, s[0], s[k], s[k + 1]);
        }
    }
}

} // namespace picarat::ctr3d
