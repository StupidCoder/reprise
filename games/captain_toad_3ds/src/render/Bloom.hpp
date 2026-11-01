// =============================================================================================
// Bloom.hpp - the stage's bloom: bright parts of the picture glow softly over their surroundings
// =============================================================================================
//
// What the player sees: in courses with Bloom enabled (3DSEft.byml), light areas such as sunlit
// water and stone get a faint halo.
//
// Recovered from the original (parameters: reader 0x2e5460; passes 0x388644 and 0x3888c4; tap
// vectors set at 0x388c30; checked against the GPU trace of RevolverRuinsStage frame 300, draws
// 200 and 201), drawn after the course's 3D scene and its particles:
//   Pass 1 (0x388644): the frame (400 x 240) is drawn into a 60 x 36 corner of a 64 x 64 target.
//     Three taps of the frame at the quad's coordinate plus (x, y), (x, z) and (w, 0) of the
//     vector (-0.0042, 0.0056, -0.0056, 0.007) x Spread (in the rotated 256 x 512 frame texture's
//     coordinates). Each tap t gives the combiner's Dot3: d = clamp(4 ((t - 0.5) . (c - 0.5))),
//     c = DotColor / 255 (the vertex colour), and the stages sum d x t over the taps (stage 1:
//     d0 t0, stage 3: d1 t1 + it, stage 5: d2 t2 + it, each clamped). The blend writes the sum x
//     Power / 255 (constant alpha) and nothing of the old target.
//   Pass 2 (0x3888c4): a full-screen quad reads three taps of the small target at the vector
//     (0.0045, 0.006, -0.006, -0.0075) x 1 (64 x 64 coordinates), sums them (two Add stages) and
//     adds the sum onto the frame (source x constant alpha 255, destination x 1).
// Native: the frame is first reduced to the original's 400 x 240 (mipmapped minification) so the
// taps read what the console's frame holds; the orientation of the taps on the unrotated screen
// (the 3DS frame is stored turned by 90 degrees) is a hypothesis. Bloom Areas / AreaPower are not
// ported.
// =============================================================================================
#pragma once

#include "gpu/Device.hpp"

#include <array>
#include <memory>

struct Bloom {
    bool enabled = false;
    std::array<float, 3> dotColor{};   // DotColorX/Y/Z / 255
    float power = 0;                   // Power / 255
    float spread = 0;

    // The frame drawn so far in the rectangle (viewX, viewY, w, h) of the current target gets the
    // glow added.
    void render(int viewX, int viewY, int w, int h) {
        if (!enabled || w <= 0 || h <= 0) {
            return;
        }
        captain_toad::gpu::Device& device = captain_toad::gpu::device();
        setup(device);
        captain_toad::gpu::Target* const frameTarget = device.target();
        const captain_toad::gpu::Rect oldViewport = device.viewport();

        // The frame, as copied, then reduced to 400 x 240.
        if (frame_->width() != w || frame_->height() != h) {
            frame_->resize(w, h);
        }
        device.copyToTexture(*frame_, {viewX, viewY, w, h});
        device.generateMipmaps(*frame_);
        device.bindTarget(small_.get());
        device.setViewport({0, 0, 400, 240});
        captain_toad::gpu::Sampler mipmapped = linear();
        mipmapped.mipmaps = true;
        pass(*copyProgram_, *frame_, mipmapped, 0, 0, 1, 1, false);

        // Pass 1: the bright pass into the 60 x 36 corner of the 64 x 64 target.
        device.bindTarget(glow_.get());
        device.setViewport({0, 0, 64, 64});
        captain_toad::gpu::Clear clear;
        clear.color = true;
        clear.colorValue = {0, 0, 0, 0};
        device.clear(clear);
        device.setViewport({0, 0, 60, 36});
        brightProgram_->set("dotColor", dotColor[0], dotColor[1], dotColor[2]);
        brightProgram_->set("power", power);
        setTaps(*brightProgram_, {-0.0042f * spread, 0.0056f * spread, -0.0056f * spread, 0.007f * spread},
                512.0f / 400.0f, 256.0f / 240.0f);
        pass(*brightProgram_, *small_->color(), linear(), 0, 0, 1, 1, false);

        // Pass 2: three taps of the small target added onto the frame.
        device.bindTarget(frameTarget);
        device.setViewport({viewX, viewY, w, h});
        setTaps(*addProgram_, {0.0045f, 0.006f, -0.006f, -0.0075f}, 1.0f, 1.0f);
        pass(*addProgram_, *glow_->color(), linear(), 0, 0, 60.0f / 64.0f, 36.0f / 64.0f, true);
        device.setViewport(oldViewport);
    }

private:
    std::unique_ptr<captain_toad::gpu::Texture> frame_;
    std::unique_ptr<captain_toad::gpu::Target> small_, glow_;
    std::unique_ptr<captain_toad::gpu::Program> copyProgram_, brightProgram_, addProgram_;

