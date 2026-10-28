#include "mindwalker_amiga/landscape/map_view.hpp"
#include <algorithm>

namespace mindwalker::landscape {
namespace {

// A's data (research layout).
constexpr std::uint32_t map_colours = 0x200EA0;  // h6: 30 colours
constexpr std::uint32_t map_texts = 0x20100A;    // IntuiText chain: title and legend
constexpr std::uint32_t feature_offsets = 0x200EDC; // 4 longs: +1..+4 by legend column, then the kinds' bases
constexpr std::uint32_t kind_bases = 0x200EEC;   // 5 longs: 1, 6, 11, 20, 24
constexpr int view_top = 10;                     // $204D74
constexpr int grid_pen = 0x1D, box_pen = 0x10, highlight = 0xFF0;
constexpr int square_frames = 3, open_frames = 7; // measured (see busy())

} // namespace

MapView::MapView(const amiga::LoadedImage& a) : a_(a) {}

std::array<std::uint16_t, 30> MapView::palette(const amiga::LoadedImage& a) {
    std::array<std::uint16_t, 30> out{};
    for (std::uint32_t i = 0; i < 30; ++i) out[i] = a.u16(map_colours + i * 2);
    return out;
}

void MapView::load_colours() {
    for (std::uint32_t i = 0; i < 30; ++i) colours_[i] = a_.u16(map_colours + i * 2);
}

void MapView::show(const screen::Canvas& display, std::uint16_t view_x, std::uint16_t view_y, const Builder& build) {
    open_ = true;
    held_ = false;
    display_ = display;
    view_x_ = view_x;
    view_y_ = view_y;
    canvas_ = display;
    canvas_.fill(0, view_top, 319, 199, 0);
    load_colours();
    // The title and legend (IntuiTexts, JAM1, topaz 8, at (0, 2) of the view).
    for (std::uint32_t t = map_texts; t != 0; t = a_.u32(t + 16)) {
        const int pen = a_.u8(t), x = a_.s16(t + 4), y = a_.s16(t + 6);
        std::string text;
        for (std::uint32_t c = a_.u32(t + 12); a_.u8(c) != 0; ++c) text.push_back(char(a_.u8(c)));
        canvas_.text(x, view_top + 2 + y + 6, text, pen);
    }
    for (int x = 0; x < 320; x += 20) canvas_.line(x, view_top + 0x1D, x, view_top + 0xBC, grid_pen);
    for (int y = 0x1D; y < 0xBB; y += 20) canvas_.line(0, view_top + y, 319, view_top + y, grid_pen);
    canvas_.line(0, view_top + 0xBC, 319, view_top + 0xBC, grid_pen);
    canvas_.line(319, view_top + 0xBC, 319, view_top + 0x1D, grid_pen);
    origin_x_ = std::uint16_t((view_x - 0x1E) & 0xFFF);
    origin_y_ = std::uint16_t((view_y - 0xE) & 0xFFF);
    pending_.clear();
    for (int c = 5; c <= 10; ++c)
        for (int r = 2; r <= 5; ++r) queue_square(c, r);
    pending_.push_back(-1);
    if (fill_all_)
        for (int c = 0; c < 16; ++c)
            for (int r = 0; r < 8; ++r) queue_square(c, r);
    wait_ = open_frames;
}

void MapView::redraw(const Builder& build) { show(display_, view_x_, view_y_, build); }

void MapView::queue_square(int column, int row) {
    row = std::clamp(row, 0, 7);
    if (canvas_.pen(column * 20 + 10, view_top + 0xB8 - row * 20 - 10) != 0) return; // drawn already
    for (const int p : pending_)
        if (p == column * 8 + row) return;
    pending_.push_back(column * 8 + row);
}

void MapView::work(const Builder& build) {
    if (wait_ > 0 && --wait_ > 0) return;
    while (!pending_.empty()) {
        const int item = pending_.front();
        pending_.pop_front();
        if (item < 0) {
            draw_box();
            continue;
        }
        draw_square(item / 8, item % 8, build);
        wait_ = square_frames;
        return;
    }
}

// The view's own square, outlined in the cycling colour.
void MapView::draw_box() {
    canvas_.line(0x96, view_top + 99, 0xAA, view_top + 99, box_pen);
    canvas_.line(0xAA, view_top + 99, 0xAA, view_top + 0x77, box_pen);
    canvas_.line(0xAA, view_top + 0x77, 0x96, view_top + 0x77, box_pen);
    canvas_.line(0x96, view_top + 0x77, 0x96, view_top + 99, box_pen);
}

// $200BF0: a world cell's block colour.
int MapView::pen(const ViewCells& v, int x, int y) {
    return pen(v.terrain[std::size_t(x)][std::size_t(y)], v.objects[std::size_t(x)][std::size_t(y)]);
}

int MapView::pen(std::uint16_t terrain, std::uint16_t objects) {
    if (objects & 2) return 0x10;                            // a beacon
    if ((terrain & 0x100) && !(terrain & 0x800)) return 0x1D; // the marked block
    int pen = 0;
    switch (terrain & 0xC000) {
    case 0x4000: pen = 6; break;
    case 0x8000: pen = 11; break;
    case 0xC000: pen = 20; break;
    default: pen = 1; break;
    }
    if (objects & 1) return pen + 4; // the walker's path
    switch (terrain & 0x1C00) {
    case 0: return pen;
    case 0x400: return pen + 1;
    case 0x800: return pen + 2;
    default: return pen + 3;
    }
}

// $200A36: a square, unless already drawn (its middle block not pen 0).
void MapView::draw_square(int column, int row, const Builder& build_) {
    row = std::clamp(row, 0, 7);
    const int left = column * 20, base = view_top + 0xB8 - row * 20;
    if (canvas_.pen(left + 10, base - 10) != 0) return;
    const auto cells = build_(std::uint16_t((column * 4 + origin_x_) & 0xFFF), std::uint16_t((row * 4 + origin_y_) & 0xFFF));
    for (int x = 0; x < 4; ++x)
        for (int y = 0; y < 4; ++y) {
            const int top = base - y * 5;
            canvas_.fill(x * 5 + left, top, x * 5 + left + 4, top + 4, pen(cells, x, y));
        }
}

void MapView::button(bool down, int x, int y) {
    held_ = down;
    if (!down) {
        load_colours();
        return;
    }
    if (y > 9 && y < 0x13 && x > 0x58 && x < 0xE8) {
        fill_all_ = true;
        for (int c = 0; c < 16; ++c)
            for (int r = 0; r < 8; ++r) queue_square(c, r);
    } else if (y > 0x13 && y < 0x27) {
        const int column = std::clamp(x / 80, 0, 3);
        if (y < 0x1E) { // a feature in every kind's colours
            const int offset = a_.s32(feature_offsets + std::uint32_t(column * 4));
            for (int k = 0; k < 5; ++k) {
                const int c = a_.s32(kind_bases + std::uint32_t(k * 4)) + offset;
                if (c < 30) colours_[std::size_t(c)] = highlight;
            }
        } else { // a kind in all its colours
            const int base = a_.s32(kind_bases + std::uint32_t(column * 4));
            for (int k = 0; k < 5; ++k)
                if (base + k < 30) colours_[std::size_t(base + k)] = highlight;
        }
    }
}

int MapView::turn(int x, int y) {
    if (!held_ || y < 0x27) {
        ++cycle_;
        colours_[16] = std::uint16_t(cycle_ & 0xFFF);
        return 2;
    }
    queue_square(std::clamp(x / 20, 0, 15), 7 - (y - 0x27) / 20);
    return 1;
}

} // namespace mindwalker::landscape
