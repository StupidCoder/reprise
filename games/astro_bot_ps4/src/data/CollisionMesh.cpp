#include "data/CollisionMesh.hpp"

#include <cstring>

namespace beacon::data {

namespace {

class Reader {
public:
    explicit Reader(ByteView b) : b_(b) {}
    bool ok(std::size_t n) const { return at_ + n <= b_.size(); }
    std::uint8_t u8() { return b_[at_++]; }
    std::uint16_t u16() {
        std::uint16_t v = read<std::uint16_t>(b_, at_);
        at_ += 2;
        return v;
    }
    math::Vec3 vec3() {
        math::Vec3 v;
        v.x = f32();
        v.y = f32();
        v.z = f32();
        return v;
    }
    std::uint32_t u32() {
        std::uint32_t v = read<std::uint32_t>(b_, at_);
        at_ += 4;
        return v;
    }
    float f32() {
        float v = read<float>(b_, at_);
        at_ += 4;
        return v;
    }
    void skip(std::size_t n) { at_ += n; }
    std::size_t at() const { return at_; }
    void seek(std::size_t a) { at_ = a; }

private:
    ByteView b_;
    std::size_t at_ = 0;
};

MeshTreeNode readNode(Reader& r) {
    MeshTreeNode n;
    for (auto& q : n.minStep) {
        q = r.u8();
    }
    for (auto& q : n.maxStep) {
        q = r.u8();
    }
    n.childKinds = r.u8();
    n.left = r.u16();
    n.right = r.u16();
    return n;
}

}   // namespace

bool decodeLargeMesh(ByteView blob, LargeMesh& out, std::string* error) {
    auto fail = [&](const char* m) {
        if (error) {
            *error = m;
        }
        return false;
    };
    if (blob.size() < 0x48 || std::memcmp(blob.data(), "PELM", 4) != 0 || blob[4] != 0 || blob[5] != 7) {
        return fail("not a serialized collision mesh");
    }
    if (blob[6] != 2) {
        return fail("unsupported collision mesh layout");
    }
    Reader r(blob);
    r.seek(7);
    out.centre = r.vec3();
    out.halfExtent = r.vec3();
    const std::uint32_t islands = r.u32();
    r.skip(16);
    if (r.u32() != blob.size()) {
        return fail("collision mesh size mismatch");
    }
    r.seek(0x40);
    const std::uint32_t treeBytes = r.u32();
    const std::uint32_t nodes = r.u32();
    if (8 + nodes * 11u != treeBytes || !r.ok(nodes * 11u)) {
        return fail("bad mesh tree");
    }
    out.tree.clear();
    for (std::uint32_t i = 0; i < nodes; ++i) {
        out.tree.push_back(readNode(r));
    }
    out.islands.assign(islands, {});
    for (MeshIsland& isl : out.islands) {
        if (!r.ok(4 + 3 + 24)) {
            return fail("truncated island");
        }
        r.u32();
        const unsigned vertexCount = r.u8(), edgeCount = r.u8(), facetCount = r.u8();
        isl.min = r.vec3();
        isl.max = r.vec3();
        const unsigned treeCount = facetCount > 1 ? facetCount - 1 : 1;
        if (!r.ok(vertexCount * 12u + edgeCount * 4u + facetCount * 26u + 9u * treeCount)) {
            return fail("truncated island data");
        }
        for (unsigned v = 0; v < vertexCount; ++v) {
            isl.vertices.push_back(r.vec3());
        }
        for (unsigned e = 0; e < edgeCount; ++e) {
            MeshEdge edge;
            for (auto& b : edge.raw) {
                b = r.u8();
            }
            isl.edges.push_back(edge);
        }
        for (unsigned f = 0; f < facetCount; ++f) {
            MeshFacet facet;
            facet.normal = r.vec3();
            facet.thickness = r.f32();
            for (auto& v : facet.vertex) {
                v = r.u8();
                if (v >= vertexCount) {
                    return fail("facet vertex out of range");
                }
            }
            for (auto& e : facet.edge) {
                e = r.u8();
                if (e >= edgeCount) {
                    return fail("facet edge out of range");
                }
            }
            facet.user = r.u32();
            isl.facets.push_back(facet);
        }
        // Island tree nodes: u8 min steps[3], u8 max steps[3], u8 child kinds, u8 left, u8 right.
        for (unsigned n = 0; n < treeCount; ++n) {
            MeshTreeNode node;
            for (auto& q : node.minStep) {
                q = r.u8();
            }
            for (auto& q : node.maxStep) {
                q = r.u8();
            }
            node.childKinds = r.u8();
            node.left = r.u8();
            node.right = r.u8();
            isl.tree.push_back(node);
        }
    }
    if (r.at() != blob.size()) {
        return fail("collision mesh not fully consumed");
    }
    return true;
}

bool decodeCollisionMesh(ByteView blob, TriangleSoup& out, std::string* error) {
    auto fail = [&](const char* m) {
        if (error) {
            *error = m;
        }
        return false;
    };
    if (blob.size() < 0x44 || std::memcmp(blob.data(), "PELM", 4) != 0 || blob[4] != 0 || blob[5] != 7) {
        return fail("not a serialized collision mesh");
    }
    if (blob[6] != 2) {
        return fail("unsupported collision mesh layout");
    }
    Reader r(blob);
    r.seek(7 + 24);
    const std::uint32_t islands = r.u32();
    r.skip(16);
    if (r.u32() != blob.size()) {
        return fail("collision mesh size mismatch");
    }
    r.seek(0x40);
    const std::uint32_t treeBytes = r.u32();
    r.seek(0x40 + treeBytes);
    for (std::uint32_t i = 0; i < islands; ++i) {
        if (!r.ok(4 + 3 + 24)) {
            return fail("truncated island");
        }
        r.u32();
        const unsigned vertexCount = r.u8(), edgeCount = r.u8(), facetCount = r.u8();
        r.skip(24);   // island bounds
        if (!r.ok(vertexCount * 12u + edgeCount * 4u + facetCount * 26u + 9u * (facetCount > 1 ? facetCount - 1 : 1))) {
            return fail("truncated island data");
        }
        const std::uint32_t base = std::uint32_t(out.vertices.size());
        for (unsigned v = 0; v < vertexCount; ++v) {
            math::Vec3 p;
            p.x = r.f32();
            p.y = r.f32();
            p.z = r.f32();
            out.vertices.push_back(p);
        }
        r.skip(edgeCount * 4u);
        for (unsigned f = 0; f < facetCount; ++f) {
            r.skip(16);   // facet normal and a float (open)
            std::uint8_t v[3] = {r.u8(), r.u8(), r.u8()};
            r.skip(3 + 4);   // edge ids, user data
            for (std::uint8_t idx : v) {
                if (idx >= vertexCount) {
                    return fail("facet vertex out of range");
                }
                out.indices.push_back(base + idx);
            }
        }
        r.skip(9u * (facetCount > 1 ? facetCount - 1 : 1));
    }
    if (r.at() != blob.size()) {
        return fail("collision mesh not fully consumed");
    }
    return true;
}

}   // namespace data
