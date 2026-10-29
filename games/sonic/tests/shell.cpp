// No ROM required: exercise shell layout, animated hit testing and resources.
#include "app/level_select.hpp"
#include "app/shell_frame.hpp"
#include <SDL3/SDL.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace shell;

void check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void capture(const std::filesystem::path& dir, const char* name, const Image& im) {
    if (dir.empty()) {
        return;
    }
    auto* surface =
        SDL_CreateSurfaceFrom(im.width, im.height, SDL_PIXELFORMAT_RGBA32,
                              const_cast<Color*>(im.pixels.data()), im.width * sizeof(Color));
    check(surface, "capture surface");
    bool ok = SDL_SaveBMP(surface, (dir / (std::string(name) + ".bmp")).string().c_str());
    SDL_DestroySurface(surface);
    check(ok, "capture file");
}

class PreviewMusic : public gg::MusicPlayer {
public:
    PreviewMusic()
        : MusicPlayer({{"Opening theme", "Original score timing and loops."},
                       {"Stage theme", "The current track plays through its own sound driver."}}) {}

    void start(unsigned) override {}

    void release() override {}

    std::vector<float> frame(gg::Psg&, bool& active) override {
        active = false;
        return {};
    }
};

int main(int argc, char** argv) {
    try {
        check(SDL_Init(0), "SDL init");
        std::filesystem::path dir = argc > 1 ? argv[1] : "";
        if (!dir.empty()) {
            std::filesystem::create_directories(dir);
        }
        check(text_width("BlueStreak", 3) > 0, "bundled font");
        for (int c = 1; c < 8; ++c) {
            check(text_width(std::string(1, char(c)), 2) == 14, "navigation symbol");
        }
        LibraryView v;
        v.time = 10;
        v.installed.fill(true);
        v.ready = true;
        v.sound_test = true;
        capture(dir, "home", library_image(v));
        v.zone = true;
        for (int g = 0; g < 3; ++g) {
            v.generation = g;
            for (int n = first_game(g); n < first_game(g) + game_count(g); ++n) {
                v.selection = n;
                v.slide_from = n - first_game(g);
                check(library_hit(v, 480, 274) == n, "carousel selected hit");
                auto im = library_image(v);
                if (n == 0) {
                    capture(dir, "library", im);
                }
                if (n == 7) {
                    capture(dir, "planned", im);
                }
                if (n == 10) {
                    capture(dir, "advance", im);
                }
                if (const auto* title = title_for(n); title && !title->levels.empty()) {
                    LevelLayout layout(*title);
                    for (int k = 0; k <= layout.back; ++k) {
                        auto r = layout.rect(k);
                        check(layout.hit(r.x + r.w / 2, r.y + r.h / 2) == k, "level hit");
                        check(layout.move(k, 0, 1) >= 0, "level navigation");
                    }
                    if (n == 0) {
                        capture(dir, "levels", level_select_image(layout, title->name, 1));
                    }
                }
            }
        }
        // At animation start the previous card remains under the pointer.
        v.generation = 0;
        v.selection = 1;
        v.slide_from = 0;
        v.moved_at = v.time;
        check(library_hit(v, 480, 274) == 0, "animated card hit");
        v.time += 0.3;
        check(library_hit(v, 480, 274) == 1, "settled card hit");
        check(library_music_hit(v, 620, 524), "music hit");
        for (int n = 0; n < 6; ++n) {
            check(pause_hit(400, 220 + n * 44) == n, "pause row");
        }
        std::array offered{Enhancement::widescreen, Enhancement::shield_transparency,
                           Enhancement::water, Enhancement::rings, Enhancement::lcd_screen};
        PauseMenuView pause;
        pause.paused = true;
        pause.game = "Sonic 1 / Game Gear";
        pause.status = "Green Hill / Act 1";
        pause.offered = offered;
        capture(dir, "pause", game_overlay(pause));
        pause.enhancements_open = true;
        pause.enhancement_selection = 4;
        capture(dir, "enhancements", game_overlay(pause));
        for (int n = 0; n < 5; ++n) {
            check(enhancements_hit(400, 230 + n * 44, 5) == n, "enhancement hit");
        }
        check(enhancements_hit(400, 515, 5) == 5, "enhancement back");
        pause.offered = {};
        pause.enhancement_selection = -1;
        game_overlay(pause);
        PreviewMusic music;
        capture(dir, "music", music_image(music, "Sonic 1", 0, 0, false));
        capture(dir, "launch",
                title_card(960, 640, "Sonic Triple Trouble", "From the beginning", 0, 1, 0));
        Image mark(340, 72);
        fill(mark, {0, 0, 0, 0});
        logo(mark, 8, 10, LogoSize::title);
        capture(dir, "logo", mark);
        reduce_motion = true;
        check(ease_out(0) == 1, "reduced motion");
        v.time = v.moved_at;
        check(library_hit(v, 480, 274) == 1, "reduced motion hit");
        SDL_Quit();
        std::cout << "Shell navigation, typography and render checks passed.\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
