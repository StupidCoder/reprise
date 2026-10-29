// The DS 3D pipeline, pixel for pixel. See ds_pipeline.hpp.
#include "graphics/ds_pipeline.hpp"
#include <algorithm>
#include <cmath>
#include <utility>

namespace picarat::dspipe {

namespace {

std::int32_t shift12(std::int64_t v) {
    return std::int32_t(v >> 12);
}

// The 64/32-bit hardware divider: 2^44 / d, truncated towards zero.
std::int64_t reciprocal(std::int32_t d) {
    return d == 0 ? 0 : (std::int64_t(1) << 44) / d;
}

// A reciprocal times w (4.12), as the SDK scales it (64-bit, divided by
// 0x1000 towards zero).
std::int64_t scaled(std::int64_t inverse, std::int32_t scaleW) {
    return scaleW == 0x1000 ? inverse : inverse * scaleW / 0x1000;
}

// The high word of (v << 13) + 2^31: v / 2^19, rounded.
std::int32_t round19(std::int64_t v) {
    return std::int32_t((v + (std::int64_t(1) << 18)) >> 19);
}

// The high word of v * f + 2^31.
std::int32_t roundHigh(std::int64_t v, std::int32_t f) {
    return std::int32_t((v * f + (std::int64_t(1) << 31)) >> 32);
}

// One edge of a polygon as the rasterizer walks it (an 18-bit fractional
// slope; `right` selects the right edge's rounding of x-major runs).
struct Slope {
    bool right = false;
    int x0 = 0, y0 = 0, y1 = 0;
    int xmin = 0, xmax = 0, xlen = 1, ylen = 0;
    std::int32_t increment = 0, dx = 0, coverageStep = 0;
    bool negative = false, xMajor = false;
    // Texture interpolation along the edge (linear).
    int interpBase = 0, interpLength = 0, interpAt = 0;
    int s0 = 0, t0 = 0, s1 = 0, t1 = 0;

    void dummy(int x, int s, int t) {
        x0 = xmin = xmax = x;
        dx = increment = 0;
        xMajor = negative = false;
        interpLength = 0;
        s0 = s1 = s;
        t0 = t1 = t;
    }

    int setup(const ScreenVertex& a, const ScreenVertex& b, int y) {
        x0 = a.x;
        y0 = a.y;
        y1 = b.y;
        if (b.x > a.x) {
            xmin = a.x;
            xmax = b.x - 1;
            negative = false;
        } else if (b.x < a.x) {
            xmin = b.x;
            xmax = a.x - 1;
            negative = true;
        } else {
            xmin = xmax = a.x;
            negative = false;
        }
        xlen = xmax + 1 - xmin;
        ylen = b.y - a.y;
        if (ylen == 0) {
            increment = 0;
        } else if (ylen == xlen && xlen != 1) {
            increment = 0x40000;
        } else {
            increment = (b.x - a.x) * ((1 << 18) / ylen);
            if (increment < 0) {
                increment = -increment;
            }
        }
        xMajor = increment > 0x40000;
        if (right) {
            dx = xMajor ? (negative ? 0x60000 : increment - 0x20000)
                        : (increment != 0 && negative ? 0x40000 : 0);
        } else {
            dx = xMajor ? (negative ? increment - 0x20000 + 0x40000 : 0x20000)
                        : (increment != 0 && negative ? 0x40000 : 0);
        }
        dx += (y - a.y) * increment;
        const int offset = (increment >= 0x40000 && (right != negative)) ? 1 : 0;
        interpBase = a.y - offset;
        interpLength = b.y - a.y;
        interpAt = y - interpBase;
        s0 = a.s;
        t0 = a.t;
        s1 = b.s;
        t1 = b.t;
        if (xMajor) {
            coverageStep = (ylen << 10) / xlen;
        }
        return x();
    }

    int step() {
        dx += increment;
        ++interpAt;
        return x();
    }

    int x() const {
        const int v = negative ? x0 - (dx >> 18) : x0 + (dx >> 18);
        return std::clamp(v, xmin, xmax);
    }

