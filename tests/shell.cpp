// Interaction checks against the real catalog, without importing or changing any
// user data. A fake module isolates menu navigation from game rules.
#include "app/catalog.hpp"
#include "app/launcher.hpp"
#include "app/pause_menu.hpp"
#include "platform/type.hpp"
#include "ui/shelf.hpp"
#include "platform/resources.hpp"
#include <SDL3/SDL.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <set>

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
struct Module : app::GameModule {
    std::vector<app::GameOption> settings;
    int edits{};
    Module() {
        for (int i = 0; i < 12; ++i) settings.push_back({"Setting " + std::to_string(i), {"Off", "On"}, 0, "An option description."});
        settings.push_back({"Grouped setting", {"Off", "On"}, 0, "A grouped option.", "Cheats"});
    }
    double step_seconds() const override { return .02; }
    void tick(const app::GameInput&) override {}
    platform::Image picture() const override { return {1, 1}; }
    double pixel_aspect() const override { return 1; }
    std::vector<app::GameOption> options() const override { return settings; }
    void set_option(std::size_t i, int value) override { settings.at(i).value = value; ++edits; }
};
platform::Controls click(int x, int y) {
    platform::Controls in; in.click = true; in.mouse_x = x; in.mouse_y = y; return in;
}
int main() {
    try {
        const auto root = std::filesystem::temp_directory_path() / ("joyport-shell-test-" + std::to_string(SDL_GetTicksNS()));
        app::Library library(root, app::catalog()); // read-only, deliberately nonexistent
        app::Launcher shell(library);
        using Request = app::Launcher::Request;
        platform::Controls right, left, down, up, enter, back, info, pgdown;
        right.nav_right = true; left.nav_left = true; down.nav_down = true; up.nav_up = true;
        enter.accept = true; back.back = true; info.info = true; pgdown.page_down = true;
        check(shell.update(enter, 1) == Request::choose_image, "Uninstalled supported game must request import");
        shell.update(right, 1); check(shell.selected() == 1, "Right must select Bruce Lee");
        shell.update(up, 1); check(shell.selected() == 1, "Top boundary must preserve column");
        shell.update(down, 1); check(shell.selected() == 4, "Down must select Marble Madness in the reordered grid");
        shell.update(down, 1); shell.update(down, 1); check(shell.selected() == 7, "All planned games must be reachable");
        shell.draw(1); // includes the last row's covers, with selection scrolled into view
        check(shell.update(enter, 1) == Request::none, "Planned game must not start or import");
        shell.draw(1); shell.update(back, 1);
        shell.select(0); shell.update(left, 1); shell.update(down, 1); shell.update(down, 1);
        shell.update(right, 1); check(shell.selected() == 4, "Keyboard filter must select first Amiga entry");
        shell.update(left, 1); shell.update(down, 1); shell.update(right, 1);
        check(shell.selected() == 3, "Planned filter must select Mindwalker");
        shell.update(click(40, 125), 1); // All games
        shell.select(0); shell.update(click(915, 80), 1);
        check(shell.selected() == 3, "Pointer paging must reveal the planned titles");
        shell.update(click(600, 400), 1); check(shell.selected() == 7, "Pointer hit testing must follow scrolling");
        shell.update(info, 1); shell.draw(1); shell.update(back, 1);
        for (std::size_t i = 0; i < library.games().size(); ++i) {
            shell.select(i); const auto image = shell.draw(1);
            check(image.width == 960 && image.height == 640, "All cards must render in logical canvas");
            shell.open_details(); shell.draw(1); shell.update(back, 1);
        }
        shell.select(0); shell.open_details();
        check(shell.update(click(50, 440), 1) == Request::choose_image, "Detail import button must work");
        shell.update(back, 1);
        check(shell.update(click(820, 35), 1) == Request::choose_image, "Global import button must work");
        Module module;
        app::PauseMenu menu;
        using Action = app::PauseMenu::Action;
        menu.show(0); menu.show_options();
        menu.update(right, module, 1); check(module.settings[0].value == 1, "Option value must change");
        menu.update(pgdown, module, 1); menu.update(right, module, 1);
        check(module.settings[8].value == 1, "Options past first screen must remain reachable");
        // Selection 8 scrolls by one; the first drawn row is option 1.
        menu.update(click(200, 195), module, 1);
        check(module.settings[1].value == 1, "Scrolled option pointer mapping must match drawing");
        menu.update(info, module, 1); const int edits = module.edits;
        menu.update(right, module, 1); check(module.edits == edits, "Description view must not edit settings");
        menu.draw(module, "Fort Apocalypse", 1, "fort_apocalypse_c64");
        menu.update(back, module, 1);
        // Focus Back without activating it: this previously indexed options[-1].
        menu.show(0); menu.show_options(); menu.update(up, module, 1);
        menu.update({}, module, 1); menu.draw(module, "Fort Apocalypse", 1, "fort_apocalypse_c64");
        menu.update(up, module, 1); menu.update(enter, module, 1); // Cheats
        menu.update(right, module, 1); check(module.settings.back().value == 1, "Grouped option must work");
        menu.update(back, module, 1); menu.update(back, module, 1);
        check(menu.update(back, module, 1) == Action::resume, "Back must return from group, then options, then resume");
        menu.show(0);
        check(menu.update(click(720, 570), module, 1) == Action::resume, "Resume pointer button must work");
        Module empty; empty.settings.clear(); menu.show(0); menu.show_options();
        menu.update({}, empty, 1); menu.update(right, empty, 1); menu.update(enter, empty, 1);
        check(menu.update(back, empty, 1) == Action::resume, "Empty options must have a safe Back row");
        // Count the disconnected light contact centres, independently of the
        // port drawing's pin positions. A decorative extra contact fails this.
        platform::Image logo(64, 40);
        ui::fill(logo, ui::shelf::paper); ui::shelf::port(logo, 4, 4);
        std::set<int> contacts;
        for (int i = 0; i < int(logo.pixels.size()); ++i) {
            const auto c = logo.pixels[std::size_t(i)];
            if (c.r > 230 && c.g > 230 && c.b > 220) contacts.insert(i);
        }
        int components = 0;
        while (!contacts.empty()) {
            ++components;
            std::vector<int> todo{*contacts.begin()}; contacts.erase(todo.back());
            while (!todo.empty()) {
                const int at = todo.back(); todo.pop_back();
                for (const int offset : {-1, 1, -64, 64}) if (contacts.erase(at + offset)) todo.push_back(at + offset);
            }
        }
        check(components == 9, "The Joyport D-sub logo must have exactly nine distinct contacts");
        const auto font = platform::typeset("Joyport – café", 18, true);
        check(font.advance > 80 && !font.coverage.pixels.empty(), "Bundled font and UTF-8 must render");
        check(!std::filesystem::exists(root), "Tests must not create or change imported data");
        std::cout << "Shell navigation, all eight covers, details, portable fonts, scrolling options and description checks passed.\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
