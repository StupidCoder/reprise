// The library: generations and the game carousel.

#include "app/level_select.hpp"
#include "app/shell_frame.hpp"
#include "platform/host.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace shell {

namespace {

// --- Library geometry ------------------------------------------------------
// The focused generation's panel is wide, the others share the rest equally;
// on a focus change the old panel shrinks while the new one grows.
SelectRect generation_rect(int g, const LibraryView& v) {
    constexpr int left = 56, top = 156, height = 330, gap = 28, wide = 380;
    constexpr int count = int(generations.size());
    constexpr int narrow = (912 - left - gap * (count - 1) - wide) / (count - 1);
    const double progress = ease_out((v.time - v.moved_at) / 0.3);
    const auto width = [&](int n) {
        const double was = n == v.generation_from ? 1 : 0, is = n == v.generation ? 1 : 0;
        return narrow + (wide - narrow) * (was + (is - was) * progress);
    };
    double x = left;
    for (int n = 0; n < g; ++n) {
        x += width(n) + gap;
    }
    SelectRect r{int(std::lround(x)), top, int(std::lround(width(g))), height};
    if (g == v.generation) {
        const int grow = int(10 * progress);
        r = {r.x - grow, r.y - grow - 4, r.w + 2 * grow, r.h + 2 * grow};
    }
    return r;
}

// Tabs: index 0 is "All", then one per generation.
SelectRect tab_rect(int n) {
    int right = 932;
    for (int i = int(generations.size()); i >= 0; --i) {
        std::string label = i == 0 ? "All" : generations[std::size_t(i - 1)].bits;
        int w = text_width(label, 2) + 24 +
                (i > 0 && !generations[std::size_t(i - 1)].available ? 18 : 0);
        if (i == n) {
            return {right - w, 50, w, 30};
        }
        right -= w + 8;
    }
    return {};
}

constexpr SelectRect info_rect{100, 462, 760, 122}, music_rect{596, 498, 56, 52},
    action_rect{664, 498, 172, 52};

struct CardPlace {
    int x, y, w, h;
    double focus;
};

CardPlace card_place(double offset) {
    const double a = std::abs(offset), near = std::min(a, 1.0);
    const int w = lerp(340, 214, near), h = lerp(292, 184, near);
    const double sign = offset < 0 ? -1 : 1;
    const double cx = 480 + (a <= 1 ? offset * 306 : sign * (306 + (a - 1) * 236));
    return {int(cx) - w / 2, 274 - h / 2, w, h, 1 - near};
}

bool inside(const SelectRect& r, int x, int y) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

const char* status_label(int n, bool ready) {
    return catalog[std::size_t(n)].implemented ? (ready ? "Ready" : "Needs ROM") : "Planned";
}

std::string full_title(const CatalogEntry& e) {
    return e.subtitle[0] ? std::string(e.title) + " " + e.subtitle : e.title;
}

// --- Library drawing -------------------------------------------------------
void game_card(Image& im, int n, const CardPlace& p, bool ready) {
    const auto& e = catalog[std::size_t(n)];
    const auto& t = theme(e.generation);
    const bool big = p.focus > 0.5;
    const int b = t.border, info = big ? 46 : 34;
    panel(im, t, p.x, p.y, p.w, p.h, t.card, big ? t.accent : mix(t.card, t.bg, 110));
    const int ax = p.x + b, ay = p.y + b, aw = p.w - 2 * b, ah = p.h - 2 * b - info;
    rect(im, ax, ay, aw, ah, e.art);
    // Original kinetic cover series: racing lanes, orbit and edition number.
    // Deliberately abstract; these are not recovered game illustrations.
    const Color dark = mix(e.art, navy, 125);
    rect(im, ax, ay, aw, ah, dark);
    ring(im, ax + aw * 3 / 4, ay + ah / 3, ah * 2 / 3, std::max(10, ah / 10), mix(e.art, white, 80),
         ax, ay, ax + aw, ay + ah);
    ring(im, ax + aw * 3 / 4, ay + ah / 3, ah / 2, 1, mix(e.art, white, 150), ax, ay, ax + aw,
         ay + ah);
    for (int row = 0; row < ah; ++row) {
        int edge = aw * 3 / 4 - row / 2;
        rect(im, ax, ay + row, std::max(0, edge), 1, dark);
        if (row < ah * 2 / 3) {
            rect(im, ax + std::max(0, edge), ay + row, std::min(10, aw - std::max(0, edge)), 1,
                 t.accent);
        }
    }
    text(im, n + 1 < 10 ? "0" + std::to_string(n + 1) : std::to_string(n + 1), ax + 16, ay + 20,
         big ? 7 : 4, mix(e.art, white, 190));
    text_right(im, "BS / ARCHIVE", ax + aw - 14, ay + ah / 2 - 10, 1, white);
    blend_rect(im, ax, ay + ah * 2 / 3, aw, ah - ah * 2 / 3, dark, 170);
    int scale = big ? 5 : 3;
    while (scale > 2 && text_width(e.title, scale) > aw - 24) {
        --scale;
    }
    const int sub_scale = std::max(2, scale - 2);
    int bottom = ay + ah - (big ? 16 : 10);
    if (e.subtitle[0]) {
        bottom -= 9 * sub_scale;
        text(im, e.subtitle, ax + (big ? 18 : 12), bottom, sub_scale, white, Ink::outline,
             mix(e.art, black, 170));
        bottom -= 3 * sub_scale;
    }
    text(im, e.title, ax + (big ? 18 : 12), bottom - 8 * scale, scale, white, Ink::outline,
         mix(e.art, black, 170));
    // Info strip: platform and status chip.
    const int iy = ay + ah + (info - 14) / 2;
    const bool is_ready = e.implemented && ready;
    std::string_view label = status_label(n, ready);
    const int cw = text_width(label, 2) + 14, cx = ax + aw - cw - 10;
    const bool fits = text_width(e.platform, 2) <= cx - ax - 20;
    text(im, fits || !e.platform_short ? e.platform : e.platform_short, ax + 12, iy, 2, t.muted);
    if (is_ready || e.implemented) {
        rounded(im, cx, iy - 4, cw, 22, 3, is_ready ? ready_green : t.accent);
    } else {
        rounded(im, cx, iy - 4, cw, 22, 3, mix(t.card, t.muted, 120));
        rounded(im, cx + 2, iy - 2, cw - 4, 18, 2, t.card);
    }
    text(im, label, cx + 7, iy, 2, is_ready ? ready_ink : (e.implemented ? t.ink : t.muted));
    if (p.focus < 1) {
        blend_rect(im, p.x, p.y, p.w + 8, p.h + 8, t.bg, int((1 - p.focus) * 110));
    }
}

void generation_panel(Image& im, int g, const LibraryView& v) {
    const auto& gen = generations[std::size_t(g)];
    const auto& t = gen.theme;
    const auto r = generation_rect(g, v);
    const bool selected = g == v.generation;
    panel(im, t, r.x, r.y, r.w, r.h, t.bg, selected ? t.accent : mix(t.bg, white, 40));
    const int first = first_game(g), count = game_count(g);
    const Color quiet = mix(t.bg, t.accent, 32);
    // Three cuts form an abstract velocity wing. Keep it inside each card.
    for (int row = 0; row < 100; ++row) {
        const int length = std::min(r.w - 36, 52 + row * 2);
        if (row % 34 < 26) {
            rect(im, r.x + r.w - 18 - length, r.y + 52 + row, length, 1,
                 selected ? t.accent2 : quiet);
        }
    }
    text(im, "0" + std::to_string(g + 1), r.x + 20, r.y + 24, 6,
         selected ? white : mix(white, t.bg, 90));
    badge(im, t, gen.bits, r.x + 20, r.y + 170, 3);
    wrapped(im, gen.systems, r.x + 20, r.y + 226, r.w - 40, 2, 3, white);
    int ready_count = 0;
    for (int k = 0; k < count; ++k) {
        ready_count +=
            catalog[std::size_t(first + k)].implemented && v.installed[std::size_t(first + k)];
    }
    const std::string line =
        std::to_string(count) + " titles / " + std::to_string(ready_count) + " ready";
    text(im, line, r.x + 20, r.y + r.h - 32, 2, t.accent);
}

void generation_screen(Image& im, const LibraryView& v) {
    frame_background(im, v.time);
    logo(im, 36, 30, LogoSize::title);
    text(im, "THE PLATFORM ARCHIVE  /  Choose a generation", 40, 106, 2, white, Ink::shadow, navy);
    int ready = 0;
    for (std::size_t n = 0; n < catalog.size(); ++n) {
        ready += catalog[n].implemented && v.installed[n];
    }
    text_right(im, std::to_string(ready) + " of " + std::to_string(catalog.size()) + " ready", 920,
               46, 2, white, Ink::shadow, navy);
    // Draw unselected panels first so the focused one overlaps its neighbours.
    for (int g = 0; g < int(generations.size()); ++g) {
        if (g != v.generation) {
            generation_panel(im, g, v);
        }
    }
    generation_panel(im, v.generation, v);
    std::string message = v.message;
    if (message.empty() &&
        std::none_of(v.installed.begin(), v.installed.end(), [](bool b) { return b; })) {
        message = "Open a generation to import your Game Gear or Game Boy Advance ROM, "
                  "or drop the ROM file here.";
    }
    if (!message.empty()) {
        panel(im, theme(0), 140, 520, 680, 58, white, frame_gold);
        wrapped(im, message, 160, 534, 640, 2, 2, navy);
    }
    hint_footer(im, {{std::string{icon_left} + std::string{icon_right}, "Generation"},
                     {"Enter", "Open"},
                     {"Esc", "Quit"}});
}

void zone_screen(Image& im, const LibraryView& v) {
    const int g = v.generation, first = first_game(g), count = game_count(g);
    const auto& t = theme(g);
    frame_background(im, v.time);
    zone_header(im, g, {}, {});
    for (int n = 0; n <= int(generations.size()); ++n) {
        auto r = tab_rect(n);
        const bool current = n == g + 1;
        const bool locked = n > 0 && !generations[std::size_t(n - 1)].available;
        std::string label = n == 0 ? "All" : generations[std::size_t(n - 1)].bits;
        const Theme& tt = n == 0 ? t : theme(n - 1);
        rounded(im, r.x, r.y, r.w, r.h, 5, current ? tt.accent : mix(navy, white, 30));
        int x = text(im, label, r.x + 12, r.y + 8, 2, current ? tt.ink : white);
        if (locked) {
            text(im, std::string(1, icon_lock), x + 6, r.y + 8, 2, tt.accent);
        }
    }
    text_right(im, "Q / E switch", tab_rect(0).x - 14, 58, 2, mix(white, t.accent, 100));
    // Carousel position animates with a slight overshoot.
    const int sel = v.selection - first;
    const double progress = ease_out((v.time - v.moved_at) / 0.3);
    const double pos = v.slide_from + (sel - v.slide_from) * progress;
    std::vector<int> order;
    for (int k = 0; k < count; ++k) {
        order.push_back(k);
    }
    std::sort(order.begin(), order.end(),
              [&](int a, int b) { return std::abs(a - pos) > std::abs(b - pos); });
    for (int k : order) {
        if (std::abs(k - pos) < 2.6) {
            game_card(im, first + k, card_place(k - pos), v.installed[std::size_t(first + k)]);
        }
    }
    // Position bar.
    const int total = count * 18 + 16;
    for (int k = 0, x = 480 - total / 2; k < count; ++k) {
        const bool on = k == sel;
        rect(im, x, 438, on ? 34 : 10, 6, on ? t.accent : mix(t.bg, white, 80));
        x += on ? 42 : 18;
    }
    // Information panel.
    const auto& e = catalog[std::size_t(v.selection)];
    panel(im, t, info_rect.x, info_rect.y, info_rect.w, info_rect.h, t.card, t.accent);
    text(im, full_title(e), info_rect.x + 24, info_rect.y + 16, 3, t.card_ink);
    std::string body = v.message;
    if (v.importing) {
        body = "Importing " + full_title(e) + " / " + e.platform + ". Keep this window open.";
    } else if (v.choosing) {
        body = "Choose a supported ROM in the file picker.";
    } else if (body.empty()) {
        const auto* game = title_for(v.selection);
        const bool acts = game && !game->levels.empty();
        body = e.implemented ? (v.ready ? (acts ? "Play the full game or start at any act."
                                                : "Play the full game from the start.")
                                        : "Import your " + full_title(e) + " " + e.platform +
                                              " ROM, or drop it here.")
                             : "Planned. Its native reconstruction has not started yet.";
    }
    wrapped(im, body, info_rect.x + 24, info_rect.y + 56, 440, 2, 2, t.muted);
    if (v.importing) {
        rect(im, info_rect.x + 24, info_rect.y + 100, 440, 8, mix(t.card, t.muted, 70));
        rect(im, info_rect.x + 24, info_rect.y + 100, int(440 * std::min(v.progress, 100u) / 100),
             8, t.accent2);
    }
    const bool ready = e.implemented && v.ready;
    if (ready && v.sound_test) {
        button(im, t, music_rect.x, music_rect.y, music_rect.w, music_rect.h,
               std::string(1, icon_note), false, 3);
    }
    std::string action =
        e.implemented ? (ready ? std::string(1, icon_play) + " Play" : "Import ROM") : "Not yet";
    button(im, t, action_rect.x, action_rect.y, action_rect.w, action_rect.h, action,
           e.implemented && !v.importing && !v.choosing, 3);
    if (ready) {
        const std::string arrows = std::string{icon_left} + std::string{icon_right};
        const std::string quick_load = std::string(platform::shortcut_modifier) + "+L";
        if (v.sound_test) {
            hint_footer(im, {{arrows, "Game"},
                             {"Q E", "Generation"},
                             {"Enter", "Play"},
                             {"M", "Music"},
                             {quick_load, "Quick load"},
                             {"Esc", "Back"}});
        } else {
            hint_footer(im, {{arrows, "Game"},
                             {"Q E", "Generation"},
                             {"Enter", "Play"},
                             {quick_load, "Quick load"},
                             {"Esc", "Back"}});
        }
    } else {
        hint_footer(im, {{std::string{icon_left} + std::string{icon_right}, "Game"},
                         {"Q E", "Generation"},
                         {"Enter", "Open"},
                         {"Esc", "Back"}});
    }
}
} // namespace

