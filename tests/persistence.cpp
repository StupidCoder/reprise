#include "app/catalog.hpp"
#include "app/quick_save.hpp"
#include "platform/files.hpp"
#include "platform/host.hpp"
#include "platform/sha256.hpp"
#include "state/archive.hpp"
#include "marble_madness_amiga/game/front_end.hpp"
#include "mindwalker_amiga/landscape/landscape_stage.hpp"
#include "mindwalker_amiga/tunnel/tunnel_stage.hpp"
#include "mindwalker_amiga/arena/arena_stage.hpp"
#include "mindwalker_amiga/psyche/psyche_stage.hpp"
#include <filesystem>
#include <iostream>
#include <cstring>

namespace fs = std::filesystem;
void check(bool ok, const std::string &why) {
    if (!ok)
        throw std::runtime_error(why);
}
app::GameInput input(int frame) {
    app::GameInput in;
    in.fire = frame % 120 >= 40 && frame % 120 < 48;
    if (frame < 400)
        return in;
    in.right = frame % 360 < 120;
    in.left = frame % 360 >= 240;
    in.up = frame % 240 < 80;
    in.down = frame % 240 >= 160;
    return in;
}
std::string advance(app::GameModule &game, int begin, int end, bool mindwalker = false, bool wings_flight = false) {
    std::vector<float> sound(882 * 2);
    std::vector<std::uint8_t> heard;
    for (int f = begin; f < end; ++f) {
        auto in = input(f);
        if (mindwalker) {
            in.pointer = true;
            in.pointer_x = 160;
            in.pointer_y = f < 100 ? 150 : 20;
            in.mouse_button = (f >= 40 && f < 44) || (f >= 120 && f < 124);
        }
        if (wings_flight && f >= 400) {
            in = {};
            in.left = true;
            in.fire = f > 1000 && f % 200 < 30;
            in.down = f > 800 && f % 1200 < 100;
        }
        game.tick(in);
        game.audio(sound, 22050);
        const auto* p = reinterpret_cast<const std::uint8_t*>(sound.data());
        heard.insert(heard.end(),p,p+sound.size()*sizeof(float));
    }
    return platform::sha256(heard);
}
std::string picture_hash(app::GameModule &game) {
    const auto im = game.picture();
    return platform::sha256({reinterpret_cast<const std::uint8_t *>(im.pixels.data()),
                             im.pixels.size() * sizeof(platform::Color)});
}
struct Fake : app::GameModule {
    int tick_{};
    bool fail{};
    double step_seconds() const override { return .02; }
    double pixel_aspect() const override { return 1; }
    void tick(const app::GameInput &) override { ++tick_; }
    platform::Image picture() const override {
        platform::Image p(32, 20);
        for (auto &c : p.pixels)
            c = {std::uint8_t(tick_), 20, 30, 255};
        return p;
    }
    std::vector<std::uint8_t> write_state() override {
        if (fail)
            throw std::runtime_error("test failure");
        state::Writer a;
        a(tick_);
        return a.bytes;
    }
    void read_state(std::span<const std::uint8_t> b) override {
        state::Reader a(b);
        a(tick_);
        a.finish();
    }
    int vhs{}; // the framework's screen filter (a presentation option)
    std::vector<app::GameOption> options() const override {
        return {{"Fail", {"Off", "On"}, int(fail), ""}, app::screen_filter_option(vhs)};
    }
    void set_option(std::size_t i, int v) override {
        if (i == 0) fail = v != 0;
        else vhs = v;
    }
    int vhs_level() const override { return vhs; }
};
int main(int argc, char **argv) try {
    if (argc == 4 && std::string(argv[1]).starts_with("--mind-")) {
        const bool record = std::string(argv[1]) == "--mind-record";
        check(record || std::string(argv[1]) == "--mind-restore", "Mind Walker test mode");
        using namespace mindwalker;
        const data::GameFiles files(argv[2]);
        const auto assets = data::load_assets(argv[2]);
        const fs::path out = argv[3];
        fs::create_directories(out);
        auto test = [&](std::string name, auto factory, auto tick) {
            for (int frame : {5, 120, 600}) for (bool paused : {false, true}) {
                auto stage = factory();
                const auto prefix = out / (name + "-" + std::to_string(frame) + (paused ? "-paused" : ""));
                std::vector<float> audio(882);
                auto encode = [](auto& s) { state::Writer ar; ar(s); return ar.bytes; };
                auto image_hash = [](auto& s) {
                    const auto p = s.picture();
                    return platform::sha256({reinterpret_cast<const std::uint8_t*>(p.pixels.data()),
                                             p.pixels.size()*sizeof(platform::Color)});
                };
                if (record) {
                    for (int f = 0; f < frame; ++f) { tick(stage, f, -1); std::fill(audio.begin(),audio.end(),0); stage.render_audio(audio, 22050); }
                    if (paused) tick(stage, frame, 0x10);
                    platform::write_file(prefix.string()+".state", encode(stage));
                    platform::write_text(prefix.string()+".picture", image_hash(stage));
                    platform::save_png(stage.picture(), prefix.string()+".png");
                } else {
                    const auto bytes = platform::read_file(prefix.string()+".state");
                    state::Reader ar(bytes); ar(stage); ar.finish();
                    check(encode(stage) == bytes, name+" stage state round trip");
                    const auto pic = platform::read_file(prefix.string()+".picture");
                    check(image_hash(stage) == std::string(pic.begin(),pic.end()), name+" stage picture round trip");
                }
                std::vector<std::uint8_t> samples;
                for (int f=frame; f<frame+120; ++f) {
                    tick(stage, f, paused && f==frame+30 ? 0x10 : -1);
                    std::fill(audio.begin(),audio.end(),0);
                    stage.render_audio(audio,22050);
                    const auto* p = reinterpret_cast<const std::uint8_t*>(audio.data());
                    samples.insert(samples.end(),p,p+audio.size()*sizeof(float));
                }
                const auto result = image_hash(stage)+"\n"+platform::sha256(samples);
                if(record) {
                    platform::write_file(prefix.string()+".after",encode(stage));
                    platform::write_text(prefix.string()+".result",result);
                } else {
                    check(encode(stage)==platform::read_file(prefix.string()+".after"), name+" continued state");
                    const auto expected = platform::read_file(prefix.string()+".result");
                    check(result==std::string(expected.begin(),expected.end()),name+" continued picture/audio");
                }
                std::cout << name << " frame " << frame << " paused " << paused << " checked\n";
            }
        };
        test("landscape", [&] { return landscape::LandscapeStage(files,assets,{12345,14,3,0,{}}); },
            [](auto& s,int f,int gadget) { s.frame([&](landscape::InputRead) {
                landscape::LandscapeInput in; in.up=f%180<90; in.right=!in.up; in.fire=f%90>70; return in;
            },gadget); });
        test("tunnel", [&] { return tunnel::TunnelStage(files,assets,{12345,14,3,0}); },
            [](auto& s,int f,int gadget) { s.frame([&](tunnel::TunnelRead) {
                tunnel::TunnelInput in; in.up=f%180<90; in.right=!in.up; in.fire=f%90>70; return in;
            },gadget); });
        for(int form=0;form<4;++form)
            test("arena-form"+std::to_string(form), [&] { return arena::ArenaStage(files,assets,{12345,14,3,0,form}); },
                [](auto& s,int f,int gadget) { s.frame([&](arena::ArenaRead) {
                    arena::ArenaInput in; in.up=f%180<90; in.right=!in.up; in.fire=f%90>70; return in;
                },gadget); });
        test("psyche", [&] { return psyche::PsycheStage(files,assets,{12345,7,3,0,42000,50}); },
            [](auto& s,int f,int gadget) { s.frame([&] {
                return psyche::PsycheInput{40+f%220,50+f%100,f%90>70};
            },gadget); });
        return 0;
    }
    if (argc == 4) {
        const bool wings_only = std::string(argv[1]).starts_with("--wings-");
        const bool record = std::string(argv[1]) == "--record" || std::string(argv[1]) == "--wings-record";
        check(record || std::string(argv[1]) == "--restore" || std::string(argv[1]) == "--wings-restore", "mode");
        const fs::path games = argv[2], out = argv[3];
        fs::create_directories(out);
        for (const auto &entry : app::catalog())
            if (app::persistent_saves(entry.id) && (!wings_only || entry.id == "wings_of_fury_amiga")) {
                for (int variant = 0; variant < (wings_only ? 3 : 2); ++variant)
                    for (int frame : wings_only ? std::vector<int>{120, 360, 600, 1800, 3600, 7200} : std::vector<int>{120, 360, 600, 1800}) {
                        const auto prefix =
                            out / (entry.id + "-" + std::to_string(variant) + "-" + std::to_string(frame));
                        auto game = entry.start(games / entry.id);
                        game->set_canvas(1280, 720);
                        if (variant) {
                            const auto opts = game->options();
                            for (std::size_t i = 0; i < opts.size(); ++i)
                                if (opts[i].choices.size() > 1)
                                    game->set_option(i, 1);
                        }
                        if (record) {
                            advance(*game, 0, frame, entry.id == "mindwalker_amiga", wings_only && variant == 2);
                            const auto before = game->write_state();
                            platform::write_file(prefix.string() + ".state", before);
                            platform::write_text(prefix.string() + ".picture", picture_hash(*game));
                            platform::save_png(game->picture(), prefix.string() + ".png");
                            if (frame == 600 && variant == 0) {
                                const auto content = fs::absolute(out / "library/games" / entry.id);
                                fs::create_directories(content.parent_path());
                                if (!fs::exists(content))
                                    fs::create_directory_symlink(fs::absolute(games / entry.id), content);
                                auto disk = app::with_quick_save(entry, content);
                                disk->set_canvas(1280, 720);
                                advance(*disk, 0, frame, entry.id == "mindwalker_amiga", wings_only && variant == 2);
                                const auto disk_picture = picture_hash(*disk);
                                check(disk->save(), disk->save_error());
                                disk.reset();
                                disk = app::with_quick_save(entry, content);
                                disk->set_canvas(1280, 720);
                                check(disk->load(), disk->save_error());
                                check(picture_hash(*disk) == disk_picture,
                                      entry.id + " full disk restore");
                            }
                            const auto heard = advance(*game, frame, frame + 120, entry.id == "mindwalker_amiga", wings_only && variant == 2);
                            platform::write_file(prefix.string() + ".after", game->write_state());
                            platform::write_text(prefix.string() + ".after-picture", picture_hash(*game));
                            platform::write_text(prefix.string() + ".audio", heard);
                            std::cout << entry.id << " variant " << variant << " frame " << frame << " saved "
                                      << before.size() << " bytes\n";
                        } else {
                            const auto bytes = platform::read_file(prefix.string() + ".state");
                            game->read_state(bytes);
                            check(game->write_state() == bytes,
                                  entry.id + " state differs immediately after restore");
                            // Original and enhanced pictures must match immediately
                            // and after continuing in a fresh process.
                            const auto picture = platform::read_file(prefix.string() + ".picture");
                            check(picture_hash(*game) == std::string(picture.begin(), picture.end()),
                                  entry.id + " restored picture differs");
                            const auto heard = advance(*game, frame, frame + 120, entry.id == "mindwalker_amiga", wings_only && variant == 2);
                            check(game->write_state() == platform::read_file(prefix.string() + ".after"),
                                  entry.id + " continued state differs");
                            const auto expected_audio = platform::read_file(prefix.string() + ".audio");
                            check(heard == std::string(expected_audio.begin(), expected_audio.end()), entry.id + " continued audio differs");
                            const auto after = platform::read_file(prefix.string() + ".after-picture");
                            check(picture_hash(*game) == std::string(after.begin(), after.end()),
                                  entry.id + " continued picture differs");
                            std::cout << entry.id << " variant " << variant << " frame " << frame
                                      << " restored and continued identically\n";
                        }
                    }
            }
        if (wings_only) return 0;
        // Force a finish only in this test to cover the window where the next
        // course is selected while the previous course's renderer is retained.
        using namespace marble_madness;
        const data::GameFiles files(games / "marble_madness_amiga");
        const auto assets = data::load_assets(games / "marble_madness_amiga");
        game::GameFlow flow(files, assets);
        flow.set_crowd(true);
        std::vector<float> audio(1764);
        bool finished = false, changed = false;
        int checked = 0;
        auto encode = [](game::GameFlow &f) {
            state::Writer a;
            a(f);
            return a.bytes;
        };
        auto hash = [](game::GameFlow &f) {
            const auto p = f.picture();
            return platform::sha256({reinterpret_cast<const std::uint8_t *>(p.pixels.data()),
                                     p.pixels.size() * sizeof(platform::Color)});
        };
        for (int frame = 0; frame < 500; ++frame) {
            game::Controls controls;
            controls.fire = frame % 120 >= 40 && frame % 120 < 48;
            flow.tick(controls);
            flow.audio(audio, 22050);
            if (flow.racing() && !finished) {
                flow.finish_now();
                finished = true;
            }
            changed |= flow.course() != data::Course::practice;
            if (!finished || frame % 5)
                continue;
            const auto prefix = out / ("transition-" + std::to_string(frame));
            if (record) {
                platform::write_file(prefix.string() + ".state", encode(flow));
                platform::write_text(prefix.string() + ".picture", hash(flow));
                auto expected = flow;
                for (int step = 0; step < 8; ++step) {
                    expected.tick({});
                    expected.audio(audio, 22050);
                }
                platform::write_file(prefix.string() + ".after", encode(expected));
                platform::write_text(prefix.string() + ".after-picture", hash(expected));
            } else {
                game::GameFlow loaded(files, assets);
                const auto bytes = platform::read_file(prefix.string() + ".state");
                state::Reader a(bytes);
                a(loaded);
                a.finish();
                check(encode(loaded) == bytes, "transition state restore");
                const auto picture = platform::read_file(prefix.string() + ".picture");
                check(hash(loaded) == std::string(picture.begin(), picture.end()), "transition picture restore");
                for (int step = 0; step < 8; ++step) {
                    loaded.tick({});
                    loaded.audio(audio, 22050);
                }
                check(encode(loaded) == platform::read_file(prefix.string() + ".after"),
                      "transition continued state at frame " + std::to_string(frame));
                const auto after = platform::read_file(prefix.string() + ".after-picture");
                check(hash(loaded) == std::string(after.begin(), after.end()), "transition continued picture");
            }
            ++checked;
        }
        check(finished && changed && checked > 20, "course transition exercised");
        std::cout << checked << " crowd-race course-transition snapshots checked\n";
        return 0;
    }
    const auto root =
        fs::temp_directory_path() /
        ("joyport-save-check-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto content = root / "games/bruce_lee_c64";
    fs::create_directories(content);
    platform::write_text(content / "release.txt", "release test\nfile test sample\n");
    app::GameEntry entry;
    entry.id = "bruce_lee_c64";
    entry.start = [](const fs::path &) { return std::make_unique<Fake>(); };
    auto game = app::with_quick_save(entry, content);
    check(!game->has_save(), "empty slot");
    advance(*game, 0, 12);
    const auto expected = picture_hash(*game);
    check(game->save(), game->save_error());
    const auto preview = app::save_preview(content, entry.id);
    check(preview.available && preview.image.has_value(), "saved PNG preview");
    game.reset();
    game = app::with_quick_save(entry, content);
    check(game->has_save() && game->load(), "fresh module loads disk slot");
    check(picture_hash(*game) == expected, "disk progress");
    // Loading brings the save's game options back, but the screen filter (a presentation
    // option) keeps the player's current choice.
    game->set_option(1, 3);
    game->set_option(0, 1);
    check(game->load(), game->save_error());
    check(game->vhs_level() == 3 && game->options()[1].value == 3, "load keeps the current screen filter");
    check(game->options()[0].value == 0, "load restores the save's game options");
    check(!app::screen_effect(*game, 0), "picture games get no host screen effect (no double filtering)");
    game->set_option(0, 1);
    check(!game->save() && !game->save_error().empty(), "failed save reports error");
    check(game->load() && picture_hash(*game) == expected, "failed save retains prior slot");
    // A second committed save and an unfinished generation. The launcher
    // must ignore the unfinished one, and a corrupt latest state must fail
    // without changing the running module.
    advance(*game, 0, 3);
    check(game->save(), game->save_error());
    const auto saved = root / "saves/bruce_lee_c64";
    fs::create_directory(saved / "pending-interrupted");
    check(game->load(), "unfinished generation ignored");
    const auto still_running = picture_hash(*game);
    const auto latest = saved / "00000000000000000002/state.bin";
    auto damaged = platform::read_file(latest);
    damaged.back() ^= 1;
    platform::write_file(latest, damaged);
    check(!game->load() && picture_hash(*game) == still_running,
          "corrupt load must not replace the running game");
    check(fs::exists(saved / "00000000000000000001/state.bin"), "preceding generation retained");
    damaged.back() ^= 1;
    platform::write_file(latest, damaged);
    platform::write_text(content / "release.txt", "release another\nfile other sample\n");
    check(!game->load(), "different imported content rejected");
    check(!app::save_preview(content, entry.id).available, "incompatible continue disabled");
    fs::remove_all(root);
    std::cout << "Persistent save, screenshot, restart, failure retention and content checks passed.\n";
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
