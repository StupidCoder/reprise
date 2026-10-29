#include "gamegear/lcd_filter.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace gg {
namespace {
// Washed-out STN range: a lifted, slightly green-grey black and a dimmer,
// faintly warm CCFL white. Colour is desaturated and mid-tones sink a little.
constexpr float black[3]{34.f, 39.f, 35.f}, white[3]{222.f, 228.f, 212.f};
constexpr float saturation = .70f, gamma = 1.15f;
// Pixel persistence time constants in seconds: STN cells brighten faster than
// they relax back to dark, which leaves trails behind moving bright objects.
constexpr double rise = .030, fall = .046;
} // namespace

Color LcdFilter::respond(Color c) {
    const float r = c.r / 255.f, g = c.g / 255.f, b = c.b / 255.f;
    const float luma = .30f * r + .59f * g + .11f * b;
    const float in[3]{r, g, b};
    Color out{0, 0, 0, 255};
    std::uint8_t* channel[3]{&out.r, &out.g, &out.b};
    for (int k = 0; k < 3; ++k) {
        float v = std::clamp(luma + (in[k] - luma) * saturation, 0.f, 1.f);
        v = std::pow(v, gamma);
        *channel[k] = std::uint8_t(std::lround(black[k] + v * (white[k] - black[k])));
    }
    return out;
}

void LcdFilter::reset() {
    state_.clear();
    width_ = height_ = 0;
}

const platform::Image& LcdFilter::apply(const platform::Image& frame, double elapsed) {
    const std::size_t count = std::size_t(frame.width) * frame.height;
    const bool fresh =
        frame.width != width_ || frame.height != height_ || state_.size() != count * 3;
    if (fresh) {
        width_ = frame.width;
        height_ = frame.height;
        state_.assign(count * 3, 0.f);
        output_ = platform::Image(frame.width, frame.height);
    }
    elapsed = std::clamp(elapsed, 0.0, 0.25);
    const float up = float(1 - std::exp(-elapsed / rise)),
                down = float(1 - std::exp(-elapsed / fall));
    for (std::size_t i = 0; i < count; ++i) {
        const Color target = respond(frame.pixels[i]);
        const float t[3]{float(target.r), float(target.g), float(target.b)};
        float* s = &state_[i * 3];
        for (int k = 0; k < 3; ++k) {
            s[k] = fresh ? t[k] : s[k] + (t[k] - s[k]) * (t[k] > s[k] ? up : down);
        }
        output_.pixels[i] = {std::uint8_t(std::lround(s[0])), std::uint8_t(std::lround(s[1])),
                             std::uint8_t(std::lround(s[2])), 255};
    }
    return output_;
}

double LcdFilter::mask_strength(double cell_width) {
    // Below three device pixels per dot the stripes alias into moire.
    return std::clamp((cell_width - 3.0) / 3.0, 0.0, 1.0);
}

platform::Image LcdFilter::mask(int width, int height, double cell_width, double cell_height,
                                double origin_x, double origin_y) {
    platform::Image image(std::max(width, 1), std::max(height, 1));
    const double strength = mask_strength(cell_width);
    // Off-colour leak through each stripe filter, and the dark matrix between
    // subpixels/rows; gaps only appear when wide enough to resolve.
    const double leak = 1 - .58 * strength, gap = 1 - .55 * strength;
    const double sub_width = cell_width / 3;
    const double column_gap = sub_width >= 3 ? .18 : sub_width >= 2 ? .12 : 0;
    const double row_gap = cell_height >= 5 ? .14 : cell_height >= 3 ? .10 : 0;
    // Area sampling: each device pixel takes the mean of 4x4 sub-samples of
    // the ideal pattern (a box filter), so stripe and gap edges that fall
    // inside a pixel blend instead of snapping, which suppresses aliasing.
    constexpr int samples = 4;
    auto column = [&](double x, int k) {
        double u = (x - origin_x) / cell_width;
        u -= std::floor(u);
        const int sub = std::min(2, int(u * 3));
        const double within = u * 3 - sub;
        double v = sub == k ? 1 : leak;
        return within >= 1 - column_gap ? v * gap : v;
    };
    std::vector<std::array<double, 3>> columns(std::size_t(image.width));
    for (int x = 0; x < image.width; ++x) {
        for (int k = 0; k < 3; ++k) {
            double sum = 0;
            for (int s = 0; s < samples; ++s) {
                sum += column(x + (s + .5) / samples, k);
            }
            columns[std::size_t(x)][std::size_t(k)] = sum / samples;
        }
    }
    for (int y = 0; y < image.height; ++y) {
        double row = 0;
        for (int s = 0; s < samples; ++s) {
            double v = (y + (s + .5) / samples - origin_y) / cell_height;
            v -= std::floor(v);
            row += v >= 1 - row_gap ? gap : 1;
        }
        row /= samples;
        for (int x = 0; x < image.width; ++x) {
            const auto& c = columns[std::size_t(x)];
            image.pixels[std::size_t(y) * image.width + x] = {
                std::uint8_t(std::lround(c[0] * row * 255)),
                std::uint8_t(std::lround(c[1] * row * 255)),
                std::uint8_t(std::lround(c[2] * row * 255)), 255};
        }
    }
    return image;
}
} // namespace gg
