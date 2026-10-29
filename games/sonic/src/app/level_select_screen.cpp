// The level selector screen (layout in level_select.cpp).

#include "app/level_select.hpp"
#include "app/shell_frame.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace shell {

// --- Level select ----------------------------------------------------------
platform::Image level_select_image(const LevelLayout& layout, std::string_view title, int selected,
                                   std::string_view message, double time) {
    Image im(960, 640);
    const auto& t = theme(0);
    backdrop(im, t, 0, 0, 960, 640, int(time * 18));
    blend_rect(im, 0, 92, 960, 512, navy, 70);
    zone_header(im, 0, title, "Choose how to play");
    const int bob = 0;
    for (int n = 0; n <= layout.back; ++n) {
        auto r = layout.rect(n);
        const bool active = n == selected;
        if (active) {
            panel(im, t, r.x, r.y - 2, r.w, r.h, t.accent, mix(t.accent, black, 100));
        } else {
            panel(im, t, r.x, r.y, r.w, r.h, t.card, mix(t.card, t.bg, 90));
        }
        const Color c = active ? t.ink : t.card_ink;
        const std::string& label = layout.labels[std::size_t(n)];
        const int ty = r.y + (r.h - 7 * (n == 0 ? 3 : 2)) / 2 - (active ? 2 : 0);
        int x = r.x + 14;
        if (active) {
            x = text(im, std::string(1, icon_play), r.x + 10 + bob, ty, n == 0 ? 3 : 2, c) + 10;
        }
        if (n == 0) {
            x = text(im, label, x, ty, 3, c);
            text(im, "From the original startup through the full adventure", x + 24, ty + 4, 2,
                 active ? t.ink : t.muted);
        } else {
            text(im, label, x, ty, 2, c);
        }
    }
    for (const auto& row : layout.rows) {
        if (!row.world.empty()) {
            const auto r = layout.rect(row.first);
            text(im, row.world, 36, r.y + 8, 2, white, Ink::outline, navy);
        }
    }
    const std::string& own = layout.hints[std::size_t(std::clamp(selected, 0, layout.back))];
    std::string hint = !message.empty() ? std::string(message)
                       : selected == 0  ? "The complete original startup. Your quick save is kept."
                       : selected == layout.back ? "Return to the game library."
                       : !own.empty()
                           ? own
                           : "Starts at the act entrance with 3 lives and fresh progress.";
    blend_rect(im, 0, 540, 960, 36, navy, 140);
    text(im, hint, 36, 551, 2, white);
    int x = 264;
    for (auto [key, action] : {std::pair<std::string_view, std::string_view>{
                                   std::string_view("\x01\x02\x03\x04"), "Select"},
                               {"Enter", "Start"},
                               {"Esc", "Back"}}) {
        x = key_hint(im, key, action, x, 589);
    }
    return im;
}

} // namespace shell
