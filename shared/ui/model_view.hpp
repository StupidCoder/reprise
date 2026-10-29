#pragma once
// A small software renderer for the asset viewer: decoded 3D models (a
// game's shapes) drawn flat-shaded with a depth buffer, auto-fitted into a
// rectangle and turned by the viewer's angles. Viewer only: games draw
// their own scenes.
#include "platform/image.hpp"
#include <array>
#include <vector>

namespace ui {

struct Model {
    // Points in the model's own units; x right, y up, z towards the viewer.
    std::vector<std::array<float, 3>> points;
    struct Face {
        std::vector<int> corners; // indices into points, in order around the face
        platform::Color colour;
    };
    struct Line {
        int from{}, to{};
        platform::Color colour;
    };
    std::vector<Face> faces; // convex polygons; both sides are drawn
    std::vector<Line> lines; // drawn over faces they lie in front of
};

// Draws `model` into (x, y, w, h) of `image`, turned by `yaw` (about the
// vertical axis) and then `pitch` (about the horizontal), in radians.
// Faces are shaded by their angle to a light behind the viewer.
void draw_model(platform::Image& image, int x, int y, int w, int h, const Model& model, double yaw, double pitch);

} // namespace ui