    static captain_toad::gpu::Sampler linear() { return {}; }

    // The taps (u, v) = (x, y), (x, z), (w, 0) of the rotated frame: v runs along the screen's
    // width (leftwards), u along its height (upwards); scaleX / scaleY turn them into this
    // texture's normalised coordinates.
    static void setTaps(captain_toad::gpu::Program& program, const std::array<float, 4>& t, float scaleX,
                        float scaleY) {
        const float taps[6] = {-t[1] * scaleX, t[0] * scaleY, -t[2] * scaleX, t[0] * scaleY, 0.0f, t[3] * scaleY};
        program.setFloats("taps", taps, 6);
    }

    // A full-viewport quad reading `source` at (u0, v0) .. (u1, v1); `add`: added onto the target.
    static void pass(captain_toad::gpu::Program& program, const captain_toad::gpu::Texture& source,
                     const captain_toad::gpu::Sampler& sampler, float u0, float v0, float u1, float v1, bool add) {
        captain_toad::gpu::Draw d;
        d.program = &program;
        d.topology = captain_toad::gpu::Topology::TriangleStrip;
        if (add) {
            d.state.blend.enabled = true;
            d.state.blend.srcColor = d.state.blend.srcAlpha = captain_toad::gpu::BlendFactor::One;
            d.state.blend.dstColor = d.state.blend.dstAlpha = captain_toad::gpu::BlendFactor::One;
        }
        d.textures[0] = {&source, sampler};
        d.samplers = {{"source", 0}};
        captain_toad::gpu::Immediate quad;
        quad.attributes = {{captain_toad::gpu::Attribute::Position, 2, 0},
                           {captain_toad::gpu::Attribute::TexCoord0, 2, 8}};
        quad.floatsPerVertex = 4;
        quad.vertex({-1, -1, u0, v0});
        quad.vertex({1, -1, u1, v0});
        quad.vertex({-1, 1, u0, v1});
        quad.vertex({1, 1, u1, v1});
        captain_toad::gpu::device().draw(d, quad);
    }

    static std::unique_ptr<captain_toad::gpu::Program> program(captain_toad::gpu::Device& device, const char* fragment,
                                                               const char* name) {
        static const char* vertex = "in vec2 inPosition;in vec2 inTexCoord0;out vec2 texCoord;\n"
                                    "void main(){texCoord=inTexCoord0;gl_Position=vec4(inPosition,0.0,1.0);}";
        return device.createProgram(vertex, fragment, name);
    }

    void setup(captain_toad::gpu::Device& device) {
        if (frame_) {
            return;
        }
        frame_ = device.createTexture(captain_toad::gpu::Format::RGBA8, 1, 1);
        small_ = device.createTarget(400, 240);
        glow_ = device.createTarget(64, 64);
        copyProgram_ = program(
            device, "in vec2 texCoord;uniform sampler2D source;void main(){fragColor=texture(source,texCoord);}",
            "bloom copy");
        // The combiner rounds every stage to 8 bits (q).
        brightProgram_ = program(device,
                                 "in vec2 texCoord;uniform sampler2D source;uniform vec3 dotColor;uniform float power;"
                                 "uniform vec2 taps[3];\n"
                                 "vec3 q(vec3 x){return floor(clamp(x,0.0,1.0)*255.0+0.5)/255.0;}\n"
                                 "void main(){vec3 sum=vec3(0.0);for(int i=0;i<3;i++){\n"
                                 " vec3 t=texture(source,texCoord+taps[i]).rgb;\n"
                                 " float d=clamp(4.0*dot(t-0.5,dotColor-0.5),0.0,1.0);d=floor(d*255.0+0.5)/255.0;\n"
                                 " sum=q(d*t+sum);}\n"
                                 " fragColor=vec4(q(sum*power),1.0);}",
                                 "bloom bright pass");
        addProgram_ = program(device,
                              "in vec2 texCoord;uniform sampler2D source;uniform vec2 taps[3];\n"
                              "vec3 q(vec3 x){return floor(clamp(x,0.0,1.0)*255.0+0.5)/255.0;}\n"
                              "void main(){vec2 c=texCoord;\n"
                              " vec3 s=q(texture(source,c+taps[0]).rgb+texture(source,c+taps[1]).rgb);\n"
                              " fragColor=vec4(q(s+texture(source,c+taps[2]).rgb),1.0);}",
                              "bloom add pass");
    }
};
