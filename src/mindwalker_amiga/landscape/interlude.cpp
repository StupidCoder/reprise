#include "mindwalker_amiga/landscape/interlude.hpp"
#include <algorithm>

namespace mindwalker::landscape {
namespace {

// A's data (research layout).
constexpr std::uint32_t face_rows = 0x203E18;    // the face: 10 rows of 16 bits
constexpr std::uint32_t face_colours = 0x203DF8; // h15: the colour each step stamps (16 words, 16-31)
constexpr int face_steps = 16;
constexpr int fade_steps = 18;                   // h14's fade: one nibble step each
constexpr int reveal_steps = 22;                 // h32
constexpr int view_top = 10;                     // $204D74

// How long each part lasts, in display frames, as measured on the A500
// reference machine (harness/catchtime.py, vertical blanks between the loop
// heads; work/ref/catchtime_*.txt): a face step 6 (any size), a fade step 8
// (WaitTOF on every odd colour) and 4 more after the last, the paint before
// the reveal 19, the 22 reveal steps 59 together (2 or 3 each, as the blits
// fall), and 11 after them (the last copy and the masks pass).
constexpr int face_frames = 6;
constexpr int fade_frames = 8;
constexpr int fade_tail_frames = 4;
constexpr int paint_frames = 19;
constexpr int reveal_total_frames = 59;
constexpr int settle_frames = 11;

// A repaint's events, in frames from its start (measured on a view change,
// harness/viewtime.py: h8 at 4, 9, 15 and 22, the colours loaded and the
// new drawing copied at 30-31, done at 41; a plain repaint (the map's close)
// 31 in all; $201602 taken as the view change's).
constexpr int row_frames[4] = {4, 9, 15, 22}; // rows 3, 2, 1, 0
constexpr int copy_frame = 30, repaint_frames = 41, plain_copy = 22, plain_frames = 31;
constexpr int mask_rows[3] = {33, 36, 39};    // the mask pass: rows 2, 1, 0
constexpr std::uint32_t h8_colours = 0x202EB6;    // the colour each h8 row blacks out (by row)
constexpr std::uint32_t flash_colours = 0x202EAE; // colour 0 in turn ($201258)
constexpr std::uint32_t star_x = 0x202EBE, star_y = 0x202EE2; // 9 rays

int reveal_frames(int step) {
    return (step + 1) * reveal_total_frames / reveal_steps - step * reveal_total_frames / reveal_steps;
}

} // namespace

Interlude::Interlude(const amiga::LoadedImage& a) : a_(a) {}

void Interlude::start_catch(const Display& now, int damage, bool lost, const Display& next) {
    display_ = now;
    next_ = next;
    // h14: the tether layer cleared ($200E62, from the view's top), colours
    // h15[i] turned to cyan of brightness i, h10 the colours to fade.
    std::fill(display_.plane4.begin() + view_top * 320, display_.plane4.end(), 0);
    for (int i = 0; i < 16; ++i) {
        const auto pen = a_.u16(face_colours + std::uint32_t(i * 2));
        display_.colours[pen & 31] = std::uint16_t(i << 4 | i);
    }
    for (int i = 0; i < 16; ++i) fading_[std::size_t(i)] = display_.colours[std::size_t(i)];
    level_ = damage / 100;
    lost_ = lost;
    phase_ = Phase::face;
    step_ = 0;
    face_step(0);
    frames_left_ = face_frames;
}

void Interlude::start_reveal(const Display& now, const Display& next) {
    display_ = now;
    next_ = next;
    start_paint();
}

// $201492: the new view is drawn off screen first; then h32 reveals it.
void Interlude::start_paint() {
    phase_ = Phase::paint;
    step_ = 0;
    frames_left_ = paint_frames;
}

// One of h14's 16 steps: the face drawn into the drawing's plane 0 as
// blocks of `size` + 1 pixels (RectFill x..x+size), then blitted into all
// five display planes as the step's colour h15[step]. Survived: the face at
// the damage's size (damage / 100), moving a pixel down and right each step.
// Lost: shrinking from 17 to 2 about a fixed point.
void Interlude::face_step(int step) {
    const int size = lost_ ? 17 - step : level_;
    const int shift = lost_ ? 8 : step;
    const int left = 0x98 - 8 * size + shift, top = 0x5C - 5 * size + shift;
    const int pen = a_.u16(face_colours + std::uint32_t(step * 2)) & 31;
    for (int r = 0; r < 10; ++r) {
        const auto bits = a_.u16(face_rows + std::uint32_t(r * 2));
        for (int c = 0; c < 16; ++c) {
            if (!(bits & (0x8000 >> c))) continue;
            const int x0 = c * size + left, y0 = r * size + top;
            for (int y = std::max(y0, 0); y <= std::min(y0 + size, 199); ++y)
                for (int x = std::max(x0, 0); x <= std::min(x0 + size, 319); ++x) {
                    display_.planes.plot(x, y, pen & 15);
                    display_.plane4[std::size_t(y * 320 + x)] = std::uint8_t(pen >> 4);
                }
        }
    }
}

// One step of h14's fade: every nibble of colours 0-15 one step nearer 0,
// loaded at the step's end.
void Interlude::fade_step() {
    for (auto& c : fading_)
        for (int shift = 8; shift >= 0; shift -= 4)
            if (c & (0xF << shift)) c = std::uint16_t(c - (1 << shift));
    std::copy(fading_.begin(), fading_.end(), display_.colours.begin());
}

// One of h32's 22 steps on the screen's five planes: the four quadrants outside
// the opening scroll outwards by 6 across and 4 down (ScrollRaster, vacated
// pixels 0), then the next drawing's middle columns (12 per step wide) and
// middle rows (8 per step high) are copied in.
void Interlude::reveal_step(int step) {
    auto& p = display_.planes;
    const int w = 6 * step, h = 4 * step, mid = view_top + 100;
    const auto scroll = [&](int dx, int dy, int x0, int y0, int x1, int y1) {
        if (x1 < x0 || y1 < y0) return;
        const screen::Canvas before = p;
        const auto plane4 = display_.plane4;
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                const int sx = x + dx, sy = y + dy;
                const bool inside = sx >= x0 && sx <= x1 && sy >= y0 && sy <= y1;
                p.plot(x, y, inside ? before.pen(sx, sy) : 0);
                display_.plane4[std::size_t(y * 320 + x)] = inside ? plane4[std::size_t(sy * 320 + sx)] : 0;
            }
    };
    scroll(6, 4, 0, view_top, 0xA0 - w, mid - h);
    scroll(-6, 4, w + 0xA0, view_top, 319, mid - h);
    scroll(6, -4, 0, mid + h, 0xA0 - w, 199);
    scroll(-6, -4, w + 0xA0, mid + h, 319, 199);
    const int w1 = 6 * (step + 1), h1 = 4 * (step + 1);
    // (The drawing has four planes; hypothesis: BltBitMap clears the
    // screen's fifth where it copies. $200E62 clears it at the end anyway.)
    const auto copy = [&](int x, int y) {
        p.plot(x, y, next_.planes.pen(x, y));
        display_.plane4[std::size_t(y * 320 + x)] = 0;
    };
    for (int y = view_top; y < 200; ++y)
        for (int x = std::max(0xA0 - w1, 0); x < std::min(0xA0 + w1, 320); ++x) copy(x, y);
    for (int y = std::max(mid - h1, view_top); y < std::min(mid + h1, 200); ++y)
        for (int x = 0; x < 320; ++x) copy(x, y);
}

