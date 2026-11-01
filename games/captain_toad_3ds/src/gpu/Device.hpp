// =============================================================================================
// Device.hpp - the graphics device: what the renderers ask of the GPU, independent of the API
// =============================================================================================
//
// Every renderer (the scene, shadows, effects, layouts, the shell) draws through this interface
// instead of calling a graphics API. A draw names everything it depends on: the program, the
// complete render state, the vertex streams and the textures with their sampler state. Nothing
// carries over from one draw to the next except a program's uniform values, which stay set until
// they are changed (as OpenGL keeps them per program; a Vulkan backend keeps the same copy and
// uploads it with each draw).
//
// The enumerations of blend factors, blend equations, compare functions and texture wraps follow
// the console GPU's register encoding (PICA200), so the ROM's material registers index them
// directly.
//
// Shaders are written in one dialect (see the "Shader dialect" note below); each backend adds its
// version line and the declarations its API needs.
//
// The current device is process-wide, like a graphics context: the platform layer creates it
// with the window (captain_toad::gpu::setDevice) and the renderers reach it through captain_toad::gpu::device().
// =============================================================================================
#pragma once

#include "platform/GameImage.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace captain_toad::gpu {

// ---- textures and samplers ----------------------------------------------------------------------

enum class Format {
    RGBA8,     // 8-bit colour
    RGBA32F,   // 32-bit float colour (lookup tables)
    Depth24,   // depth only, sampled as a texture (shadow maps)
};

// PICA200 wrap encoding: 0 clamp to edge, 1 clamp to border, 2 repeat, 3 mirrored repeat.
enum class Wrap { ClampToEdge, ClampToBorder, Repeat, MirroredRepeat };
enum class Filter { Nearest, Linear };

struct Sampler {
    Wrap wrapS = Wrap::ClampToEdge;
    Wrap wrapT = Wrap::ClampToEdge;
    Filter minFilter = Filter::Linear;
    Filter magFilter = Filter::Linear;
    bool mipmaps = false;   // minify between mipmap levels (linear), see Device::generateMipmaps
    std::array<float, 4> border{0, 0, 0, 0};
    bool operator==(const Sampler&) const = default;
};

class Texture {
public:
    virtual ~Texture() = default;
    int width() const { return width_; }
    int height() const { return height_; }
    Format format() const { return format_; }
    // Replaces the contents (rows bottom-up as in OpenGL; RGBA8: 4 bytes per texel, RGBA32F: 16).
    virtual void upload(const void* pixels) = 0;
    // Reallocates at a new size (contents undefined until written).
    virtual void resize(int width, int height) = 0;

protected:
    int width_ = 0, height_ = 0;
    Format format_ = Format::RGBA8;
};

// ---- render state ---------------------------------------------------------------------------------

// PICA200 encodings (register 0x101 / 0x107 fields).
enum class Compare { Never, Always, Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual };
enum class BlendFactor {
    Zero,
    One,
    SrcColor,
    OneMinusSrcColor,
    DstColor,
    OneMinusDstColor,
    SrcAlpha,
    OneMinusSrcAlpha,
    DstAlpha,
    OneMinusDstAlpha,
    ConstantColor,
    OneMinusConstantColor,
    ConstantAlpha,
    OneMinusConstantAlpha,
    SrcAlphaSaturate,
};
enum class BlendOp { Add, Subtract, ReverseSubtract, Min, Max };
enum class StencilOp { Keep, Zero, Replace, Increment, Decrement, Invert, IncrementWrap, DecrementWrap };
enum class Cull { None, Front, Back };   // which faces are dropped (counter-clockwise is front)

struct BlendState {
    bool enabled = false;
    BlendOp colorOp = BlendOp::Add, alphaOp = BlendOp::Add;
    BlendFactor srcColor = BlendFactor::One, dstColor = BlendFactor::Zero;
    BlendFactor srcAlpha = BlendFactor::One, dstAlpha = BlendFactor::Zero;
    std::array<float, 4> constant{0, 0, 0, 0};
    // The usual "source over" blend (source alpha, one minus source alpha).
    static BlendState alpha();
    bool operator==(const BlendState&) const = default;
};

struct DepthState {
    bool test = false;
    bool write = false;
    Compare func = Compare::Less;
    bool operator==(const DepthState&) const = default;
};

struct StencilState {
    bool enabled = false;
    Compare func = Compare::Always;
    uint8_t reference = 0, readMask = 0xff, writeMask = 0xff;
    StencilOp fail = StencilOp::Keep, depthFail = StencilOp::Keep, pass = StencilOp::Keep;
    bool operator==(const StencilState&) const = default;
};