int first_game(int generation) {
    for (int n = 0; n < int(catalog.size()); ++n) {
        if (catalog[std::size_t(n)].generation == generation) {
            return n;
        }
    }
    return 0;
}

int game_count(int generation) {
    return int(std::count_if(catalog.begin(), catalog.end(),
                             [&](auto& e) { return e.generation == generation; }));
}

int library_hit(const LibraryView& v, int x, int y) {
    if (!v.zone) {
        // Topmost (focused) panel first.
        if (inside(generation_rect(v.generation, v), x, y)) {
            return hit_generation + v.generation;
        }
        for (int g = 0; g < int(generations.size()); ++g) {
            if (inside(generation_rect(g, v), x, y)) {
                return hit_generation + g;
            }
        }
        return -1;
    }
    for (int n = 0; n <= int(generations.size()); ++n) {
        if (inside(tab_rect(n), x, y)) {
            return n == 0 ? hit_back : hit_generation + n - 1;
        }
    }
    if (inside(action_rect, x, y)) {
        return hit_action;
    }
    const int first = first_game(v.generation), count = game_count(v.generation);
    const int sel = v.selection - first;
    // Centre card first: it overlaps its neighbours.
    for (int d : {0, -1, 1, -2, 2}) {
        int k = sel + d;
        if (k < 0 || k >= count) {
            continue;
        }
        const double progress = ease_out((v.time - v.moved_at) / 0.3);
        const double pos = v.slide_from + (sel - v.slide_from) * progress;
        auto p = card_place(k - pos);
        if (x >= p.x && x < p.x + p.w && y >= p.y && y < p.y + p.h) {
            return first + k;
        }
    }
    return -1;
}

bool library_music_hit(const LibraryView& v, int x, int y) {
    return v.zone && v.ready && v.sound_test && catalog[std::size_t(v.selection)].implemented &&
           inside(music_rect, x, y);
}

platform::Image library_image(const LibraryView& v) {
    Image im(960, 640);
    if (v.zone) {
        zone_screen(im, v);
    } else {
        generation_screen(im, v);
    }
    return im;
}

} // namespace shell
