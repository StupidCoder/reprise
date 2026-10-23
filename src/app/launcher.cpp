#include "app/launcher.hpp"
#include "app/catalog.hpp"
#include "ui/draw.hpp"
#include <algorithm>
#include <cmath>
#include <exception>
#include <vector>

namespace app {
using platform::Color;
using platform::Image;

namespace {
// Layout in the 960x640 design space.
constexpr int margin = 64, card_w = 196, card_h = 146, card_gap = 18;
constexpr int shelf_y[] = {136, 318}; // C64, Amiga
constexpr int panel_y = 490, panel_h = 108, hints_y = 610;

constexpr Color background{13, 15, 30}, background_dots{24, 28, 52}, text_main{232, 236, 248},
    text_muted{138, 146, 178}, highlight{255, 214, 92}, good{124, 222, 140}, bad{255, 120, 104};

// Card colours per machine. C64: the machine's dark and light blue. Amiga:
// the blue, white and orange of its desktop.
struct Look {
    Color body, border, ink, accent;
};
constexpr Look c64_look{{53, 40, 121}, {108, 94, 181}, {170, 160, 236}, {108, 94, 181}};
constexpr Look amiga_look{{0, 85, 170}, {236, 240, 248}, {255, 255, 255}, {255, 136, 0}};

const Look& look(Machine machine) {
    return machine == Machine::c64 ? c64_look : amiga_look;
}

int shelf(Machine machine) {
    return machine == Machine::c64 ? 0 : 1;
}

// Games on a shelf, in catalog order.
std::vector<std::size_t> shelf_games(const std::vector<GameEntry>& games, int row) {
    std::vector<std::size_t> result;
    for (std::size_t i = 0; i < games.size(); ++i) {
        if (shelf(games[i].machine) == row) {
            result.push_back(i);
        }
    }
    return result;
}

struct Place {
    int row, column;
};

Place place(const std::vector<GameEntry>& games, std::size_t game) {
    const int row = shelf(games[game].machine);
    const auto on_shelf = shelf_games(games, row);
    const auto at = std::find(on_shelf.begin(), on_shelf.end(), game) - on_shelf.begin();
    return {row, int(at)};
}

int card_x(int column) {
    return margin + column * (card_w + card_gap);
}

// The nine-pin joystick port the C64 and the Amiga share, as the logo mark:
// a D-shaped shell, wider at the top, with five pins over four.
void port_mark(Image& image, int x, int y, int scale) {
    const int w = 30 * scale, h = 16 * scale;
    for (int row = 0; row < h; ++row) {
        const int inset = row * 3 * scale / h + (row < scale || row >= h - scale ? scale : 0);
        ui::rect(image, x + inset, y + row, w - 2 * inset, 1, {196, 202, 222});
    }
    for (int row = scale; row < h - scale; ++row) {
        const int inset = row * 3 * scale / h + scale;
        ui::rect(image, x + inset + scale, y + row, w - 2 * inset - 2 * scale, 1, {34, 38, 60});
    }
    for (int pin = 0; pin < 5; ++pin) {
        ui::rect(image, x + 5 * scale + pin * 4 * scale, y + 4 * scale, 2 * scale, 2 * scale,
                 highlight);
    }
    for (int pin = 0; pin < 4; ++pin) {
        ui::rect(image, x + 7 * scale + pin * 4 * scale, y + 9 * scale, 2 * scale, 2 * scale,
                 highlight);
    }
}

std::string size_text(std::uintmax_t bytes) {
    std::string digits = std::to_string(bytes);
    for (int at = int(digits.size()) - 3; at > 0; at -= 3) {
        digits.insert(std::size_t(at), ",");
    }
    return digits + " bytes";
}
} // namespace

Launcher::Launcher(Library& library) : library_(library) {
    // Start on the first imported game, if any.
    for (std::size_t i = 0; i < library_.games().size(); ++i) {
        if (library_.installed(i)) {
            selected_ = i;
            break;
        }
    }
}

void Launcher::move(int columns, int rows) {
    const auto& games = library_.games();
    auto [row, column] = place(games, selected_);
    if (rows) {
        const int target = std::clamp(row + rows, 0, 1);
        const auto on_shelf = shelf_games(games, target);
        if (target == row || on_shelf.empty()) {
            return;
        }
        selected_ = on_shelf[std::size_t(std::min(column, int(on_shelf.size()) - 1))];
    } else {
        const auto on_shelf = shelf_games(games, row);
        selected_ = on_shelf[std::size_t(std::clamp(column + columns, 0, int(on_shelf.size()) - 1))];
    }
}

void Launcher::open_details(std::size_t page) {
    if (!library_.installed(selected_)) {
        return;
    }
    details_ = true;
    previews_.clear();
    const auto& game = library_.games()[selected_];
    if (game.previews) {
        try {
            previews_ = game.previews(library_.content_directory(selected_));
        } catch (const std::exception& e) {
            message_ = std::string("The imported data could not be decoded: ") + e.what();
            message_ok_ = false;
            message_time_ = 0;
        }
    }
    page_ = std::min(page, previews_.size());
}

void Launcher::close_details() {
    details_ = false;
    previews_.clear();
}

void Launcher::activate() {
    const auto& game = library_.games()[selected_];
    if (!game.import) {
        return; // planned
    }
    if (library_.installed(selected_) && game.start) {
        pending_play_ = true;
    } else if (library_.installed(selected_)) {
        open_details(); // nothing to play yet; show what was imported
    } else {
        pending_choice_ = true;
    }
}

Launcher::Request Launcher::update(const platform::Controls& in, double now) {
    if (details_) {
        const std::size_t pages = previews_.size() + 1;
        if (in.nav_right || in.nav_down) {
            page_ = (page_ + 1) % pages;
        } else if (in.nav_left || in.nav_up) {
            page_ = (page_ + pages - 1) % pages;
        } else if (in.back || in.accept) {
            close_details();
        } else if (in.remove) {
            library_.remove(selected_);
            close_details();
            message_ = library_.games()[selected_].title + ": imported files removed.";
            message_ok_ = true;
            message_time_ = now;
        }
        return Request::none;
    }
    const auto before = selected_;
    if (in.nav_left) {
        move(-1, 0);
    }
    if (in.nav_right) {
        move(1, 0);
    }
    if (in.nav_up) {
        move(0, -1);
    }
    if (in.nav_down) {
        move(0, 1);
    }
    if (in.click) {
        for (std::size_t i = 0; i < library_.games().size(); ++i) {
            const auto [row, column] = place(library_.games(), i);
            const int x = card_x(column), y = shelf_y[row];
            if (in.mouse_x >= x && in.mouse_x < x + card_w && in.mouse_y >= y &&
                in.mouse_y < y + card_h) {
                if (i == selected_) {
                    activate();
                }
                selected_ = i;
            }
        }
    }
    if (in.accept) {
        activate();
    }
    if ((in.remove || in.info) && library_.installed(selected_)) {
        open_details(); // removal is confirmed on the details screen
    }
    if (selected_ != before) {
        selected_time_ = now;
    }
    if (pending_choice_) {
        pending_choice_ = false;
        return Request::choose_image;
    }
    if (pending_play_) {
        pending_play_ = false;
        return Request::play;
    }
    return Request::none;
}

void Launcher::show_message(std::string message, bool ok, double now) {
    message_ = std::move(message);
    message_ok_ = ok;
    message_time_ = now;
}

void Launcher::import(const std::filesystem::path& image, double now) {
    const auto& games = library_.games();
    const auto outcome =
        library_.import(image, games[selected_].import ? std::optional(selected_) : std::nullopt);
    if (outcome.game) {
        selected_ = *outcome.game;
        selected_time_ = now;
    }
    message_ = outcome.message;
    message_ok_ = outcome.ok;
    message_time_ = now;
}

void Launcher::show_error(std::string message, double now) {
    message_ = std::move(message);
    message_ok_ = false;
    message_time_ = now;
}

void Launcher::draw_card(Image& image, std::size_t index, int x, int y, double now) const {
    const auto& game = library_.games()[index];
    const auto& colours = look(game.machine);
    const bool selected = index == selected_ && !details_;
    const bool planned = !game.import;
    if (selected) {
        y -= int(std::lround(5 * ui::ease_back((now - selected_time_) * 5)));
    }
    auto tone = [&](Color c) { return planned ? ui::mix(c, background, 120) : c; };

    ui::rect(image, x + 6, y + 6, card_w, card_h, {6, 7, 14}); // block shadow
    if (selected) {
        ui::rect(image, x - 4, y - 4, card_w + 8, card_h + 8, highlight);
    }
    ui::rect(image, x, y, card_w, card_h, tone(colours.border));
    ui::rect(image, x + 4, y + 4, card_w - 8, card_h - 8, tone(colours.body));
    ui::rect(image, x + 4, y + 4, card_w - 8, 6, tone(colours.accent));

    ui::text(image, machine_name(game.machine), x + 14, y + 20, 2, tone(colours.ink));
    ui::wrapped(image, game.title, x + 14, y + 46, card_w - 28, 2, 3, tone(ui::white));

    const char* state = planned                    ? "PLANNED"
                        : !library_.installed(index) ? "IMPORT"
                        : game.start                 ? "PLAY"
                                                     : "IMPORTED";
    const Color chip = planned                      ? Color{70, 74, 100}
                       : !library_.installed(index) ? Color{236, 240, 248}
                                                    : good;
    const int chip_w = ui::text_width(state, 2) + 14;
    ui::rect(image, x + 14, y + card_h - 34, chip_w, 22, chip);
    ui::text(image, state, x + 21, y + card_h - 30, 2,
             planned ? Color{150, 154, 180} : Color{16, 18, 36});
}

void Launcher::draw_details(Image& image) const {
    const auto& game = library_.games()[selected_];
    const int x = 64, y = 40, w = image.width - 128, h = image.height - 80;
    ui::blend_rect(image, 0, 0, image.width, image.height, {0, 0, 0}, 170);
    ui::rect(image, x + 8, y + 8, w, h, {4, 5, 10});
    ui::rect(image, x, y, w, h, look(game.machine).border);
    ui::rect(image, x + 4, y + 4, w - 8, h - 8, {20, 23, 44});
    ui::text(image, game.title, x + 28, y + 24, 3, ui::white);
    ui::text(image, library_.release(selected_), x + 28, y + 60, 2, text_muted);
    const std::size_t pages = previews_.size() + 1;
    ui::text_right(image, std::to_string(page_ + 1) + " / " + std::to_string(pages), x + w - 28,
                   y + 28, 2, text_muted);

    const int body_y = y + 96, body_h = h - 96 - 64;
    if (page_ == 0) {
        int line = body_y;
        ui::text(image, "Imported files", x + 28, line, 2, highlight);
        line += 30;
        for (const auto& [name, size] : library_.installed_files(selected_)) {
            ui::text(image, name, x + 44, line, 2, text_main);
            ui::text_right(image, size_text(size), x + 356, line, 2, text_muted);
            line += 26;
        }
        line += 20;
        ui::wrapped(image,
                    previews_.empty()
                        ? "The game itself is still being rebuilt. Its graphics, levels and rules "
                          "will be read from these files once they are reconstructed."
                        : "The game itself is still being rebuilt. The next pages show the "
                          "graphics and levels decoded from these files so far.",
                    x + 28, line, w - 56, 3, 2, text_main);
    } else {
        const auto& preview = previews_[page_ - 1];
        ui::text(image, preview.title, x + 28, body_y, 2, highlight);
        ui::rect(image, x + 28, body_y + 28, w - 56, body_h - 28, {0, 0, 0});
        ui::picture(image, preview.image, x + 36, body_y + 36, w - 72, body_h - 44);
    }

    int hint = x + 28;
    if (pages > 1) {
        hint = ui::key_hint(image, "Arrows", "Pages", hint, y + h - 46);
    }
    hint = ui::key_hint(image, "Esc", "Back", hint, y + h - 46);
    ui::key_hint(image, "Del", "Remove imported files", hint, y + h - 46);
}

Image Launcher::draw(double now) const {
    Image image(platform::ui_width, platform::ui_height);
    ui::fill(image, background);
    ui::dots(image, 0, 0, image.width, image.height, 24, background_dots);

    // Header: logo mark, name and a line about the collection.
    port_mark(image, margin, 26, 3);
    ui::text(image, "Joyport", margin + 112, 22, 6, text_main, ui::Ink::shadow, {44, 50, 92});
    ui::text(image, "C64 and Amiga games, rebuilt to run natively", margin + 114, 78, 2,
             text_muted);

    const char* shelf_names[] = {"COMMODORE 64", "AMIGA"};
    for (int row = 0; row < 2; ++row) {
        ui::text(image, shelf_names[row], margin, shelf_y[row] - 26, 2, text_muted);
        const auto on_shelf = shelf_games(library_.games(), row);
        for (std::size_t column = 0; column < on_shelf.size(); ++column) {
            draw_card(image, on_shelf[column], card_x(int(column)), shelf_y[row], now);
        }
    }

    // Details of the selected game.
    const auto& game = library_.games()[selected_];
    ui::rect(image, margin, panel_y, image.width - 2 * margin, panel_h, {22, 25, 48});
    ui::rect(image, margin, panel_y, 4, panel_h, look(game.machine).accent);
    ui::text(image, game.title, margin + 24, panel_y + 16, 3, ui::white);
    ui::text(image, machine_name(game.machine),
             margin + 24 + ui::text_width(game.title, 3) + 14, panel_y + 22, 2, text_muted);
    std::string status;
    if (!game.import) {
        status = "Planned for a later version of Joyport.";
    } else if (!library_.installed(selected_)) {
        status = "Import your own copy: drop " + game.media +
                 " onto this window, or press Enter to choose the file.";
    } else if (!game.start) {
        status = "Imported: " + library_.release(selected_) +
                 ". The game itself is being rebuilt; Enter shows the imported files.";
    } else {
        status = game.progress.empty() ? "Ready to play." : game.progress;
    }
    ui::wrapped(image, status, margin + 24, panel_y + 52, image.width - 2 * margin - 48, 2, 2,
                text_main);

    // Import outcome, shown for a while.
    if (!message_.empty() && now - message_time_ < 8) {
        ui::text(image, message_, margin + 24, hints_y - 30 - 8, 2, message_ok_ ? good : bad);
    }

    int hint = margin;
    hint = ui::key_hint(image, "Arrows", "Choose", hint, hints_y);
    if (game.import) {
        const bool installed = library_.installed(selected_);
        hint = ui::key_hint(image, "Enter",
                            !installed ? "Import" : game.start ? "Play" : "Details", hint, hints_y);
        if (installed && game.start) {
            hint = ui::key_hint(image, "I", "Data", hint, hints_y);
        }
    }
    if (library_.installed(selected_)) {
        ui::key_hint(image, "Del", "Remove", hint, hints_y);
    }

    if (details_) {
        draw_details(image);
    }
    return image;
}

} // namespace app
