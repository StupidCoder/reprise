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
#include "platform/host.hpp"
#include <algorithm>
#include <cstdio>
#include <memory>
#include <exception>
#include <optional>
#include <string_view>

namespace {
struct Options {
    std::optional<std::filesystem::path> data, import_image, preview;
    std::size_t select{};
    std::optional<std::size_t> details; // page
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
        } else if (arg == "--details" && has_value) {
            options.details = std::size_t(std::stoul(argv[++i]));
        }
        // Anything else (such as macOS's -psn_ argument) is ignored.
    }
    return options;
}

int run(const Options& options) {
    app::Library library(options.data ? *options.data : platform::data_directory(),
                         app::catalog());

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
    double game_clock = 0;
    while (true) {
        const auto controls = host.poll();
        if (controls.quit) {
            break;
        }
        const double now = host.seconds();

        if (game) {
            // Fixed steps at the original's frame rate; at most a few per
            // displayed frame, so a stall does not fast-forward the game.
            const app::GameInput input{controls.up,   controls.down, controls.left,
                                       controls.right, controls.fire, controls.f1,
                                       controls.f3,   controls.f5,   controls.f7,
                                       controls.c64_space};
            game_clock = std::max(game_clock, now - 4 * game->step_seconds());
            while (game_clock + game->step_seconds() <= now && game->stopped().empty()) {
                game->tick(input);
                game_clock += game->step_seconds();
            }
            if (controls.back || !game->stopped().empty()) {
                launcher.show_message(game->stopped().empty() ? "" : game->stopped(), true, now);
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
                game = library.games()[launcher.selected()].start(
                    library.content_directory(launcher.selected()));
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
