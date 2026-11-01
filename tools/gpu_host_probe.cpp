// Verifies programmable 3D passes share Reprise's device/targets with the SDL compositor.
// Synthetic geometry only: depth occlusion, stencil restriction, sampled texture, resize,
// repeated submissions, raw capture and the real VHS/overlay path. Requires a graphical session.
#include "platform/gpu_commands.hpp"
#include "ui/vhs.hpp"
#include "shaders/gpu_host_probe.hpp"
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <stdexcept>

namespace {
void check(bool ok) {
    if (!ok) {
        throw std::runtime_error(SDL_GetError());
    }
}
template <class T>
T* need(T* p) {
    check(p != nullptr);
    return p;
}
struct Probe {
    SDL_GPUDevice* device{};
    SDL_GPUTexture *depth{}, *white{};
    SDL_GPUSampler* sampler{};
    SDL_GPUShader *vertex{}, *fragment{};
    SDL_GPUGraphicsPipeline *near{}, *far{}, *stencil{};
    SDL_GPUTextureFormat color_format{};
    int width{}, height{};
    ~Probe() {
        if (!device) {
            return;
        }
        SDL_WaitForGPUIdle(device);
        for (auto* p : {near, far, stencil}) {
            if (p) {
                SDL_ReleaseGPUGraphicsPipeline(device, p);
            }
        }
        if (depth) {
            SDL_ReleaseGPUTexture(device, depth);
        }
        if (white) {
            SDL_ReleaseGPUTexture(device, white);
        }
        if (sampler) {
            SDL_ReleaseGPUSampler(device, sampler);
        }
        if (vertex) {
            SDL_ReleaseGPUShader(device, vertex);
        }
        if (fragment) {
            SDL_ReleaseGPUShader(device, fragment);
        }
    }
    SDL_GPUShader* shader(bool vert) {
        using namespace probe_shaders;
        const bool spv = SDL_GetGPUShaderFormats(device) & SDL_GPU_SHADERFORMAT_SPIRV;
        SDL_GPUShaderCreateInfo s{};
        if (vert) {
            s.code = spv ? vert_spv : vert_msl;
            s.code_size = spv ? sizeof vert_spv : sizeof vert_msl;
        } else {
            s.code = spv ? frag_spv : frag_msl;
            s.code_size = spv ? sizeof frag_spv : sizeof frag_msl;
        }
        s.entrypoint = spv ? "main" : "main0";
        s.format = spv ? SDL_GPU_SHADERFORMAT_SPIRV : SDL_GPU_SHADERFORMAT_MSL;
        s.stage = vert ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
        s.num_uniform_buffers = 1;
        s.num_samplers = vert ? 0 : 1;
        return need(SDL_CreateGPUShader(device, &s));
    }
    SDL_GPUGraphicsPipeline* pipeline(int mode) {
        SDL_GPUGraphicsPipelineCreateInfo p{};
        p.vertex_shader = vertex;
        p.fragment_shader = fragment;
        p.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        p.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        p.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        p.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
        p.depth_stencil_state.enable_depth_test = mode != 2;
        p.depth_stencil_state.enable_depth_write = mode != 2;
        p.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
        p.depth_stencil_state.enable_stencil_test = mode != 1;
        p.depth_stencil_state.compare_mask = 255;
        p.depth_stencil_state.write_mask = mode == 0 ? 255 : 0;
        SDL_GPUStencilOpState s{};
        s.fail_op = s.depth_fail_op = SDL_GPU_STENCILOP_KEEP;
        s.pass_op = mode == 0 ? SDL_GPU_STENCILOP_REPLACE : SDL_GPU_STENCILOP_KEEP;
        s.compare_op = mode == 0 ? SDL_GPU_COMPAREOP_ALWAYS : SDL_GPU_COMPAREOP_EQUAL;
        p.depth_stencil_state.front_stencil_state = p.depth_stencil_state.back_stencil_state = s;
        SDL_GPUColorTargetDescription color{};
        color.format = color_format;
        p.target_info.color_target_descriptions = &color;
        p.target_info.num_color_targets = 1;
        p.target_info.has_depth_stencil_target = true;
        p.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;
        return need(SDL_CreateGPUGraphicsPipeline(device, &p));
    }
    void init(platform::GpuCommands& frame) {
        device = frame.device;
        color_format = frame.format;
        vertex = shader(true);
        fragment = shader(false);
        near = pipeline(0);
        far = pipeline(1);
        stencil = pipeline(2);
        SDL_GPUTextureCreateInfo t{};
        t.type = SDL_GPU_TEXTURETYPE_2D;
        t.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        t.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        t.width = t.height = t.layer_count_or_depth = t.num_levels = 1;
        white = need(SDL_CreateGPUTexture(device, &t));
        SDL_GPUTransferBufferCreateInfo b{};
        b.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        b.size = 4;
        auto* upload = need(SDL_CreateGPUTransferBuffer(device, &b));
        auto* bytes = need(static_cast<unsigned char*>(SDL_MapGPUTransferBuffer(device, upload, false)));
        std::memset(bytes, 255, 4);
        SDL_UnmapGPUTransferBuffer(device, upload);
        auto* copy = SDL_BeginGPUCopyPass(frame.command);
        SDL_GPUTextureTransferInfo from{};
        from.transfer_buffer = upload;
        SDL_GPUTextureRegion to{};
        to.texture = white;
        to.w = to.h = to.d = 1;
        SDL_UploadToGPUTexture(copy, &from, &to, false);
        SDL_EndGPUCopyPass(copy);
        SDL_ReleaseGPUTransferBuffer(device, upload);
        SDL_GPUSamplerCreateInfo si{};
        si.min_filter = si.mag_filter = SDL_GPU_FILTER_NEAREST;
        si.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        si.address_mode_u = si.address_mode_v = si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        sampler = need(SDL_CreateGPUSampler(device, &si));
    }
    void draw(const platform::GpuFrame& target, bool mask) {
        platform::GpuCommands frame(target);
        if (!device) {
            init(frame);
        }
        if (frame.device != device || frame.format != color_format) {
            throw std::runtime_error("Host changed GPU unexpectedly");
        }
        if (width != frame.width || height != frame.height) {
            if (depth) {
                SDL_ReleaseGPUTexture(device, depth);
            }
            width = frame.width;
            height = frame.height;
            SDL_GPUTextureCreateInfo t{};
            t.type = SDL_GPU_TEXTURETYPE_2D;
            t.format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;
            t.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
            t.width = width;
            t.height = height;
            t.layer_count_or_depth = t.num_levels = 1;
            depth = need(SDL_CreateGPUTexture(device, &t));
        }
        SDL_GPUColorTargetInfo color{};
        color.texture = frame.color;
        color.load_op = SDL_GPU_LOADOP_CLEAR;
        color.store_op = SDL_GPU_STOREOP_STORE;
        color.clear_color = {0, 0, 0, 1};
        SDL_GPUDepthStencilTargetInfo ds{};
        ds.texture = depth;
        ds.clear_depth = 1;
        ds.load_op = ds.stencil_load_op = SDL_GPU_LOADOP_CLEAR;
        ds.store_op = ds.stencil_store_op = SDL_GPU_STOREOP_STORE;
        auto* pass = need(SDL_BeginGPURenderPass(frame.command, &color, 1, &ds));
        const SDL_GPUTextureSamplerBinding texture{white, sampler};
        SDL_BindGPUFragmentSamplers(pass, 0, &texture, 1);
        SDL_SetGPUStencilReference(pass, 1);
        auto triangle = [&](SDL_GPUGraphicsPipeline* p, SDL_Rect box, float z, std::array<float, 4> tint) {
            SDL_BindGPUGraphicsPipeline(pass, p);
            SDL_SetGPUScissor(pass, &box);
            const std::array<float, 4> position{z, 0, 0, 0};
            SDL_PushGPUVertexUniformData(frame.command, 0, position.data(), sizeof position);
            SDL_PushGPUFragmentUniformData(frame.command, 0, tint.data(), sizeof tint);
            SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
        };
        triangle(near, {width / 4, height / 4, width / 2, height / 2}, .2f, {1, 0, 0, 1});
        triangle(far, {0, 0, width, height}, .8f, {0, 0, 1, 1});
        if (mask) {
            triangle(stencil, {0, 0, width, height}, .1f, {0, 1, 0, 1});
        }
        SDL_EndGPURenderPass(pass);
        frame.submit();
    }
};
void verify(const platform::Image& image, bool mask) {
    const auto center = image.pixels[std::size_t(image.height / 2) * image.width + image.width / 2];
    const auto corner = image.pixels[std::size_t(image.height / 8) * image.width + image.width / 8];
    const bool inner =
        mask ? center.g > 250 && center.r < 5 && center.b < 5 : center.r > 250 && center.g < 5 && center.b < 5;
    if (!inner || corner.b < 250 || corner.r > 5 || corner.g > 5) {
        throw std::runtime_error("Depth/stencil/texture/composition pixels differ from expected colours: center=" +
                                 std::to_string(center.r) + "," + std::to_string(center.g) + "," +
                                 std::to_string(center.b));
    }
}
}   // namespace
int main(int argc, char** argv) {
    try {
        if (argc > 1) {
            std::filesystem::create_directories(argv[1]);
        }
        platform::Host host("Reprise programmable GPU check");
        Probe probe;   // destroy GPU resources before host
        bool failed = false;
        try {
            host.capture(24, 24,
                         [](const platform::GpuFrame&) { throw std::runtime_error("intentional capture failure"); });
        } catch (const std::runtime_error&) {
            failed = true;
        }
        if (!failed) {
            throw std::runtime_error("Capture swallowed a draw failure");
        }
        for (const auto size : {std::array{320, 240}, std::array{517, 291}}) {
            for (bool mask : {false, true}) {
                auto draw = [&](const platform::GpuFrame& f) {
                    probe.draw(f, mask);
                };
                for (int i = 0; i < 8; ++i) {
                    host.poll();
                    host.present_game_gpu(size[0], size[1], 1, draw);
                }
                auto image = host.capture(size[0], size[1], draw);
                verify(image, mask);
                if (argc > 1) {
                    platform::save_png(image, std::filesystem::path(argv[1]) /
                                                  (std::to_string(size[0]) + (mask ? "-stencil.png" : "-depth.png")));
                }
            }
        }
        auto draw = [&](const platform::GpuFrame& f) {
            probe.draw(f, true);
        };
        auto effect = ui::vhs_effect(40, 2, 320, 240, 1);
        platform::Image overlay(platform::ui_width, platform::ui_height);
        for (auto& p : overlay.pixels) {
            p = {0, 0, 0, 0};
        }
        for (int y = 20; y < 50; ++y) {
            for (int x = 20; x < 180; ++x) {
                overlay.put(x, y, {255, 255, 0, 255});
            }
        }
        if (argc > 1) {
            host.capture_next_frame(std::filesystem::path(argv[1]) / "filtered-overlay.png");
        }
        host.present_game_gpu(320, 240, 1, draw, &overlay, false, nullptr, 1, 0, &effect);
        if (host.screen_effect_path().find("GPU shader") == std::string::npos) {
            throw std::runtime_error("Screen effect did not use the GPU");
        }
        std::printf("PASS: sampled colour, depth occlusion, stencil masking, repeated submissions, target resize, raw "
                    "capture; %s; %s\n",
                    host.renderer_name().c_str(), host.screen_effect_path().c_str());
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL: %s\n", e.what());
        return 1;
    }
}
