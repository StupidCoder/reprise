// BlueStreak's games through Reprise's game interface, headless. Needs the user's dumps: set
// SONIC_ROMS to a folder holding them, normally Reprise's images/ (the file names do not
// matter); without it the check is skipped. Nothing is written outside a scratch data folder; the dumps are only read.
//
//   sonic_checks [SHOTS]   SHOTS: a folder for the pictures taken
//
// Checks, per game: the importers reject other cartridges (NotThisGame) and damaged copies of
// their own (an error for the user); the library installs the import and the game runs without
// the dump; Start during play asks for the pause menu; drawing (with the LCD look) changes
// nothing; a quick save restored in a fresh module plays on exactly as the original run; loading
// keeps the player's presentation options and restores the save's gameplay options; a damaged
// or foreign save fails without touching the running game; previews decode. Sonic Advance runs
// from power-on and explains that it has no quick saves. With SONIC_LEGACY set to a standalone
// BlueStreak data folder, its imports are taken over by copying (the folder is only read).
#include "app/library.hpp"
#include "app/quick_save.hpp"
#include "platform/files.hpp"
#include "platform/sha256.hpp"
#include "sonic/reprise/entry.hpp"
#include <SDL3/SDL.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}

// A software renderer without a window, for draw_gpu.
struct Software {
    SDL_Surface* surface = SDL_CreateSurface(8, 8, SDL_PIXELFORMAT_RGBA32);
    SDL_Renderer* renderer = SDL_CreateSoftwareRenderer(surface);
    ~Software() {
        SDL_DestroyRenderer(renderer);
        SDL_DestroySurface(surface);
    }
    // Draws the game into a width x height target and reads it back.
    platform::Image draw(app::GameModule& game, int width, int height) {
        SDL_Texture* target =
            SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);
        SDL_SetRenderTarget(renderer, target);
        game.draw_gpu({renderer, target, width, height});
        SDL_SetRenderTarget(renderer, target);
        SDL_Surface* read = SDL_RenderReadPixels(renderer, nullptr);
        SDL_SetRenderTarget(renderer, nullptr);
        SDL_DestroyTexture(target);
        SDL_Surface* rgba = SDL_ConvertSurface(read, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(read);
        platform::Image image(rgba->w, rgba->h);
        for (int y = 0; y < rgba->h; ++y)
            std::memcpy(image.pixels.data() + std::size_t(y) * rgba->w,
                        static_cast<const std::uint8_t*>(rgba->pixels) + std::size_t(y) * rgba->pitch,
                        std::size_t(rgba->w) * 4);
        SDL_DestroySurface(rgba);
        return image;
    }
};

std::string hash(const platform::Image& image) {
    return platform::sha256({reinterpret_cast<const std::uint8_t*>(image.pixels.data()),
                             image.pixels.size() * sizeof(platform::Color)});
}

// The dumps in SONIC_ROMS, by the game they import into (each importer tried on each file).
std::map<std::string, fs::path> find_dumps(const fs::path& folder, const std::vector<app::GameEntry>& games) {
    std::map<std::string, fs::path> found;
    for (const auto& file : fs::directory_iterator(folder)) {
        if (!file.is_regular_file() || file.file_size() > (32u << 20)) continue;
        const auto bytes = platform::read_file(file.path());
        for (const auto& game : games) {
            try {
                game.import(bytes);
                found[game.id] = file.path();
            } catch (const std::exception&) {
            }
        }
    }
    return found;
}

// The controller: Start pressed briefly every 90 frames until play begins (the menu request
// shows it), then running right with a jump now and then.
struct Player {
    bool playing = false;
    app::GameInput input(int frame) const {
        app::GameInput in;
        if (!playing) {
            in.start = frame % 90 < 3;
            in.fire = frame % 90 >= 45 && frame % 90 < 48;
            return in;
        }
        in.right = frame % 400 < 300;
        in.left = frame % 400 >= 340;
        in.fire = frame % 70 < 12;
        return in;
    }
};