struct RasterState {
    Cull cull = Cull::None;
    bool wireframe = false;   // polygons as their edges (debug views)
    // Depth offset: factor x the polygon's depth slope + units x the smallest depth step.
    bool depthBias = false;
    float biasFactor = 0, biasUnits = 0;
    std::array<bool, 4> colorWrite{true, true, true, true};
    bool operator==(const RasterState&) const = default;
};

struct RenderState {
    BlendState blend;
    DepthState depth;
    StencilState stencil;
    RasterState raster;
    bool operator==(const RenderState&) const = default;
};

// ---- programs ----------------------------------------------------------------------------------

// Shader dialect. Sources are GLSL bodies without a version line:
//   vertex inputs   in vec3 inPosition; in vec3 inNormal; in vec4 inColor; in vec2 inTexCoord0;
//                   in vec3 inTexCoord1; in vec2 inTexCoord2; in vec4 inJoints; in vec4 inWeights
//                   (GPU skinning: bone indices and weights; declare the ones used; inPosition
//                   may also be declared vec4)
//   stage interface out (vertex) / in (fragment) variables
//   fragment output write fragColor (declared by the backend)
//   textures        uniform sampler2D ...; read with texture(sampler, coordinate)
//   uniforms        plain `uniform` declarations of floats, ints, vectors, matrices and arrays
//   deviceViewIndex the view a layered (multiview) target is drawing (an int; 0 otherwise)
// Attributes a draw does not supply read as (0, 0, 0, 1).
enum class Attribute { Position, Normal, Color, TexCoord0, TexCoord1, TexCoord2, Joints, Weights };
inline constexpr int kAttributeCount = 8;

class Program {
public:
    virtual ~Program() = default;
    // Uniform values, kept until changed. Setting a uniform the program does not use is ignored
    // (as the shaders are generated per material, not every program has every input).
    void set(std::string_view name, int v) { setInts(name, &v, 1); }
    void set(std::string_view name, bool v) { set(name, int(v)); }
    void set(std::string_view name, float v) { setFloats(name, &v, 1); }
    void set(std::string_view name, float x, float y, float z) {
        const float v[3] = {x, y, z};
        setFloats(name, v, 3);
    }
    void set(std::string_view name, float x, float y, float z, float w) {
        const float v[4] = {x, y, z, w};
        setFloats(name, v, 4);
    }
    // `count` floats (vectors, matrices column by column, arrays element by element).
    virtual void setFloats(std::string_view name, const float* v, int count) = 0;
    virtual void setInts(std::string_view name, const int* v, int count) = 0;
    virtual bool has(std::string_view name) const = 0;
    const std::string& name() const { return name_; }

protected:
    std::string name_;
};

// ---- buffers and draws -----------------------------------------------------------------------

// GPU-side vertex data, updated in whole or by range (skinned models rewrite theirs per frame).
class Buffer {
public:
    virtual ~Buffer() = default;
    size_t size() const { return size_; }
    virtual void update(size_t offset, const void* data, size_t bytes) = 0;

protected:
    size_t size_ = 0;
};

// One stream of float vertex data: a buffer with a stride and the attributes it feeds.
struct VertexAttribute {
    Attribute attribute;
    int components;   // 1..4 floats
    int offset;       // bytes into the vertex
};
struct VertexStream {
    const Buffer* buffer = nullptr;
    size_t offset = 0;
    int stride = 0;
    std::vector<VertexAttribute> attributes;
};

enum class Topology { Triangles, TriangleStrip };

struct TextureBinding {
    const Texture* texture = nullptr;   // nullptr: reads as (0, 0, 0, 1)
    Sampler sampler;
};
inline constexpr int kTextureUnits = 8;

struct Draw {
    Program* program = nullptr;
    RenderState state;
    Topology topology = Topology::Triangles;
    std::vector<VertexStream> streams;
    std::array<TextureBinding, kTextureUnits> textures{};
    // The texture units the program's samplers read: sampler name -> unit.
    std::vector<std::pair<std::string_view, int>> samplers;
    int first = 0;
    int count = 0;
};

// Vertices built on the CPU for one draw (layouts, particles, full-screen passes): interleaved
// floats with the attribute layout, uploaded into the device's streaming buffer.
struct Immediate {
    std::vector<VertexAttribute> attributes;
    int floatsPerVertex = 0;
    std::vector<float> data;
    int vertexCount() const { return floatsPerVertex ? int(data.size()) / floatsPerVertex : 0; }
    void vertex(std::initializer_list<float> values) { data.insert(data.end(), values); }
    // A quad given by its corners in order around it, as two triangles split along the diagonal
    // from corner 1 to corner 3 (how OpenGL's quads were split: interpolated values at exact texel
    // boundaries round as before).
    void quad(const float* c0, const float* c1, const float* c2, const float* c3) {
        for (const float* c : {c0, c1, c3, c1, c2, c3}) {
            data.insert(data.end(), c, c + floatsPerVertex);
        }
    }
};

