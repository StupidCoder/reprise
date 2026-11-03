// The quantized trees of the physics library's large meshes (mesh tree over islands, island tree
// over facets; data in data/CollisionMesh.hpp) as the contact and cast routines walk them
// (findings in .analysis/docs/physics.md, "Mesh contacts").
#pragma once

#include "data/CollisionMesh.hpp"
#include "math/Vector.hpp"

#include <cstddef>
#include <vector>

namespace beacon::physics::rigid::meshtree {

using math::Vec3;

constexpr float kStep = 1.0f / 255.0f;      // tree boxes (the library takes an estimate of 1/255)
constexpr std::size_t kMaxStack = 100;      // tree stacks (asserted)

struct Box {
    Vec3 min, max;
};

inline bool apart(const Box& a, const Box& b) {
    return a.max.x < b.min.x || a.max.y < b.min.y || a.max.z < b.min.z || b.max.x < a.min.x ||
           b.max.y < a.min.y || b.max.z < a.min.z;
}

inline Box childBox(const Box& parent, const data::MeshTreeNode& n) {
    const Vec3 step = (parent.max - parent.min) * kStep;
    const Vec3 lo{float(n.minStep[0]), float(n.minStep[1]), float(n.minStep[2])};
    const Vec3 hi{float(n.maxStep[0]), float(n.maxStep[1]), float(n.maxStep[2])};
    return {parent.min + Vec3{step.x * lo.x, step.y * lo.y, step.z * lo.z},
            parent.max - Vec3{step.x * hi.x, step.y * hi.y, step.z * hi.z}};
}

// Depth-first walk of a quantized tree: nodes whose box `keep` rejects are dropped; of a node's two
// inner children the right one is visited first (the left one takes the popped stack slot, the
// right one is pushed above it); leaves are reported left before right, as each node is reached.
template <class Keep, class OnLeaves>
void walk(const std::vector<data::MeshTreeNode>& tree, const Box& root, Keep keep, OnLeaves onLeaves) {
    struct Entry {
        unsigned node;
        Box box;
    };
    std::vector<Entry> stack{{0, root}};
    while (!stack.empty()) {
        const Entry e = stack.back();
        const data::MeshTreeNode& n = tree[e.node];
        const Box box = childBox(e.box, n);
        if (!keep(box)) {
            stack.pop_back();
            continue;
        }
        unsigned leaves[2];
        int leafCount = 0;
        const unsigned leftKind = n.childKinds & 3, rightKind = (n.childKinds >> 2) & 3;
        if (leftKind == 0) {
            stack.back() = {n.left, box};
        } else {
            stack.pop_back();
            if (leftKind == 1) {
                leaves[leafCount++] = n.left;
            }
        }
        if (rightKind == 0) {
            if (stack.size() >= kMaxStack) {
                break;
            }
            stack.push_back({n.right, box});
        } else if (rightKind == 1) {
            leaves[leafCount++] = n.right;
        }
        if (leafCount > 0) {
            onLeaves(leaves, leafCount);
        }
    }
}

// Corner (and edge) order of a facet: reversed for mirrored meshes.
struct Corners {
    int v[3];
    int e[3];
};

inline Corners corners(const data::MeshFacet& f, bool mirrored) {
    const int m = mirrored ? 1 : 0;
    return {{f.vertex[std::size_t(2 * m)], f.vertex[1], f.vertex[std::size_t(2 - 2 * m)]},
            {f.edge[std::size_t(m)], f.edge[std::size_t(1 - m)], f.edge[2]}};
}

}   // namespace beacon::physics::rigid::meshtree
