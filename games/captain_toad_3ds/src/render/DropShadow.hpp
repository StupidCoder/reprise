// =============================================================================================
// DropShadow.hpp - the dark disc under the protagonist
// =============================================================================================
//
// Besides the shadow map, the game draws a soft round "drop shadow" under the protagonist so the
// player can always judge where they will land. It is a stencil shadow volume: an invisible
// cylinder hanging down from the character, and every scene pixel inside it is darkened. The
// original draws it in two passes (traced draws 252 / 253 in evidence/runs/gpu-trace-opening-b):
//   pass 1: cylinder faces kept by the cull mode, depth GEQUAL, stencil +1, no colour;
//   pass 2: depth LESS, stencil EQUAL 1, colour = destination * (1 - 120 / 255), stencil -1.
//
// ROM: the protagonist's InitShadowMask (cylinder, JointRoot, Radius 85). Interpreted from the
// trace: radius = Radius / 2, top at the JointRoot height, height 100 (the original's
// DropLength / ground probe logic is not recovered), 12 sides.
// =============================================================================================
#pragma once
#include "gpu/Device.hpp"
#include <array>
#include <cmath>

struct DropShadow {
    bool enabled = true;
    double radius = 42.5;
    double height = 100;
    double darkness = 120.0 / 255.0;
    int sides = 12;

    // The triangles of a closed 12-sided cylinder hanging from `top`: side quads plus both caps,
    // wound counter-clockwise when seen from outside (`reverse` flips the winding).
    void ring(captain_toad::gpu::Immediate& v, const std::array<double, 3>& top, bool reverse) const {
        auto point = [&](int i, double y) {
            double a = 2 * M_PI * i / sides;
            return std::array<double, 3>{top[0] + radius * cos(a), y, top[2] + radius * sin(a)};
        };
        auto tri = [&](std::array<double, 3> a, std::array<double, 3> b, std::array<double, 3> c) {
            if (reverse) {
                std::swap(b, c);
            }
            for (const auto& p : {a, b, c}) {
                v.vertex({float(p[0]), float(p[1]), float(p[2])});
            }
        };
        double y0 = top[1];
        double y1 = top[1] - height;
        for (int i = 0; i < sides; i++) {
            auto a = point(i, y0);
            auto b = point(i + 1, y0);
            auto c = point(i, y1);
            auto d = point(i + 1, y1);
            tri(a, b, c);
            tri(b, d, c);
            tri({top[0], y0, top[2]}, b, a);
            tri({top[0], y1, top[2]}, c, d);
        }
    }

    // Draws the shadow of a cylinder hanging from `top`; `mvp` is the view's clip transform.
    void draw(const std::array<double, 3>& top, double cylinderRadius, double cylinderHeight,
              const std::array<float, 16>& mvp) {
        if (!enabled) {
            return;
        }
        radius = cylinderRadius;
        height = cylinderHeight;
        captain_toad::gpu::Device& device = captain_toad::gpu::device();
        captain_toad::gpu::Program& program =
            device.sharedProgram("drop shadow",
                                 "in vec3 inPosition;uniform mat4 modelViewProjectionMatrix;\n"
                                 "void main(){gl_Position=modelViewProjectionMatrix*vec4(inPosition,1.0);}",
                                 "void main(){fragColor=vec4(0.0,0.0,0.0,1.0);}");
        program.setFloats("modelViewProjectionMatrix", mvp.data(), 16);
        captain_toad::gpu::Immediate volume;
        volume.attributes = {{captain_toad::gpu::Attribute::Position, 3, 0}};
        volume.floatsPerVertex = 3;
        ring(volume, top, false);
        captain_toad::gpu::Clear clear;
        clear.stencil = true;
        device.clear(clear);

        // Pass 1: mark the back faces that lie behind the scene surface.
        captain_toad::gpu::Draw d;
        d.program = &program;
        captain_toad::gpu::RenderState& s = d.state;
        s.depth = {true, false, captain_toad::gpu::Compare::GreaterEqual};
        s.raster.cull = captain_toad::gpu::Cull::Front;
        s.raster.colorWrite = {false, false, false, false};
        s.stencil.enabled = true;
        s.stencil.func = captain_toad::gpu::Compare::Always;
        s.stencil.reference = 1;
        s.stencil.readMask = 0xff;
        s.stencil.pass = captain_toad::gpu::StencilOp::Increment;
        device.draw(d, volume);

        // Pass 2: front faces in front of the surface darken the marked pixels (those inside the
        // volume) and clear the mark again.
        s.depth.func = captain_toad::gpu::Compare::Less;
        s.raster.cull = captain_toad::gpu::Cull::Back;
        s.raster.colorWrite = {true, true, true, true};
        s.stencil.func = captain_toad::gpu::Compare::Equal;
        s.stencil.readMask = 0x01;
        s.stencil.pass = captain_toad::gpu::StencilOp::Decrement;
        s.stencil.depthFail = captain_toad::gpu::StencilOp::Decrement;
        s.blend.enabled = true;
        s.blend.constant = {0, 0, 0, float(darkness)};
        s.blend.srcColor = captain_toad::gpu::BlendFactor::Zero;
        s.blend.dstColor = captain_toad::gpu::BlendFactor::OneMinusConstantAlpha;
        s.blend.srcAlpha = captain_toad::gpu::BlendFactor::Zero;
        s.blend.dstAlpha = captain_toad::gpu::BlendFactor::One;
        device.draw(d, volume);
    }
};
