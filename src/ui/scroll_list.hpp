#pragma once
#include "ui/draw.hpp"
#include <algorithm>

// A list of equal-height rows shown in a box that holds fewer of them,
// scrolled by whole rows. It keeps only the position; the owner draws the
// rows from first() to first() + shown() and calls draw_bar() beside them.
namespace ui {

class ScrollList {
public:
    // The number of rows and how many fit; keeps the position in range.
    void resize(int total, int fits) {
        total_ = std::max(total, 0);
        fits_ = std::max(fits, 1);
        scroll(0);
    }
    void reset() { first_ = 0; }

    bool scrollable() const { return total_ > fits_; }
    int first() const { return first_; }
    int shown() const { return std::min(fits_, total_ - first_); }
    int total() const { return total_; }

    // Moves by `rows` (negative: up); returns whether the position changed.
    bool scroll(int rows) {
        const int before = first_;
        first_ = std::clamp(first_ + rows, 0, std::max(total_ - fits_, 0));
        return first_ != before;
    }
    // Moves by whole boxes, keeping one row of context.
    bool page(int pages) { return scroll(pages * std::max(fits_ - 1, 1)); }

    // A scroll bar of width `w` over the box's height `h` (nothing when
    // everything fits).
    void draw_bar(Image& image, int x, int y, int w, int h, Color track, Color thumb) const {
        if (!scrollable()) {
            return;
        }
        rect(image, x, y, w, h, track);
        const int thumb_h = std::max(h * fits_ / total_, w * 2);
        const int range = total_ - fits_;
        const int thumb_y = y + (h - thumb_h) * first_ / range;
        rect(image, x, thumb_y, w, thumb_h, thumb);
    }

private:
    int total_{}, fits_{1}, first_{};
};

} // namespace ui
