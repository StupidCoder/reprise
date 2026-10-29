// Widescreen view size; see wide_view.hpp.

#include "gamegear/wide_view.hpp"
#include <algorithm>
#include <cmath>

namespace gg {

std::array<int, 2> expanded_view_size(int canvas_width, int canvas_height) {
    if (canvas_width <= 0 || canvas_height <= 0) {
        return {320, 224};
    }
    const double aspect =
        std::clamp(double(canvas_width) / canvas_height, min_view_aspect, max_view_aspect);
    if (aspect >= 320.0 / 224.0) {
        return {int(std::ceil(224 * aspect - 1e-9)), 224};
    }
    return {320, int(std::ceil(320 / aspect - 1e-9))};
}

} // namespace gg
