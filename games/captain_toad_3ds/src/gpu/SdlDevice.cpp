// Portable scene backend. All resources belong to Reprise's borrowed SDL GPU
// device; submissions never acquire a swapchain or present a second window.
#include "gpu/SdlDevice.hpp"
#include "gpu/VulkanShaders.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <spirv_msl.hpp>
#include <stdexcept>
#include <unordered_map>

namespace captain_toad::gpu {
namespace {
template <class T>
T* checked(T* p) {
    if (!p) {
        throw std::runtime_error(SDL_GetError());
    }
    return p;
}
SDL_GPUTextureFormat textureFormat(Format f) {
    if (f == Format::RGBA32F) {
        return SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
    }
    if (f == Format::Depth24) {
        return SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    }
    return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
}
SDL_GPUCompareOp compare(Compare c) {
    constexpr SDL_GPUCompareOp a[] = {SDL_GPU_COMPAREOP_NEVER,   SDL_GPU_COMPAREOP_ALWAYS,
                                      SDL_GPU_COMPAREOP_EQUAL,   SDL_GPU_COMPAREOP_NOT_EQUAL,
                                      SDL_GPU_COMPAREOP_LESS,    SDL_GPU_COMPAREOP_LESS_OR_EQUAL,
                                      SDL_GPU_COMPAREOP_GREATER, SDL_GPU_COMPAREOP_GREATER_OR_EQUAL};
    return a[int(c)];
}
SDL_GPUBlendFactor factor(BlendFactor f) {
    constexpr SDL_GPUBlendFactor a[] = {SDL_GPU_BLENDFACTOR_ZERO,
                                        SDL_GPU_BLENDFACTOR_ONE,
                                        SDL_GPU_BLENDFACTOR_SRC_COLOR,
                                        SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR,
                                        SDL_GPU_BLENDFACTOR_DST_COLOR,
                                        SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_COLOR,
                                        SDL_GPU_BLENDFACTOR_SRC_ALPHA,
                                        SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                                        SDL_GPU_BLENDFACTOR_DST_ALPHA,
                                        SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_ALPHA,
                                        SDL_GPU_BLENDFACTOR_CONSTANT_COLOR,
                                        SDL_GPU_BLENDFACTOR_ONE_MINUS_CONSTANT_COLOR,
                                        SDL_GPU_BLENDFACTOR_CONSTANT_COLOR,
                                        SDL_GPU_BLENDFACTOR_ONE_MINUS_CONSTANT_COLOR,
                                        SDL_GPU_BLENDFACTOR_SRC_ALPHA_SATURATE};
    return a[int(f)];
}
SDL_GPUStencilOp stencilOp(StencilOp op) {
    constexpr SDL_GPUStencilOp a[] = {SDL_GPU_STENCILOP_KEEP,
                                      SDL_GPU_STENCILOP_ZERO,
                                      SDL_GPU_STENCILOP_REPLACE,
                                      SDL_GPU_STENCILOP_INCREMENT_AND_CLAMP,
                                      SDL_GPU_STENCILOP_DECREMENT_AND_CLAMP,
                                      SDL_GPU_STENCILOP_INVERT,
                                      SDL_GPU_STENCILOP_INCREMENT_AND_WRAP,
                                      SDL_GPU_STENCILOP_DECREMENT_AND_WRAP};
    return a[int(op)];
}
const char* attributeNames[] = {"inPosition",  "inNormal",    "inColor",  "inTexCoord0",
                                "inTexCoord1", "inTexCoord2", "inJoints", "inWeights"};
}   // namespace
struct SdlDevice::Impl {
    SDL_GPUDevice* device;
    SDL_GPUCommandBuffer* commands{};
    SDL_GPURenderPass* pass{};
    SDL_GPUTexture* external{};
    SDL_GPUTexture* externalDepth{};
    SDL_GPUTexture* mainColor{};
    SDL_GPUTextureFormat externalFormat{};
    int width{}, height{};
    Target* bound{};
    Rect view{}, clip{};
    std::map<std::string, SDL_GPUGraphicsPipeline*> pipelines;
    std::vector<std::pair<Sampler, SDL_GPUSampler*>> samplers;
    std::unique_ptr<Texture> black;
    std::unique_ptr<Buffer> defaults;
    explicit Impl(SDL_GPUDevice* d) : device(d) {}
    void stop() {
        if (pass) {
            SDL_EndGPURenderPass(pass);
            pass = nullptr;
        }
    }
    SDL_GPUCommandBuffer* command() {
        if (!commands) {
            commands = checked(SDL_AcquireGPUCommandBuffer(device));
        }
        return commands;
    }
    void submit() {
        stop();
        if (commands) {
            auto* c = commands;
            commands = nullptr;
            if (!SDL_SubmitGPUCommandBuffer(c)) {
                throw std::runtime_error(SDL_GetError());
            }
        }
    }
    void upload(SDL_GPUBuffer* b, size_t offset, const void* data, size_t n) {
        if (!n) {
            return;
        }
        stop();
        SDL_GPUTransferBufferCreateInfo ti{SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, Uint32(n), 0};
        auto* t = checked(SDL_CreateGPUTransferBuffer(device, &ti));
        auto* dst = SDL_MapGPUTransferBuffer(device, t, false);
        if (!dst) {
            SDL_ReleaseGPUTransferBuffer(device, t);
            throw std::runtime_error(SDL_GetError());
        }
        memcpy(dst, data, n);
        SDL_UnmapGPUTransferBuffer(device, t);
        auto* cp = checked(SDL_BeginGPUCopyPass(command()));
        SDL_GPUTransferBufferLocation src{t, 0};
        SDL_GPUBufferRegion out{b, Uint32(offset), Uint32(n)};
        SDL_UploadToGPUBuffer(cp, &src, &out, false);
        SDL_EndGPUCopyPass(cp);
        SDL_ReleaseGPUTransferBuffer(device, t);
    }
    SDL_GPUSampler* sampler(const Sampler& s) {
        for (auto& [key, value] : samplers) {
            if (key == s) {
                return value;
            }
        }
        SDL_GPUSamplerCreateInfo ci{};
        ci.min_filter = s.minFilter == Filter::Linear ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
        ci.mag_filter = s.magFilter == Filter::Linear ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
        ci.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
        auto wrap = [](Wrap w) {
            return w == Wrap::Repeat           ? SDL_GPU_SAMPLERADDRESSMODE_REPEAT
                   : w == Wrap::MirroredRepeat ? SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT
                                               : SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        };
        ci.address_mode_u = wrap(s.wrapS);
        ci.address_mode_v = wrap(s.wrapT);
        ci.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        ci.max_lod = s.mipmaps ? 1000 : 0;
        auto* out = checked(SDL_CreateGPUSampler(device, &ci));
        samplers.emplace_back(s, out);
        return out;
    }
    void start(const Clear* clear = nullptr);
};
namespace {
class SdlTexture final : public Texture {
public:
    SdlDevice::Impl& host;
    SDL_GPUTexture* handle{};
    SDL_GPUTextureFormat native;
    bool attachment;
    SdlTexture(SdlDevice::Impl& h, Format f, int w, int z, const void* pixels = nullptr, bool target = false,
               SDL_GPUTextureFormat overrideFormat = SDL_GPU_TEXTUREFORMAT_INVALID)
        : host(h), native(overrideFormat == SDL_GPU_TEXTUREFORMAT_INVALID ? textureFormat(f) : overrideFormat),
          attachment(target) {
        format_ = f;
        resize(w, z);
        if (pixels) {
            upload(pixels);
        }
    }
    ~SdlTexture() override {
        if (handle) {
            SDL_ReleaseGPUTexture(host.device, handle);
        }
    }
    void resize(int w, int h) override {
        if (w <= 0 || h <= 0 || w > 16384 || h > 16384) {
            throw std::runtime_error("Invalid scene texture size");
        }
        host.stop();
        if (handle) {
            SDL_ReleaseGPUTexture(host.device, handle);
        }
        handle = nullptr;
        width_ = w;
        height_ = h;
        SDL_GPUTextureCreateInfo ci{};
        ci.type = SDL_GPU_TEXTURETYPE_2D;
        ci.format = native;
        ci.width = w;
        ci.height = h;
        ci.layer_count_or_depth = 1;
        ci.num_levels = format_ == Format::RGBA8 && !attachment ? 1 + int(std::floor(std::log2(std::max(w, h)))) : 1;
        ci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        if (format_ == Format::Depth24) {
            ci.usage |= SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
        } else if (attachment || format_ == Format::RGBA8) {
            ci.usage |= SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
        }
        handle = checked(SDL_CreateGPUTexture(host.device, &ci));
    }
    void upload(const void* pixels) override {
        if (format_ == Format::Depth24) {
            throw std::runtime_error("Depth texture upload is not supported");
        }
        host.stop();
        const Uint32 size = width_ * height_ * (format_ == Format::RGBA32F ? 16 : 4);
        SDL_GPUTransferBufferCreateInfo ti{SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, size, 0};
        auto* t = checked(SDL_CreateGPUTransferBuffer(host.device, &ti));
        auto* dst = SDL_MapGPUTransferBuffer(host.device, t, false);
        if (!dst) {
            SDL_ReleaseGPUTransferBuffer(host.device, t);
            throw std::runtime_error(SDL_GetError());
        }
        memcpy(dst, pixels, size);
        SDL_UnmapGPUTransferBuffer(host.device, t);
        auto* cp = checked(SDL_BeginGPUCopyPass(host.command()));
        SDL_GPUTextureTransferInfo src{};
        src.transfer_buffer = t;
        SDL_GPUTextureRegion out{};
        out.texture = handle;
        out.w = width_;
        out.h = height_;
        out.d = 1;
        SDL_UploadToGPUTexture(cp, &src, &out, false);
        SDL_EndGPUCopyPass(cp);
        SDL_ReleaseGPUTransferBuffer(host.device, t);
    }
};
class SdlBuffer final : public Buffer {
public:
    SdlDevice::Impl& host;
    SDL_GPUBuffer* handle{};
    SdlBuffer(SdlDevice::Impl& h, size_t n, const void* data) : host(h) {
        if (!n || n > UINT32_MAX) {
            throw std::runtime_error("Invalid scene buffer size");
        }
        size_ = n;
        SDL_GPUBufferCreateInfo ci{SDL_GPU_BUFFERUSAGE_VERTEX, Uint32(n), 0};
        handle = checked(SDL_CreateGPUBuffer(host.device, &ci));
        if (data) {
            update(0, data, n);
        }
    }
    ~SdlBuffer() override { SDL_ReleaseGPUBuffer(host.device, handle); }
    void update(size_t offset, const void* data, size_t n) override {
        if (offset > size_ || n > size_ - offset) {
            throw std::runtime_error("Scene buffer update outside allocation");
        }
        host.upload(handle, offset, data, n);
    }
};
class SdlTarget final : public Target {
public:
    std::unique_ptr<SdlTexture> rgba, z;
    SdlTarget(SdlDevice::Impl& host, int w, int h, bool depthOnly) {
        width_ = w;
        height_ = h;
        if (!depthOnly) {
            rgba = std::make_unique<SdlTexture>(host, Format::RGBA8, w, h, nullptr, true);
        }
        z = std::make_unique<SdlTexture>(host, Format::Depth24, w, h, nullptr, true,
                                         depthOnly ? SDL_GPU_TEXTUREFORMAT_D32_FLOAT
                                                   : SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT);
    }
    Texture* color() override { return rgba.get(); }
    Texture* depth() override { return rgba ? nullptr : z.get(); }
};
class SdlProgram final : public Program {
public:
    SdlDevice::Impl& host;
    vulkan::CompiledProgram reflection;
    std::vector<uint8_t> block;
    inline static uint64_t nextId = 0;
    uint64_t id = ++nextId;
    SDL_GPUShader* vertex{};
    SDL_GPUShader* fragment{};
    std::vector<std::string> textures[2];
    SdlProgram(SdlDevice::Impl& h, std::string_view vs, std::string_view fs, std::string_view name)
        : host(h), reflection(vulkan::compileProgram(vs, fs, name)) {
        name_ = name;
        block.resize(reflection.blockSize);
        try {
            vertex = shader(reflection.vertex, 0);
            fragment = shader(reflection.fragment, 1);
        } catch (...) {
            if (vertex) {
                SDL_ReleaseGPUShader(host.device, vertex);
            }
            throw;
        }
    }
    ~SdlProgram() override {
        SDL_ReleaseGPUShader(host.device, vertex);
        SDL_ReleaseGPUShader(host.device, fragment);
    }
    SDL_GPUShader* shader(std::vector<uint32_t> words, int stage) {
        spirv_cross::CompilerMSL compiler(words);
        auto resources = compiler.get_shader_resources();
        std::unordered_map<uint32_t, std::pair<uint32_t, uint32_t>> remap;
        auto sorted = resources.sampled_images;
        std::sort(sorted.begin(), sorted.end(), [&](auto& a, auto& b) {
            return compiler.get_decoration(a.id, spv::DecorationBinding) <
                   compiler.get_decoration(b.id, spv::DecorationBinding);
        });
        for (auto& r : sorted) {
            unsigned binding = textures[stage].size();
            textures[stage].push_back(r.name);
            remap[r.id] = {stage ? 2u : 0u, binding};
        }
        for (auto& r : resources.uniform_buffers) {
            remap[r.id] = {stage ? 3u : 1u, 0u};
        }
        for (size_t i = 5; i < words.size();) {
            unsigned count = words[i] >> 16, op = words[i] & 65535;
            if (!count || i + count > words.size()) {
                throw std::runtime_error("Malformed generated SPIR-V");
            }
            if (op == spv::OpDecorate && count >= 4) {
                if (auto it = remap.find(words[i + 1]); it != remap.end()) {
                    if (words[i + 2] == spv::DecorationDescriptorSet) {
                        words[i + 3] = it->second.first;
                    }
                    if (words[i + 2] == spv::DecorationBinding) {
                        words[i + 3] = it->second.second;
                    }
                }
            }
            i += count;
        }
        SDL_GPUShaderCreateInfo ci{};
        ci.stage = stage ? SDL_GPU_SHADERSTAGE_FRAGMENT : SDL_GPU_SHADERSTAGE_VERTEX;
        ci.num_samplers = textures[stage].size();
        ci.num_uniform_buffers = resources.uniform_buffers.size();
        std::string metal;
        if (SDL_GetGPUShaderFormats(host.device) & SDL_GPU_SHADERFORMAT_SPIRV) {
            ci.format = SDL_GPU_SHADERFORMAT_SPIRV;
            ci.code = reinterpret_cast<const Uint8*>(words.data());
            ci.code_size = words.size() * 4;
            ci.entrypoint = "main";
        } else {
            spirv_cross::CompilerMSL msl(words);
            auto options = msl.get_msl_options();
            options.platform = spirv_cross::CompilerMSL::Options::macOS;
            options.msl_version = 20100;
            msl.set_msl_options(options);
            for (auto& [id, binding] : remap) {
                spirv_cross::MSLResourceBinding b{};
                b.stage = stage ? spv::ExecutionModelFragment : spv::ExecutionModelVertex;
                b.desc_set = binding.first;
                b.binding = binding.second;
                b.msl_buffer = 0;
                b.msl_texture = binding.second;
                b.msl_sampler = binding.second;
                msl.add_msl_resource_binding(b);
            }
            metal = msl.compile();
            ci.format = SDL_GPU_SHADERFORMAT_MSL;
            ci.code = reinterpret_cast<const Uint8*>(metal.data());
            ci.code_size = metal.size();
            ci.entrypoint = "main0";
        }
        return checked(SDL_CreateGPUShader(host.device, &ci));
    }
    bool has(std::string_view n) const override { return reflection.uniforms.count(n) || reflection.samplers.count(n); }
    void setFloats(std::string_view n, const float* v, int count) override { write(n, v, count); }
    void setInts(std::string_view n, const int* v, int count) override { write(n, v, count); }
    void write(std::string_view name, const void* values, int count) {
        auto it = reflection.uniforms.find(name);
        if (it == reflection.uniforms.end()) {
            return;
        }
        const auto& u = it->second;
        int rows = 1, columns = 1;
        switch (u.type) {
        case 0x8B50:
        case 0x8B53:
            rows = 2;
            break;
        case 0x8B51:
        case 0x8B54:
            rows = 3;
            break;
        case 0x8B52:
        case 0x8B55:
            rows = 4;
            break;
        case 0x8B5A:
            columns = rows = 2;
            break;
        case 0x8B5B:
            columns = rows = 3;
            break;
        case 0x8B5C:
            columns = rows = 4;
            break;
        }
        int elements = std::min(u.arraySize, count / (rows * columns));
        int stride = u.arrayStride > 0 ? u.arrayStride : columns * 16;
        for (int e = 0; e < elements; ++e) {
            for (int c = 0; c < columns; ++c) {
                size_t at = u.offset + e * stride + (columns > 1 ? c * 16 : 0);
                if (at + rows * 4 > block.size()) {
                    throw std::runtime_error("Invalid uniform layout");
                }
                memcpy(block.data() + at, static_cast<const uint8_t*>(values) + (e * rows * columns + c * rows) * 4,
                       rows * 4);
            }
        }
    }
};
}   // namespace
void SdlDevice::Impl::start(const Clear* clear) {
    if (pass) {
        return;
    }
    auto* t = static_cast<SdlTarget*>(bound);
    SDL_GPUColorTargetInfo color{};
    SDL_GPUDepthStencilTargetInfo depth{};
    color.texture = t ? (t->rgba ? t->rgba->handle : nullptr) : mainColor;
    depth.texture = t ? t->z->handle : externalDepth;
    if (!depth.texture) {
        throw std::runtime_error("Scene draw outside host frame");
    }
    color.load_op = clear && clear->color ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
    color.store_op = SDL_GPU_STOREOP_STORE;
    if (clear) {
        color.clear_color = {clear->colorValue[0], clear->colorValue[1], clear->colorValue[2], clear->colorValue[3]};
    }
    depth.load_op = clear && clear->depth ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
    depth.store_op = SDL_GPU_STOREOP_STORE;
    const bool hasStencil = !t || bool(t->rgba);
    depth.stencil_load_op =
        hasStencil ? (clear && clear->stencil ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD) : SDL_GPU_LOADOP_DONT_CARE;
    depth.stencil_store_op = hasStencil ? SDL_GPU_STOREOP_STORE : SDL_GPU_STOREOP_DONT_CARE;
    if (clear) {
        depth.clear_depth = clear->depthValue;
        depth.clear_stencil = clear->stencilValue;
    }
    pass = checked(SDL_BeginGPURenderPass(command(), color.texture ? &color : nullptr, color.texture ? 1 : 0, &depth));
}
SdlDevice::SdlDevice(SDL_GPUDevice* d) : impl_(std::make_unique<Impl>(d)) {
    if (!d) {
        throw std::runtime_error("No SDL GPU device");
    }
    const uint8_t black[4] = {0, 0, 0, 255};
    impl_->black = createTexture(Format::RGBA8, 1, 1, black);
    const float defaults[4] = {0, 0, 0, 1};
    impl_->defaults = createBuffer(sizeof defaults, defaults);
}
SdlDevice::~SdlDevice() {
    auto& h = *impl_;
    h.stop();
    if (h.commands) {
        SDL_CancelGPUCommandBuffer(h.commands);
        h.commands = nullptr;
    }
    SDL_WaitForGPUIdle(h.device);
    shared_.clear();
    if (h.mainColor) {
        SDL_ReleaseGPUTexture(h.device, h.mainColor);
    }
    h.black.reset();
    h.defaults.reset();
    for (auto& [k, p] : h.pipelines) {
        SDL_ReleaseGPUGraphicsPipeline(h.device, p);
    }
    for (auto& [k, s] : h.samplers) {
        SDL_ReleaseGPUSampler(h.device, s);
    }
    if (h.externalDepth) {
        SDL_ReleaseGPUTexture(h.device, h.externalDepth);
    }
}
void SdlDevice::begin(SDL_GPUTexture* color, SDL_GPUTextureFormat format, int w, int z) {
    auto& h = *impl_;
    h.stop();
    if (w != h.width || z != h.height) {
        if (h.externalDepth) {
            SDL_ReleaseGPUTexture(h.device, h.externalDepth);
        }
        SDL_GPUTextureCreateInfo ci{};
        ci.type = SDL_GPU_TEXTURETYPE_2D;
        ci.format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;
        ci.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
        ci.width = w;
        ci.height = z;
        ci.layer_count_or_depth = ci.num_levels = 1;
        h.externalDepth = checked(SDL_CreateGPUTexture(h.device, &ci));
    }
    if (!h.mainColor || w != h.width || z != h.height) {
        if (h.mainColor) {
            SDL_ReleaseGPUTexture(h.device, h.mainColor);
        }
        SDL_GPUTextureCreateInfo ci{};
        ci.type = SDL_GPU_TEXTURETYPE_2D;
        ci.format = format;
        ci.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        ci.width = w;
        ci.height = z;
        ci.layer_count_or_depth = ci.num_levels = 1;
        h.mainColor = checked(SDL_CreateGPUTexture(h.device, &ci));
    }
    h.external = color;
    h.externalFormat = format;
    h.width = w;
    h.height = z;
    h.bound = nullptr;
    h.view = {0, 0, w, z};
    h.clip = {};
}
void SdlDevice::end() {
    auto& h = *impl_;
    // The standalone window is opaque. Material alpha remains meaningful inside scene
    // passes, but must not turn a finished 3D frame transparent in SDL's compositor.
    // Seal alpha on the GPU, preserving RGB and avoiding per-frame readback.
    bindTarget(nullptr);
    h.view = {0, 0, h.width, h.height};
    h.clip = {};
    auto& opaque =
        sharedProgram("opaque host frame", "in vec2 inPosition;void main(){gl_Position=vec4(inPosition,0,1);}",
                      "void main(){fragColor=vec4(0,0,0,1);}");
    Draw seal;
    seal.program = &opaque;
    seal.state.raster.colorWrite = {false, false, false, true};
    Immediate vertices;
    vertices.floatsPerVertex = 2;
    vertices.attributes = {{Attribute::Position, 2, 0}};
    vertices.data = {-1, -1, 3, -1, -1, 3};
    draw(seal, vertices);
    h.stop();
    SDL_GPUBlitInfo blit{};
    blit.source.texture = h.mainColor;
    blit.source.w = h.width;
    blit.source.h = h.height;
    blit.destination.texture = h.external;
    blit.destination.w = h.width;
    blit.destination.h = h.height;
    blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
    blit.filter = SDL_GPU_FILTER_NEAREST;
    blit.flip_mode = SDL_FLIP_VERTICAL;
    SDL_BlitGPUTexture(h.command(), &blit);
    impl_->submit();
    impl_->external = nullptr;
}
std::unique_ptr<Texture> SdlDevice::createTexture(Format f, int w, int h, const void* p) {
    return std::make_unique<SdlTexture>(*impl_, f, w, h, p);
}
std::unique_ptr<Buffer> SdlDevice::createBuffer(size_t n, const void* p) {
    return std::make_unique<SdlBuffer>(*impl_, n, p);
}
std::unique_ptr<Program> SdlDevice::createProgram(std::string_view v, std::string_view f, std::string_view n) {
    return std::make_unique<SdlProgram>(*impl_, v, f, n);
}
std::unique_ptr<Target> SdlDevice::createTarget(int w, int h, bool d) {
    return std::make_unique<SdlTarget>(*impl_, w, h, d);
}
void SdlDevice::bindTarget(Target* t) {
    impl_->stop();
    impl_->bound = t;
}
Target* SdlDevice::target() const {
    return impl_->bound;
}
void SdlDevice::setViewport(const Rect& r) {
    impl_->view = r;
}
Rect SdlDevice::viewport() const {
    return impl_->view;
}
void SdlDevice::setScissor(const Rect& r) {
    impl_->clip = r;
}
Rect SdlDevice::scissor() const {
    return impl_->clip;
}
void SdlDevice::clear(const Clear& c, const Rect& r) {
    auto& h = *impl_;
    const int w = h.bound ? h.bound->width() : h.width, z = h.bound ? h.bound->height() : h.height;
    if (r.w && (r.x != 0 || r.y != 0 || r.w != w || r.h != z)) {
        auto savedView = h.view, savedClip = h.clip;
        auto& program = sharedProgram("device rectangle clear",
                                      "in vec2 inPosition;uniform float clearDepth;void "
                                      "main(){gl_Position=vec4(inPosition,clearDepth*2.0-1.0,1);}",
                                      "uniform vec4 clearColor;void main(){fragColor=clearColor;}");
        program.set("clearDepth", c.depthValue);
        program.setFloats("clearColor", c.colorValue.data(), 4);
        Draw d;
        d.program = &program;
        d.state.raster.colorWrite.fill(c.color);
        d.state.depth = {c.depth, c.depth, Compare::Always};
        if (c.stencil) {
            d.state.stencil.enabled = true;
            d.state.stencil.reference = c.stencilValue;
            d.state.stencil.pass = StencilOp::Replace;
        }
        Immediate v;
        v.floatsPerVertex = 2;
        v.attributes = {{Attribute::Position, 2, 0}};
        v.data = {-1, -1, 3, -1, -1, 3};
        h.view = {0, 0, w, z};
        h.clip = r;
        try {
            draw(d, v);
        } catch (...) {
            h.view = savedView;
            h.clip = savedClip;
            throw;
        }
        h.view = savedView;
        h.clip = savedClip;
        return;
    }
    h.stop();
    h.start(&c);
}
void SdlDevice::draw(const Draw& d, const Immediate& v) {
    if (!v.vertexCount()) {
        return;
    }
    auto buffer = createBuffer(v.data.size() * 4, v.data.data());
    Draw copy = d;
    copy.streams = {{buffer.get(), 0, v.floatsPerVertex * 4, v.attributes}};
    copy.first = 0;
    copy.count = v.vertexCount();
    draw(copy);
}
void SdlDevice::draw(const Draw& d) {
    if (d.count <= 0) {
        return;
    }
    auto& h = *impl_;
    auto& p = *static_cast<SdlProgram*>(d.program);
    auto* t = static_cast<SdlTarget*>(h.bound);
    std::vector<SDL_GPUVertexBufferDescription> buffers;
    std::vector<SDL_GPUVertexAttribute> attrs;
    std::vector<SDL_GPUBufferBinding> bindings;
    for (auto& stream : d.streams) {
        Uint32 slot = buffers.size();
        buffers.push_back({slot, Uint32(stream.stride), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0});
        bindings.push_back({static_cast<const SdlBuffer*>(stream.buffer)->handle, Uint32(stream.offset)});
        for (auto& a : stream.attributes) {
            if (auto it = p.reflection.inputs.find(attributeNames[int(a.attribute)]); it != p.reflection.inputs.end()) {
                constexpr SDL_GPUVertexElementFormat formats[] = {
                    SDL_GPU_VERTEXELEMENTFORMAT_FLOAT, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
                    SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4};
                attrs.push_back({Uint32(it->second), slot, formats[a.components - 1], Uint32(a.offset)});
            }
        }
    }
    for (auto& [name, location] : p.reflection.inputs) {
        if (std::none_of(attrs.begin(), attrs.end(), [&](auto& a) { return a.location == Uint32(location); })) {
            Uint32 slot = buffers.size();
            buffers.push_back({slot, 16, SDL_GPU_VERTEXINPUTRATE_INSTANCE, 0});
            bindings.push_back({static_cast<SdlBuffer*>(h.defaults.get())->handle, 0});
            attrs.push_back({Uint32(location), slot, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 0});
        }
    }
    SDL_GPUGraphicsPipelineCreateInfo ci{};
    ci.vertex_shader = p.vertex;
    ci.fragment_shader = p.fragment;
    ci.vertex_input_state = {buffers.data(), Uint32(buffers.size()), attrs.data(), Uint32(attrs.size())};
    ci.primitive_type =
        d.topology == Topology::Triangles ? SDL_GPU_PRIMITIVETYPE_TRIANGLELIST : SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP;
    ci.rasterizer_state.fill_mode = d.state.raster.wireframe ? SDL_GPU_FILLMODE_LINE : SDL_GPU_FILLMODE_FILL;
    ci.rasterizer_state.cull_mode = d.state.raster.cull == Cull::None   ? SDL_GPU_CULLMODE_NONE
                                    : d.state.raster.cull == Cull::Back ? SDL_GPU_CULLMODE_BACK
                                                                        : SDL_GPU_CULLMODE_FRONT;
    ci.rasterizer_state.front_face = SDL_GPU_FRONTFACE_CLOCKWISE;
    ci.rasterizer_state.enable_depth_clip = true;
    ci.rasterizer_state.enable_depth_bias = d.state.raster.depthBias;
    ci.rasterizer_state.depth_bias_constant_factor = d.state.raster.biasUnits;
    ci.rasterizer_state.depth_bias_slope_factor = d.state.raster.biasFactor;
    ci.depth_stencil_state.compare_op = compare(d.state.depth.func);
    ci.depth_stencil_state.enable_depth_test = d.state.depth.test;
    ci.depth_stencil_state.enable_depth_write = d.state.depth.write;
    auto& s = d.state.stencil;
    ci.depth_stencil_state.enable_stencil_test = s.enabled;
    ci.depth_stencil_state.compare_mask = s.readMask;
    ci.depth_stencil_state.write_mask = s.writeMask;
    SDL_GPUStencilOpState stencil{stencilOp(s.fail), stencilOp(s.pass), stencilOp(s.depthFail), compare(s.func)};
    ci.depth_stencil_state.front_stencil_state = ci.depth_stencil_state.back_stencil_state = stencil;
    SDL_GPUColorTargetDescription color{};
    color.format = t ? SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM : h.externalFormat;
    auto& blend = color.blend_state;
    const auto& b = d.state.blend;
    blend.enable_blend = b.enabled;
    blend.src_color_blendfactor = factor(b.srcColor);
    blend.dst_color_blendfactor = factor(b.dstColor);
    blend.src_alpha_blendfactor = factor(b.srcAlpha);
    blend.dst_alpha_blendfactor = factor(b.dstAlpha);
    constexpr SDL_GPUBlendOp ops[] = {SDL_GPU_BLENDOP_ADD, SDL_GPU_BLENDOP_SUBTRACT, SDL_GPU_BLENDOP_REVERSE_SUBTRACT,
                                      SDL_GPU_BLENDOP_MIN, SDL_GPU_BLENDOP_MAX};
    blend.color_blend_op = ops[int(b.colorOp)];
    blend.alpha_blend_op = ops[int(b.alphaOp)];
    blend.enable_color_write_mask = true;
    for (int i = 0; i < 4; ++i) {
        if (d.state.raster.colorWrite[i]) {
            blend.color_write_mask |= 1 << i;
        }
    }
    ci.target_info.has_depth_stencil_target = true;
    ci.target_info.depth_stencil_format = t ? t->z->native : SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;
    ci.target_info.num_color_targets = !t || t->rgba ? 1 : 0;
    ci.target_info.color_target_descriptions = &color;
    // Pointer fields are replaced by their contents in the key; all structs are
    // value-initialized.
    auto keyInfo = ci;
    keyInfo.vertex_input_state = {};
    keyInfo.target_info.color_target_descriptions = nullptr;
    std::string key(reinterpret_cast<const char*>(&keyInfo), sizeof keyInfo);
    key.append(reinterpret_cast<const char*>(&p.id), sizeof p.id);
    key.append(reinterpret_cast<const char*>(&color), sizeof color);
    for (auto& v : buffers) {
        key.append(reinterpret_cast<const char*>(&v), sizeof v);
    }
    for (auto& a : attrs) {
        key.append(reinterpret_cast<const char*>(&a), sizeof a);
    }
    auto found = h.pipelines.find(key);
    SDL_GPUGraphicsPipeline* pipeline;
    if (found == h.pipelines.end()) {
        pipeline = checked(SDL_CreateGPUGraphicsPipeline(h.device, &ci));
        h.pipelines.emplace(std::move(key), pipeline);
    } else {
        pipeline = found->second;
    }
    std::vector<SDL_GPUTextureSamplerBinding> sampled[2];
    for (int stage = 0; stage < 2; ++stage) {
        for (auto& name : p.textures[stage]) {
            int unit = 0;
            for (auto& [n, u] : d.samplers) {
                if (n == name) {
                    unit = u;
                }
            }
            if (unit < 0 || unit >= kTextureUnits) {
                throw std::runtime_error("Invalid scene sampler unit");
            }
            const auto& binding = d.textures[unit];
            const auto& sampler = binding.sampler;
            float axes[] = {sampler.wrapS == Wrap::ClampToBorder ? 1.f : 0.f,
                            sampler.wrapT == Wrap::ClampToBorder ? 1.f : 0.f};
            p.setFloats("deviceBorderAxes_" + name, axes, 2);
            p.setFloats("deviceBorder_" + name, sampler.border.data(), 4);
            sampled[stage].push_back(
                {static_cast<const SdlTexture*>(binding.texture ? binding.texture : h.black.get())->handle,
                 h.sampler(sampler)});
        }
    }
    h.start();
    SDL_BindGPUGraphicsPipeline(h.pass, pipeline);
    // Preserve the source backend's bottom-up render textures: render with
    // inverted clip Y (the final host copy flips once). Texture coordinates and
    // framebuffer copies stay unchanged.
    SDL_GPUViewport viewport{float(h.view.x), float(h.view.y), float(h.view.w), float(h.view.h), 0, 1};
    SDL_SetGPUViewport(h.pass, &viewport);
    Rect clip = h.clip.w ? h.clip : Rect{0, 0, t ? t->width() : h.width, t ? t->height() : h.height};
    SDL_Rect scissor{clip.x, clip.y, clip.w, clip.h};
    SDL_SetGPUScissor(h.pass, &scissor);
    SDL_SetGPUStencilReference(h.pass, s.reference);
    SDL_FColor constant{b.constant[0], b.constant[1], b.constant[2], b.constant[3]};
    const auto alphaFactor = [](BlendFactor f) {
        return f == BlendFactor::ConstantAlpha || f == BlendFactor::OneMinusConstantAlpha;
    };
    const auto colorFactor = [](BlendFactor f) {
        return f == BlendFactor::ConstantColor || f == BlendFactor::OneMinusConstantColor;
    };
    if (alphaFactor(b.srcColor) || alphaFactor(b.dstColor)) {
        if (colorFactor(b.srcColor) || colorFactor(b.dstColor)) {
            throw std::runtime_error("Mixed constant RGB and alpha blend factors are unsupported by SDL GPU");
        }
        constant.r = constant.g = constant.b = constant.a;
    }
    SDL_SetGPUBlendConstants(h.pass, constant);
    SDL_BindGPUVertexBuffers(h.pass, 0, bindings.data(), bindings.size());
    if (!sampled[0].empty()) {
        SDL_BindGPUVertexSamplers(h.pass, 0, sampled[0].data(), sampled[0].size());
    }
    if (!sampled[1].empty()) {
        SDL_BindGPUFragmentSamplers(h.pass, 0, sampled[1].data(), sampled[1].size());
    }
    if (!p.block.empty()) {
        SDL_PushGPUVertexUniformData(h.command(), 0, p.block.data(), p.block.size());
        SDL_PushGPUFragmentUniformData(h.command(), 0, p.block.data(), p.block.size());
    }
    SDL_DrawGPUPrimitives(h.pass, d.count, 1, d.first, 0);
}
void SdlDevice::copyToTexture(Texture& out, const Rect& r) {
    auto& h = *impl_;
    h.stop();
    auto* t = static_cast<SdlTarget*>(h.bound);
    auto& dst = static_cast<SdlTexture&>(out);
    SDL_GPUTextureLocation src{};
    src.texture = t ? (t->rgba ? t->rgba->handle : t->z->handle) : h.mainColor;
    src.x = r.x;
    src.y = r.y;
    SDL_GPUTextureLocation dest{};
    dest.texture = dst.handle;
    auto* cp = checked(SDL_BeginGPUCopyPass(h.command()));
    SDL_CopyGPUTextureToTexture(cp, &src, &dest, r.w, r.h, 1, false);
    SDL_EndGPUCopyPass(cp);
}
void SdlDevice::generateMipmaps(Texture& t) {
    impl_->stop();
    SDL_GenerateMipmapsForGPUTexture(impl_->command(), static_cast<SdlTexture&>(t).handle);
}
bool SdlDevice::copyDepth(Target& a, Target& b) {
    auto& x = static_cast<SdlTarget&>(a);
    auto& y = static_cast<SdlTarget&>(b);
    if (x.rgba || y.rgba || a.width() != b.width() || a.height() != b.height()) {
        return false;
    }
    impl_->stop();
    SDL_GPUTextureLocation src{}, dst{};
    src.texture = x.z->handle;
    dst.texture = y.z->handle;
    auto* cp = checked(SDL_BeginGPUCopyPass(impl_->command()));
    SDL_CopyGPUTextureToTexture(cp, &src, &dst, a.width(), a.height(), 1, false);
    SDL_EndGPUCopyPass(cp);
    return true;
}
captain_toad::platform::Image SdlDevice::readPixels(const Rect& r) {
    auto& h = *impl_;
    h.stop();
    SDL_GPUTransferBufferCreateInfo ci{SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, Uint32(r.w * r.h * 4), 0};
    auto* transfer = checked(SDL_CreateGPUTransferBuffer(h.device, &ci));
    SDL_GPUTextureRegion src{};
    auto* t = static_cast<SdlTarget*>(h.bound);
    src.texture = t ? t->rgba->handle : h.mainColor;
    src.x = r.x;
    src.y = r.y;
    src.w = r.w;
    src.h = r.h;
    src.d = 1;
    SDL_GPUTextureTransferInfo dst{};
    dst.transfer_buffer = transfer;
    auto* cp = checked(SDL_BeginGPUCopyPass(h.command()));
    SDL_DownloadFromGPUTexture(cp, &src, &dst);
    SDL_EndGPUCopyPass(cp);
    h.submit();
    SDL_WaitForGPUIdle(h.device);
    auto* data = checked(static_cast<uint8_t*>(SDL_MapGPUTransferBuffer(h.device, transfer, false)));
    captain_toad::platform::Image image(r.w, r.h);
    for (int y = 0; y < r.h; ++y) {
        memcpy(image.pixels.data() + y * r.w, data + (r.h - 1 - y) * r.w * 4, r.w * 4);
    }
    SDL_UnmapGPUTransferBuffer(h.device, transfer);
    SDL_ReleaseGPUTransferBuffer(h.device, transfer);
    return image;
}
void SdlDevice::checkErrors(const char*) {}
void SdlDevice::present() {
    end();
}
}   // namespace captain_toad::gpu
