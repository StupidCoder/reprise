#include "app/quick_save.hpp"
// Joyport: the launcher and its main loop.
//
// Command line (for testing without the window):
//   joyport --import <image>            import an image into the data folder
//   joyport --preview <png> [--select N] [--details PAGE [--scroll ROWS]]
//                                       render the launcher to a PNG
//   --data <folder>                     use another data folder

#include "app/catalog.hpp"
#include "app/launcher.hpp"
#include "app/library.hpp"
#include "app/pause_menu.hpp"
#include "app/saved_options.hpp"
#include "platform/files.hpp"
#include "ui/draw.hpp"
#include "ui/shelf.hpp"
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
    int scroll{}; // --scroll ROWS: mouse wheel steps on the details page
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
        } else if (arg == "--scroll" && has_value) {
            options.scroll = std::stoi(argv[++i]);
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
        if (outcome.ok && !outcome.waiting) {
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
        const auto overlay = menu.draw(*game, entry.title, 10, entry.id);
        platform::Image out(overlay.width, overlay.height);
        const auto frame = app::shown_picture(*game, 0).image;
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
            if (options.scroll != 0) {
                platform::Controls wheel;
                wheel.wheel = options.scroll;
                launcher.update(wheel, 1.0);
            }
        }
        platform::save_png(launcher.draw(1.0), *options.preview);
        return 0;
    }

    platform::Host host("Joyport");
    std::unique_ptr<app::GameModule> game;
    std::size_t playing = 0;
    app::PauseMenu menu;
    double game_clock = 0, sample_credit = 0, launcher_audio_clock = 0;
    std::string quick_message;
    double quick_message_until{};
    // The frames run (for the VHS look's noise) and the picture last shown,
    // over which the host reports the mouse.
    long frames_run = 0;
    app::ShownPicture shown;
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
            // A game that fails (an error in rebuilt code) goes back to the
            // library with the reason instead of closing the app.
            try {
                const auto& entry = library.games()[playing];
                if (menu.open()) {
                    switch (menu.update(controls, *game, now)) {
                    case app::PauseMenu::Action::resume:
                        host.pause_audio(false);
                        game_clock = now;
                        break;
                    case app::PauseMenu::Action::restart:
                        game = app::with_quick_save(entry, library.content_directory(playing));
                        restore_options(*game);
                        host.pause_audio(false);
                        game_clock = now;
                        break;
                    case app::PauseMenu::Action::save:
                        if (game->save())
                            menu.set_message(app::persistent_saves(entry.id) ? "Saved state"
                                                                             : "Saved for this session only.",
                                             now);
                        else
                            menu.set_message(game->save_error().empty() ? "Quick save is not available here."
                                                                        : game->save_error(),
                                             now);
                        break;
                    case app::PauseMenu::Action::load:
                        if (game->load())
                            menu.set_message("Loaded state", now);
                        else
                            menu.set_message(
                                game->save_error().empty() ? "No quick save yet." : game->save_error(), now);
                        break;
                    case app::PauseMenu::Action::quit:
                        game.reset();
                        launcher.refresh_save();
                        host.pause_audio(false);
                        continue;
                    case app::PauseMenu::Action::none:
                        break;
                    }
                    host.mute(menu.muted());
                    if (game) {
                        remember_options(*game);
                        const auto overlay = menu.draw(*game, entry.title, now, entry.id);
                        shown = app::shown_picture(*game, frames_run);
                        host.present_game(shown.image, game->pixel_aspect(), &overlay, true);
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
                if (controls.quick_save || controls.quick_load) {
                    const bool ok = controls.quick_save ? game->save() : game->load();
                    const std::string message =
                        ok ? (controls.quick_save ? "Saved state" : "Loaded state")
                           : (game->save_error().empty() ? "Quick save or load is not available here."
                                                         : game->save_error());
                    quick_message = message;
                    quick_message_until = now + 3;
                    game_clock = now;
                    sample_credit = 0;
                }
                // Fixed steps at the original's frame rate; at most a few per
                // displayed frame, so a stall does not fast-forward the game.
                app::GameInput input{controls.up,    controls.down,  controls.left,  controls.right,
                                     controls.fire,  controls.f1,    controls.f3,    controls.f5,
                                     controls.f7,    controls.c64_space, controls.up2, controls.down2,
                                     controls.left2, controls.right2, controls.fire2, controls.c64_keys};
                input.ctrl_keys = controls.ctrl_keys;
                input.pointer = controls.game_pointer.has_value();
                if (controls.game_pointer) {
                    const auto at = shown.to_picture(*controls.game_pointer);
                    input.pointer_x = at[0];
                    input.pointer_y = at[1];
                }
                input.mouse_button = controls.mouse_held;
                input.mouse_right = controls.mouse_right_held;
                input.command = controls.command_held;
                input.option = controls.option_held;
                input.control = controls.control_held;
                const auto canvas = host.canvas_size();
                game->set_canvas(canvas[0], canvas[1]);
                game_clock = std::max(game_clock, now - 4 * game->step_seconds());
                while (game_clock + game->step_seconds() <= now && game->stopped().empty()) {
                    game->tick(input);
                    ++frames_run;
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
                game->set_frame_fraction((now - game_clock) / game->step_seconds());
                game->set_display_pointer(controls.game_pointer
                                              ? std::optional(shown.to_picture(*controls.game_pointer))
                                              : std::nullopt);
                shown = app::shown_picture(*game, frames_run);
                host.set_game_draws_pointer(game->draws_pointer());
                // The app's note, above the game picture (and its VHS
                // filter), drawn at the window's own pixels.
                std::optional<platform::Image> note;
                if (now < quick_message_until) {
                    const auto canvas = host.canvas_size();
                    const double scale = std::max(1.0, std::min(double(canvas[0]) / platform::ui_width,
                                                                double(canvas[1]) / platform::ui_height));
                    const int font = int(16 * scale + .5), pad = int(10 * scale + .5);
                    const int text_width = std::min(canvas[0] / 2, ui::shelf::width(quick_message, font));
                    const int lines = ui::shelf::width(quick_message, font) > text_width ? 3 : 1;
                    note.emplace(text_width + 2 * pad, font * lines + pad * 3 / 2);
                    ui::fill(*note, {0, 0, 0, 0});
                    ui::rounded(*note, 0, 0, note->width, note->height, int(6 * scale), ui::shelf::card);
                    ui::shelf::wrap(*note, quick_message, pad, pad * 3 / 4, text_width, lines, font,
                                    ui::shelf::ink);
                }
                // Native-resolution app feedback, independent of the game's pixel shape.
                const double age = now - (quick_message_until - 3);
                const double enter = std::clamp(age / .16, 0.0, 1.0);
                const double leave = std::clamp((quick_message_until - now) / .18, 0.0, 1.0);
                const float opacity = launcher.reduced_motion() ? 1.0f : float(enter * leave);
                const int offset = launcher.reduced_motion() ? 0 : int(8 * (1 - enter) * (1 - enter));
                host.present_game(shown.image, game->pixel_aspect(), nullptr, false, note ? &*note : nullptr,
                                  opacity, offset);
            } catch (const std::exception& e) {
                game.reset();
                menu.hide();
                host.pause_audio(false);
                launcher.show_error(std::string("The game stopped because of an error: ") + e.what(), now);
            }
            continue;
        }
        if (!controls.dropped_files.empty()) {
            launcher.import(controls.dropped_files, now);
        }
        if (auto choice = host.take_file_choice()) {
            if (!choice->files.empty()) {
                launcher.import(choice->files, now);
            } else if (!choice->error.empty()) {
                launcher.show_error(choice->error, now);
            }
        }
        const auto request = launcher.update(controls, now);
        switch (request) {
        case app::Launcher::Request::choose_image:
            host.choose_image();
            break;
        case app::Launcher::Request::play:
        case app::Launcher::Request::continue_game:
            try {
                playing = launcher.selected();
                auto next =
                    app::with_quick_save(library.games()[playing], library.content_directory(playing));
                const auto canvas = host.canvas_size();
                next->set_canvas(canvas[0], canvas[1]);
                if (request == app::Launcher::Request::continue_game) {
                    if (!next->load())
                        throw std::runtime_error(next->save_error());
                } else
                    restore_options(*next);
                game = std::move(next);
                quick_message_until = 0;
                menu.hide();
                game_clock = now;
                sample_credit = 0;
                launcher.refresh_save();
            } catch (const std::exception& e) {
                launcher.show_error(std::string("The game could not start: ") + e.what(), now);
            }
            break;
        case app::Launcher::Request::none:
            break;
        }
        // The asset viewer's sounds: as many samples as the time since the
        // last frame takes; a sound stopped early drops what is queued.
        if (launcher.take_audio_cut()) {
            host.clear_audio();
        }
        if (!game) {
            sample_credit += std::clamp(now - launcher_audio_clock, 0.0, 0.1) * platform::Host::audio_sample_rate;
            const int samples = int(sample_credit);
            sample_credit -= samples;
            audio.resize(std::size_t(samples) * 2);
            launcher.preview_audio(audio, platform::Host::audio_sample_rate);
            host.audio(audio);
        }
        launcher_audio_clock = now;
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
