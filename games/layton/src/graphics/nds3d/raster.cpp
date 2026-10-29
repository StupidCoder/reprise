// The software rasterizer. See raster.hpp for the approach.
#include "graphics/nds3d/raster.hpp"
#include <algorithm>
#include <cmath>

namespace picarat::nds3d {

namespace {
constexpr int kWidth = 256;
constexpr int kHeight = 192;

// What the frame keeps per pixel while polygons are drawn.
struct Cell {
    RasterPixel color;
    RasterPixel below; // what was there before the opaque pixel (for anti-aliasing)
    std::uint32_t depth = 0;
    int opaqueId = 0;       // polygon ID of the opaque pixel (for outlines)
    int translucentId = -1; // polygon ID of the last translucent pixel, -1 none
    bool edge = false;      // the opaque pixel lies on its polygon's border
    float coverage = 1;     // how much of the edge pixel the polygon covers
};

RasterPixel fromRgb555(std::uint16_t c, int alpha) {
    return {std::uint8_t(widen(c & 31)), std::uint8_t(widen((c >> 5) & 31)),
            std::uint8_t(widen((c >> 10) & 31)), std::uint8_t(alpha)};
}

// Values interpolated across a polygon. Colour and texture coordinates are
// stored divided by w (perspective correction); `q` is 1/w.
struct Attributes {
    float q = 0, r = 0, g = 0, b = 0, s = 0, t = 0;
    float depth = 0;
};

Attributes lerp(const Attributes& a, const Attributes& b, float u) {
    auto mix = [u](float x, float y) {
        return x + (y - x) * u;
    };
    return {mix(a.q, b.q), mix(a.r, b.r), mix(a.g, b.g),        mix(a.b, b.b),
            mix(a.s, b.s), mix(a.t, b.t), mix(a.depth, b.depth)};
}

Attributes attributesOf(const RasterVertex& v) {
    const float q = 1.0f / v.w;
    return {q, v.r * q, v.g * q, v.b * q, v.s * q, v.t * q, float(v.depth)};
}

int clampColor(int v) {
    return std::clamp(v, 0, 63);
}

// The final colour of a fragment from vertex colour and texture.
bool shade(const RasterPolygon& p, const RasterSettings& settings, float vr, float vg, float vb,
           float s, float t, RasterPixel& out) {
    const int r5 = std::clamp(int(vr + 0.5f), 0, 31);
    const int g5 = std::clamp(int(vg + 0.5f), 0, 31);
    const int b5 = std::clamp(int(vb + 0.5f), 0, 31);
    RasterPixel vertex{std::uint8_t(widen(r5)), std::uint8_t(widen(g5)), std::uint8_t(widen(b5)),
                       std::uint8_t(p.alpha == 0 ? 31 : p.alpha)};
    if (p.shading == Shading::Toon || p.shading == Shading::Highlight) {
        const auto toon = fromRgb555(settings.toonTable[std::size_t(r5)], vertex.a);
        if (p.shading == Shading::Toon) {
            vertex = toon;
        }
    }
    if (!p.texture) {
        if (p.shading == Shading::Highlight) {
            const auto toon = fromRgb555(settings.toonTable[std::size_t(r5)], vertex.a);
            vertex = {std::uint8_t(clampColor(vertex.r + toon.r)),
                      std::uint8_t(clampColor(vertex.g + toon.g)),
                      std::uint8_t(clampColor(vertex.b + toon.b)), vertex.a};
        }
        out = vertex;
        return true;
    }
    const Texel texel = p.texture->sample(int(std::floor(s)), int(std::floor(t)));
    if (texel.a == 0) {
        return false; // transparent texel
    }
    auto modulate = [](int a, int b) {
        return ((a + 1) * (b + 1) - 1) / 64;
    };
    switch (p.shading) {
    case Shading::Decal:
        if (texel.a == 31) {
            out = {texel.r, texel.g, texel.b, vertex.a};
        } else {
            auto mix = [&](int tc, int vc) {
                return std::uint8_t((tc * texel.a + vc * (31 - texel.a)) / 31);
            };
            out = {mix(texel.r, vertex.r), mix(texel.g, vertex.g), mix(texel.b, vertex.b),
                   vertex.a};
        }
        return true;
    case Shading::Highlight: {
        const auto toon = fromRgb555(settings.toonTable[std::size_t(r5)], 31);
        const int grey = widen(r5);
        out = {std::uint8_t(clampColor(modulate(texel.r, grey) + toon.r)),
               std::uint8_t(clampColor(modulate(texel.g, grey) + toon.g)),
               std::uint8_t(clampColor(modulate(texel.b, grey) + toon.b)),
               std::uint8_t(((texel.a + 1) * (vertex.a + 1) - 1) / 32)};
        return true;
    }
    case Shading::Modulate:
    case Shading::Toon:
        out = {std::uint8_t(modulate(texel.r, vertex.r)), std::uint8_t(modulate(texel.g, vertex.g)),
               std::uint8_t(modulate(texel.b, vertex.b)),
               std::uint8_t(((texel.a + 1) * (vertex.a + 1) - 1) / 32)};
        return true;
    }
    return false;
}

class Frame {
public:
    explicit Frame(const RasterSettings& settings) : settings_(settings) {
        Cell clear;
        clear.color = fromRgb555(settings.clearColor, settings.clearAlpha);
        clear.depth = settings.clearDepth;
        clear.opaqueId = settings.clearId;
        cells_.assign(std::size_t(kWidth * kHeight), clear);
    }

