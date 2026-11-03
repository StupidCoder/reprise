// The large-mesh builder 0xde8550 with the parameters the module passes (flags 0x2c: 32-bit
// indices, automatic elimination, layout 2). Findings in .analysis/docs/physics.md ("Run-time
// meshes"); the stages:
//   facets      0xdedc50: triangles whose doubled area squared is below 1e-10 dropped; normal
//               (p2 - p1) x (p0 - p1) normalized; box of the corners and the corners moved
//               down by the thickness;
//   vertices    merged (later into earlier) closer than epsilon (squared distance < eps^2);
//   edges       shared by at most two facets, found through a hash of the vertex pair;
//   edge type   per edge with two facets: both centroids moved along the edge to the plane
//               through its first point, their mean m; dot(n0, m - c0) and dot(n1, m - c1)
//               both below -1e-5: convex (1), both above 1e-5: concave (2), else flat (0);
//               open edges convex; edge angle = acos(n0 . n1) / 2 (above 1.57: zero, and the
//               two facets lose their thickness);
//   facet box   with a convex edge of nonzero angle: the facet's solid below it with side
//               planes tilted down by the edges' angles, cut at the thickness;
//   islands     0xdf07b0 / 0xdf0e50: facets halved at the median along the axis in which
//               their box centres vary most until at most the facet limit remain.
#include "physics/rigid/MeshBuilder.hpp"

#include "math/Vector.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace beacon::physics::rigid {

