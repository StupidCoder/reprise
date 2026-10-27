#include "mindwalker_amiga/tunnel/tunnel_stage.hpp"
#include "amiga/intuition_imagery.hpp"
#include "mindwalker_amiga/screen/area_fill.hpp"
#include <algorithm>
#include <map>

namespace mindwalker::tunnel {
namespace {

// B's data (research layout).
constexpr std::uint32_t palette = 0x3002E8;      // h3: 32 colours (4-7 rotate)
constexpr std::uint32_t ring_radii = 0x3012EC;   // h9: 17 radii, outermost first
constexpr std::uint32_t corner_x = 0x301330, corner_y = 0x301340; // the outer ring's corners
constexpr std::uint32_t pace_points = 0x30326C;  // h27: points for a catch by pace
constexpr std::uint32_t sample_periods = 0x3034B0; // h30: the four channels' periods
constexpr std::uint32_t new_window = 0x300A9A;
// The Image data pointers B sets to S's icons ($300040; as A's).
constexpr std::uint32_t pause_box_data = 0x30093A, knob_data = 0x300926, first_life_data = 0x300978;
constexpr std::uint32_t s_life = 0x700A2C, s_pause_box = 0x700A40, s_knob = 0x700A54;
constexpr int pause_gadget = 0x10;
constexpr int view_top = 11;                     // $300B6C: the rectangles' top row (the window's BorderTop + 1)
constexpr int picture_top = 10;                  // the tunnel's first row on screen (dumps tunnel1-3)
constexpr int hand_x[2] = {0x40, 0xB4}, hand_y[2] = {0x7D, 0x73};
constexpr int centre_x = 0xF0, centre_y = 0x96;  // the tunnel's middle in its picture

std::vector<std::uint8_t> bytes_at(const amiga::LoadedImage& program, std::uint32_t at, std::size_t count) {
    std::vector<std::uint8_t> out(count);
    for (std::size_t i = 0; i < count; ++i) out[i] = program.u8(at + std::uint32_t(i));
    return out;
}

// $301260: a wander of -3..3 from bits `shift` up of the mixer.
int wobble(world::Mixer& random, int shift) {
    int v = (random.step() >> shift) & 7;
    if (v & 4) v = 4 - v;
    return v;
}

} // namespace

TunnelStage::TunnelStage(const data::GameFiles& files, const data::Assets& assets, const Start& start)
    : b_(files.program(data::Stage::tunnel)), assets_(assets), stage_(start.stage), lives_(start.lives),
      score_(start.score) {
    const auto& s = files.program(data::Stage::setup);
    std::map<std::uint32_t, std::vector<std::uint8_t>> data = {{pause_box_data, bytes_at(s, s_pause_box, 20)},
                                                               {knob_data, bytes_at(s, s_knob, 24)}};
    for (std::uint32_t i = 0; i < 5; ++i) data[first_life_data + i * 20] = bytes_at(s, s_life, 20);
    bar_ = screen::GadgetBar(b_, new_window, std::move(data));
    for (auto& gadget : bar_.gadgets()) {
        if (gadget.proportional) gadget.horizontal_pot = std::uint16_t(stage_ * 0x618);
        if (gadget.images.size() == 5) // $300390: figure i while i + 1 < lives (PlanePick 8)
            for (std::size_t i = 0; i < 5; ++i) {
                gadget.images[i].depth = int(i) + 1 < lives_ ? 1 : 0;
                gadget.images[i].plane_pick = int(i) + 1 < lives_ ? 8 : 0;
            }
    }
    for (std::uint32_t i = 0; i < 32; ++i) colours_[i] = b_.u16(palette + i * 2);
    shown_colours_ = colours_;
    // h8: the wander's mixer from two draws of the seed; h20: the rectangles'.
    seed_.state = start.seed;
    {
        const auto high = seed_.next();
        wander_ = world::Mixer::seeded(high, seed_.next());
    }
    for (int i = 0; i < 12; ++i) {
        const auto high = seed_.next();
        const auto low = std::uint16_t(seed_.next() * i);
        rect_random_[std::size_t(i)] = world::Mixer::seeded(high, low);
    }
    growth_ = 0x16 - stage_ / 3;
    draw_tunnel();
    // $303302: the tunnel's sample on all four channels, silent.
    const auto& sample = assets_.samples.front().data; // "Tunnel (B h33)", shifted as h29 does
    for (int c = 0; c < 4; ++c) {
        voices_.set_samples(c, sample);
        voices_.set_volume(c, 0);
        voices_.set_period(c, b_.u16(sample_periods + std::uint32_t(c * 2)));
    }
}

// h8 / $300EDE: 17 octagonal rings from the outside in, each of eight
// triangles from the middle; the outermost reaching the picture's corners.
void TunnelStage::draw_tunnel() {
    int first = 0;
    for (std::uint32_t k = 0; k < 17; ++k) ring(b_.s32(ring_radii + k * 4), ++first, true, 0);
}

void TunnelStage::ring(int r, int first_pen, bool pens, int pen) {
    const int wide = (r << 3) / 5, half_wide = wide >> 1, half = r >> 1;
    const int xs[8] = {centre_x + half_wide, centre_x + wide, centre_x + wide, centre_x + half_wide,
                       centre_x - half_wide, centre_x - wide, centre_x - wide, centre_x - half_wide};
    const int ys[8] = {centre_y - r, centre_y - half, centre_y + half, centre_y + r,
                       centre_y + r, centre_y + half, centre_y - half, centre_y - r};
    const bool outermost = r == b_.s32(ring_radii);
    for (int s = 0; s < 8; ++s) {
        if (pens) pen = (++first_pen) & 3;
        std::vector<screen::AreaPoint> points{{centre_x, centre_y}, {xs[s], ys[s]}};
        if (outermost && (s & 1) == 0)
            points.push_back({b_.s32(corner_x + std::uint32_t(s / 2 * 4)), b_.s32(corner_y + std::uint32_t(s / 2 * 4))});
        points.push_back({xs[(s + 1) & 7], ys[(s + 1) & 7]});
        screen::fill_area(points, [&](int x, int y) {
            if (x >= 0 && x < 480 && y >= 0 && y < 300) tunnel_[std::size_t(y * 480 + x)] = std::uint8_t(pen);
        });
    }
}

// $301100: colours 4-7 turn one step; loaded with colours 0-7.
void TunnelStage::rotate_colours(bool load) {
    const auto first = colours_[4];
    colours_[4] = colours_[5];
    colours_[5] = colours_[6];
    colours_[6] = colours_[7];
    colours_[7] = first;
    if (load) std::copy(colours_.begin(), colours_.begin() + 8, shown_colours_.begin());
}

// $303380: each channel as loud as its rectangle is big (averaged).
void TunnelStage::sound() {
    for (int c = 0; c < 4; ++c) {
        const int v = std::min(rects_[std::size_t(c)].size + 8, 0x3F);
        voices_.set_volume(c, (v + last_volume_[std::size_t(c)]) >> 1);
        last_volume_[std::size_t(c)] = v;
    }
}

int TunnelStage::gadget_at(int x, int y) const {
    if (y < 0 || y >= 10) return -1;
    if (x >= 0 && x < amiga::WindowCloseGadget::width) return -2;
    for (const auto& gadget : bar_.gadgets())
        if (x >= gadget.x() && x < gadget.x() + gadget.width && y >= gadget.top && y < gadget.top + gadget.height)
            return gadget.id;
    return -1;
}

void TunnelStage::frame(const TunnelReader& read, int gadget) {
    if (collapse_ >= 0) {
        collapse_step();
        return;
    }
    if (outcome_ != Outcome::playing) return;
    if (paused_) { // $300536: Wait for the P box again
        if (gadget == pause_gadget) {
            paused_ = false;
            resume_wait_ = 30;
        }
        return;
    }
    if (resume_wait_ > 0) { // $30059C: 30 frames, then the colours again
        if (--resume_wait_ == 0) {
            shown_colours_ = colours_;
            voices_.set_dma(true);
        }
        return;
    }
    sound();
    // $30040C: the left button held starts a wait of 300 frames.
    const bool button = read(TunnelRead::button).button;
    if (button && frozen_ == 0) frozen_ = 300;
    button_was_down_ = button;
    if (gadget == -2) {
        outcome_ = Outcome::quit; // CLOSEWINDOW
        return;
    }
    if (gadget >= 0) {
        frozen_ = 0;
        if (gadget == pause_gadget) { // $300536
            paused_ = true;
            std::fill(rect_layer_.begin(), rect_layer_.end(), 0); // $302E94
            voices_.set_dma(false);
            for (int c = 0; c < 4; ++c) voices_.set_volume(c, 0);
            return;
        }
    }
    if (frozen_ == 0) {
        move(read);
        rectangle(++frame_count_ & 3); // $302828
    } else {
        --frozen_;
    }
    if (outcome_ != Outcome::playing) {
        // $300CFE: the tunnel collapses, then the points ($303244).
        std::fill(rect_layer_.begin(), rect_layer_.end(), 0);
        shown_colours_ = colours_;
        collapse_ = 16;
        collapse_frame_ = 0;
        if (outcome_ == Outcome::caught) score_ += std::uint32_t(b_.s32(pace_points + std::uint32_t(pace_ * 4)));
    }
}

// $301086: the stick read; the pace's tick (colours on, the tunnel's wander,
// a step of steering; every (8 - pace) x 16 + 8 ticks the pace drops, at 0
// the tunnel is over); then steering, clamped.
void TunnelStage::move(const TunnelReader& read) {
    const auto stick = read(TunnelRead::stick);
    (void)read(TunnelRead::fire);
    stick_left_ = stick.left;
    stick_right_ = stick.right;
    stick_up_ = stick.up;
    stick_down_ = stick.down;
    if (pace_ < ++ticks_) {
        ticks_ = 0;
        if ((8 - pace_) * 0x10 + 8 < ++laps_) {
            laps_ = 0;
            if (--pace_ == 0) outcome_ = Outcome::ran_out; // $300B68
        }
        rotate_colours(true);
        pan_x_ += wobble(wander_, 3); // $301166
        pan_y_ += wobble(wander_, 0);
        steer();
    }
    steer(); // $301158
    pan_x_ = std::clamp(pan_x_, 0, 0x9D); // $301298
    pan_y_ = std::clamp(pan_y_, 0, 99);
}

// $301186: the stick pans the view a pixel and carries the rectangles with
// it, the bigger the faster (the slider's stage slows them: pot / $1D4).
void TunnelStage::steer() {
    const int slider = (stage_ * 0x618 & 0xFFFF) / 0x1D4;
    const int b = stick_left_ ? -1 : stick_right_ ? 1 : 0;
    const int a = stick_up_ ? -1 : stick_down_ ? 1 : 0;
    pan_x_ += b;
    pan_y_ += a;
    for (auto& r : rects_) {
        const int k = r.size / (0xA0 - slider);
        r.x += (k + 2) * b;
        r.y += a * (k + 2);
    }
}

// $302C84: a rectangle's box on screen (a third of its size across, half
// as much again down), clamped to the view.
TunnelStage::Box TunnelStage::box(int x, int y, int size) const {
    const int w = size / 3, h = w * 3 / 2;
    return {std::clamp(x - w, 0, 319), std::clamp(x + w, 0, 319), std::clamp(y - h, view_top, 199),
            std::clamp(y + h, view_top, 199)};
}

// $302842, one slot a frame: a free one may start (by its mixer) near the
// view's middle; a live one grows ever faster and drifts away from the
// screen's middle, drawn in display plane 4 (only what changed); one that
// had grown across the middle is caught; one gone off screen is freed.
void TunnelStage::rectangle(int slot) {
    auto& r = rects_[std::size_t(slot)];
    const auto fill = [&](int x0, int y0, int x1, int y1, std::uint8_t v) {
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) rect_layer_[std::size_t(y * 320 + x)] = v;
    };
    if (r.size == 0) {
        if ((rect_random_[std::size_t(slot)].step() & 0x5D) == 9) {
            r.size = 1;
            r.x = (rect_random_[std::size_t(4 + slot)].step() & 0x3F) + pan_x_ + 0x30;
            r.y = (rect_random_[std::size_t(8 + slot)].step() & 0x1F) + pan_y_ + 0x22;
            r.shown_x = r.x;
            r.shown_y = r.y;
        }
        return;
    }
    ++ticks_;
    const Box old = box(r.shown_x, r.shown_y, r.size);
    int grow = r.size / growth_ + 1;
    grow *= grow;
    r.size += grow;
    if (old.x0 < 0x51 && old.x1 > 0xEF) {
        outcome_ = Outcome::caught; // $300B66; colours 4-7 turn green
        for (int c = 4; c < 8; ++c) colours_[std::size_t(c)] = std::uint16_t(colours_[std::size_t(c)] << 4);
        return;
    }
    const int step = std::max(grow / 5, 1);
    r.x += r.x < 0xA0 ? -step : step;
    r.y += r.y < 100 ? -step : step;
    if (r.x < 0 || r.x > 0x13F || r.y < 0 || r.y > 0xFA) {
        r.size = 0;
        fill(old.x0, old.y0, old.x1, old.y1, 0);
        return;
    }
    const Box now = box(r.x, r.y, r.size);
    if (old.y0 < now.y0) fill(old.x0, old.y0, old.x1, now.y0, 0);
    if (now.y1 < old.y1) fill(old.x0, now.y1, old.x1, old.y1, 0);
    if (old.x0 < now.x0) fill(old.x0, old.y0, now.x0, old.y1, 0);
    if (now.x1 < old.x1) fill(now.x1, old.y0, old.x1, old.y1, 0);
    fill(now.x0, now.y0, now.x1, now.y1, 1);
    r.shown_x = r.x;
    r.shown_y = r.y;
}