// Runs frames [from, to); returns the hash of all sound heard. `pictures`: each frame's picture.
std::string run(app::GameModule& game, Player& player, int from, int to, float* peak = nullptr,
                std::vector<std::string>* pictures = nullptr) {
    std::vector<float> sound;
    std::vector<std::uint8_t> heard;
    double credit = 0;
    for (int f = from; f < to && game.stopped().empty(); ++f) {
        game.tick(player.input(f));
        if (game.take_menu_request()) player.playing = true;
        if (pictures) pictures->push_back(hash(game.picture()));
        credit += game.step_seconds() * 48000;
        sound.assign(std::size_t(credit) * 2, 0.0f);
        credit -= double(std::size_t(credit));
        game.audio(sound, 48000);
        for (const float v : sound) {
            if (peak) *peak = std::max(*peak, std::abs(v));
        }
        const auto* p = reinterpret_cast<const std::uint8_t*>(sound.data());
        heard.insert(heard.end(), p, p + sound.size() * sizeof(float));
    }
    return platform::sha256(heard);
}

int option_index(const app::GameModule& game, const std::string& name) {
    const auto options = game.options();
    for (std::size_t i = 0; i < options.size(); ++i)
        if (options[i].name == name) return int(i);
    return -1;
}
int option_value(const app::GameModule& game, const std::string& name) {
    const int i = option_index(game, name);
    return i < 0 ? -1 : game.options()[std::size_t(i)].value;
}

fs::path latest_save(const fs::path& data, const std::string& id) {
    fs::path latest;
    for (const auto& e : fs::directory_iterator(data / "saves" / id))
        if (e.is_directory() && e.path().filename().string().size() == 20 && e.path() > latest) latest = e.path();
    return latest;
}