// ---- render targets ----------------------------------------------------------------------------

// An offscreen framebuffer: a colour texture and a depth/stencil buffer, or only a depth texture.
class Target {
public:
    virtual ~Target() = default;
    int width() const { return width_; }
    int height() const { return height_; }
    // The colour texture (nullptr for a depth-only target) and the depth texture of a depth-only
    // target (nullptr otherwise).
    virtual Texture* color() = 0;
    virtual Texture* depth() = 0;

protected:
    int width_ = 0, height_ = 0;
};

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;   // pixels, origin at the bottom left
    bool operator==(const Rect&) const = default;
};

struct Clear {
    bool color = false, depth = false, stencil = false;
    std::array<float, 4> colorValue{0, 0, 0, 1};
    float depthValue = 1;
    uint8_t stencilValue = 0;
};

// ---- the device --------------------------------------------------------------------------------

class Device {
public:
    virtual ~Device() = default;

    virtual std::unique_ptr<Texture> createTexture(Format format, int width, int height,
                                                   const void* pixels = nullptr) = 0;
    virtual std::unique_ptr<Buffer> createBuffer(size_t bytes, const void* data = nullptr) = 0;
    // A program from vertex and fragment sources in the shader dialect; throws with the
    // compiler's log on errors (`name` identifies it).
    virtual std::unique_ptr<Program> createProgram(std::string_view vertex, std::string_view fragment,
                                                   std::string_view name) = 0;
    // A program shared by all users of one renderer, created on first use and owned by the device
    // (it lives as long as the device and its context).
    Program& sharedProgram(std::string_view name, std::string_view vertex, std::string_view fragment) {
        for (auto& p : shared_) {
            if (p->name() == name) {
                return *p;
            }
        }
        shared_.push_back(createProgram(vertex, fragment, name));
        return *shared_.back();
    }
    // An offscreen target: colour (RGBA8) with depth/stencil, or depth only (a Depth24 texture).
    virtual std::unique_ptr<Target> createTarget(int width, int height, bool depthOnly = false) = 0;

    // Where draws go: an offscreen target, or the window (nullptr).
    virtual void bindTarget(Target* target) = 0;
    virtual Target* target() const = 0;
    virtual void setViewport(const Rect& r) = 0;
    virtual Rect viewport() const = 0;
    // Draws touch only pixels inside the scissor rectangle (none when w is 0).
    virtual void setScissor(const Rect& r) = 0;
    virtual Rect scissor() const = 0;
    // Clears within `scissor` (the whole target when w is 0).
    virtual void clear(const Clear& c, const Rect& scissor = {}) = 0;

    virtual void draw(const Draw& d) = 0;
    // A draw whose vertices come from `vertices` (Draw::streams is ignored).
    virtual void draw(const Draw& d, const Immediate& vertices) = 0;

    // Copies the rectangle of the current target into the texture's lower left corner; with a
    // layered target (views() > 1) each view's rectangle side by side, view 0 leftmost.
    virtual void copyToTexture(Texture& texture, const Rect& source) = 0;
    // The views the bound target draws at once (a headset's multiview: 2).
    virtual int views() const { return 1; }
    virtual void generateMipmaps(Texture& texture) = 0;
    // Copies the depth of one depth-only target into another of the same size (false: the backend
    // has no such copy; draw it instead).
    virtual bool copyDepth(Target& from, Target& to) { return false; }
    // The current target's pixels as an opaque top-down image.
    virtual captain_toad::platform::Image readPixels(const Rect& r) = 0;
    // Throws when the backend has recorded an error.
    virtual void checkErrors(const char* where) = 0;
    // Shows the window target's picture (backends whose window system presents it themselves,
    // as OpenGL's buffer swap, do nothing here).
    virtual void present() = 0;
    // The window's surface goes away (the app is sent to the background, Android) and comes
    // back; nothing is presented in between.
    virtual void suspend() {}
    virtual void resume() {}
    // Research: counters since the last call, as one line (empty: none).
    virtual std::string report() { return {}; }

protected:
    // Derived destructors release these first (while the backend can still delete them).
    std::vector<std::unique_ptr<Program>> shared_;
};

// The current device (set by the platform layer when it creates the window).
Device& device();
void setDevice(Device* device);

// ---- small helpers ------------------------------------------------------------------------------

inline BlendState BlendState::alpha() {
    BlendState b;
    b.enabled = true;
    b.srcColor = b.srcAlpha = BlendFactor::SrcAlpha;
    b.dstColor = b.dstAlpha = BlendFactor::OneMinusSrcAlpha;
    return b;
}

}   // namespace captain_toad::gpu
