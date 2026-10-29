// Spy Mouse through Reprise's game interface, headless: the catalog entry's module ticks at the
// original's fixed step, takes ordered pointer events and draws through GpuFrame into SDL's
// software renderer (no window). Needs an imported copy of the game: set SPY_MOUSE_CONTENT to
// a Reprise content folder (<data>/games/spy_mouse_ios); without it the check is skipped.
//
//   spy_mouse_checks [SCRIPT [SHOTS]]   SCRIPT (or SPY_MOUSE_SCRIPT): a reference-harness touch
//                               script of level 1-1 (FRAME down|move|up X Y; the level's first
//                               tick is frame 1008); SHOTS: a folder for the pictures taken
//
// Checks: a first start goes into level 1-1; the script's path wins it (results and the
// profile); drawing changes nothing (pictures before and after an extra draw, and the state);
// a quick save restored in a fresh module (as after a restart, through the save file Reprise
// writes) shows the same pictures as the original run at the same later ticks; the results'
// continue leads to the world map; a level started from the map plays.
#include "app/quick_save.hpp"
#include "spy_mouse_ios/reprise/entry.hpp"
#include <SDL3/SDL.h>
#include <cmath>
#include <cstring>
#include <functional>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}

// The GPU surface of a software renderer (no window).
class SoftwareSurface final : public platform::GpuSurface {
public:
    SoftwareSurface() {
        surface_ = SDL_CreateSurface(8, 8, SDL_PIXELFORMAT_RGBA32);
        renderer_ = SDL_CreateSoftwareRenderer(surface_);
    }
    ~SoftwareSurface() override {
        SDL_DestroyRenderer(renderer_);
        SDL_DestroySurface(surface_);
    }
    SDL_Renderer* renderer() const { return renderer_; }
    platform::Image capture(int width, int height, const std::function<void(const platform::GpuFrame&)>& draw) override {
        SDL_Texture* target = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);
        SDL_SetRenderTarget(renderer_, target);
        draw(platform::GpuFrame{renderer_, target, width, height});
        SDL_SetRenderTarget(renderer_, target);
        SDL_Surface* read = SDL_RenderReadPixels(renderer_, nullptr);
        SDL_SetRenderTarget(renderer_, nullptr);
        SDL_DestroyTexture(target);
        SDL_Surface* rgba = SDL_ConvertSurface(read, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(read);
        platform::Image image(rgba->w, rgba->h);
        for (int y = 0; y < rgba->h; ++y)
            std::memcpy(image.pixels.data() + std::size_t(y) * rgba->w,
                        static_cast<const std::uint8_t*>(rgba->pixels) + std::size_t(y) * rgba->pitch, std::size_t(rgba->w) * 4);
        SDL_DestroySurface(rgba);
        return image;
    }

private:
    SDL_Surface* surface_{};
    SDL_Renderer* renderer_{};
};

bool same(const platform::Image& a, const platform::Image& b) {
    if (a.width != b.width || a.height != b.height) return false;
    for (std::size_t i = 0; i < a.pixels.size(); ++i)
        if (a.pixels[i].r != b.pixels[i].r || a.pixels[i].g != b.pixels[i].g || a.pixels[i].b != b.pixels[i].b) return false;
    return true;
}

struct Touch {
    int frame;
    platform::PointerEvent event;
};

std::vector<Touch> read_script(const fs::path& file) {
    std::vector<Touch> touches;
    std::ifstream in(file);
    for (std::string line; std::getline(in, line);) {
        std::istringstream ss(line);
        int frame;
        std::string kind;
        float x, y;
        if (!(ss >> frame >> kind >> x >> y)) continue;
        const auto k = kind == "down" ? platform::PointerEvent::Kind::down
                       : kind == "move" ? platform::PointerEvent::Kind::move
                       : kind == "up"   ? platform::PointerEvent::Kind::up
                                        : platform::PointerEvent::Kind(-1);
        if (int(k) < 0) continue;
        touches.push_back({frame, {k, x, y}});
    }
    return touches;
}

// The loudest sample the game's mix gave so far (one tick's sound pulled after each tick, as
// Reprise's loop does).
float audio_peak = 0.0f;