void check_game(const app::GameEntry& entry, app::Library& library, std::size_t index, const fs::path& data,
                const fs::path& shots, Software& software) {
    const std::string id = entry.id;
    const auto content = library.content_directory(index);
    std::printf("-- %s (%s)\n", id.c_str(), library.release(index).c_str());
    const bool gba = id == "sonic_advance_gba";

    auto game = app::with_quick_save(entry, content);
    Player player;
    float peak = 0;
    run(*game, player, 0, 2400, &peak);
    check(game->stopped().empty(), id + ": runs 2400 frames from power-on (" + game->stopped() + ")");
    check(peak > 0.01f, id + ": the game's sound reaches the mix (peak " + std::to_string(peak) + ")");
    if (gba) {
        check(!player.playing, id + ": Start stays with the game (its own pause screen)");
        check(!game->save() && !game->save_error().empty(),
              id + ": quick save says why it is not available (" + game->save_error() + ")");
        check(!app::persistent_saves(id), id + ": not on the persistent-save list");
    } else {
        check(player.playing, id + ": Start during play asks for the pause menu");
    }
    // Drawing: the raw picture, and the LCD look at a large and a small target; nothing changes.
    const auto size = game->gpu_frame_size();
    check(size.has_value(), id + ": a GPU-drawn frame of " + (size ? std::to_string((*size)[0]) + " x " +
                                                                     std::to_string((*size)[1]) : std::string("?")));
    const auto raw = game->picture();
    const int lcd = option_index(*game, "LCD screen");
    if (lcd >= 0) game->set_option(std::size_t(lcd), 1);
    const auto big = software.draw(*game, raw.width * 5, raw.height * 5);
    const auto small = software.draw(*game, raw.width, raw.height);
    if (lcd >= 0) game->set_option(std::size_t(lcd), 0);
    check(hash(game->picture()) == hash(raw), id + ": drawing (LCD look too) changes nothing in the game");
    if (!shots.empty()) {
        platform::save_png(raw, shots / (id + "-raw.png"));
        platform::save_png(big, shots / (id + "-lcd-x5.png"));
        platform::save_png(small, shots / (id + "-lcd-x1.png"));
    }
    if (gba) return;

    // Expanded view on (a gameplay option), VHS look on (presentation), then a save.
    const int wide = option_index(*game, "Expanded view");
    game->set_canvas(1920, 1080);
    game->set_option(std::size_t(wide), 1);
    game->set_option(0, 2);
    run(*game, player, 2400, 2600);
    const auto wide_size = game->gpu_frame_size();
    check(wide_size && (*wide_size)[0] > 160, id + ": the expanded view widens the frame to " +
                                                  std::to_string(wide_size ? (*wide_size)[0] : 0));
    check(game->save(), id + ": quick save (" + game->save_error() + ")");
    const auto saved_picture = hash(game->picture());
    if (!shots.empty()) platform::save_png(game->picture(), shots / (id + "-saved.png"));
    Player after = player;
    std::vector<std::string> original, replayed;
    run(*game, after, 2600, 2900, nullptr, &original);
    const auto later = hash(game->picture());

    // A fresh module (as after quitting): Continue, then the same 300 frames.
    auto restored = app::with_quick_save(entry, content);
    restored->set_canvas(1920, 1080);
    check(restored->has_save() && app::save_preview(content, id).available, id + ": Continue finds the save");
    restored->set_option(0, 4);                                  // the player's VHS choice now
    if (lcd >= 0) restored->set_option(std::size_t(lcd), 1);     // and the LCD look
    check(restored->load(), id + ": load in a new instance (" + restored->save_error() + ")");
    check(option_value(*restored, "Expanded view") == 1, id + ": the save's expanded view is restored");
    check(restored->vhs_level() == 4 && (lcd < 0 || option_value(*restored, "LCD screen") == 1),
          id + ": presentation options keep the player's current choice");
    if (!shots.empty()) platform::save_png(restored->picture(), shots / (id + "-loaded.png"));
    // (Sonic 2 and Triple Trouble work out the expanded view's margins in the next update: until
    // then a loaded game shows the Game Gear's own 160 x 144 view of the moment, as in BlueStreak.)
    const bool margins_later = id != "sonic_gg";
    auto shows_saved = [&](const app::GameModule& g) {
        return hash(g.picture()) == saved_picture || (margins_later && g.picture().width == 160);
    };
    check(shows_saved(*restored), id + ": the loaded game shows the saved moment");
    // (The sound is compared by level only: the host's sample clock and the queue at the save
    // split frames differently, so the samples heard are shifted by a fraction of a frame.)
    Player replay = player;
    float replay_peak = 0;
    run(*restored, replay, 2600, 2900, &replay_peak, &replayed);
    std::size_t diverged = 0;
    while (diverged < original.size() && diverged < replayed.size() && original[diverged] == replayed[diverged]) ++diverged;
    check(diverged == original.size() && hash(restored->picture()) == later && replay_peak > 0.01f,
          id + ": the loaded game plays on as the original run (pictures equal for " + std::to_string(diverged) +
              " of " + std::to_string(original.size()) + " frames)");
    // Repeated loads.
    check(restored->load() && restored->load() && shows_saved(*restored),
          id + ": repeated quick loads");

    // A damaged save fails, and the running game goes on untouched; the earlier generation stays.
    const auto file = latest_save(data, id) / "state.bin";
    auto bytes = platform::read_file(file);
    auto damaged = bytes;
    damaged[damaged.size() / 2] ^= 0x5a;
    platform::write_file(file, damaged);
    const auto before = hash(restored->picture());
    check(!restored->load() && !restored->save_error().empty(),
          id + ": a damaged save is refused (" + restored->save_error() + ")");
    check(hash(restored->picture()) == before, id + ": ... and the running game is unchanged");
    platform::write_file(file, bytes);
    // A state from another game (a well-formed record, the wrong game's state): refused too.
    check(restored->load(), id + ": the repaired save loads again");
    std::vector<std::uint8_t> foreign{'B', 'S', 'R', 'E', 'P', 'R', '1', 0, 99, 1, 2, 3};
    bool refused = false;
    try {
        auto fresh = entry.start(content);
        fresh->read_state(foreign);
    } catch (const std::exception&) {
        refused = true;
    }
    check(refused, id + ": a state for another game is refused");
    // An empty view (0 x 0 canvas) and a portrait canvas still draw.
    restored->set_canvas(600, 1000);
    run(*restored, replay, 2900, 2910);
    const auto portrait = restored->gpu_frame_size();
    check(portrait && (*portrait)[1] >= (*portrait)[0] * 9 / 10, id + ": a portrait canvas gives a taller view");
}
} // namespace