namespace {

using math::Vec3;

struct BuildVertex {
    Vec3 p;
    bool merged = false;
    std::vector<int> facets;   // facets using it (before merging)
};

struct BuildEdge {
    int vmin = 0, vmax = 0;
    int slot[2] = {0, 0};    // the edge's slot in each facet
    int facet[2] = {0, 0};
    int facetCount = 1;
    std::uint8_t type = 1;   // 0 flat, 1 convex, 2 concave
    float angle = 0;
    int next = -1;           // hash chain
};

struct BuildFacet {
    int v[3] = {0, 0, 0};
    int e[3] = {-1, -1, -1};
    float thickness = 0, area = 0;
    std::uint32_t user = 0;
    Vec3 n, min, max;
};

// SSE min / max as the library uses them: the second operand when either is not a number.
float minps(float a, float b) { return a < b ? a : b; }
float maxps(float a, float b) { return a > b ? a : b; }
Vec3 minps(const Vec3& a, const Vec3& b) { return {minps(a.x, b.x), minps(a.y, b.y), minps(a.z, b.z)}; }
Vec3 maxps(const Vec3& a, const Vec3& b) { return {maxps(a.x, b.x), maxps(a.y, b.y), maxps(a.z, b.z)}; }

// Unit vector as the builder makes it: an estimate of 1/sqrt (here the exact value; the hardware's
// is a 12-bit approximation) refined by two Newton steps, the second folded into the scaling;
// squared length summed (x + y) + z.
Vec3 unit(const Vec3& v) {
    const float l2 = (v.x * v.x + v.y * v.y) + v.z * v.z;
    const float r0 = 1.0f / std::sqrt(l2);
    const float r1 = r0 * 0.5f * (3.0f - r0 * l2 * r0);
    const float t = 3.0f - r1 * l2 * r1;
    return {v.x * r1 * 0.5f * t, v.y * r1 * 0.5f * t, v.z * r1 * 0.5f * t};
}

// a / b as the builder divides: the reciprocal refined by one Newton step, the quotient by
// another.
float quotient(float a, float b) {
    const float r0 = 1.0f / b;
    const float r1 = (r0 + r0) - r0 * b * r0;
    const float q = a * r1;
    return (q + q) - q * b * r1;
}

// The third of a sum (the facet centroid): x (2 r - 3 r^2 refined) ... as 2 x r - x r (3 r).
float third(float sum) {
    const float r0 = 1.0f / 3.0f;
    const float r1 = (r0 + r0) - r0 * r0 * 3.0f;
    const float x = sum * r1;
    return (x + x) - x * (r1 * 3.0f);
}

// The order-preserving integer of a float (the islands' radix sort keys).
std::uint32_t sortKey(float f) {
    std::uint32_t u;
    std::memcpy(&u, &f, 4);
    return u ^ ((u >> 31) ? 0xffffffffu : 0x80000000u);
}

bool fail(std::string* error, const char* message) {
    if (error) *error = message;
    return false;
}

// The facet box with tilted sides (0xdedc50, last pass): side plane i through corner i with
// normal (edge i x n) cos(angle i) - n sin(angle i); the three meet at an apex; the bottom
// corners are the corners moved towards it down to the thickness (or the apex when nearer).
void tiltedBox(BuildFacet& f, const std::vector<BuildVertex>& verts, const std::vector<BuildEdge>& edges) {
    const Vec3 p[3] = {verts[std::size_t(f.v[0])].p, verts[std::size_t(f.v[1])].p, verts[std::size_t(f.v[2])].p};
    Vec3 s[3];
    float d[3];
    for (int i = 0; i < 3; ++i) {
        const float a = edges[std::size_t(f.e[i])].angle;
        s[i] = unit(math::cross(p[(i + 1) % 3] - p[i], f.n)) * std::cos(a) - f.n * std::sin(a);
        d[i] = math::dot(p[i], s[i]);
    }
    const Vec3 c12 = math::cross(s[1], s[2]), c20 = math::cross(s[2], s[0]), c01 = math::cross(s[0], s[1]);
    const float r = 1.0f / math::dot(s[0], c12);
    const Vec3 apex = c12 * (d[0] * r) + c20 * (d[1] * r) + c01 * (d[2] * r);
    const float depth = -math::dot(apex - p[0], f.n);
    Vec3 b[3] = {apex, apex, apex};
    if (f.thickness < depth) {
        const float k = f.thickness / depth;
        for (int i = 0; i < 3; ++i) b[i] = p[i] + (apex - p[i]) * k;
    }
    f.min = minps(b[2], minps(b[1], minps(b[0], minps(p[2], minps(p[1], p[0])))));
    f.max = maxps(b[2], maxps(b[1], maxps(b[0], maxps(p[2], maxps(p[1], p[0])))));
}

// A node of the island division (0xdf0e50; 0x50 bytes there): its facets' box, its halves, or
// the island it became.
struct DivisionNode {
    Vec3 min, max;
    int child[2] = {-1, -1};   // lower, upper half
    int island = -1;
};

// 0xdf0e50: divides `node` (its facets in list order) until at most `limit` facets remain.
void divide(int node, std::vector<int> facets, const std::vector<BuildFacet>& all, int limit,
            std::vector<DivisionNode>& nodes, std::vector<std::vector<int>>& islands) {
    while (true) {
        if (int(facets.size()) <= limit) {
            nodes[std::size_t(node)].island = int(islands.size());
            islands.push_back(std::move(facets));
            return;
        }
        Vec3 sum, sumSq;
        for (int i : facets) {
            const Vec3 c = (all[std::size_t(i)].min + all[std::size_t(i)].max) * 0.5f;
            sum = sum + c;
            sumSq = sumSq + Vec3{c.x * c.x, c.y * c.y, c.z * c.z};
        }
        const float n = float(facets.size());
        const Vec3 var = sumSq - Vec3{sum.x * sum.x, sum.y * sum.y, sum.z * sum.z} * (1.0f / n);
        const int a = var.x < var.y ? 1 : 0;
        const int axis = var.z <= (a ? var.y : var.x) ? a : 2;
        std::vector<std::pair<std::uint32_t, int>> keys;
        keys.reserve(facets.size());
        for (int i : facets) {
            const Vec3 c = (all[std::size_t(i)].min + all[std::size_t(i)].max) * 0.5f;
            keys.emplace_back(sortKey(axis == 0 ? c.x : axis == 1 ? c.y : c.z), i);
        }
        std::stable_sort(keys.begin(), keys.end(),
                         [](const auto& l, const auto& r) { return l.first < r.first; });
        // Each half is built by pushing at the head of its list: reversed.
        const std::size_t half = facets.size() / 2;
        std::vector<int> lower, upper;
        for (std::size_t k = half; k-- > 0;) lower.push_back(keys[k].second);
        for (std::size_t k = keys.size(); k-- > half;) upper.push_back(keys[k].second);
        const int c0 = int(nodes.size()), c1 = c0 + 1;
        nodes.resize(nodes.size() + 2);
        nodes[std::size_t(node)].child[0] = c0;
        nodes[std::size_t(node)].child[1] = c1;
        for (int k = 0; k < 2; ++k) {
            DivisionNode& c = nodes[std::size_t(k ? c1 : c0)];
            c.min = Vec3{1e38f, 1e38f, 1e38f};
            c.max = Vec3{-1e38f, -1e38f, -1e38f};
            for (int i : k ? upper : lower) {
                c.min = minps(c.min, all[std::size_t(i)].min);
                c.max = maxps(c.max, all[std::size_t(i)].max);
            }
        }
        if (lower.size() < upper.size()) {
            if (!lower.empty()) divide(c0, std::move(lower), all, limit, nodes, islands);
            node = c1;
            facets = std::move(upper);
        } else {
            if (!upper.empty()) divide(c1, std::move(upper), all, limit, nodes, islands);
            node = c0;
            facets = std::move(lower);
        }
        if (facets.empty()) return;
    }
}

// A child's box in quantized steps of 1/255 of its parent's (floored; the island trees one step
// more); the child's box becomes the dequantized one, which its own children are measured in.
void quantize(Vec3& min, Vec3& max, const Vec3& parentMin, const Vec3& parentMax, bool extraStep,
              std::array<std::uint8_t, 3>& minStep, std::array<std::uint8_t, 3>& maxStep) {
    const float pmin[3] = {parentMin.x, parentMin.y, parentMin.z}, pmax[3] = {parentMax.x, parentMax.y, parentMax.z};
    float cmin[3] = {min.x, min.y, min.z}, cmax[3] = {max.x, max.y, max.z};
    for (int a = 0; a < 3; ++a) {
        const float ext = pmax[a] - pmin[a];
        auto step = [&](float d) {
            int q = int(std::floor(minps(maxps(0.0f, quotient(d, ext)), 1.0f) * 255.0f)) & 0xff;
            if (extraStep && q != 0) --q;
            return std::uint8_t(q);
        };
        // s / 255 as each tree computes it (the reciprocal refined once, then the quotient).
        const float r0 = 1.0f / 255.0f, r1 = (r0 + r0) - r0 * r0 * 255.0f;
        auto fraction = [&](std::uint8_t s) {
            const float x = float(s) * r1;
            return extraStep ? (x + x) - x * r1 * 255.0f : (x + x) - x * (r1 * 255.0f);
        };
        minStep[std::size_t(a)] = step(cmin[a] - pmin[a]);
        maxStep[std::size_t(a)] = step(pmax[a] - cmax[a]);
        cmin[a] = pmin[a] + ext * fraction(minStep[std::size_t(a)]);
        cmax[a] = pmax[a] - ext * fraction(maxStep[std::size_t(a)]);
    }
    min = {cmin[0], cmin[1], cmin[2]};
    max = {cmax[0], cmax[1], cmax[2]};
}

// A binary tree written depth first, the upper (right) child before the lower: node boxes
// quantized against their parents'.
struct TreeNode {
    Vec3 min, max;
    int child[2] = {-1, -1};   // tree nodes
    int leaf[2] = {-1, -1};    // or leaves
    int parent = -1;
};

std::vector<data::MeshTreeNode> writeTree(std::vector<TreeNode>& nodes, int root, bool extraStep) {
    std::vector<data::MeshTreeNode> out;
    std::vector<int> outIndex(nodes.size(), -1);
    std::vector<int> stack{root};
    while (!stack.empty()) {
        const int n = stack.back();
        stack.pop_back();
        TreeNode& node = nodes[std::size_t(n)];
        outIndex[std::size_t(n)] = int(out.size());
        data::MeshTreeNode o;
        if (node.parent >= 0) {
            const TreeNode& p = nodes[std::size_t(node.parent)];
            quantize(node.min, node.max, p.min, p.max, extraStep, o.minStep, o.maxStep);
            data::MeshTreeNode& po = out[std::size_t(outIndex[std::size_t(node.parent)])];
            (p.child[0] == n ? po.left : po.right) = std::uint16_t(out.size());
        }
        for (int k = 0; k < 2; ++k) {
            if (node.leaf[k] >= 0) {
                o.childKinds = std::uint8_t(o.childKinds | (1 << (2 * k)));
                (k ? o.right : o.left) = std::uint16_t(node.leaf[k]);
            } else {
                stack.push_back(node.child[k]);
            }
        }
        out.push_back(o);
    }
    return out;
}

// The island's tree (0xde8550): facets as leaves, repeatedly the two nodes whose joint box has
// the smallest x + y + z extent joined (first such pair in list order; the joined pair leaves the
// list, the higher place filled from the end first, and the new node is appended).
std::vector<data::MeshTreeNode> islandTree(const std::vector<int>& list, const std::vector<BuildFacet>& all) {
    if (list.size() == 1) {
        data::MeshTreeNode single;
        single.childKinds = 9;   // left: facet 0, right: none
        return {single};
    }
    std::vector<TreeNode> nodes(list.size());
    for (std::size_t i = 0; i < list.size(); ++i) {
        nodes[i].min = all[std::size_t(list[i])].min;
        nodes[i].max = all[std::size_t(list[i])].max;
    }
    std::vector<int> active(list.size());
    for (std::size_t i = 0; i < list.size(); ++i) active[i] = int(i);
    while (active.size() > 1) {
        float best = 1e38f;
        std::size_t bi = 0, bj = 0;
        for (std::size_t i = 0; i + 1 < active.size(); ++i)
            for (std::size_t j = i + 1; j < active.size(); ++j) {
                const TreeNode& a = nodes[std::size_t(active[i])];
                const TreeNode& b = nodes[std::size_t(active[j])];
                const Vec3 e = maxps(a.max, b.max) - minps(a.min, b.min);
                const float size = e.z + e.y + e.x;
                if (size < best) {
                    best = size;
                    bi = i;
                    bj = j;
                }
            }
        TreeNode joined;
        const int l = active[bi], r = active[bj];
        joined.min = minps(nodes[std::size_t(l)].min, nodes[std::size_t(r)].min);
        joined.max = maxps(nodes[std::size_t(l)].max, nodes[std::size_t(r)].max);
        joined.child[0] = l;
        joined.child[1] = r;
        const int id = int(nodes.size());
        nodes[std::size_t(l)].parent = id;
        nodes[std::size_t(r)].parent = id;
        nodes.push_back(joined);
        active[bj] = active.back();
        active.pop_back();
        if (bi < active.size()) {
            active[bi] = active.back();
            active.pop_back();
        }
        active.push_back(id);
    }
    // Leaves (the first list.size() nodes) are facets.
    for (TreeNode& n : nodes)
        for (int k = 0; k < 2; ++k)
            if (n.child[k] >= 0 && n.child[k] < int(list.size())) {
                n.leaf[k] = n.child[k];
            }
    return writeTree(nodes, active[0], true);
}

}   // namespace

