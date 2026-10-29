// The in-game overlay: pause menu, enhancements page, messages, frame rate.

#include "app/shell_frame.hpp"
#include "platform/host.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace shell {

// --- Pause and enhancements -------------------------------------------------
int pause_hit(int x, int y) {
    if (x < 272 || x >= 688 || y < 202 || y >= 466) {
        return -1;
    }
    int row = (y - 202) / 44;
    return (y - 202) % 44 < 38 ? row : -1;
}

int enhancements_hit(int x, int y, int count) {
    if (x < 272 || x >= 688) {
        return -1;
    }
    if (y >= 214 && y < 214 + count * 44) {
        int row = (y - 214) / 44;
        return (y - 214) % 44 < 38 ? row : -1;
    }
    if (y >= 500 && y < 538) {
        return count;
    }
    return -1;
}

namespace {

struct EnhancementText {
    const char* name;
    const char* description;
};

EnhancementText enhancement_text(Enhancement which) {
    switch (which) {
    case Enhancement::shield_transparency:
        return {"Shield transparency",
                "A translucent shield replaces the original alternating frames."};
    case Enhancement::widescreen:
        return {"Expanded view",
                "See more of the level. Unchecked uses the original Game Gear view."};
    case Enhancement::water:
        return {"Enhanced water",
                "Pixelated refraction, translucent water, waves, splashes and rain on the lens."};
    case Enhancement::rings:
        return {"Scattered rings", "Lost rings scatter, bounce and slide with transparency."};
    case Enhancement::lcd_screen:
        break;
    }
    return {"LCD screen", "Game Gear screen: muted colours, motion smear and RGB stripes."};
}

} // namespace

platform::Image game_overlay(const PauseMenuView& view) {
    const std::string_view status = view.status, message = view.message;
    const bool paused = view.paused, muted = view.muted, saved = view.saved;
    const int selected = view.selection, enhancement_selection = view.enhancement_selection;
    const double since = view.since;
    const Enhancements& enhancements = view.enhancements;
    Image im(960, 640);
    fill(im, {0, 0, 0, 0});
    const auto& t = theme(0);
    const std::string mod = platform::shortcut_modifier; // "Cmd" or "Ctrl"
    if (!paused) {
        if (!message.empty()) {
            const int w = text_width(message, 2) + 40;
            panel(im, t, 24, 570, w, 36, t.accent, mix(t.accent, black, 100));
            text(im, message, 44, 581, 2, t.ink);
        }
        return im;
    }
    // Short eased entry keeps the paused frame visible around the menu.
    blend_rect(im, 0, 0, 960, 640, navy, 100);
    const double in = ease_out(since / 0.20);
    const int dx = int((1 - in) * -42);
    blend_rect(im, 0, 0, 960, 40, navy, 220);
    rect(im, 0, 40, 960, 3, t.accent);
    text(im, status, 24, 12, 2, white);
    text_right(im, "Esc / Start: menu", 800, 12, 2, t.accent);
    blend_rect(im, 0, 608, 960, 32, navy, 220);
    text(im, "Arrows: move   Space: jump   Down: roll   " + mod + "+S: save   " + mod + "+L: load",
         24, 617, 2, {200, 210, 230});
    if (muted) {
        text_right(im, "Muted", 936, 617, 2, t.accent2);
    }
    Image menu(960, 640);
    fill(menu, {0, 0, 0, 0});
    panel(menu, t, 244, 92, 472, 488, t.card, {90, 114, 138});
    rounded(menu, 245, 93, 470, 72, 9, navy);
    rect(menu, 245, 151, 470, 14, navy);
    rect(menu, 272, 158, 48, 3, t.accent);
    logo(menu, 486, 115, LogoSize::header);
    if (view.enhancements_open) {
        text(menu, "Enhancements", 262, 120, 3, white, Ink::outline, navy);
        text(menu, view.game, 272, 176, 2, t.muted);
        const int count = int(view.offered.size());
        Enhancements current = enhancements;
        for (int n = 0; n < count; ++n) {
            const auto which = view.offered[std::size_t(n)];
            const bool active = enhancement_selection == n;
            const int y = 214 + n * 44;
            if (active) {
                panel(menu, t, 272, y, 416, 38, t.accent, mix(t.accent, black, 100));
            } else {
                rect(menu, 272, y, 416, 38, mix(t.card, t.bg, 18));
            }
            const Color box = active ? t.ink : t.card_ink;
            rect(menu, 288, y + 9, 20, 20, box);
            rect(menu, 291, y + 12, 14, 14, active ? t.accent : t.card);
            if (setting(current, which)) {
                const Color mark = active ? t.ink : ready_green;
                rect(menu, 292, y + 18, 3, 3, mark);
                rect(menu, 295, y + 21, 3, 3, mark);
                rect(menu, 298, y + 18, 3, 3, mark);
                rect(menu, 301, y + 15, 3, 3, mark);
            }
            text(menu, enhancement_text(which).name, 324, y + 9, 3, box);
        }
        const char* description =
            enhancement_selection >= 0 && enhancement_selection < count
                ? enhancement_text(view.offered[std::size_t(enhancement_selection)]).description
                : "Return to the pause menu.";
        wrapped(menu, description, 272, 440, 416, 2, 2, t.muted);
        menu_row(menu, t, 500, "Back", enhancement_selection == count, false, since);
        text(menu, "Escape / B: back to pause menu", 272, 546, 2, t.muted);
    } else {
        text(menu, "Paused", 262, 117, 4, white, Ink::outline, navy);
        text(menu, view.game, 272, 174, 2, t.muted);
        const std::array<std::string, 6> labels = {
            "Resume",       "Quick save",       "Quick load", muted ? "Sound: off" : "Sound: on",
            "Enhancements", "Return to library"};
        // The shortcut keys, right-aligned in their rows.
        const std::array<std::string, 6> keys = {"", mod + "+S", saved ? mod + "+L" : "Empty",
                                                 "", "",         ""};
        for (int n = 0; n < 6; ++n) {
            menu_row(menu, t, 202 + n * 44, labels[std::size_t(n)], n == selected, n == 2 && !saved,
                     since, keys[std::size_t(n)]);
        }
        std::string hint = !message.empty() ? std::string(message)
                           : selected == 1  ? "Replaces this game's quick-save slot."
                           : selected == 2  ? "Restores the saved moment. Remains paused."
                           : selected == 5  ? "Ends this run. Your quick save is kept."
                                            : "Escape / B: resume";
        wrapped(menu, hint, 272, 480, 416, 3, 2, t.muted);
    }
    for (int y = 0; y < 640; ++y) {
        for (int x = 0; x < 960; ++x) {
            const int sx = x - dx;
            if (sx < 0 || sx >= 960) {
                continue;
            }
            auto c = menu.pixels[std::size_t(y) * 960 + sx];
            if (c.a) {
                im.pixels[std::size_t(y) * 960 + x] = c;
            }
        }
    }
    return im;
}

platform::Image frame_rate_image(std::optional<double> fps, int canvas_height) {
    char label[32];
    if (fps) {
        std::snprintf(label, sizeof(label), "%.1f FPS", *fps);
    } else {
        std::snprintf(label, sizeof(label), "-- FPS");
    }
    const int scale = std::clamp(canvas_height / 320, 2, 4);
    const int padding = 3 * scale;
    const int width = text_width(label, scale) + 2 * padding;
    Image image(width, 7 * scale + 2 * padding);
    fill(image, Color{12, 18, 26, 255});
    text(image, label, padding, padding, scale, white);
    return image;
}

} // namespace shell
