// =============================================================================================
// CameraMask.hpp - the course's colour mask over the picture's edges
// =============================================================================================
//
// What the player sees: in courses with a CameraMask (3DSEft.byml), the edges of the picture
// (strongest at the bottom) are tinted with the course's colours, e.g. a pink glow under
// TouchPuzzleMiniStage's checkered floor.
//
// Recovered from the original (parameters: reader 0x2e5460 -> object 0x247340, set up by 0x2471d8
// with the model ObjectData/CameraMaskFull; drawn by 0x247140 after the bloom; GPU trace of
// TouchPuzzleMiniStage, draw 380):
//   The model is a funnel in camera space (an outer ring of radius 0.934 at z 0, an inner one at
//   z -1), scaled by Scale and moved to Pos in view coordinates, drawn with its own projection:
//   1 / tan(0.5) vertically and aspect 1.6667 (0x24725c: the half angle 20.3718 in the sine
//   table's 256-per-turn units, 0x2b9df8, is 0.5 radians).
//   Combiner (traced registers): stage 0 colour = Colour1, alpha = Alpha x I; stage 1 colour =
//   Colour2 x (1 - I) + previous; I the mask texture's intensity (I8, mirrored repeat). Blended
//   over the frame with the source alpha, no depth test.
// Native: the aspect is the viewport's (a wider window keeps the mask at the picture's edges).
// The Depth parameter and the mask's Areas are not ported.
// =============================================================================================
#pragma once

#include "gpu/Device.hpp"
#include "gpu/Matrix.hpp"

#include <array>
#include <cmath>
#include <memory>
#include <vector>

struct CameraMask {
    bool enabled = false;
    std::array<float, 3> color1{}, color2{};
    float alpha = 0;
    std::array<float, 3> position{}, scale{1, 1, 1};
    std::vector<std::array<float, 5>> vertices;   // x, y, z, u, v (triangles)
    const captain_toad::gpu::Texture* texture = nullptr;

    void render(int viewX, int viewY, int w, int h) {
        if (!enabled || !texture || w <= 0 || h <= 0) {
            return;
        }
        captain_toad::gpu::Device& device = captain_toad::gpu::device();
        if (!program_) {
            program_ = device.createProgram(
                "in vec3 inPosition;in vec2 inTexCoord0;out vec2 texCoord;uniform mat4 modelViewProjectionMatrix;\n"
                "void main(){texCoord=inTexCoord0;gl_Position=modelViewProjectionMatrix*vec4(inPosition,1.0);}",
                "in vec2 texCoord;uniform sampler2D mask;uniform vec3 color1;uniform vec3 color2;uniform float alpha;\n"
                "void main(){float i=texture(mask,texCoord).r;\n"
                " fragColor=vec4(clamp(color1+color2*(1.0-i),0.0,1.0),alpha*i);}",
                "camera mask");
        }
        const captain_toad::gpu::Rect oldViewport = device.viewport();
        device.setViewport({viewX, viewY, w, h});
        // 0x247290..0x2472d0: x' = x f / aspect, y' = y f, w' = -z; the original's depth row
        // (z' = z) gives a constant depth, here 0 so that nothing is clipped but w' <= 0.
        const float f = 1.0f / std::tan(0.5f);
        const float aspect = float(w) / float(h);
        const float p[16] = {f / aspect, 0, 0, 0, 0, f, 0, 0, 0, 0, 0, -1, 0, 0, 0, 0};
        const captain_toad::gpu::Mat4 modelView =
            captain_toad::gpu::Mat4::translation(position[0], position[1], position[2]) *
            captain_toad::gpu::Mat4::scaling(scale[0], scale[1], scale[2]);
        const auto mvp = captain_toad::gpu::modelViewProjection(captain_toad::gpu::Mat4::fromColumnMajor(p), modelView);
        program_->setFloats("modelViewProjectionMatrix", mvp.data(), 16);
        program_->set("color1", color1[0], color1[1], color1[2]);
        program_->set("color2", color2[0], color2[1], color2[2]);
        program_->set("alpha", alpha);
        captain_toad::gpu::Draw d;
        d.program = program_.get();
        d.state.blend = captain_toad::gpu::BlendState::alpha();
        captain_toad::gpu::Sampler mirrored;
        mirrored.wrapS = mirrored.wrapT = captain_toad::gpu::Wrap::MirroredRepeat;
        d.textures[0] = {texture, mirrored};
        d.samplers = {{"mask", 0}};
        captain_toad::gpu::Immediate triangles;
        triangles.attributes = {{captain_toad::gpu::Attribute::Position, 3, 0},
                                {captain_toad::gpu::Attribute::TexCoord0, 2, 12}};
        triangles.floatsPerVertex = 5;
        for (const auto& v : vertices) {
            triangles.data.insert(triangles.data.end(), v.begin(), v.end());
        }
        device.draw(d, triangles);
        device.setViewport(oldViewport);
    }

private:
    std::unique_ptr<captain_toad::gpu::Program> program_;
};
