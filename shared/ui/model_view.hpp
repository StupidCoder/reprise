#pragma once
// A small software renderer for the asset viewer: decoded 3D models (a
// game's shapes) drawn flat-shaded with a depth buffer, auto-fitted into a
// rectangle and turned by the viewer's angles. Viewer only: games draw
// their own scenes.
#include "platform/image.hpp"
#include <array>
#include <vector>
#include <memory>
#include <string>
#include <optional>

namespace ui {

struct Model {
    // Points in the model's own units; x right, y up, z towards the viewer.
    std::vector<std::array<float, 3>> points;
    struct Face {
        std::vector<int> corners; // indices into points, in order around the face
        platform::Color colour;
        std::vector<std::array<float, 2>> uv;
        int texture{-1};
        float alpha_threshold{};
        bool unlit{};
    };
    struct Line {
        int from{}, to{};
        platform::Color colour;
    };
    std::vector<Face> faces; // convex polygons; both sides are drawn
    std::vector<std::shared_ptr<const platform::Image>> textures;
    std::optional<std::array<float, 6>> fit_bounds; // fixed framing across animated poses
    std::vector<Line> lines; // drawn over faces they lie in front of
};

// A lazy viewer session. Engines decode and pose their own imported formats.
struct ModelClip { std::string name; double duration{}, sample_rate{30}; };
class AnimatedModel {
public:
    virtual ~AnimatedModel() = default;
    virtual const std::vector<ModelClip>& clips() const = 0;
    virtual void select(std::size_t clip) = 0; // throws with a useful unsupported-clip error
    virtual void pose(double seconds) = 0;
    virtual const Model& model() const = 0;
};

// Draws `model` into (x, y, w, h) of `image`, turned by `yaw` (about the
// vertical axis) and then `pitch` (about the horizontal), in radians.
// Faces are shaded by their angle to a light behind the viewer.
void draw_model(platform::Image& image, int x, int y, int w, int h, const Model& model, double yaw, double pitch, double zoom = 1, double pan_x = 0, double pan_y = 0);

} // namespace ui