    // The run of pixels and the coverage word of this row (bit 31: an
    // x-major run with its start coverage and step; else a fixed value).
    void edge(bool swapped, int& length, std::int32_t& coverage) const {
        if (xMajor) {
            if (!swapped || right) {
                if (right != negative) {
                    length = (dx >> 18) - ((dx - increment) >> 18);
                } else {
                    length = ((dx + increment) >> 18) - (dx >> 18);
                }
            }
            int start = dx >> 18;
            if (negative) {
                start = xlen - start;
            }
            if (right) {
                start = start - length + 1;
            }
            const int startCoverage = (((start << 10) + 0x1ff) * ylen) / xlen;
            coverage = std::int32_t(0x80000000u | std::uint32_t((startCoverage & 0x3ff) << 12) |
                                    std::uint32_t(coverageStep & 0x3ff));
            if (swapped) {
                length = 1;
            }
            return;
        }
        length = 1;
        if (increment == 0) {
            // (Vertical edges' values are inverted too when swapped.)
            coverage = swapped ? 0 : 31;
            return;
        }
        int c = ((dx >> 9) + (increment >> 10)) >> 4;
        if ((c >> 5) != (dx >> 18)) {
            c = 31;
        }
        c &= 0x1f;
        if (swapped ? (right != negative) : !(right != negative)) {
            c = 0x1f - c;
        }
        coverage = c;
    }

    static int interpolate(int v0, int v1, int at, int length) {
        if (length == 0 || v0 == v1) {
            return v0;
        }
        if (v0 < v1) {
            return v0 + int(std::int64_t(v1 - v0) * at / length);
        }
        return v1 + int(std::int64_t(v0 - v1) * (length - at) / length);
    }

    int s() const { return interpolate(s0, s1, interpAt, interpLength); }

    int t() const { return interpolate(t0, t1, interpAt, interpLength); }
};

} // namespace

Matrix identity() {
    Matrix m{};
    m[0] = m[5] = m[10] = m[15] = 0x1000;
    return m;
}

void translate(Matrix& m, std::int32_t x, std::int32_t y, std::int32_t z) {
    for (int j = 0; j < 4; ++j) {
        m[std::size_t(12 + j)] +=
            shift12(std::int64_t(x) * m[std::size_t(j)] + std::int64_t(y) * m[std::size_t(4 + j)] +
                    std::int64_t(z) * m[std::size_t(8 + j)]);
    }
}

void scale(Matrix& m, std::int32_t x, std::int32_t y, std::int32_t z) {
    for (int j = 0; j < 4; ++j) {
        m[std::size_t(j)] = shift12(std::int64_t(x) * m[std::size_t(j)]);
        m[std::size_t(4 + j)] = shift12(std::int64_t(y) * m[std::size_t(4 + j)]);
        m[std::size_t(8 + j)] = shift12(std::int64_t(z) * m[std::size_t(8 + j)]);
    }
}

void multiply3x3(Matrix& m, const std::array<std::int32_t, 9>& n) {
    Matrix r = m;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 4; ++j) {
            r[std::size_t(i * 4 + j)] =
                shift12(std::int64_t(n[std::size_t(i * 3)]) * m[std::size_t(j)] +
                        std::int64_t(n[std::size_t(i * 3 + 1)]) * m[std::size_t(4 + j)] +
                        std::int64_t(n[std::size_t(i * 3 + 2)]) * m[std::size_t(8 + j)]);
        }
    }
    m = r;
}

Matrix multiply(const Matrix& a, const Matrix& b) {
    Matrix r{};
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            std::int64_t sum = 0;
            for (int k = 0; k < 4; ++k) {
                sum += std::int64_t(a[std::size_t(i * 4 + k)]) * b[std::size_t(k * 4 + j)];
            }
            r[std::size_t(i * 4 + j)] = shift12(sum);
        }
    }
    return r;
}

Matrix orthoW(std::int32_t top, std::int32_t bottom, std::int32_t left, std::int32_t right,
              std::int32_t near, std::int32_t far, std::int32_t scaleW) {
    // (First adventure 02099034.)
    const std::int64_t x = scaled(reciprocal(right - left), scaleW);
    const std::int64_t y = scaled(reciprocal(top - bottom), scaleW);
    const std::int64_t z = scaled(reciprocal(near - far), scaleW);
    Matrix m{};
    m[0] = round19(x);
    m[5] = round19(y);
    m[10] = round19(z);
    m[12] = roundHigh(x, -(right + left));
    m[13] = roundHigh(y, -(top + bottom));
    m[14] = roundHigh(z, near + far);
    m[15] = scaleW;
    return m;
}

std::array<std::int32_t, 9> rotationZ(std::int32_t sine, std::int32_t cosine) {
    // (First adventure 0209b3a0.)
    return {cosine, sine, 0, -sine, cosine, 0, 0, 0, 0x1000};
}

