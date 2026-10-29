#pragma once
#include "app/shell_style.hpp"
#include "app/shell_ui.hpp"
#include <initializer_list>
#include <string_view>
#include <utility>

// Drawing pieces shared by the shell's screens: the app's frame, header
// and footer, buttons and menu rows. All shell artwork is original geometry:
// no ROM pixels, logos or cover scans.
namespace shell {

using platform::Color;
using platform::Image;
using namespace style;

inline constexpr Color navy{8, 16, 44}, ready_green{47, 191, 91}, ready_ink{8, 36, 15},
    hint_bar{6, 10, 28};

const Theme& theme(int generation);
double clamp01(double v);
int lerp(int a, int b, double t);

// Midnight archive background and a travelling registration dash.
void frame_background(Image& im, double time);
// The original vector-style BlueStreak mark with its top-left at (x, y): the
// screen headers' size or the larger one of the library's title.
enum class LogoSize { header, title };
void logo(Image& im, int x, int y, LogoSize size);
// The key hints along the bottom edge.
void hint_footer(Image& im,
                 std::initializer_list<std::pair<std::string_view, std::string_view>> keys);
// Header strip for screens inside a generation.
void zone_header(Image& im, int generation, std::string_view title, std::string_view right);
void button(Image& im, const Theme& t, int x, int y, int w, int h, std::string_view label,
            bool active, int scale = 2);
// A row of the pause or enhancements menu.
// A pause-menu row; `key` (a shortcut, or a state such as "Empty") is
// right-aligned in it.
void menu_row(Image& im, const Theme& t, int y, std::string_view label, bool active, bool dim,
              double time, std::string_view key = {});

} // namespace shell
