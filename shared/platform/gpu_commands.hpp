// A programmable pass into a Reprise-owned game target. The SDL GPU device and target are
// borrowed; this scope owns only its command buffer. The host still composes and presents.
#pragma once
#include "platform/host.hpp"
#include <SDL3/SDL.h>
#include <stdexcept>

namespace platform {
class GpuCommands {
public:
    explicit GpuCommands(const GpuFrame& frame) : width(frame.width), height(frame.height) {
        device = SDL_GetGPURendererDevice(frame.renderer);
        if (!device) {
            throw std::runtime_error("This game needs Reprise's SDL GPU renderer.");
        }
        if (!SDL_FlushRenderer(frame.renderer)) {
            throw std::runtime_error(SDL_GetError());
        }
        const auto props = SDL_GetTextureProperties(frame.target);
        color =
            static_cast<SDL_GPUTexture*>(SDL_GetPointerProperty(props, SDL_PROP_TEXTURE_GPU_TEXTURE_POINTER, nullptr));
        format = SDL_GetGPUTextureFormatFromPixelFormat(frame.target->format);
        if (!color || format == SDL_GPU_TEXTUREFORMAT_INVALID) {
            throw std::runtime_error("The host target cannot be used for programmable rendering.");
        }
        command = SDL_AcquireGPUCommandBuffer(device);
        if (!command) {
            throw std::runtime_error(SDL_GetError());
        }
    }
    ~GpuCommands() {
        if (command) {
            SDL_CancelGPUCommandBuffer(command);
        }
    }
    GpuCommands(const GpuCommands&) = delete;
    GpuCommands& operator=(const GpuCommands&) = delete;
    void submit() {
        if (!command) {
            throw std::logic_error("GPU commands already submitted.");
        }
        auto* pending = command;
        command = nullptr;
        if (!SDL_SubmitGPUCommandBuffer(pending)) {
            throw std::runtime_error(SDL_GetError());
        }
    }
    SDL_GPUDevice* device{};
    SDL_GPUTexture* color{};
    SDL_GPUCommandBuffer* command{};
    SDL_GPUTextureFormat format{};
    int width{}, height{};
};
}   // namespace platform