ClipVertex transform(const Matrix& c, std::int32_t x, std::int32_t y, std::int32_t z) {
    auto row = [&](int j) {
        return shift12(std::int64_t(x) * c[std::size_t(j)] +
                       std::int64_t(y) * c[std::size_t(4 + j)] +
                       std::int64_t(z) * c[std::size_t(8 + j)] +
                       std::int64_t(0x1000) * c[std::size_t(12 + j)]);
    };
    ClipVertex v;
    v.x = row(0);
    v.y = row(1);
    v.z = row(2);
    v.w = row(3);
    return v;
}

namespace {

std::int32_t& component(ClipVertex& v, int c) {
    return c == 0 ? v.x : c == 1 ? v.y : v.z;
}

// A new vertex where the edge from `in` (outside) to `out` (inside) meets
// the plane component = side * w.
ClipVertex cut(const ClipVertex& in, const ClipVertex& out, int c, int side) {
    ClipVertex a = in, b = out;
    const std::int64_t num = std::int64_t(a.w) - std::int64_t(side) * component(a, c);
    const std::int32_t den =
        std::int32_t(num - (std::int64_t(b.w) - std::int64_t(side) * component(b, c)));
    auto mix = [&](std::int32_t p, std::int32_t q) {
        return den == 0 ? p : std::int32_t(p + (std::int64_t(q) - p) * num / den);
    };
    ClipVertex r;
    r.x = mix(a.x, b.x);
    r.y = mix(a.y, b.y);
    r.z = mix(a.z, b.z);
    r.w = mix(a.w, b.w);
    component(r, c) = side * r.w;
    r.s = mix(a.s, b.s);
    r.t = mix(a.t, b.t);
    return r;
}

int clipPlane(std::array<ClipVertex, 10>& v, int n, int c) {
    std::array<ClipVertex, 10> temp;
    int count = 0;
    for (int i = 0; i < n; ++i) {
        ClipVertex& p = v[std::size_t(i)];
        if (component(p, c) > p.w) {
            if (c == 2) {
                return 0; // beyond the far plane (polygon attribute bit 12 off)
            }
            const ClipVertex& prev = v[std::size_t((i + n - 1) % n)];
            const ClipVertex& next = v[std::size_t((i + 1) % n)];
            ClipVertex q = prev, r = next;
            if (component(q, c) <= q.w && count < 10) {
                temp[std::size_t(count++)] = cut(p, prev, c, 1);
            }
            if (component(r, c) <= r.w && count < 10) {
                temp[std::size_t(count++)] = cut(p, next, c, 1);
            }
        } else if (count < 10) {
            temp[std::size_t(count++)] = p;
        }
    }
    n = count;
    count = 0;
    for (int i = 0; i < n; ++i) {
        ClipVertex& p = temp[std::size_t(i)];
        if (component(p, c) < -p.w) {
            ClipVertex prev = temp[std::size_t((i + n - 1) % n)];
            ClipVertex next = temp[std::size_t((i + 1) % n)];
            if (component(prev, c) >= -prev.w && count < 10) {
                v[std::size_t(count++)] = cut(p, prev, c, -1);
            }
            if (component(next, c) >= -next.w && count < 10) {
                v[std::size_t(count++)] = cut(p, next, c, -1);
            }
        } else if (count < 10) {
            v[std::size_t(count++)] = p;
        }
    }
    return count;
}

} // namespace

int clip(std::array<ClipVertex, 10>& v, int n) {
    bool inside = true;
    for (int i = 0; i < n; ++i) {
        const auto& p = v[std::size_t(i)];
        inside = inside && p.x <= p.w && p.x >= -p.w && p.y <= p.w && p.y >= -p.w && p.z <= p.w &&
                 p.z >= -p.w;
    }
    if (inside) {
        return n;
    }
    n = clipPlane(v, n, 2);
    n = clipPlane(v, n, 1);
    return clipPlane(v, n, 0);
}

ScreenVertex viewport(const ClipVertex& c) {
    ScreenVertex v;
    v.s = c.s;
    v.t = c.t;
    const std::int32_t w = c.w;
    if (w == 0) {
        return v;
    }
    std::uint32_t px = std::uint32_t(c.x + w), py = std::uint32_t(-c.y + w), den = std::uint32_t(w);
    if (std::uint32_t(w) > 0xffff) {
        px >>= 1;
        py >>= 1;
        den >>= 1;
    }
    den <<= 1;
    v.x = int((std::uint64_t(px) * 256 / den) & 0x1ff);
    v.y = int((std::uint64_t(py) * 192 / den) & 0xff);
    return v;
}