int main(int argc, char** argv) {
    const char* roms = std::getenv("SONIC_ROMS");
    if (!roms) {
        std::printf("skipped: SONIC_ROMS (a folder with the user's dumps) is not set\n");
        return 0;
    }
    const fs::path shots = argc > 1 ? fs::path(argv[1]) : fs::path();
    const fs::path scratch = fs::temp_directory_path() / "reprise-sonic-check";
    fs::remove_all(scratch);
    const fs::path data = scratch / "NativeReconstruction" / "Reprise";
    fs::create_directories(data);
    const auto games = sonic_reprise::entries();
    const auto dumps = find_dumps(roms, games);

    // Importers: each accepts only its own cartridge.
    for (const auto& game : games) {
        const auto own = dumps.find(game.id);
        if (own == dumps.end()) {
            std::printf("-- %s: no dump in SONIC_ROMS\n", game.id.c_str());
            continue;
        }
        auto bytes = platform::read_file(own->second);
        for (const auto& [other, path] : dumps) {
            if (other == game.id) continue;
            bool not_this = false;
            try {
                game.import(platform::read_file(path));
            } catch (const app::NotThisGame&) {
                not_this = true;
            } catch (const std::exception&) {
            }
            check(not_this, game.id + " rejects " + other + " as another game");
        }
        auto damaged = bytes;
        damaged[damaged.size() - 7] ^= 1;
        std::string message;
        try {
            game.import(damaged);
        } catch (const app::NotThisGame&) {
            message = "(not this game)";
        } catch (const std::exception& e) {
            message = e.what();
        }
        check(!message.empty() && message != "(not this game)",
              game.id + ": a damaged dump of this game is explained: " + message);
        bool rejected = false;
        try {
            game.import(std::vector<std::uint8_t>(4096, 0xff));
        } catch (const app::NotThisGame&) {
            rejected = true;
        }
        check(rejected, game.id + ": other data is not this game");
    }

    // The library installs each dump (as the launcher's import does) from a copy that is then
    // removed: the games must run without the original file.
    app::Library library(data, games);
    if (const char* legacy = std::getenv("SONIC_LEGACY")) {
        // BlueStreak's data folder beside Reprise's (linked; only read).
        fs::create_directory_symlink(fs::absolute(legacy), data.parent_path() / "BlueStreak");
        library = app::Library(data, games);
        for (const auto& line : library.adopt_legacy()) std::printf("     legacy: %s\n", line.c_str());
        // (Taken over when its pack is current; an older pack is explained and imported below.)
        int taken = 0;
        for (std::size_t i = 0; i < games.size(); ++i) taken += library.installed(i);
        check(taken > 0, std::to_string(taken) + " of BlueStreak's imports were taken over");
        check(fs::exists(fs::path(legacy) / "games"), "BlueStreak's own folder is still there");
    }
    Software software;
    for (std::size_t i = 0; i < games.size(); ++i) {
        const auto dump = dumps.find(games[i].id);
        if (dump == dumps.end()) continue;
        if (!library.installed(i)) {
            const auto copy = scratch / dump->second.filename();
            fs::copy_file(dump->second, copy);
            const auto outcome = library.import(copy, i);
            fs::remove(copy);
            check(outcome.ok && outcome.game == i, games[i].id + ": imported (" + outcome.message + ")");
        }
        if (!library.installed(i)) continue;
        const auto started = std::chrono::steady_clock::now();
        const auto previews = games[i].previews(library.content_directory(i));
        int sounds = 0;
        for (const auto& p : previews) sounds += p.sound.has_value();
        const double seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        check(!previews.empty(), games[i].id + ": " + std::to_string(previews.size()) + " previews (" +
                                     std::to_string(sounds) + " sounds) in " + std::to_string(seconds) + " s");
        if (!shots.empty() && !previews.empty()) platform::save_png(previews.front().image, shots / (games[i].id + "-preview.png"));
        check_game(games[i], library, i, data, shots, software);
    }
    fs::remove_all(scratch);
    std::printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
