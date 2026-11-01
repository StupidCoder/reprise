// Draws the reconstructed scene into a borrowed Reprise SDL GPU target.
#pragma once
#include "gpu/Device.hpp"
#include <SDL3/SDL.h>
namespace captain_toad::gpu {
class SdlDevice final : public Device {
public:
    explicit SdlDevice(SDL_GPUDevice*);
    ~SdlDevice() override;
    void begin(SDL_GPUTexture*, SDL_GPUTextureFormat, int width, int height);
    void end();
    std::unique_ptr<Texture> createTexture(Format, int, int, const void* = nullptr) override;
    std::unique_ptr<Buffer> createBuffer(size_t, const void* = nullptr) override;
    std::unique_ptr<Program> createProgram(std::string_view, std::string_view, std::string_view) override;
    std::unique_ptr<Target> createTarget(int, int, bool = false) override;
    void bindTarget(Target*) override;
    Target* target() const override;
    void setViewport(const Rect&) override;
    Rect viewport() const override;
    void setScissor(const Rect&) override;
    Rect scissor() const override;
    void clear(const Clear&, const Rect& = {}) override;
    void draw(const Draw&) override;
    void draw(const Draw&, const Immediate&) override;
    void copyToTexture(Texture&, const Rect&) override;
    void generateMipmaps(Texture&) override;
    bool copyDepth(Target&, Target&) override;
    captain_toad::platform::Image readPixels(const Rect&) override;
    void checkErrors(const char*) override;
    void present() override;
    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};
}   // namespace captain_toad::gpu
