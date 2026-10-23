#include "app/pause_menu.hpp"
#include "ui/draw.hpp"
#include <algorithm>
#include <array>

namespace app {
using platform::Color;
using platform::Image;

namespace {
constexpr int panel_x = 250, panel_y = 96, panel_w = 460, panel_h = 470;
constexpr int row_x = 274, row_w = 412, row_h = 38, first_row = 196, row_step = 44;
constexpr Color ink{232, 236, 248}, muted_ink{138, 146, 178}, accent{255, 214, 92},
    panel_colour{20, 23, 44}, row_colour{30, 34, 62}, good{124, 222, 140};

int row_at(int x, int y, int count) {
    if (x < row_x || x >= row_x + row_w || y < first_row) {
        return -1;
    }
    const int row = (y - first_row) / row_step;
    return row < count && (y - first_row) % row_step < row_h ? row : -1;
}

void row(Image& im, int index, const std::string& label, const std::string& right, bool selected,
         bool disabled) {
    const int y = first_row + index * row_step;
    ui::rect(im, row_x, y, row_w, row_h, selected ? accent : row_colour);
    const Color c = selected ? Color{16, 18, 36} : disabled ? muted_ink : ink;
    ui::text(im, label, row_x + 16, y + 10, 2, c);
    if (!right.empty()) {
        ui::text_right(im, right, row_x + row_w - 16, y + 10, 2, c);
    }
}
} // namespace

void PauseMenu::show(double now) {
    open_ = true;
    options_page_ = false;
    selection_ = 0;
    opened_ = now;
}

PauseMenu::Action PauseMenu::update(const platform::Controls& in, GameModule& module, double now) {
    if (options_page_) {
        const auto options = module.options();
        const int count = int(options.size()) + 1; // + Back
        option_selection_ = (option_selection_ + count + in.nav_down - in.nav_up) % count;
        int clicked = in.click ? row_at(in.mouse_x, in.mouse_y, count) : -1;
        if (clicked >= 0) {
            option_selection_ = clicked;
        }
        const bool back = in.back || ((in.accept || clicked >= 0) && option_selection_ == count - 1);
        if (back) {
            options_page_ = false;
            return Action::none;
        }
        if (option_selection_ < int(options.size())) {
            const auto& o = options[std::size_t(option_selection_)];
            const int n = int(o.choices.size());
            int step = in.nav_right - in.nav_left;
            if (in.accept || clicked >= 0) {
                step = 1;
            }
            if (step) {
                module.set_option(std::size_t(option_selection_), (o.value + step + n) % n);
            }
        }
        (void)now;
        return Action::none;
    }
    selection_ = (selection_ + items + in.nav_down - in.nav_up) % items;
    int clicked = in.click ? row_at(in.mouse_x, in.mouse_y, items) : -1;
    if (clicked >= 0) {
        selection_ = clicked;
    }
    if (in.back) {
        open_ = false;
        return Action::resume;
    }
    if (!in.accept && clicked < 0) {
        return Action::none;
    }
    switch (selection_) {
    case resume_item:
        open_ = false;
        return Action::resume;
    case restart_item:
        open_ = false;
        return Action::restart;
    case save_item:
        return Action::save;
    case load_item:
        return Action::load;
    case sound_item:
        muted_ = !muted_;
        return Action::none;
    case options_item:
        options_page_ = true;
        option_selection_ = 0;
        return Action::none;
    default:
        open_ = false;
        return Action::quit;
    }
}

Image PauseMenu::draw(const GameModule& module, const std::string& title, double now) const {
    Image im(platform::ui_width, platform::ui_height);
    ui::fill(im, {0, 0, 0, 0});
    // The panel slides in from the left.
    const double in = ui::ease_out((now - opened_) / 0.22);
    const int dx = int((1 - in) * -520);
    ui::rect(im, panel_x + 8 + dx, panel_y + 8, panel_w, panel_h, {4, 5, 10});
    ui::rect(im, panel_x + dx, panel_y, panel_w, panel_h, accent);
    ui::rect(im, panel_x + 4 + dx, panel_y + 4, panel_w - 8, panel_h - 8, panel_colour);
    Image menu(platform::ui_width, platform::ui_height);
    ui::fill(menu, {0, 0, 0, 0});
    if (options_page_) {
        ui::text(menu, "Options", row_x, 118, 4, ink);
        ui::text(menu, title, row_x, 162, 2, muted_ink);
        const auto options = module.options();
        for (std::size_t i = 0; i < options.size(); ++i) {
            const auto& o = options[i];
            row(menu, int(i), o.name, std::string(1, ui::icon_left) + " " +
                                          o.choices[std::size_t(o.value)] + " " + ui::icon_right,
                option_selection_ == int(i), false);
        }
        row(menu, int(options.size()), "Back", "", option_selection_ == int(options.size()), false);
        const std::string description =
            option_selection_ < int(options.size()) ? options[std::size_t(option_selection_)].description
                                                    : "Return to the pause menu.";
        ui::wrapped(menu, description, row_x, first_row + (int(options.size()) + 1) * row_step + 20,
                    row_w, 4, 2, muted_ink);
        ui::text(menu, "Left/Right or Enter: change   Esc: back", row_x, 536, 2, muted_ink);
    } else {
        ui::text(menu, "Paused", row_x, 118, 4, ink);
        ui::text(menu, title, row_x, 162, 2, muted_ink);
        const std::array<std::string, items> labels = {
            "Resume", "Restart game", "Quick save", "Quick load",
            muted_ ? "Sound: off" : "Sound: on", std::string("Options ") + ui::icon_right,
            "Return to launcher"};
        const std::string mod = platform::shortcut_modifier;
        const std::array<std::string, items> keys = {
            "Esc", "", mod + "+S", module.has_save() ? mod + "+L" : "Empty", "M", "", ""};
        for (int i = 0; i < items; ++i) {
            row(menu, i, labels[std::size_t(i)], keys[std::size_t(i)], selection_ == i,
                i == load_item && !module.has_save());
        }
        if (!message_.empty() && now - message_time_ < 3) {
            ui::text(menu, message_, row_x, 520, 2, good);
        }
        ui::text(menu, "Arrows, Enter or the mouse", row_x, 540, 2, muted_ink);
    }
    for (int y = 0; y < im.height; ++y) {
        for (int x = 0; x < im.width; ++x) {
            const int sx = x - dx;
            if (sx < 0 || sx >= im.width) {
                continue;
            }
            const auto c = menu.pixels[std::size_t(y * im.width + sx)];
            if (c.a) {
                im.pixels[std::size_t(y * im.width + x)] = c;
            }
        }
    }
    return im;
}

} // namespace app