bool buildLargeMesh(const std::vector<Vec3>& vertices, const std::vector<std::uint32_t>& indices,
                    data::LargeMesh& out, std::string* error, const MeshBuildParams& params) {
    out = {};
    const std::size_t triangleCount = indices.size() / 3;
    if (vertices.empty() || triangleCount == 0) return fail(error, "no vertices or triangles (0x80880001)");
    for (std::uint32_t i : indices)
        if (i >= vertices.size()) return fail(error, "index out of range (0x80880009)");
    const float eps2 = params.epsilon * params.epsilon;

    std::vector<BuildVertex> verts(vertices.size());
    for (std::size_t i = 0; i < vertices.size(); ++i) verts[i].p = vertices[i];

    // Facets.
    std::vector<BuildFacet> facets;
    for (std::size_t t = 0; t < triangleCount; ++t) {
        BuildFacet f;
        for (int k = 0; k < 3; ++k) f.v[k] = int(indices[t * 3 + std::size_t(k)]);
        const Vec3 p0 = vertices[std::size_t(f.v[0])], p1 = vertices[std::size_t(f.v[1])],
                   p2 = vertices[std::size_t(f.v[2])];
        const Vec3 c = math::cross(p1 - p0, p2 - p0);
        f.area = math::dot(c, c);
        if (!(f.area >= 1e-10f) || !(f.area >= eps2)) continue;
        f.n = unit(math::cross(p2 - p1, p0 - p1));
        f.thickness = params.thickness;
        const Vec3 down = f.n * f.thickness;
        f.min = minps(p2 - down, minps(p1 - down, minps(p0 - down, minps(p2, minps(p1, p0)))));
        f.max = maxps(p2 - down, maxps(p1 - down, maxps(p0 - down, maxps(p2, maxps(p1, p0)))));
        facets.push_back(f);
    }
    if (facets.empty()) return fail(error, "every triangle eliminated (0x8088000d)");
    for (std::size_t i = 0; i < facets.size(); ++i)
        for (int k = 0; k < 3; ++k) verts[std::size_t(facets[i].v[k])].facets.push_back(int(i));

    // Merge coincident vertices into the earliest.
    for (std::size_t i = 0; i < verts.size(); ++i) {
        if (verts[i].merged) continue;
        if (verts[i].facets.empty()) {
            verts[i].merged = true;
            continue;
        }
        for (std::size_t j = i + 1; j < verts.size(); ++j) {
            if (verts[j].merged) continue;
            const Vec3 d = verts[i].p - verts[j].p;
            if (!(math::dot(d, d) < eps2)) continue;
            verts[j].merged = true;
            for (int fi : verts[j].facets) {
                BuildFacet& f = facets[std::size_t(fi)];
                for (int& v : f.v)
                    if (v == int(j)) {
                        v = int(i);
                        break;
                    }
            }
            verts[i].facets.insert(verts[i].facets.end(), verts[j].facets.begin(), verts[j].facets.end());
        }
    }

    // Edges.
    std::vector<BuildEdge> edges;
    const std::uint32_t buckets = std::uint32_t(facets.size() * 3);
    std::vector<int> head(buckets, -1);
    for (std::size_t fi = 0; fi < facets.size(); ++fi) {
        BuildFacet& f = facets[fi];
        for (int s = 0; s < 3; ++s) {
            const int a = f.v[s], b = f.v[(s + 1) % 3];
            const int lo = std::min(a, b), hi = std::max(a, b);
            const std::uint32_t key = (std::uint32_t(hi) * 0xd8163841u + std::uint32_t(lo) * 0x8da6b343u) % buckets;
            int found = -1;
            for (int e = head[key]; e >= 0; e = edges[std::size_t(e)].next) {
                const BuildEdge& x = edges[std::size_t(e)];
                if (x.vmin == lo && x.vmax == hi && x.facetCount == 1) {
                    found = e;
                    break;
                }
            }
            if (found >= 0) {
                BuildEdge& x = edges[std::size_t(found)];
                x.facet[1] = int(fi);
                x.slot[1] = s;
                x.facetCount = 2;
                f.e[s] = found;
                continue;
            }
            BuildEdge x;
            x.vmin = lo;
            x.vmax = hi;
            x.facet[0] = int(fi);
            x.slot[0] = s;
            x.next = head[key];
            head[key] = int(edges.size());
            f.e[s] = int(edges.size());
            edges.push_back(x);
        }
    }

    // Edge types and angles.
    for (BuildEdge& e : edges) {
        if (e.facetCount != 2) continue;
        const BuildFacet& f0 = facets[std::size_t(e.facet[0])];
        const BuildFacet& f1 = facets[std::size_t(e.facet[1])];
        const Vec3 p = verts[std::size_t(f0.v[e.slot[0]])].p;
        const Vec3 dir = unit(verts[std::size_t(f0.v[(e.slot[0] + 1) % 3])].p - p);
        auto onPlane = [&](const BuildFacet& f) {
            const Vec3 sum = verts[std::size_t(f.v[1])].p + verts[std::size_t(f.v[0])].p + verts[std::size_t(f.v[2])].p;
            const Vec3 c{third(sum.x), third(sum.y), third(sum.z)};
            return c - dir * math::dot(dir, c - p);
        };
        const Vec3 c0 = onPlane(f0), c1 = onPlane(f1);
        const Vec3 mid = (c0 + c1) * 0.5f;
        const float chk0 = math::dot(f0.n, mid - c0), chk1 = math::dot(f1.n, mid - c1);
        e.angle = std::acos(std::min(1.0f, std::max(-1.0f, math::dot(f0.n, f1.n)))) * 0.5f;
        e.type = 0;
        if (chk0 < -1e-5f && chk1 < -1e-5f) e.type = 1;
        else if (chk0 > 1e-5f && chk1 > 1e-5f) e.type = 2;
    }
    for (BuildEdge& e : edges) {
        if (!(e.angle > 1.57f)) continue;
        e.angle = 0;
        for (int k = 0; k < e.facetCount; ++k) facets[std::size_t(e.facet[k])].thickness = 0;
    }

    // Facet boxes, again: tilted where a convex edge has an angle.
    Vec3 rootMin{1e38f, 1e38f, 1e38f}, rootMax{-1e38f, -1e38f, -1e38f};
    for (BuildFacet& f : facets) {
        bool tilted = false;
        for (int k = 0; k < 3; ++k) {
            const BuildEdge& e = edges[std::size_t(f.e[k])];
            tilted = tilted || (e.type == 1 && e.angle > 0);
        }
        if (tilted) {
            tiltedBox(f, verts, edges);
        } else {
            const Vec3 p0 = verts[std::size_t(f.v[0])].p, p1 = verts[std::size_t(f.v[1])].p,
                       p2 = verts[std::size_t(f.v[2])].p;
            const Vec3 down = f.n * f.thickness;
            f.min = minps(p2 - down, minps(p1 - down, minps(p0 - down, minps(p2, minps(p1, p0)))));
            f.max = maxps(p2 - down, maxps(p1 - down, maxps(p0 - down, maxps(p2, maxps(p1, p0)))));
        }
        rootMin = minps(rootMin, f.min);
        rootMax = maxps(rootMax, f.max);
    }

    // Islands.
    std::vector<int> all;
    for (std::size_t i = facets.size(); i-- > 0;) all.push_back(int(i));
    std::vector<DivisionNode> division(1);
    division[0].min = rootMin;
    division[0].max = rootMax;
    std::vector<std::vector<int>> islands;
    divide(0, std::move(all), facets, params.facetLimit, division, islands);

    out.centre = (rootMin + rootMax) * 0.5f;
    out.halfExtent = (rootMax - rootMin) * 0.5f;
    for (const std::vector<int>& list : islands) {
        data::MeshIsland is;
        is.min = Vec3{1e38f, 1e38f, 1e38f};
        is.max = Vec3{-1e38f, -1e38f, -1e38f};
        std::vector<int> local(verts.size(), -1);
        std::vector<int> localEdge(edges.size(), -1);
        for (int fi : list) {
            const BuildFacet& f = facets[std::size_t(fi)];
            data::MeshFacet o;
            o.normal = f.n;
            o.thickness = f.thickness;
            o.user = f.user;
            for (int k = 0; k < 3; ++k) {
                int& l = local[std::size_t(f.v[k])];
                if (l < 0) {
                    l = int(is.vertices.size());
                    is.vertices.push_back(verts[std::size_t(f.v[k])].p);
                }
                o.vertex[std::size_t(k)] = std::uint8_t(l);
            }
            for (int k = 0; k < 3; ++k) {
                int& l = localEdge[std::size_t(f.e[k])];
                if (l < 0) {
                    l = int(is.edges.size());
                    const BuildEdge& e = edges[std::size_t(f.e[k])];
                    data::MeshEdge me;
                    const std::uint8_t a = std::uint8_t(local[std::size_t(e.vmin)]), b = std::uint8_t(local[std::size_t(e.vmax)]);
                    me.raw = {std::min(a, b), std::max(a, b), e.type, 0};
                    is.edges.push_back(me);
                }
                o.edge[std::size_t(k)] = std::uint8_t(l);
            }
            is.min = minps(is.min, f.min);
            is.max = maxps(is.max, f.max);
            is.facets.push_back(o);
        }
        is.tree = islandTree(list, facets);
        out.islands.push_back(std::move(is));
    }

    // The mesh tree: the division (lower half left), islands as leaves.
    if (islands.size() == 1) {
        data::MeshTreeNode single;
        single.childKinds = 9;
        out.tree = {single};
    } else {
        std::vector<TreeNode> nodes(division.size());
        for (std::size_t i = 0; i < division.size(); ++i) {
            nodes[i].min = division[i].min;
            nodes[i].max = division[i].max;
            for (int k = 0; k < 2; ++k) {
                const int c = division[i].child[k];
                if (c < 0) continue;
                nodes[std::size_t(c)].parent = int(i);
                if (division[std::size_t(c)].island >= 0) nodes[i].leaf[k] = division[std::size_t(c)].island;
                nodes[i].child[k] = c;
            }
        }
        out.tree = writeTree(nodes, 0, false);
    }
    return true;
}

}   // namespace beacon::physics::rigid
