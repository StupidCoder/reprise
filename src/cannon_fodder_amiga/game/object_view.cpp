#include "cannon_fodder_amiga/game/object_view.hpp"
#include <algorithm>
#include <utility>

namespace fodder::game {

std::vector<int> draw_list(MissionObjects& objects, Camera snapshot, int direction_x, int direction_y) {
    // The window, in the original's word arithmetic.
    int left = std::max(0, snapshot.x - 0x14);
    int right = snapshot.x + 0x13a;
    const int sideways = (direction_x >> 1) * 16; // 0 moving right, -16 moving left
    if (sideways >= 0) {
        left -= 10;
    }
    if (sideways >= 0) {
        right += sideways;
    }
    left += sideways;
    const int top = std::max(0, snapshot.y - 4) + direction_y * 4;
    const int bottom = snapshot.y + 0x14b + direction_y * 4;

    std::vector<int> first, middle, last;
    for (int i = 0; i < 43; ++i) {
        auto& o = objects.slots[std::size_t(i)];
        if (!o.used) {
            continue;
        }
        if (o.removed) {
            o = GameObject{};
            continue;
        }
        const int x = o.x >> 16, y = o.y >> 16;
        if (x < left || x > right || y < top || y > bottom) {
            continue;
        }
        (o.layer == 0 ? middle : o.layer < 0 ? first : last).push_back(i);
    }
    // $AB89A: for every j from the end, every other i from the end: swap
    // when i's y is above j's. This exchange sort decides ties.
    const int n = int(middle.size());
    for (int j = n - 1; j >= 0; --j) {
        for (int i = n - 1; i >= 0; --i) {
            if (i != j && std::int16_t(objects.slots[std::size_t(middle[std::size_t(i)])].y >> 16) <
                              std::int16_t(objects.slots[std::size_t(middle[std::size_t(j)])].y >> 16)) {
                std::swap(middle[std::size_t(i)], middle[std::size_t(j)]);
            }
        }
    }
    first.insert(first.end(), middle.begin(), middle.end());
    first.insert(first.end(), last.begin(), last.end());
    return first;
}

void draw_objects(std::vector<std::uint8_t>& view, Camera camera, const MissionObjects& objects,
                  const std::vector<int>& order, const FrameTables& frames, const ObjectSheets& sheets) {
    draw_objects({view, view_width, view_height, camera.x - view_column_of_camera, camera.y, view_column_of_camera},
                 objects, order, frames, sheets);
}

void draw_objects(ObjectTarget target, const MissionObjects& objects, const std::vector<int>& order,
                  const FrameTables& frames, const ObjectSheets& sheets) {
    for (const int index : order) {
        const auto& o = objects.slots[std::size_t(index)];
        const auto frame = frames.frame(o.graphic, o.frame);
        if (!frame || frame->width_words <= 0) {
            continue;
        }
        const int lines = frame->height - o.hidden_lines;
        if (lines <= 0) {
            continue;
        }
        const auto& sheet = frame->sheet == 0 ? sheets.army : sheets.vehicles;
        const int width = (frame->width_words - 1) * 16;
        const int left = (o.x >> 16) + frame->offset_x;
        const int top = (o.y >> 16) + frame->offset_y - lines - o.height;
        for (int row = 0; row < lines; ++row) {
            const int sy = frame->sheet_y + row;
            const int line = top + row - target.origin_y;
            if (sy >= sheet.height || line < 0 || line >= target.height) {
                continue;
            }
            for (int col = 0; col < width; ++col) {
                const int sx = frame->sheet_x + col;
                const int column = left + col - target.origin_x;
                if (sx >= sheet.width || column < target.first_column || column >= target.width) {
                    continue;
                }
                const auto colour = sheet.pixels[std::size_t(sy * sheet.width + sx)];
                if (colour != 0) {
                    target.pixels[std::size_t(line * target.width + column)] = colour;
                }
            }
        }
    }
}

} // namespace fodder::game