// $300CFE, one ring at a time from the outside in (four frames each): the
// sound falls, the colours turn, the ring goes black.
void TunnelStage::collapse_step() {
    if (collapse_frame_ == 0) {
        for (int c = 0; c < 4; ++c) { // $303406
            voices_.set_volume(c, std::min(collapse_ * 4, 0x3F));
            voices_.set_period(c, collapse_ * 8 + 0x15E);
        }
        rotate_colours(false);
    } else if (collapse_frame_ == 1) {
        shown_colours_ = colours_; // h23
    } else if (collapse_frame_ == 2) {
        ring(b_.s32(ring_radii + std::uint32_t(collapse_ * 4)), 0, false, 0);
    }
    if (++collapse_frame_ == 4) {
        collapse_frame_ = 0;
        if (--collapse_ < 0) {
            voices_.set_dma(false);
        }
    }
}

platform::Image TunnelStage::picture() const {
    std::vector<screen::Rgb4> colours;
    for (const auto c : shown_colours_) colours.push_back(c & 0xFFF);
    screen::Canvas canvas;
    bar_.draw(canvas, 2);
    const auto& close = amiga::window_close_gadget();
    for (int y = 0; y < close.height; ++y)
        for (int x = 0; x < close.width; ++x)
            canvas.plot(x, y, (close.planes[0][std::size_t(y)] >> (15 - x) & 1) | (close.planes[1][std::size_t(y)] >> (15 - x) & 1) << 1);
    // The view: the tunnel (pens 4-7: B's blits set plane 2 where no hand
    // is), the hands in front (plane 3 set, plane 2 by minterm $AB), the
    // rectangles in plane 4.
    for (int y = picture_top; y < 200; ++y)
        for (int x = 0; x < 320; ++x) {
            const int tx = x + 159 - pan_x_, ty = y + 100 - pan_y_; // ($300D86: the descending blit)
            int pen = 4 + ((tx >= 0 && tx < 480 && ty >= 0 && ty < 300) ? tunnel_[std::size_t(ty * 480 + tx)] : 0);
            for (int h = 0; h < 2; ++h) {
                const auto& hand = assets_.hands[std::size_t(h)];
                const int hx = x - hand_x[h], hy = y - hand_y[h];
                if (hx < 0 || hy < 0 || hx >= hand.width || hy >= hand.height) continue;
                const int v = hand.at(hx, hy);
                if (v == 0) continue;
                const int p0 = v & 1, p1 = v >> 1 & 1, p2 = v >> 2 & 1;
                pen = 8 | p0 | p1 << 1 | ((0xAB >> (p0 << 2 | p1 << 1 | p2)) & 1) << 2;
            }
            if (rect_layer_[std::size_t(y * 320 + x)]) pen |= 16;
            canvas.plot(x, y, pen);
        }
    if (paused_) canvas.fill(0, view_top, 319, 199, 0); // (hypothesis until a dump)
    return canvas.picture(colours);
}

} // namespace mindwalker::tunnel