void rasterize(std::span<const ScreenVertex> v, const std::function<void(const Fragment&)>& put) {
    const int n = int(v.size());
    if (n < 3) {
        return;
    }
    int top = 0, bottom = 0;
    for (int i = 1; i < n; ++i) {
        if (v[std::size_t(i)].y < v[std::size_t(top)].y) {
            top = i;
        }
        if (v[std::size_t(i)].y > v[std::size_t(bottom)].y ||
            (v[std::size_t(i)].y == v[std::size_t(bottom)].y &&
             v[std::size_t(i)].x > v[std::size_t(bottom)].x)) {
            bottom = i;
        }
    }
    // Which way round the left edge goes from the top vertex: on the screen
    // (y down) a clockwise polygon continues to the right with the next
    // vertex.
    long area = 0;
    for (int i = 0; i < n; ++i) {
        const auto& a = v[std::size_t(i)];
        const auto& b = v[std::size_t((i + 1) % n)];
        area += long(a.x) * b.y - long(b.x) * a.y;
    }
    const int leftStep = area > 0 ? n - 1 : 1, rightStep = area > 0 ? 1 : n - 1;
    const int yTop = v[std::size_t(top)].y, yBottom = v[std::size_t(bottom)].y;

    Slope left, right;
    right.right = true;
    int curL = top, nextL = (top + leftStep) % n, curR = top, nextR = (top + rightStep) % n;
    int xl = 0, xr = 0;
    const bool flat = yTop == yBottom;
    if (flat) {
        int lo = 0, hi = 0;
        for (int i = 1; i < n; ++i) {
            if (v[std::size_t(i)].x < v[std::size_t(lo)].x) {
                lo = i;
            }
            if (v[std::size_t(i)].x > v[std::size_t(hi)].x) {
                hi = i;
            }
        }
        left.dummy(v[std::size_t(lo)].x, v[std::size_t(lo)].s, v[std::size_t(lo)].t);
        right.dummy(v[std::size_t(hi)].x, v[std::size_t(hi)].s, v[std::size_t(hi)].t);
        xl = left.x();
        xr = right.x();
    }
    auto advanceLeft = [&](int y) {
        while (y >= v[std::size_t(nextL)].y && curL != bottom) {
            curL = nextL;
            nextL = (nextL + leftStep) % n;
        }
        xl = left.setup(v[std::size_t(curL)], v[std::size_t(nextL)], y);
    };
    auto advanceRight = [&](int y) {
        while (y >= v[std::size_t(nextR)].y && curR != bottom) {
            curR = nextR;
            nextR = (nextR + rightStep) % n;
        }
        xr = right.setup(v[std::size_t(curR)], v[std::size_t(nextR)], y);
    };
    if (!flat) {
        advanceLeft(yTop);
        advanceRight(yTop);
    }
    const int yEnd = flat ? yTop + 1 : yBottom;
    for (int y = yTop; y < yEnd; ++y) {
        if (!flat && y != yTop) {
            if (y >= v[std::size_t(nextL)].y && curL != bottom) {
                advanceLeft(y);
            } else {
                xl = left.step();
            }
            if (y >= v[std::size_t(nextR)].y && curR != bottom) {
                advanceRight(y);
            } else {
                xr = right.step();
            }
        }
        int xstart = xl, xend = xr;
        // A vertical right edge is pushed a pixel left.
        if (right.increment == 0 && (left.increment != 0 || xstart != xend) && xend != 0) {
            --xend;
        }
        const Slope* first = &left;
        const Slope* second = &right;
        const bool swapped = xstart > xend;
        int lengthL = 1, lengthR = 1;
        std::int32_t coverL = 31, coverR = 31;
        if (swapped) {
            std::swap(xstart, xend);
            right.edge(true, lengthL, coverL);
            left.edge(true, lengthR, coverR);
            first = &right;
            second = &left;
        } else {
            left.edge(false, lengthL, coverL);
            right.edge(false, lengthR, coverR);
        }
        const int sl = first->s(), tl = first->t(), sr = second->s(), tr = second->t();
        const bool rowEdge = y == yTop || y == yBottom - 1;
        if (y < 0 || y >= 192) {
            continue;
        }
        Fragment f;
        f.y = y;
        const int spanStart = xstart, spanLength = xend + 1 - xstart;
        auto emit = [&](int x, int coverage) {
            f.x = x;
            f.s = std::int16_t(Slope::interpolate(sl, sr, x - spanStart, spanLength));
            f.t = std::int16_t(Slope::interpolate(tl, tr, x - spanStart, spanLength));
            f.coverage = coverage;
            put(f);
        };
        auto coverageAt = [](std::int32_t cover, int& run) {
            if (cover & std::int32_t(0x80000000u)) {
                const int c = std::min(run >> 5, 31);
                run += cover & 0x3ff;
                return c;
            }
            return int(cover);
        };
        auto runStart = [](std::int32_t cover) {
            if (!(cover & std::int32_t(0x80000000u))) {
                return 0;
            }
            const int r = (cover >> 12) & 0x3ff;
            return r == 0x3ff ? 0 : r;
        };
        int x = std::max(xstart, 0);
        // Left edge.
        int limit = std::min({xstart + lengthL, xend + 1, 256});
        int run = runStart(coverL);
        for (; x < limit; ++x) {
            emit(x, coverageAt(coverL, run));
        }
        // Inside (the top and bottom rows count as edges, fully covered).
        limit = std::min({xend - lengthR + 1, xend + 1, 256});
        for (; x < limit; ++x) {
            emit(x, 31);
        }
        (void)rowEdge;
        // Right edge.
        limit = std::min(xend + 1, 256);
        run = runStart(coverR);
        for (; x < limit; ++x) {
            emit(x, coverageAt(coverR, run));
        }
    }
}

