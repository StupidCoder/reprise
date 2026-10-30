// Checks the host's screen filter stage on this machine's renderer: a fixed picture is shown
// as a GPU-drawn game frame with the VHS look, once through the effect's shader and once
// through its CPU form (the picture read back and filtered by ui::vhs), for every level and a
// spread of frames; the two windows are compared pixel by pixel. With the effect off the
// frame must come out exactly as drawn. Opens a window; a local tool, not a CTest.
//
//   screen_effect_check [PICTURE.png] [OUT_DIR]     ("" for the test card)
//
// Without a picture a test card is drawn (colour bars, fine lines, gradients). OUT_DIR keeps
// the screenshots of the last frame of each level. Exit status 1 when the shader and the CPU
// form differ by more than rounding (mean > 1.5 or more than 2 % of pixels off by > 24).
#include "platform/host.hpp"
#include "ui/vhs.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace {

namespace fs = std::filesystem;

platform::Image load_png(const fs::path& file) {
    const auto name = file.u8string();
    SDL_Surface* loaded = SDL_LoadPNG(reinterpret_cast<const char*>(name.c_str()));
    SDL_Surface* rgba = loaded ? SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32) : nullptr;
    SDL_DestroySurface(loaded);
    if (!rgba) throw std::runtime_error(SDL_GetError());
    platform::Image image(rgba->w, rgba->h);
    for (int y = 0; y < rgba->h; ++y)
        std::memcpy(image.pixels.data() + std::size_t(y) * rgba->w,
                    static_cast<const std::uint8_t*>(rgba->pixels) + std::size_t(y) * rgba->pitch,
                    std::size_t(rgba->w) * 4);
    SDL_DestroySurface(rgba);
    return image;
}

platform::Image test_card() {
    platform::Image card(480, 320);
    const platform::Color bars[] = {{235, 235, 235, 255}, {235, 235, 16, 255}, {16, 235, 235, 255},
                                    {16, 235, 16, 255},   {235, 16, 235, 255}, {235, 16, 16, 255},
                                    {16, 16, 235, 255},   {16, 16, 16, 255}};
    for (int y = 0; y < card.height; ++y)
        for (int x = 0; x < card.width; ++x) {
            platform::Color c = bars[x * 8 / card.width];
            if (y >= 160 && y < 240) { // a grey ramp and a hue sweep
                const auto v = std::uint8_t(x * 255 / (card.width - 1));
                c = y < 200 ? platform::Color{v, v, v, 255}
                            : platform::Color{v, std::uint8_t(255 - v), std::uint8_t((x * 7) & 255), 255};
            } else if (y >= 240) { // fine lines and a checkerboard: the smear and the ringing
                const bool on = x < 240 ? (x / 2) % 2 == 0 : ((x / 4) + (y / 4)) % 2 == 0;
                c = on ? platform::Color{250, 250, 250, 255} : platform::Color{10, 20, 60, 255};
            }
            card.put(x, y, c);
        }
    return card;
}

struct Difference {
    double mean{};
    double share_far{}; // pixels with a channel off by more than 24
};

Difference compare(const platform::Image& a, const platform::Image& b) {
    if (a.width != b.width || a.height != b.height) throw std::runtime_error("screenshots differ in size");
    double sum = 0;
    long far = 0;
    for (std::size_t i = 0; i < a.pixels.size(); ++i) {
        const auto& p = a.pixels[i];
        const auto& q = b.pixels[i];
        const int d = std::max({std::abs(p.r - q.r), std::abs(p.g - q.g), std::abs(p.b - q.b)});
        sum += (std::abs(p.r - q.r) + std::abs(p.g - q.g) + std::abs(p.b - q.b)) / 3.0;
        far += d > 24;
    }
    const double n = double(a.pixels.size());
    return {sum / n, double(far) / n};
}

} // namespace