// Runs ticks first..last (1-based) of the current scene; a touch of harness frame F is handled in
// tick F - 1006 (the harness's level ticks start at frame 1008).
void run(app::GameModule& game, const std::vector<Touch>& touches, int first, int last, int frame_offset = 1006) {
    for (int tick = first; tick <= last; ++tick) {
        app::GameInput input;
        for (const Touch& t : touches)
            if (t.frame == tick + frame_offset) input.pointer_events.push_back(t.event);
        game.tick(input);
        std::vector<float> sound(816 * 2);  // 17 ms at 48 kHz
        game.audio(sound, 48000);
        for (const float v : sound) audio_peak = std::max(audio_peak, std::abs(v));
    }
}

void tap(app::GameModule& game, float x, float y) {
    app::GameInput input;
    input.pointer_events = {{platform::PointerEvent::Kind::down, x, y}, {platform::PointerEvent::Kind::up, x, y}};
    game.tick(input);
}
} // namespace

int main(int argc, char** argv) {
    const char* content_env = std::getenv("SPY_MOUSE_CONTENT");
    const char* script = argc > 1 ? argv[1] : std::getenv("SPY_MOUSE_SCRIPT");
    if (!content_env || !script) {
        std::printf("skipped: SPY_MOUSE_CONTENT and SPY_MOUSE_SCRIPT (an imported copy and a 1-1 touch script) are not set\n");
        return 0;
    }
    // A scratch data folder: the import (linked), progress and saves of its own.
    const fs::path source = content_env;
    const fs::path data = fs::temp_directory_path() / "reprise-spy-mouse-check";
    fs::remove_all(data);
    fs::create_directories(data / "games");
    fs::create_directory_symlink(fs::absolute(source), data / "games" / "spy_mouse_ios");
    const fs::path content = data / "games" / "spy_mouse_ios";
    const fs::path shots = argc > 2 ? fs::path(argv[2]) : fs::path();
    const auto touches = read_script(script);
    if (touches.empty()) {
        std::printf("no touch script given\n");
        return 2;
    }
    SoftwareSurface surface;
    const auto entry = spy_mouse_reprise::entry();
    auto save_shot = [&](const platform::Image& image, const std::string& name) {
        if (!shots.empty()) platform::save_png(image, shots / (name + ".png"));
    };

    // A first start: level 1-1 (the tutorial), its path drawn by the script.
    auto game = app::with_quick_save(entry, content);
    game->attach_gpu(&surface);
    check(game->gpu_frame_size() && (*game->gpu_frame_size())[0] == 480, "a GPU-drawn 480 x 320 frame");
    // The first 60 ticks have no touch yet: the level's music (HE-AAC, decoded by the engine on
    // every platform) and its quiet ambience. Without the music the peak stays near 0.03.
    run(*game, touches, 1, 60);
    check(audio_peak > 0.1f, "the level's music reaches the mix (peak " + std::to_string(audio_peak) + ")");
    run(*game, touches, 61, 400);
    const auto before = game->picture();
    const auto again = game->picture();
    check(same(before, again), "drawing twice gives the same picture (drawing changes nothing)");
    save_shot(before, "tick0400");
    check(game->save(), "quick save in the level (" + game->save_error() + ")");
    run(*game, touches, 401, 700);
    const auto at700 = game->picture();
    save_shot(at700, "tick0700");

    // As after a restart: a fresh module from the save files.
    auto restored = app::with_quick_save(entry, content);
    restored->attach_gpu(&surface);
    check(restored->has_save(), "the save is found by a new instance");
    check(restored->load(), "load into a new instance (" + restored->save_error() + ")");
    check(same(restored->picture(), before), "the loaded game shows the saved moment");
    run(*restored, touches, 401, 700);
    check(same(restored->picture(), at700), "300 ticks later the loaded game matches the original run");

    // The screen filter is presentation only: the same save played on with the VHS look on
    // keeps the look across the load and gives the same raw pictures, state and sound.
    auto filtered = app::with_quick_save(entry, content);
    filtered->attach_gpu(&surface);
    filtered->set_option(0, 3);
    check(filtered->load() && filtered->vhs_level() == 3, "loading a save keeps the chosen VHS look");
    check(app::screen_effect(*filtered, 0).has_value(), "... which Reprise's filter stage applies to the frame");
    auto replay = app::with_quick_save(entry, content);
    replay->attach_gpu(&surface);
    check(replay->load(), "the same save without the look");
    const float peak_before = audio_peak;
    audio_peak = 0;
    run(*filtered, touches, 401, 700);
    const float filtered_peak = audio_peak;
    audio_peak = 0;
    run(*replay, touches, 401, 700);
    check(same(filtered->picture(), at700) && filtered->write_state() == replay->write_state() &&
              filtered_peak == audio_peak,
          "with the look on: the same raw picture (save previews), state and sound");
    audio_peak = std::max(peak_before, audio_peak);
    filtered.reset();
    replay.reset();
    const auto preview = app::save_preview(content, "spy_mouse_ios");
    check(preview.available && preview.image && preview.error.empty(), "the details screen's save preview (" + preview.error + ")");

    // On to the results.
    run(*game, touches, 701, 1500);
    save_shot(game->picture(), "tick1500");
    std::ifstream progress(data / "progress" / "spy_mouse_ios" / "progress.txt");
    std::stringstream text;
    text << progress.rdbuf();
    check(text.str().find("\n1 ") != std::string::npos && text.str().find("current 1") != std::string::npos,
          "level 1-1 won and recorded in the campaign progress");
    // The results' continue button, then the map.
    for (int i = 0; i < 300; ++i) {
        if (i == 120) tap(*game, 365, 245);
        else game->tick({});
    }
    save_shot(game->picture(), "map");
    // A level from the map: the 1-2 pad (the mouse walks to it), then its Play bubble.
    for (int i = 0; i < 400; ++i) {
        if (i == 20) tap(*game, 120, 288);
        else game->tick({});
    }
    const auto map_picture = game->picture();
    save_shot(map_picture, "map_later");
    tap(*game, 161, 245);  // the Play bubble beside the pad
    for (int i = 0; i < 400; ++i) game->tick({});
    const auto level12 = game->picture();
    save_shot(level12, "level_1_2");
    check(!same(level12, map_picture), "Play on the map starts level 1-2 (see level_1_2.png)");
    check(game->stopped().empty() && !game->finished(), "the game goes on (" + game->stopped() + ")");

    // Loading the save made before 1-1 was won keeps the win (progress is never lowered).
    auto read_progress = [&] {
        std::ifstream in(data / "progress" / "spy_mouse_ios" / "progress.txt");
        std::stringstream all;
        all << in.rdbuf();
        return all.str();
    };
    const auto won = read_progress();
    check(game->load(), "load the earlier save after winning (" + game->save_error() + ")");
    const auto after = read_progress();
    auto level_line = [](const std::string& text, const std::string& uid) {
        const auto at = text.find("\n" + uid + " ");
        return at == std::string::npos ? std::string() : text.substr(at + 1, text.find('\n', at + 1) - at - 1);
    };
    check(!level_line(won, "1").empty() && level_line(won, "1").find(" -1 ") == std::string::npos &&
              level_line(after, "1") == level_line(won, "1"),
          "the earlier save keeps 1-1's win and best total in the campaign progress (" + level_line(after, "1") + ")");
    check(after.find("\n2 -1 0 0 0 0 0 1") != std::string::npos, "... and 1-2 stays unlocked");
    // On to the map again (the loaded 1-1 played out, the results continued), and a quick save
    // there, loaded in a new instance.
    run(*game, touches, 401, 1500);
    for (int i = 0; i < 300; ++i) {
        if (i == 120) tap(*game, 365, 245);
        else game->tick({});
    }
    const auto after_load_map = game->picture();
    save_shot(after_load_map, "map_again");
    check(game->save(), "quick save on the world map (" + game->save_error() + ")");
    {
        auto again = app::with_quick_save(entry, content);
        again->attach_gpu(&surface);
        check(again->load() && same(again->picture(), after_load_map), "the map's save loads in a new instance (" + again->save_error() + ")");
        for (int i = 0; i < 60; ++i) again->tick({});
        for (int i = 0; i < 60; ++i) game->tick({});
        check(same(again->picture(), game->picture()), "... and goes on the same way");
    }
    // The map's own quit button (top left): the player leaves for the library.
    tap(*game, 20, 20);
    check(game->finished() && game->stopped().empty(), "the map's quit button leaves the game (back to the library)");
    std::printf("%d failure(s)\n", failures);
    fs::remove_all(data);
    return failures ? 1 : 0;
}
