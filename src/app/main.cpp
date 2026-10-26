// Joyport: the launcher and its main loop.
//
// Command line (for testing without the window):
//   joyport --import <image>            import an image into the data folder
//   joyport --preview <png> [--select N] [--details PAGE]
//                                       render the launcher to a PNG
//   --data <folder>                     use another data folder

#include "app/catalog.hpp"
#include "app/launcher.hpp"
#include "app/library.hpp"
#include "app/pause_menu.hpp"
#include "app/saved_options.hpp"
#include "platform/files.hpp"
#include "ui/draw.hpp"
#include "platform/host.hpp"
#include <algorithm>
#include <cstdio>
#include <map>
#include <memory>
#include <vector>
#include <exception>
#include <optional>
#include <string_view>

namespace {
struct Options {
    std::optional<std::filesystem::path> data, import_image, preview;
    std::size_t select{};
    std::optional<std::size_t> details; // page
    // --pause-preview PAGE: play the selected game for a few seconds, then
    // draw the pause menu (0), its options page (1) or the options page's
    // first group (2, such as the cheats) over it to --preview.
    std::optional<std::size_t> pause_page;
};

Options parse(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        const bool has_value = i + 1 < argc;
        if (arg == "--data" && has_value) {
            options.data = platform::utf8_path(argv[++i]);
        } else if (arg == "--import" && has_value) {
            options.import_image = platform::utf8_path(argv[++i]);
        } else if (arg == "--preview" && has_value) {
            options.preview = platform::utf8_path(argv[++i]);
        } else if (arg == "--select" && has_value) {
            options.select = std::size_t(std::stoul(argv[++i]));
        } else if (arg == "--pause-preview" && has_value) {
            options.pause_page = std::size_t(std::stoul(argv[++i]));
        } else if (arg == "--details" && has_value) {
            options.details = std::size_t(std::stoul(argv[++i]));
        }
        // Anything else (such as macOS's -psn_ argument) is ignored.
    }
    return options;
}