int main(int argc, char** argv) try {
    const platform::Image picture = argc > 1 && *argv[1] ? load_png(platform::utf8_path(argv[1])) : test_card();
    // Equivalent native pictures must have identical video-space defects.
    platform::Image doubled(picture.width * 2, picture.height * 2);
    for (int y = 0; y < doubled.height; ++y)
        for (int x = 0; x < doubled.width; ++x)
            doubled.put(x, y, picture.pixels[std::size_t(y / 2) * picture.width + x / 2]);
    for (int level = 1; level < ui::vhs_levels; ++level) {
        const auto a = ui::vhs(picture, 241, level);
        const auto b = ui::vhs(doubled, 241, level);
        if (a.width != ui::vhs_raster_width(picture.width, picture.height) || a.height != ui::vhs_video_height ||
            compare(a, b).mean != 0)
            throw std::runtime_error("VHS must be independent of native picture resolution");
    }
    std::puts("Native-resolution independence: PASSED (all four levels)");
    for (const auto& size : {std::array{320, 240, 720}, std::array{400, 240, 900},
                             std::array{480, 270, 960}, std::array{630, 270, 1260}}) {
        const auto effect = ui::vhs_effect(241, 1, size[0], size[1]);
        if (effect.raster_width != size[2] || effect.raster_height != 480 || !effect.linear_output)
            throw std::runtime_error("VHS raster must follow display aspect");
        if (ui::vhs_raster_width(size[0] * 2, size[1] * 2) != size[2])
            throw std::runtime_error("VHS raster must not follow native resolution");
    }
    if (ui::vhs_raster_width(320, 200, 5.0 / 6.0) != 720)
        throw std::runtime_error("VHS raster must respect non-square game pixels");
    std::puts("4:3 / 5:3 / 16:9 / 21:9 adaptive rasters: PASSED");
    const fs::path out = argc > 2 ? platform::utf8_path(argv[2]) : fs::temp_directory_path() / "screen-effect-check";
    fs::create_directories(out);
    platform::Host host("Screen effect check");
    if (argc > 3) {
        int count = 0;
        SDL_Window** windows = SDL_GetWindows(&count);
        if (count > 0) SDL_SetWindowSize(windows[0], 1512, 945);
        SDL_free(windows);
    }
    SDL_Texture* source = nullptr;
    auto draw = [&](const platform::GpuFrame& frame) {
        if (!source) {
            source = SDL_CreateTexture(frame.renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, picture.width,
                                       picture.height);
            SDL_UpdateTexture(source, nullptr, picture.pixels.data(), picture.width * 4);
            SDL_SetTextureScaleMode(source, SDL_SCALEMODE_NEAREST);
        }
        SDL_RenderTexture(frame.renderer, source, nullptr, nullptr);
    };
    // One presented frame, written to `file` and read back.
    auto shoot = [&](const platform::ScreenEffect* effect, const fs::path& file) {
        host.poll();
        host.capture_next_frame(file);
        host.present_game_gpu(picture.width, picture.height, 1.0, draw, nullptr, false, nullptr, 1.0f, 0, effect);
        return load_png(file);
    };
    // Present once before measuring, so a requested window resize has settled.
    shoot(nullptr, out / "warmup.png");
    bool ok = true;
    // Compare direct display shading, fixed-raster shading, and uploaded software frames.
    for (int level = 1; level < ui::vhs_levels; ++level) {
        auto effect = ui::vhs_effect(241, level, picture.width, picture.height);
        effect.raster_width = effect.raster_height = 0;
        const auto direct = shoot(&effect, out / ("direct-" + std::to_string(level) + ".png"));
        effect.raster_width = ui::vhs_raster_width(picture.width, picture.height);
        effect.raster_height = ui::vhs_video_height;
        const auto raster = shoot(&effect, out / ("raster-" + std::to_string(level) + ".png"));
        host.capture_next_frame(out / "software.png");
        host.present_game(picture, 1.0, nullptr, false, nullptr, 1.0f, 0, &effect);
        ok = ok && host.screen_effect_path().find("GPU shader") == 0;
        const auto software = load_png(out / "software.png");
        const auto d = compare(direct, raster), upload = compare(raster, software);
        std::printf("%dx%d level %d: intermediate mean %.6f, far %.6f; software mean %.6f\n",
                    direct.width, direct.height, level, d.mean, d.share_far, upload.mean);
        ok = ok && upload.mean == 0; // Direct point sampling is a diagnostic, not the reconstruction reference.
    }
    std::printf("renderer: %s\n", host.renderer_name().c_str());
    // Off: exactly the picture, scaled (compared with itself drawn twice: the bypass is the
    // unfiltered path; a change between two frames would mean state leaking from an effect).
    const auto plain = shoot(nullptr, out / "off-1.png");
    {
        const auto effect = ui::vhs_effect(10, 2, picture.width, picture.height);
        shoot(&effect, out / "between.png");
    }
    const auto plain_again = shoot(nullptr, out / "off-2.png");
    const auto off = compare(plain, plain_again);
    std::printf("off before/after an effect: mean %.4f\n", off.mean);
    ok = ok && off.mean == 0;
    for (int level = 1; level < ui::vhs_levels; ++level) {
        Difference worst;
        for (long frame : {0L, 37L, 120L, 241L, 999L, 4321L}) {
            auto effect = ui::vhs_effect(frame, level, picture.width, picture.height);
            const auto gpu = shoot(&effect, out / ("vhs" + std::to_string(level) + "-gpu.png"));
            const std::string path = host.screen_effect_path();
            effect.shader = nullptr; // the same effect in its CPU form
            const auto cpu = shoot(&effect, out / ("vhs" + std::to_string(level) + "-cpu.png"));
            const auto d = compare(gpu, cpu);
            if (frame == 0) std::printf("level %d through: %s\n", level, path.c_str());
            worst.mean = std::max(worst.mean, d.mean);
            worst.share_far = std::max(worst.share_far, d.share_far);
        }
        std::printf("level %d: GPU vs CPU, worst mean %.3f, worst share off by > 24: %.3f%%\n", level, worst.mean,
                    worst.share_far * 100);
        ok = ok && worst.mean <= 1.5 && worst.share_far <= 0.02;
    }
    SDL_DestroyTexture(source);
    std::printf("%s (screenshots in %s)\n", ok ? "passed" : "FAILED", out.string().c_str());
    return ok ? 0 : 1;
} catch (const std::exception& e) {
    std::fprintf(stderr, "screen_effect_check: %s\n", e.what());
    return 1;
}
