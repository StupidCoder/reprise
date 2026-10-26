#include "app/pause_menu.hpp"
#include "ui/shelf.hpp"
#include <algorithm>
#include <array>

namespace app {
using platform::Color;
using platform::Image;

namespace s = ui::shelf;
namespace {
constexpr int panel_x = 146, panel_y = 28, panel_w = 668, panel_h = 584;
constexpr int row_x = 170, row_w = 370, row_h = 39, first_row = 184, row_step = 44;
constexpr int visible_rows = 8;
int first_visible(int selected, int count) {
    return std::clamp(selected - visible_rows + 1, 0, std::max(0, count - visible_rows));
}
int row_at(int x, int y, int count, int first = 0) {
    if (x < row_x || x >= row_x + row_w || y < first_row) return -1;
    const int row = (y - first_row) / row_step;
    return row < visible_rows && row + first < count && (y - first_row) % row_step < row_h ? row + first : -1;
}
void row(Image& im, int slot, const std::string& label, const std::string& right, bool selected, bool disabled) {
    const int y = first_row + slot * row_step;
    ui::rounded(im, row_x, y, row_w, row_h, 6, selected ? s::cobalt : s::paper);
    const Color c = selected ? s::white : disabled ? s::muted : s::ink;
    const int rw = right.empty() ? 0 : s::width(right, 14, true) + 22;
    s::clipped(im, label, row_x + 13, y + 9, row_w - rw - 26, 16, c, selected);
    if (!right.empty()) s::right(im, right, row_x + row_w - 13, y + 11, 14, c, true);
}
bool hit(const platform::Controls& in, int x, int y, int w, int h) {
    return in.click && in.mouse_x >= x && in.mouse_x < x + w && in.mouse_y >= y && in.mouse_y < y + h;
}
} // namespace

std::vector<PauseMenu::OptionRow> PauseMenu::option_rows(const GameModule& module) const {
    std::vector<OptionRow> rows;
    const auto options = module.options();
    for (std::size_t i = 0; i < options.size(); ++i) {
        if (options[i].group == group_) {
            rows.push_back({int(i), {}});
        }
    }
    if (group_.empty()) {
        for (const auto& o : options) { // each group's page, once
            const bool listed = std::any_of(rows.begin(), rows.end(),
                                            [&](const OptionRow& r) { return r.group == o.group; });
            if (!o.group.empty() && !listed) {
                rows.push_back({-1, o.group});
            }
        }
    }
    rows.push_back({}); // Back
    return rows;
}

void PauseMenu::show(double now) {
    open_ = true;
    options_page_ = false;
    group_.clear();
    selection_ = 0;
    help_page_ = false;
    option_selection_ = 0;
    (void)now;
}

PauseMenu::Action PauseMenu::update(const platform::Controls& in, GameModule& module, double now) {
    if (help_page_) {
        if (in.back || in.accept || in.info || hit(in, 690, 557, 98, 32)) help_page_ = false;
        return Action::none;
    }
    if (options_page_ && (in.info || hit(in, 578, 498, 202, 30))) { help_page_ = true; return Action::none; }
    if (options_page_) {
        const auto options = module.options();
        const auto rows = option_rows(module);
        const int count = int(rows.size());
        option_selection_ = (option_selection_ + count + in.nav_down - in.nav_up) % count;
        option_selection_ = std::clamp(option_selection_, 0, count - 1);
        const int old_first = first_visible(option_selection_, count);
        if (in.wheel || in.page_up || in.page_down || hit(in, 572, 557, 40, 32) || hit(in, 626, 557, 40, 32))
            option_selection_ = std::clamp(option_selection_ + in.wheel +
                (int(in.page_down || hit(in, 626, 557, 40, 32)) - int(in.page_up || hit(in, 572, 557, 40, 32))) * visible_rows, 0, count - 1);
        int clicked = in.click ? row_at(in.mouse_x, in.mouse_y, count, old_first) : -1;
        if (clicked >= 0) {
            option_selection_ = clicked;
        }
        const auto& chosen = rows[std::size_t(option_selection_)];
        const bool pressed = in.accept || clicked >= 0;
        const bool back = in.back || hit(in, 690, 557, 98, 32) || (pressed && chosen.option < 0 && chosen.group.empty());
        if (back) {
            if (group_.empty()) {
                options_page_ = false;
            } else {
                // Back to the options page, on the group's row.
                const std::string left = group_;
                group_.clear();
                const auto parent = option_rows(module);
                option_selection_ = 0;
                for (std::size_t i = 0; i < parent.size(); ++i) {
                    if (parent[i].group == left) {
                        option_selection_ = int(i);
                    }
                }
            }
            return Action::none;
        }
        if (!chosen.group.empty()) {
            if (pressed || in.nav_right) {
                group_ = chosen.group;
                option_selection_ = 0;
            }
            return Action::none;
        }
        if (chosen.option < 0) return Action::none;
        const auto& o = options[std::size_t(chosen.option)];
        const int n = int(o.choices.size());
        int step = in.nav_right - in.nav_left;
        if (pressed) {
            step = 1;
        }
        if (step && n > 0) {
            module.set_option(std::size_t(chosen.option), (o.value + step + n) % n);
        }
        (void)now;
        return Action::none;
    }
    selection_ = (selection_ + items + in.nav_down - in.nav_up) % items;
    int clicked = in.click ? row_at(in.mouse_x, in.mouse_y, items) : -1;
    if (clicked >= 0) {
        selection_ = clicked;
    }
    if (in.back || hit(in, 690, 557, 98, 32)) {
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
        group_.clear();
        option_selection_ = 0;
        return Action::none;
    default:
        open_ = false;
        return Action::quit;
    }
}

Image PauseMenu::draw(const GameModule& module, const std::string& title, double now, const std::string& cover_id) const {
    Image im(platform::ui_width, platform::ui_height);
    ui::fill(im, {0, 0, 0, 0});
    // Stable bounds keep pointer hit targets aligned even on the opening frame.
    ui::rounded(im, panel_x + 4, panel_y + 6, panel_w, panel_h, 12, {12, 26, 30, 120});
    ui::rounded(im, panel_x, panel_y, panel_w, panel_h, 12, s::card);
    s::cover(im, cover_id, 170, 48, 620, 78);
    const std::string heading = options_page_ ? (group_.empty() ? "Options" : group_) : "Paused";
    s::text(im, heading, 170, 137, 28, s::ink, true);
    s::right(im, title, 790, 148, 16, s::muted);
    ui::rect(im, 558, 184, 1, 346, s::line);
    std::string description;
    std::string help_title;
    if (options_page_) {
        const auto options = module.options();
        const auto rows = option_rows(module);
        const int selected_index = std::clamp(option_selection_, 0, int(rows.size()) - 1);
        const int first = first_visible(selected_index, int(rows.size()));
        for (int i = first; i < std::min(first + visible_rows, int(rows.size())); ++i) {
            const auto& r = rows[std::size_t(i)];
            const bool selected = selected_index == i;
            if (r.option >= 0) {
                const auto& o = options[std::size_t(r.option)];
                row(im, i - first, o.name, "<  " + o.choices[std::size_t(o.value)] + "  >", selected, false);
                if (selected) { description = o.description; help_title = o.name; }
            } else if (!r.group.empty()) {
                row(im, i - first, r.group, ">", selected, false);
                if (selected) { description = "Open the " + r.group + " settings."; help_title = r.group; }
            } else {
                row(im, i - first, "Back", "", selected, false);
                if (selected) { description = group_.empty() ? "Return to the pause menu." : "Return to the options."; help_title = "Back"; }
            }
        }
        if (rows.size() > visible_rows) {
            const int track_h = visible_rows * row_step - 5;
            ui::rect(im, 547, first_row, 3, track_h, s::paper);
            const int thumb_h = track_h * visible_rows / int(rows.size());
            ui::rect(im, 547, first_row + (track_h - thumb_h) * first / (int(rows.size()) - visible_rows), 3, thumb_h, s::muted);
        }
        s::wrap(im, "Left / Right or Enter to change. Settings are saved automatically.", 578, 446, 202, 3, 13);
        s::button(im, "Read more  /  I", 578, 498, 202, 30);
    } else {
        const std::array<std::string, items> labels = {"Resume", "Restart game", "Quick save", "Quick load",
            muted_ ? "Sound: off" : "Sound: on", "Options", "Return to library"};
        const std::string mod = platform::shortcut_modifier;
        const std::array<std::string, items> keys = {"Esc", "", mod + "+S", module.has_save() ? mod + "+L" : "Empty", "M", ">", ""};
        const std::array<std::string, items> descriptions = {
            "Pick up exactly where you left off.", "Start this game again from its beginning.",
            "Keep a snapshot of your progress so you can return to it later.",
            module.has_save() ? "Continue from your last quick save." : "There is no quick save for this game yet.",
            "Turn the game's sound on or off.", "Adjust presentation, enhancements and game options.",
            "Close this game and browse your collection."};
        for (int i = 0; i < items; ++i) row(im, i, labels[std::size_t(i)], keys[std::size_t(i)], selection_ == i,
                                           i == load_item && !module.has_save());
        help_title = labels[std::size_t(selection_)];
        description = descriptions[std::size_t(selection_)];
        if (!message_.empty() && now - message_time_ < 3) s::wrap(im, message_, 578, 450, 202, 4, 14, s::green);
    }
    s::wrap(im, help_title, 578, 190, 202, 2, 19, s::ink);
    s::wrap(im, description, 578, 248, 202, 9, 15);
    if (help_page_) {
        ui::rect(im, 170, 182, 620, 352, s::card);
        s::text(im, help_title, 170, 188, 23, s::ink, true);
        s::wrap(im, description, 170, 234, 610, 14, 16);
    }
    ui::rect(im, 170, 545, 620, 1, s::line);
    s::text(im, help_page_ ? "Esc or Enter: back to options" : "Arrows: choose    Enter: select", 170, 564, 14, s::muted);
    if (options_page_ && !help_page_ && option_rows(module).size() > visible_rows) {
        s::button(im, "Up", 572, 557, 40, 32);
        s::button(im, "Down", 626, 557, 40, 32);
    }
    s::button(im, options_page_ ? "Back" : "Resume", 690, 557, 98, 32);
    return im;
}

} // namespace app