int run(const Options& options) {
    const auto data = options.data ? *options.data : platform::data_directory();
    app::Library library(data, app::catalog());

    if (options.import_image) {
        const auto outcome = library.import(*options.import_image, std::nullopt);
        std::printf("%s\n", outcome.message.c_str());
        if (outcome.ok) {
            std::printf("Installed in %s\n",
                        library.content_directory(*outcome.game).string().c_str());
        }
        return outcome.ok ? 0 : 1;
    }

    app::Launcher launcher(library);
    if (options.preview && options.pause_page) {
        const auto& entry = library.games()[options.select];
        auto game = entry.start(library.content_directory(options.select));
        for (int i = 0; i < 400; ++i) {
            game->tick({});
        }
        app::PauseMenu menu;
        menu.show(0);
        if (*options.pause_page == 1) {
            menu.show_options();
        } else if (*options.pause_page == 2) {
            menu.show_options();
            for (const auto& o : game->options()) {
                if (!o.group.empty()) {
                    menu.show_group(o.group);
                    break;
                }
            }
        }
        const auto overlay = menu.draw(*game, entry.title, 10);
        platform::Image out(overlay.width, overlay.height);
        const auto& frame = game->picture();
        const int h = overlay.height, w = int(h * frame.width * game->pixel_aspect() / frame.height);
        ui::picture(out, frame, (overlay.width - w) / 2, 0, w, h);
        ui::blend_rect(out, 0, 0, out.width, out.height, {0, 0, 0}, 140);
        for (std::size_t i = 0; i < out.pixels.size(); ++i) {
            const auto c = overlay.pixels[i];
            if (c.a) {
                out.pixels[i] = c;
            }
        }
        platform::save_png(out, *options.preview);
        return 0;
    }
    if (options.preview) {
        launcher.select(options.select);
        if (options.details) {
            launcher.open_details(*options.details);
        }
        platform::save_png(launcher.draw(1.0), *options.preview);
        return 0;
    }

    platform::Host host("Joyport");
    std::unique_ptr<app::GameModule> game;
    std::size_t playing = 0;
    app::PauseMenu menu;
    double game_clock = 0, sample_credit = 0;
    std::vector<float> audio;
    // A game's options stay as the player set them, also for the next
    // launch: written to the data folder when they change.
    const auto options_folder = data / "options";
    std::map<std::string, std::string> written; // per game, what its file holds
    auto options_file = [&] { return options_folder / (library.games()[playing].id + ".txt"); };
    auto remember_options = [&](const app::GameModule& module) {
        const auto text = app::options_text(module);
        auto& last = written[library.games()[playing].id];
        if (text == last) {
            return;
        }
        try {
            std::filesystem::create_directories(options_folder);
            platform::write_text(options_file(), text);
            last = text;
        } catch (const std::exception&) {
            // Not written (a read-only data folder): the choices last only
            // until the game is started again.
        }
    };
    auto restore_options = [&](app::GameModule& module) {
        app::restore_options(module, options_file());
        written[library.games()[playing].id] = app::options_text(module);
    };
    while (true) {
        const auto controls = host.poll();
        if (controls.quit) {
            break;
        }
        const double now = host.seconds();

        if (game) {
            const auto& entry = library.games()[playing];
            if (menu.open()) {
                switch (menu.update(controls, *game, now)) {
                case app::PauseMenu::Action::resume:
                    host.pause_audio(false);
                    game_clock = now;
                    break;
                case app::PauseMenu::Action::restart:
                    game = entry.start(library.content_directory(playing));
                    restore_options(*game);
                    host.pause_audio(false);
                    game_clock = now;
                    break;
                case app::PauseMenu::Action::save:
                    game->save();
                    menu.set_message("Saved.", now);
                    break;
                case app::PauseMenu::Action::load:
                    menu.set_message(game->load() ? "Loaded." : "No quick save yet.", now);
                    break;
                case app::PauseMenu::Action::quit:
                    game.reset();
                    host.pause_audio(false);
                    continue;
                case app::PauseMenu::Action::none:
                    break;
                }
                host.mute(menu.muted());
                if (game) {
                    remember_options(*game);
                    const auto overlay = menu.draw(*game, entry.title, now);
                    host.present_game(game->picture(), game->pixel_aspect(), &overlay, true);
                }
                continue;
            }
            if (controls.menu) {
                menu.show(now);
                host.pause_audio(true);
                continue;
            }
            if (controls.toggle_mute) {
                menu.toggle_muted();
                host.mute(menu.muted());
            }
            if (controls.quick_save) {
                game->save();
            }
            if (controls.quick_load) {
                game->load();
                game_clock = now;
            }
            // Fixed steps at the original's frame rate; at most a few per
            // displayed frame, so a stall does not fast-forward the game.
            const app::GameInput input{controls.up,    controls.down,  controls.left,  controls.right,
                                       controls.fire,  controls.f1,    controls.f3,    controls.f5,
                                       controls.f7,    controls.c64_space, controls.up2, controls.down2,
                                       controls.left2, controls.right2, controls.fire2, controls.c64_keys};
            const auto canvas = host.canvas_size();
            game->set_canvas(canvas[0], canvas[1]);
            game_clock = std::max(game_clock, now - 4 * game->step_seconds());
            while (game_clock + game->step_seconds() <= now && game->stopped().empty()) {
                game->tick(input);
                game_clock += game->step_seconds();
                // The sound of this frame.
                sample_credit += game->step_seconds() * platform::Host::audio_sample_rate;
                const int samples = int(sample_credit);
                sample_credit -= samples;
                audio.resize(std::size_t(samples) * 2);
                game->audio(audio, platform::Host::audio_sample_rate);
                host.audio(audio);
            }
            if (!game->stopped().empty()) {
                launcher.show_message(game->stopped(), true, now);
                game.reset();
                continue;
            }
            host.present_game(game->picture(), game->pixel_aspect());
            continue;
        }
        if (controls.dropped_file) {
            launcher.import(*controls.dropped_file, now);
        }
        if (auto choice = host.take_file_choice()) {
            if (choice->file) {
                launcher.import(*choice->file, now);
            } else if (!choice->error.empty()) {
                launcher.show_error(choice->error, now);
            }
        }
        switch (launcher.update(controls, now)) {
        case app::Launcher::Request::choose_image:
            host.choose_image();
            break;
        case app::Launcher::Request::play:
            try {
                playing = launcher.selected();
                game = library.games()[playing].start(library.content_directory(playing));
                restore_options(*game);
                game_clock = now;
            } catch (const std::exception& e) {
                launcher.show_error(std::string("The game could not start: ") + e.what(), now);
            }
            break;
        case app::Launcher::Request::none:
            break;
        }
        host.present(launcher.draw(now));
    }
    return 0;
}
} // namespace

int main(int argc, char** argv) {
    try {
        return run(parse(argc, argv));
    } catch (const std::exception& e) {
        std::fprintf(stderr, "joyport: %s\n", e.what());
        return 1;
    }
}