    void draw(const RasterPolygon& polygon);
    std::vector<RasterPixel> resolve() const;

private:
    const RasterSettings& settings_;
    std::vector<Cell> cells_;

    void write(const RasterPolygon& p, int x, int y, const RasterPixel& c, std::uint32_t depth,
               bool edge, float coverage);
};

void Frame::draw(const RasterPolygon& p) {
    const auto& v = p.vertices;
    const std::size_t n = v.size();
    if (n < 3) {
        return;
    }
    // Orientation, so that edge distances are positive inside.
    float area = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const auto& a = v[i];
        const auto& b = v[(i + 1) % n];
        area += a.x * b.y - b.x * a.y;
    }
    if (area == 0) {
        return;
    }
    const float orientation = area > 0 ? 1.0f : -1.0f;

    float top = v[0].y, bottom = v[0].y;
    for (const auto& vertex : v) {
        top = std::min(top, vertex.y);
        bottom = std::max(bottom, vertex.y);
    }
    // Rows within [top, bottom], both ends included as on the DS.
    const int firstRow = std::max(0, int(std::ceil(top)));
    const int endRow = std::min(kHeight, int(std::floor(bottom)) + 1);

    // How far inside the polygon a point is, in pixels, measured against
    // the nearest edge (using the larger of the edge normal's components,
    // which matches the pixel square's coverage well).
    auto insideDistance = [&](float px, float py) {
        float nearest = 1e9f;
        for (std::size_t i = 0; i < n; ++i) {
            const auto& a = v[i];
            const auto& b = v[(i + 1) % n];
            const float ex = b.x - a.x;
            const float ey = b.y - a.y;
            const float norm = std::max(std::abs(ex), std::abs(ey));
            if (norm == 0) {
                continue;
            }
            const float d = orientation * (ex * (py - a.y) - ey * (px - a.x)) / norm;
            nearest = std::min(nearest, d);
        }
        return nearest;
    };

