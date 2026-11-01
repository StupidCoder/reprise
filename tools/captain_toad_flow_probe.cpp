#include "captain_toad_3ds/reprise/module.hpp"
#include "captain_toad_3ds/reprise/checkpoint.hpp"
#include "platform/files.hpp"
#include "app/quick_save.hpp"
#include "platform/host.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
bool same(const platform::Image& a, const platform::Image& b) {
    return a.width == b.width && a.height == b.height && a.pixels.size() == b.pixels.size() &&
           !std::memcmp(a.pixels.data(), b.pixels.data(), a.pixels.size() * sizeof(platform::Color));
}
int main(int argc, char** argv) {
    try {
        if (argc != 4) {
            throw std::runtime_error("usage: captain_toad_flow_probe IMPORT OUTPUT Opening|Book|STAGE|restore");
        }
        std::filesystem::path out = argv[2];
        std::filesystem::create_directories(out);
        platform::Host host("Captain Toad campaign check");
        std::string scene = argv[3];
        if (scene == "durable-save" || scene == "durable-continue") {
            app::GameEntry entry;
            entry.id = "captain_toad_3ds";
            entry.start = captain_toad::reprise::opening;
            if (scene == "durable-save") {
                entry.start = [&](const std::filesystem::path& content) {
                    auto g = captain_toad::reprise::opening(content);
                    g->read_state(platform::read_file(out / "checkpoint.bin"));
                    return g;
                };
            }
            auto wrapped = app::with_quick_save(entry, argv[1]);
            wrapped->attach_gpu(&host);
            if (scene == "durable-save") {
                wrapped->picture();
                if (!wrapped->save()) {
                    throw std::runtime_error(wrapped->save_error());
                }
                auto saves = std::filesystem::path(argv[1]).parent_path().parent_path() / "saves/captain_toad_3ds";
                auto backup = out / "saved-backup";
                if (std::filesystem::exists(backup)) {
                    throw std::runtime_error("Probe backup already exists");
                }
                std::filesystem::rename(saves, backup);
                bool failed = false;
                try {
                    platform::write_text(saves, "blocked test destination");
                    failed = !wrapped->save() && !wrapped->save_error().empty();
                    std::filesystem::remove(saves);
                    std::filesystem::rename(backup, saves);
                } catch (...) {
                    std::filesystem::remove(saves);
                    std::filesystem::rename(backup, saves);
                    throw;
                }
                if (!failed || !wrapped->load()) {
                    throw std::runtime_error("Failed write did not preserve previous generation");
                }
                std::vector<std::filesystem::path> generations;
                for (const auto& item : std::filesystem::directory_iterator(saves)) {
                    if (item.is_directory() && item.path().filename().string().size() == 20) {
                        generations.push_back(item.path());
                    }
                }
                std::sort(generations.begin(), generations.end());
                auto file = generations.back() / "state.bin";
                auto bytes = platform::read_file(file);
                auto damaged = bytes;
                damaged.back() ^= 1;
                auto live = wrapped->picture();
                platform::write_file(file, damaged);
                bool preserved = !wrapped->load() && same(live, wrapped->picture());
                platform::write_file(file, bytes);
                if (!preserved) {
                    throw std::runtime_error("Corrupt durable load changed running scene");
                }
            } else {
                if (!wrapped->has_save() || !wrapped->load()) {
                    throw std::runtime_error(wrapped->save_error());
                }
            }
            platform::save_png(wrapped->picture(), out / (scene + ".png"));
            if (!app::save_preview(argv[1], entry.id).available) {
                throw std::runtime_error("Continue not available");
            }
            wrapped.reset();
            std::printf("PASS %s through Reprise save generations and Continue\n", scene.c_str());
            return 0;
        }
        auto game = captain_toad::reprise::opening(argv[1]);
        game->attach_gpu(&host);
        if (scene == "restore") {
            game->read_state(platform::read_file(out / "checkpoint.bin"));
        } else {
            captain_toad::reprise::Checkpoint c;
            c.scene = scene == "BookFlow" ? "Book" : scene;
            if (scene != "Opening") {
                c.progress.cleared.insert("Season1OpeningStage");
                c.progress.lastPlayed = "Season1OpeningStage";
            }
            game->read_state(c.encode());
            const std::array<std::string, 4> stages{"PullUpStage", "RevolverRuinsStage", "TouchPuzzleMiniStage",
                                                    "FreshRuinsStage"};
            for (size_t i = 0; i < stages.size(); ++i) {
                if (scene == stages[i]) {
                    game->set_option(4, int(i) + 2);
                    if (game->options()[4].value != int(i) + 2) {
                        throw std::runtime_error("Course option cannot retain its selection");
                    }
                }
            }
        }
        auto initial = game->picture();
        platform::save_png(initial, out / (scene + "-initial.png"));
        auto state = game->write_state();
        platform::write_file(out / "checkpoint.bin", state);
        auto corrupt = state;
        corrupt[0] ^= 1;
        bool rejected = false;
        try {
            game->read_state(corrupt);
        } catch (const std::exception&) {
            rejected = true;
        }
        if (!rejected || game->write_state() != state) {
            throw std::runtime_error("Corrupt load replaced checkpoint");
        }
        std::vector<float> audio(1600);
        double energy = 0;
        for (int i = 0; i < (scene == "BookFlow" ? 1800 : 720); ++i) {
            app::GameInput in;
            if (scene == "Book" || scene == "BookFlow") {
                if (i == 300 || i == 700 || i == 1300) {
                    in.fire = true;
                }
                if (scene == "BookFlow" && i >= 450 && i < 460) {
                    in.right = true;
                }
            } else if (i >= 360 && i < 480) {
                in.right = true;
            }
            if (i == 500 && scene != "Book" && scene != "BookFlow" && scene != "Opening" && scene != "restore") {
                in.start = true;
            }
            if (i >= 580 && i < 590 && scene != "Book" && scene != "BookFlow") {
                in.button2 = true;
            }
            game->tick(in);
            game->audio(audio, 48000);
            for (float x : audio) {
                if (!std::isfinite(x) || std::abs(x) > 1.001) {
                    throw std::runtime_error("Invalid audio");
                }
                energy += x * x;
            }
            if (i % 30 == 0) {
                host.poll();
                game->picture();
            }
            if (i == 530) {
                platform::save_png(game->picture(), out / (scene + "-pause.png"));
            }
        }
        std::printf("Final checkpoint scene: %s\n",
                    captain_toad::reprise::Checkpoint::decode(game->write_state()).scene.c_str());
        if (scene == "BookFlow" &&
            captain_toad::reprise::Checkpoint::decode(game->write_state()).scene != "PullUpStage") {
            throw std::runtime_error("Book navigation did not reach the first course");
        }
        auto before = game->picture();
        platform::save_png(before, out / (scene + "-played.png"));
        auto preview = game->save_picture();
        auto after = game->picture();
        if (!same(before, after)) {
            throw std::runtime_error("Checkpoint preview changed live scene");
        }
        platform::save_png(preview, out / (scene + "-checkpoint.png"));
        game->set_option(1, 1);
        game->set_option(2, 1);
        game->read_state(state);
        if (game->options()[1].value != 1 || game->options()[2].value != 1) {
            throw std::runtime_error("Load changed presentation preferences");
        }
        game->set_option(1, 0);
        game->set_option(2, 0);
        auto restored = game->picture();
        platform::save_png(restored, out / (scene + "-restored.png"));
        if (!same(initial, restored)) {
            double total = 0;
            size_t changed = 0;
            for (size_t i = 0; i < initial.pixels.size(); ++i) {
                auto a = initial.pixels[i], b = restored.pixels[i];
                int d = std::abs(int(a.r) - b.r) + std::abs(int(a.g) - b.g) + std::abs(int(a.b) - b.b);
                total += d;
                changed += d != 0;
            }
            double mean = total / (initial.pixels.size() * 3);
            std::printf("Restart render difference: RGB mean %.8f, changed pixels %zu/%zu\n", mean, changed,
                        initial.pixels.size());
            if (mean > .05 || changed > initial.pixels.size() / 200) {
                throw std::runtime_error("Checkpoint restart render exceeds cosmetic tolerance");
            }
        }
        if (game->write_state() != state) {
            throw std::runtime_error("Restart changed owned checkpoint values");
        }
        game.reset();
        std::printf(
            "PASS %s: input/audio ticks, audio energy %.5f, corrupt-load preservation, preview, restart, presentation "
            "preferences\n",
            scene.c_str(), energy);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL: %s\n", e.what());
        return 1;
    }
}
