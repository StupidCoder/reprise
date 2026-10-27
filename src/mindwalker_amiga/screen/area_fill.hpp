#pragma once
#include "mindwalker_amiga/screen/canvas.hpp"
#include <functional>
#include <vector>

namespace mindwalker::screen {

struct AreaPoint {
    int x, y;
};

// A polygon filled as graphics.library's AreaMove/AreaDraw/AreaEnd do it
// with the blitter (see area_fill.cpp), rows offset by `top`; with
// `outline` >= 0 (AREAOUTLINE) its edges drawn in that pen afterwards.
void fill(Canvas& canvas, const std::vector<AreaPoint>& points, int pen, int outline = -1, int top = 0);
// The same fill onto any raster: `plot` for each filled pixel.
void fill_area(const std::vector<AreaPoint>& points, const std::function<void(int x, int y)>& plot);

} // namespace mindwalker::screen