    for (int row = firstRow; row < endRow; ++row) {
        const float cy = float(row);
        // Intersect the polygon's edges with the row; the leftmost and
        // rightmost intersections bound the span.
        bool any = false;
        float xs[2]{};
        Attributes at[2];
        for (std::size_t i = 0; i < n; ++i) {
            const auto& a = v[i];
            const auto& b = v[(i + 1) % n];
            if (cy < std::min(a.y, b.y) || cy > std::max(a.y, b.y)) {
                continue;
            }
            float x;
            Attributes attributes;
            if (a.y == b.y) {
                // A horizontal edge on this row: both of its ends count.
                for (const auto* end : {&a, &b}) {
                    x = end->x;
                    attributes = attributesOf(*end);
                    if (!any || x < xs[0]) {
                        xs[0] = x;
                        at[0] = attributes;
                    }
                    if (!any || x > xs[1]) {
                        xs[1] = x;
                        at[1] = attributes;
                    }
                    any = true;
                }
                continue;
            }
            const float u = (cy - a.y) / (b.y - a.y);
            x = a.x + (b.x - a.x) * u;
            attributes = lerp(attributesOf(a), attributesOf(b), u);
            if (!any || x < xs[0]) {
                xs[0] = x;
                at[0] = attributes;
            }
            if (!any || x > xs[1]) {
                xs[1] = x;
                at[1] = attributes;
            }
            any = true;
        }
        if (!any) {
            continue;
        }
        // Pixels from the left to the right intersection, both included.
        const int firstX = std::max(0, int(std::ceil(xs[0])));
        const int endX = std::min(kWidth, int(std::floor(xs[1])) + 1);
        const float span = xs[1] - xs[0];
        for (int x = firstX; x < endX; ++x) {
            const float cx = float(x);
            const float u = span > 0 ? (cx - xs[0]) / span : 0.0f;
            const Attributes a = lerp(at[0], at[1], u);
            // An edge pixel has the polygon's border within one pixel of its
            // centre, i.e. some neighbouring pixel lies outside the polygon.
            const float inside = insideDistance(cx, cy);
            const bool edge = inside < 1.0f;
            if (p.alpha == 0 && !edge) {
                continue; // wireframe: outline only
            }
            const float w = a.q != 0 ? 1.0f / a.q : 0.0f;
            RasterPixel color;
            if (!shade(p, settings_, a.r * w, a.g * w, a.b * w, a.s * w, a.t * w, color)) {
                continue;
            }
            const float depth = p.wBuffer ? std::min(w, 16777215.0f) : a.depth;
            const float coverage = edge ? std::clamp(0.5f + inside, 0.0f, 1.0f) : 1;
            write(p, x, row, color, std::uint32_t(std::max(depth, 0.0f)), edge, coverage);
        }
    }
}

void Frame::write(const RasterPolygon& p, int x, int y, const RasterPixel& c, std::uint32_t depth,
                  bool edge, float coverage) {
    if (settings_.alphaTest && c.a <= settings_.alphaTestReference) {
        return;
    }
    Cell& cell = cells_[std::size_t(y * kWidth + x)];
    const bool passes =
        p.depthEqual ? (depth >= cell.depth ? depth - cell.depth : cell.depth - depth) <= 0x200
                     : depth < cell.depth;
    if (!passes) {
        return;
    }
    if (c.a == 31) {
        cell.color = c;
        cell.depth = depth;
        cell.opaqueId = p.id;
        cell.translucentId = -1;
        cell.edge = edge;
        cell.coverage = coverage;
        return;
    }
    // Translucent: a mesh does not blend over its own earlier pixels.
    if (cell.translucentId == p.id) {
        return;
    }
    RasterPixel out = c;
    if (settings_.alphaBlend && cell.color.a > 0) {
        auto mix = [&](int src, int dst) {
            return std::uint8_t((src * (c.a + 1) + dst * (31 - c.a)) / 32);
        };
        out = {mix(c.r, cell.color.r), mix(c.g, cell.color.g), mix(c.b, cell.color.b),
               std::max(c.a, cell.color.a)};
    }
    cell.color = out;
    cell.translucentId = p.id;
    if (p.updateDepthWhenTranslucent) {
        cell.depth = depth;
    }
}

std::vector<RasterPixel> Frame::resolve() const {
    std::vector<RasterPixel> out(cells_.size());
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            const Cell& cell = cells_[std::size_t(y * kWidth + x)];
            RasterPixel color = cell.color;
            if (cell.edge) {
                // A silhouette pixel: it borders a different polygon (or the
                // background) that lies behind it. Interior borders between
                // polygons of the same mesh are left alone.
                const Cell* behind = nullptr;
                static constexpr int kNeighbours[4][2]{{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
                for (const auto& d : kNeighbours) {
                    const int nx = x + d[0];
                    const int ny = y + d[1];
                    if (nx < 0 || ny < 0 || nx >= kWidth || ny >= kHeight) {
                        continue;
                    }
                    const Cell& other = cells_[std::size_t(ny * kWidth + nx)];
                    if (other.opaqueId != cell.opaqueId && other.depth > cell.depth) {
                        behind = &other;
                        break;
                    }
                }
                // Outline in the edge colour of the polygon's ID group.
                if (behind && settings_.edgeMarking) {
                    color =
                        fromRgb555(settings_.edgeColors[std::size_t(cell.opaqueId >> 3)], color.a);
                }
                // Anti-aliasing: mix a partly covered silhouette pixel with
                // the colour behind it.
                if (behind && settings_.antialias && cell.coverage < 1) {
                    const float c = cell.coverage;
                    const RasterPixel back = behind->color;
                    auto mix = [c](int front, int rear) {
                        return std::uint8_t(float(rear) + (float(front) - float(rear)) * c + 0.5f);
                    };
                    color = {mix(color.r, back.r), mix(color.g, back.g), mix(color.b, back.b),
                             mix(color.a, back.a)};
                }
            }
            out[std::size_t(y * kWidth + x)] = color;
        }
    }
    return out;
}
} // namespace

std::vector<RasterPixel> rasterize(const std::vector<RasterPolygon>& polygons,
                                   const RasterSettings& settings) {
    // Opaque polygons first, then translucent ones; each group from top to
    // bottom of the screen (by lowest, then highest point), keeping the
    // submission order for ties. Translucent polygons keep their submission
    // order when the game asks for manual sorting.
    std::vector<const RasterPolygon*> order;
    for (const auto& p : polygons) {
        if (!p.translucent) {
            order.push_back(&p);
        }
    }
    const std::size_t opaque = order.size();
    for (const auto& p : polygons) {
        if (p.translucent) {
            order.push_back(&p);
        }
    }
    auto extent = [](const RasterPolygon* p) {
        float top = 1e9f, bottom = -1e9f;
        for (const auto& v : p->vertices) {
            top = std::min(top, v.y);
            bottom = std::max(bottom, v.y);
        }
        return std::pair{bottom, top};
    };
    auto byHeight = [&](const RasterPolygon* a, const RasterPolygon* b) {
        return extent(a) < extent(b);
    };
    std::stable_sort(order.begin(), order.begin() + std::ptrdiff_t(opaque), byHeight);
    if (!settings.manualTranslucentSort) {
        std::stable_sort(order.begin() + std::ptrdiff_t(opaque), order.end(), byHeight);
    }
    Frame frame(settings);
    for (const auto* p : order) {
        frame.draw(*p);
    }
    return frame.resolve();
}

} // namespace picarat::nds3d
