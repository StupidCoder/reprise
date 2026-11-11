#define XR_USE_GRAPHICS_API_VULKAN
#include "game_bridge.hpp"
#include "xr_texture.hpp"
#include <openxr/openxr_platform.h>
#include "spy_mouse_ios/reprise/entry.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace {
void require(bool ok, const char* what) {
    if (!ok) throw std::runtime_error(std::string(what) + ": " + SDL_GetError());
}
using Clock = std::chrono::steady_clock;
double ms(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b-a).count();
}
}

struct GameBridge::Impl {
    SDL_Renderer* renderer{};
    SDL_Texture* target{};
    std::unique_ptr<app::GameModule> game;
    XrTexture texture;
    XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD};
    int width = 960, height = 640;
    XrTime previous{};
    double accumulated{};
    uint64_t frames{}, ticks{};
    std::vector<double> readback_ms, upload_ms, total_ms, ownership_ms, fence_ms;
    bool ready{}, was_focused{};

    ~Impl() {
        game.reset(); // Release game textures while the SDL renderer is alive.
        if (target) SDL_DestroyTexture(target);
        if (renderer) SDL_DestroyRenderer(renderer);
        SDL_Quit();
    }
    void report() {
        const auto summary = [](const char* name, std::vector<double>& values) {
            if (values.empty()) return;
            double sum = 0;
            for (double v : values) sum += v;
            std::sort(values.begin(), values.end());
            std::printf("Bridge %s n=%zu mean=%.3f p95=%.3f max=%.3f ms\n", name,
                values.size(), sum / values.size(), values[(values.size()-1)*95/100], values.back());
            values.clear();
        };
        summary("draw-readback-convert", readback_ms);
        summary("copy-upload-wait", upload_ms);
        summary("XR-image-ownership-wait", ownership_ms);
        summary("Vulkan-upload-fence", fence_ms);
        summary("total", total_ms);
        std::printf("Bridge frames=%llu game ticks=%llu\n",
                    (unsigned long long)frames, (unsigned long long)ticks);
    }
};

GameBridge::GameBridge() : impl_(std::make_unique<Impl>()) {}
GameBridge::~GameBridge() { impl_->report(); }

void GameBridge::open(XrSession session, XrSpace space, VkPhysicalDevice physical,
                      VkDevice device, VkQueue queue, unsigned family, const char* content) {
    auto& p = *impl_;
    // SDL needs its video subsystem to load Vulkan even without a window.
    require(SDL_Init(SDL_INIT_VIDEO), "SDL_Init video");
    p.renderer = SDL_CreateGPURenderer(nullptr, nullptr);
    require(p.renderer != nullptr, "offscreen SDL GPU renderer");
    p.target = SDL_CreateTexture(p.renderer, SDL_PIXELFORMAT_RGBA32,
                                SDL_TEXTUREACCESS_TARGET, p.width, p.height);
    require(p.target != nullptr, "SDL game target");
    auto* gpu = SDL_GetGPURendererDevice(p.renderer);
    auto props = SDL_GetGPUDeviceProperties(gpu);
    std::printf("SDL %d renderer=%s GPU=%s driver=%s\n", SDL_GetVersion(), SDL_GetRendererName(p.renderer),
                SDL_GetStringProperty(props, SDL_PROP_GPU_DEVICE_NAME_STRING, "unknown"), SDL_GetGPUDeviceDriver(gpu));
    const auto texture_props = SDL_GetTextureProperties(p.target);
    std::printf("SDL target GPU pointer=%s native VkImage=%llu; separate-device readback bridge\n",
        SDL_GetPointerProperty(texture_props, SDL_PROP_TEXTURE_GPU_TEXTURE_POINTER, nullptr) ? "yes" : "no",
        (unsigned long long)SDL_GetNumberProperty(texture_props, SDL_PROP_TEXTURE_VULKAN_TEXTURE_NUMBER, 0));
    p.game = spy_mouse_reprise::entry().start(content);
    std::printf("Spy Mouse module loaded; simulation step %.3f ms; no input/audio/save UI in this diagnostic\n",
                p.game->step_seconds()*1000);

    p.texture.open(session, physical, device, queue, family, p.width, p.height);
    p.quad.space = space;
    p.quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    p.quad.subImage.swapchain = p.texture.swapchain();
    p.quad.subImage.imageRect.extent = {p.width, p.height};
    p.quad.pose.orientation.w = 1;
    p.quad.pose.position = {0, 0, -2};
    p.quad.size = {1.5f, 1.f};
}

void GameBridge::update(XrTime predicted_time, bool focused) {
    auto& p = *impl_;
    const auto start = Clock::now();
    SDL_PumpEvents();
    // Drop time accumulated during focus loss; never accelerate simulation on return.
    if (focused && p.was_focused && p.previous)
        p.accumulated += std::clamp((predicted_time - p.previous) / 1e9, 0.0, .1);
    else p.accumulated = 0;
    p.previous = predicted_time;
    p.was_focused = focused;
    bool changed = !p.ready;
    while (p.accumulated >= p.game->step_seconds()) {
        p.game->tick({});
        p.accumulated -= p.game->step_seconds();
        ++p.ticks; changed = true;
    }
    if (!changed) return;
    if (!p.game->stopped().empty()) throw std::runtime_error(p.game->stopped());
    const auto draw_start = Clock::now();
    require(SDL_SetRenderTarget(p.renderer, p.target), "set game target");
    p.game->draw_gpu({p.renderer, p.target, p.width, p.height});
    SDL_Surface* read = SDL_RenderReadPixels(p.renderer, nullptr);
    require(read != nullptr, "game readback");
    SDL_Surface* rgba = SDL_ConvertSurface(read, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(read);
    require(rgba != nullptr, "RGBA conversion");
    if (p.frames == 0 || (p.frames+1) % 300 == 0) {
        uint64_t hash = 14695981039346656037ull;
        for (int y = 0; y < rgba->h; y += 16) {
            const auto* row = static_cast<const unsigned char*>(rgba->pixels) + y*rgba->pitch;
            for (int x = 0; x < rgba->w*4; x += 13) hash = (hash ^ row[x])*1099511628211ull;
        }
        std::printf("Bridge RGBA sample hash frame=%llu %016llx\n",
                    (unsigned long long)(p.frames+1), (unsigned long long)hash);
    }
    const auto read_end = Clock::now();
    const auto transfer = p.texture.upload(rgba->pixels, rgba->pitch);
    SDL_DestroySurface(rgba);
    p.ready = true;
    const auto finish = Clock::now();
    p.readback_ms.push_back(ms(draw_start, read_end));
    p.upload_ms.push_back(ms(read_end, finish));
    p.ownership_ms.push_back(transfer.ownership_ms);
    p.fence_ms.push_back(transfer.fence_ms);
    p.total_ms.push_back(ms(start, finish));
    if (++p.frames % 300 == 0) p.report();
}

const XrCompositionLayerBaseHeader* GameBridge::layer() const {
    return impl_->ready ? reinterpret_cast<const XrCompositionLayerBaseHeader*>(&impl_->quad) : nullptr;
}

void GameBridge::pause() {
    impl_->previous = 0;
    impl_->accumulated = 0;
    impl_->was_focused = false;
}