void Interlude::start_repaint(const Display& now, const Display& next, Repaint kind, int repeats, int sx, int sy,
                              std::function<void(screen::Canvas&)> grid) {
    if (phase_ == Phase::repaint) {
        queued_.push_back({next, kind, repeats, sx, sy});
        return;
    }
    display_ = now;
    next_ = next;
    repaint_ = kind;
    repeats_ = repeats;
    star_x_ = sx;
    star_y_ = sy;
    grid_ = std::move(grid);
    frame_ = 0;
    phase_ = Phase::repaint;
    // $201492 modes 0 and 1 clear the tether layer at once ($200E62).
    if (kind == Repaint::plain || kind == Repaint::view_change)
        std::fill(display_.plane4.begin() + view_top * 320, display_.plane4.end(), 0);
    if (kind == Repaint::star) display_.colours[13] = 0xFFF; // $201602(1)
    blackened_ = next_.colours;
    repaint_frame();
}

void Interlude::repaint_frame() {
    const int t = frame_;
    if (repaint_ == Repaint::plain) {
        if (t == plain_copy) display_ = next_;
        return;
    }
    for (int k = 0; k < 4; ++k) {
        if (t != row_frames[k]) continue;
        const int row = 3 - k;
        if (repaint_ == Repaint::view_change) {
            // h8: the display's plane (8 >> row) cleared below the bar, a
            // colour of the new palette blacked out, the grid drawn again.
            blackened_[std::size_t(a_.s16(h8_colours + std::uint32_t(row * 2)) & 31)] = 0;
            display_.colours = blackened_;
            const int mask = 8 >> row;
            for (int y = view_top; y < 200; ++y)
                for (int x = 0; x < 320; ++x) display_.planes.plot(x, y, display_.planes.pen(x, y) & ~mask);
            if (grid_) grid_(display_.planes);
        } else {
            // $201244 / $201122: colour 0 in turn; the star at 8 x (4 - row).
            display_.colours[0] = a_.u16(flash_colours + std::uint32_t((3 - row) * 2));
            if (repaint_ == Repaint::star) {
                const int scale = (4 - row) * 8;
                const auto ray = [&](int i) {
                    return std::pair{std::clamp(scale * a_.s32(star_x + std::uint32_t(i * 4)) + star_x_, 0, 319),
                                     std::clamp(scale * a_.s32(star_y + std::uint32_t(i * 4)) + star_y_, view_top, 199)};
                };
                for (int i = 0; i < 9; ++i) {
                    const auto [px, py] = ray(i);
                    if (i > 0) {
                        const auto [qx, qy] = ray(i - 1);
                        display_.planes.line(qx, qy, px, py, 13);
                    }
                    display_.planes.line(px, py, star_x_, star_y_, 13);
                }
            }
        }
    }
    if (t == copy_frame) {
        const auto colours = display_.colours;
        display_.planes = next_.planes;
        display_.plane4 = next_.plane4;
        display_.colours = repaint_ == Repaint::view_change ? next_.colours : colours;
    }
    if (repaint_ != Repaint::view_change)
        for (int k = 0; k < 3; ++k)
            if (t == mask_rows[k]) display_.colours[0] = a_.u16(flash_colours + std::uint32_t((2 - k) * 2));
}

