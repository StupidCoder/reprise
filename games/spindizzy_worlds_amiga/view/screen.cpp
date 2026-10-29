#include "spindizzy_worlds_amiga/view/screen.hpp"
#include "spindizzy_worlds_amiga/data/preview.hpp"

namespace spindizzy::view {

void Screen::paste(const Canvas& canvas, int x, int y) {
    for (int row = 0; row < canvas.height; ++row)
        for (int column = 0; column < canvas.width; ++column) put(x + column, y + row, canvas.at(column, row));
}

platform::Image Screen::image() const {
    platform::Image out(width, height);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) out.put(x, y, data::to_color(palette[at(x, y) & 31]));
    return out;
}

} // namespace spindizzy::view
