#pragma once
#include "fa_18_interceptor_amiga/data/shapes.hpp"
namespace interceptor::game {
// Shared by the game and the lesson: interpret imported ordering data, not a
// hard-coded aircraft order. Facing is evaluated in the caller's camera space.
template <class FacingTest>
std::vector<int> shape_draw_order(const data::Shape &shape, FacingTest facing,
                                  const std::array<double, 3> &origin, const std::array<double, 3> &eye) {
    using Kind = data::DrawNode::Kind;
    std::vector<int> order;
    if (shape.tree.empty()) {
        for (int i = 0; i < shape.lists; ++i)
            order.push_back(i);
    } else {
        int at = 0;
        for (int guard = 0; at >= 0 && guard < 512; ++guard) {
            const auto &node = shape.tree[std::size_t(at)];
            switch (node.kind) {
            case Kind::leaf:
                order.insert(order.end(), node.lists.begin(), node.lists.end());
                at = node.stop ? -1 : node.next;
                break;
            case Kind::facing:
                at = facing(node.points) ? node.first : node.second;
                break;
            case Kind::plane: {
                const auto &p = node.points[0];
                const auto &n = node.points[1];
                const double side = (origin[0] + p.x - eye[0]) * n.x + (origin[1] + p.y - eye[1]) * n.y +
                                    (origin[2] + p.z - eye[2]) * n.z;
                at = side < 0 ? node.first : node.second;
                break;
            }
            case Kind::axis: {
                const auto &p = node.points[0];
                const double model = (node.axis == 0 ? p.x : node.axis == 1 ? p.y : p.z) * shape.scale;
                const double camera = eye[std::size_t(node.axis)] - origin[std::size_t(node.axis)];
                at = (camera < model) != node.invert ? node.second : node.first;
                break;
            }
            }
        }
    }
    return order;
}
} // namespace interceptor::game