void Interlude::advance() {
    if (phase_ == Phase::repaint) {
        ++frame_;
        const int total = repaint_ == Repaint::plain ? plain_frames : repaint_frames;
        if (frame_ >= total) {
            display_ = next_;
            phase_ = Phase::idle;
            if (--repeats_ > 0) {
                start_repaint(display_, next_, repaint_, repeats_, star_x_, star_y_, grid_);
            } else if (!queued_.empty()) {
                const auto q = queued_.front();
                queued_.erase(queued_.begin());
                start_repaint(display_, q.next, q.kind, q.repeats, q.star_x, q.star_y, grid_);
            }
            return;
        }
        repaint_frame();
        return;
    }
    if (phase_ == Phase::idle || --frames_left_ > 0) return;
    ++step_;
    switch (phase_) {
    case Phase::face:
        if (step_ < face_steps) {
            face_step(step_);
            frames_left_ = face_frames;
        } else if (lost_) {
            phase_ = Phase::fade;
            step_ = 0;
            fade_step();
            frames_left_ = fade_frames;
        } else {
            start_paint();
        }
        break;
    case Phase::fade:
        if (step_ < fade_steps) {
            fade_step();
            frames_left_ = fade_frames;
        } else {
            phase_ = Phase::fade_tail; // h17: the bar's gadgets refreshed
            frames_left_ = fade_tail_frames;
        }
        break;
    case Phase::fade_tail:
        start_paint();
        break;
    case Phase::paint:
        phase_ = Phase::reveal;
        step_ = 0;
        reveal_step(0);
        frames_left_ = reveal_frames(0);
        break;
    case Phase::reveal:
        if (step_ < reveal_steps) {
            reveal_step(step_);
            frames_left_ = reveal_frames(step_);
        } else {
            // $201492's end: the colours loaded, the whole drawing copied,
            // the tether layer cleared ($200E62); then the masks pass.
            phase_ = Phase::settle;
            display_ = next_;
            frames_left_ = settle_frames;
        }
        break;
    case Phase::settle:
        phase_ = Phase::idle;
        break;
    case Phase::idle:
        break;
    }
}

} // namespace mindwalker::landscape
