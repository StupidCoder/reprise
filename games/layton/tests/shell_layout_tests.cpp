// Render the shell at desktop and phone proportions without opening a ROM.
#include "platform/type.hpp"
#include "shell/game_menu.hpp"
#include "shell/library_screen.hpp"
#include "shell/widgets.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace picarat;

void require(bool value, const char* why) {
    if (!value) {
        throw std::runtime_error(why);
    }
}

void save(const std::filesystem::path& dir, const std::string& name, const platform::Image& im) {
    if (dir.empty()) {
        return;
    }
    std::ofstream out(dir / (name + ".ppm"), std::ios::binary);
    out << "P6\n" << im.width << ' ' << im.height << "\n255\n";
    for (auto p : im.pixels) {
        out.put(char(p.r));
        out.put(char(p.g));
        out.put(char(p.b));
    }
}

int main(int argc, char** argv) {
    try {
        const std::filesystem::path root = PICARAT_SOURCE_ROOT;
        const std::filesystem::path out = argc > 1 ? argv[1] : "";
        if (!out.empty()) {
            std::filesystem::create_directories(out);
        }
        shell::LibraryScreen screen(root);
        shell::Library library;
        library.ready = true;
        library.story = true;
        library.languages = {{0, "en", "English"},
                             {1, "de", "Deutsch"},
                             {2, "fr", "Français"},
                             {3, "it", "Italiano"},
                             {4, "es", "Español"}};
        for (unsigned n = 1; n <= 45; ++n) {
            library.puzzles.push_back(
                {n, n, std::to_string(n), "A curious question", "Logic", true});
        }
        for (int n = 0; n < 6; ++n) {
            screen.setLibrary(n, library);
            screen.setMusicTracks(n, 24);
        }
        const auto font = platform::typeset("Énigmes • Grüße", 24, false);
        require(font.advance == platform::textAdvance("Énigmes • Grüße", 24, false),
                "text metrics");
        bool smooth = false;
        for (auto p : font.coverage.pixels) {
            smooth |= p.a > 0 && p.a < 255;
        }
        require(smooth, "antialiased type");
        // Small toolbar icons must retain smooth coverage at fractional density.
        for (auto icon :
             {shell::style::kIconLeft, shell::style::kIconRight, shell::style::kIconUp,
              shell::style::kIconDown, shell::style::kIconCheck, shell::style::kIconSearch,
              shell::style::kIconPuzzle, shell::style::kIconMinigame, shell::style::kIconMovie,
              shell::style::kIconMusic, shell::style::kIconRestart}) {
            for (float density : {1.f, 1.5f, 2.f}) {
                platform::Image sample(64, 64);
                shell::style::Canvas canvas(sample, density);
                canvas.fill({255, 255, 255});
                canvas.icon(icon, 4, 4, 24, {0, 0, 0});
                bool coverage = false;
                for (const auto pixel : sample.pixels) {
                    coverage |= pixel.r > 0 && pixel.r < 255;
                }
                require(coverage, "smooth icon coverage at device density");
                require(sample.at(0, 0).r == 255, "icon respects its bounds");
            }
        }
        for (auto [w, h] : {std::pair{1080, 720}, std::pair{390, 844}, std::pair{844, 390},
                            std::pair{600, 800}}) {
            const std::string tag = std::to_string(w) + "x" + std::to_string(h);
            auto viewport = shell::LibraryScreen::viewport(float(w), float(h));
            require(viewport.area.w <= w + 1 && viewport.area.h <= h + 1, "viewport fits");
            screen.resize(viewport.width, viewport.height);
            platform::Image im(int(viewport.width), int(viewport.height));
            shell::style::Canvas c(im, 1);
            screen.showGames();
            screen.draw(c);
            save(out, "library-" + tag, im);
            screen.showHub(0);
            screen.draw(c);
            save(out, "hub-" + tag, im);
            screen.showCollection(0);
            screen.draw(c);
            save(out, "puzzles-" + tag, im);
            shell::ui::Dialog dialog;
            dialog.open(1, "Choose an action", "Your progress stays in this collection.",
                        {"Keep playing", "Return to library", "Cancel"});
            platform::Image dialogImage(w, h);
            shell::style::Canvas dc(dialogImage, 1);
            dc.fill(shell::ui::kPaper);
            dialog.draw(dc, float(w), float(h));
            save(out, "dialog-" + tag, dialogImage);
            shell::GameMenu menu;
            menu.open("An adventure", true, false);
            platform::Image overlay(w, h);
            shell::style::Canvas mc(overlay, 1);
            mc.fill(shell::ui::kPaper);
            menu.draw(mc, float(w), float(h));
            save(out, "pause-" + tag, overlay);
            platform::Event e{};
            e.type = platform::Event::Type::KeyDown;
            e.key = platform::Key::Return;
            require(menu.handle(e) == shell::GameMenu::Action::Resume, "default resume");
            menu.open("An adventure", true, false);
            menu.draw(mc, float(w), float(h));
            e.key = platform::Key::Down;
            menu.handle(e);
            e.key = platform::Key::Return;
            require(menu.handle(e) == shell::GameMenu::Action::Skip, "skip navigation");
            menu.open("An adventure", false, false);
            menu.draw(mc, float(w), float(h));
            e.key = platform::Key::Down;
            menu.handle(e);
            e.key = platform::Key::Return;
            require(menu.handle(e) == shell::GameMenu::Action::TouchAreas && menu.isOpen(),
                    "touch toggle");
            e.key = platform::Key::Escape;
            require(menu.handle(e) == shell::GameMenu::Action::Resume, "back resumes");
        }
        std::cout << "Portrait, landscape, font and menu checks passed.\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