SpriteCamera spriteCamera() {
    SpriteCamera camera;
    camera.projection =
        orthoW(0, 0xc0000, 0, 0x100000, std::int32_t(0xffc00000u), 0x400000, 0x400000);
    camera.view = identity();
    camera.view[14] = -0x1000; // looking from (0, 0, 1) at the origin
    camera.sine.resize(4096 * 2);
    for (std::size_t i = 0; i < 4096; ++i) {
        const double a = double(i) * 6.283185307179586 / 4096.0;
        camera.sine[2 * i] = std::int16_t(std::lround(std::sin(a) * 4096.0));
        camera.sine[2 * i + 1] = std::int16_t(std::lround(std::cos(a) * 4096.0));
    }
    return camera;
}

void drawPolygon(const SpriteCamera& camera, const Matrix& m,
                 std::span<const std::array<int, 4>> corners,
                 const std::function<void(const Fragment&)>& put) {
    const Matrix clipMatrix = multiply(m, camera.projection);
    std::array<ClipVertex, 10> c;
    for (std::size_t k = 0; k < corners.size(); ++k) {
        c[k] = transform(clipMatrix, corners[k][0], corners[k][1], 0x1000);
        c[k].s = corners[k][2];
        c[k].t = corners[k][3];
    }
    const int n = clip(c, int(corners.size()));
    std::array<ScreenVertex, 10> v;
    for (int k = 0; k < n; ++k) {
        v[std::size_t(k)] = viewport(c[std::size_t(k)]);
    }
    rasterize(std::span<const ScreenVertex>(v.data(), std::size_t(n)), put);
}

void drawTurnedChunk(const SpriteCamera& camera, int x, int y, unsigned turns, int frameWidth,
                     int frameHeight, int qx, int qy, int width, int height,
                     const std::function<void(const Fragment&, int u, int v)>& put) {
    Matrix m = camera.view;
    translate(m, x << 12, y << 12, 0);
    if (turns & 0xffff) {
        const std::size_t at = std::size_t((turns & 0xffff) >> 4) * 2;
        multiply3x3(m, rotationZ(camera.sine[at], camera.sine[at + 1]));
    }
    translate(m, (qx - (frameWidth >> 1)) << 12, (qy - (frameHeight >> 1)) << 12, 0);
    scale(m, width << 12, height << 12, 0x1000);
    const std::array<std::array<int, 4>, 4> corners{{{0, 0, 0, 0},
                                                     {0x1000, 0, width << 4, 0},
                                                     {0x1000, 0x1000, width << 4, height << 4},
                                                     {0, 0x1000, 0, height << 4}}};
    drawPolygon(camera, m, corners, [&](const Fragment& f) {
        put(f, std::clamp(f.s >> 4, 0, width - 1), std::clamp(f.t >> 4, 0, height - 1));
    });
}

} // namespace picarat::dspipe
