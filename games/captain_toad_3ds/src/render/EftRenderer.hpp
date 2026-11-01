// =============================================================================================
// EftRenderer.hpp - drawing the particles of the effect runtime
// =============================================================================================
//
// The particle engine (Eft.hpp) produces one uniform block per particle and frame. This file
// draws those blocks with the ROM's own render data, so the particles look exactly as authored:
//
//   * Vertex stage: the eft vertex program, chosen like 0x351b40 (key table code.bin 0x4afa2c:
//     draw type data +0x328, rotation flag data +0x18c != 0, data +0x324 == 1), runs for each
//     quad corner on the PICA shader interpreter (PicaShader.hpp; validated against traced
//     post-shader vertices, tools/validate_pica_shader.cpp). The uniforms are the ones the draw
//     setup 0x278e3c uploads: particle block c1..c10, colour c82, texture cell sizes c83 (data
//     +0x368, +0x36c, +0x3e0, +0x3e4), quad offset / camera push c84 (data +0x32c..+0x334), eye
//     c85, projection c86..c89, view c90..c92. The quad buffer is the one built by 0x34e228.
//   * Fragment stage: the emitter's PICA command list (the MATC blob at PTCL header word 0x40 +
//     data +0x158, data +0x154 words; bound by 0x34e108) is translated to GLSL: texture combiner
//     stages 0xc0..0xfd, alpha test 0x104, blending 0x100..0x103, depth / colour masks 0x107 and
//     the texture sampler registers. The combiner constants selected by data +0x168 are replaced
//     per draw with the computed constant (0x27a574: bit 0 -> stage 0 white, bits 1..5 -> stages
//     1..5).
// The PICA register meanings come from public hardware documentation (graphics service).
// =============================================================================================
#pragma once
#include "gpu/Device.hpp"
#include "render/PicaShader.hpp"
#include "data/Package.hpp"
#include "effects/Eft.hpp"
#include <map>
#include <sstream>

struct EftRenderer {
    // A compiled emitter material: its GPU register values and the program built from them.
    struct Material {
        std::map<int, uint32_t> regs;
        std::shared_ptr<captain_toad::gpu::Program> program;
        uint32_t konstMask = 0;
        int shader = 0;
        uint32_t texOffset[2] = {0, 0};
        bool texUsed[2] = {false, false};
    };

    const captain_toad::RecoveredEft::Resource* res = nullptr;
    std::vector<captain_toad::pica::Program> programs;
    std::map<uint32_t, std::shared_ptr<captain_toad::gpu::Texture>> textures;   // PTCL texture-table offset -> texture
    std::map<std::pair<const uint8_t*, bool>, Material> materials;   // (emitter data, child pass) -> material
    bool enabled = true;

    // The real vertex work happens on the CPU interpreter; the GPU only receives clip-space vertices.
    static constexpr const char* kVertexShader =
        "in vec4 inPosition;in vec4 inColor;in vec2 inTexCoord0;in vec2 inTexCoord1;\n"
        "out vec4 vColor;out vec2 vTc0;out vec2 vTc1;\n"
        "void main(){gl_Position=inPosition;vColor=inColor;vTc0=inTexCoord0;vTc1=inTexCoord1;}\n";

    // dir: package directory; meta: effects.json.
    void load(const std::filesystem::path& dir, captain_toad::data::JsonView meta,
              const captain_toad::RecoveredEft::Resource& r) {
        res = &r;
        for (captain_toad::data::JsonView f : meta["shaders"].items()) {
            const captain_toad::data::Bytes d = captain_toad::data::readBytes(dir / f.str());
            programs.emplace_back(d.data(), d.size());
        }
        captain_toad::data::JsonView tex = meta["textures"];
        for (const std::string& key : tex.keys()) {
            captain_toad::data::JsonView t = tex[key];
            const captain_toad::data::Bytes d = captain_toad::data::readBytes(dir / t["file"].str());
            int w = t["width"].i();
            int h = t["height"].i();
            if ((int)d.size() != w * h * 4) {
                throw std::runtime_error("bad eft texture");
            }
            // PTCL textures are stored bottom row first relative to the particle texture coordinates:
            // the traced particle fragments (gpu-trace got-a, frame 56: draws 336/338/339/342, texel
            // colour against the shader's s,t) match the decoded image with its rows reversed
            // (mean error 0.6-16 against 1.2-46 as stored). Symmetric textures hide it; the quarter
            // glow of CollectItemGot/Glow (mirrored over a 2x2 range) does not.
            std::vector<uint8_t> rows(d.size());
            for (int y = 0; y < h; y++) {
                memcpy(rows.data() + size_t(y) * size_t(w) * 4, d.data() + size_t(h - 1 - y) * size_t(w) * 4,
                       size_t(w) * 4);
            }
            textures[(uint32_t)strtoul(key.c_str(), nullptr, 16)] =
                captain_toad::gpu::device().createTexture(captain_toad::gpu::Format::RGBA8, w, h, rows.data());
        }
    }

    // 0x351b40: the vertex program of an emitter (stripe types 5 / 6 use programs 0x18..0x1d). The
    // child pass keys on data +0x5a4 (type), +0x668 (rotation) and +0x5a0 == 1 (0x352d78).
    static int shaderIndex(const captain_toad::RecoveredEft::EmitterData& d, bool child = false) {
        static const int key[24][3] = {{0, 0, 0}, {0, 1, 0}, {1, 0, 0}, {1, 1, 0}, {2, 0, 0}, {2, 1, 0},
                                       {3, 0, 0}, {3, 1, 0}, {4, 0, 0}, {4, 1, 0}, {8, 0, 0}, {8, 1, 0},
                                       {0, 0, 1}, {0, 1, 1}, {1, 0, 1}, {1, 1, 1}, {2, 0, 1}, {2, 1, 1},
                                       {3, 0, 1}, {3, 1, 1}, {4, 0, 1}, {4, 1, 1}, {8, 0, 1}, {8, 1, 1}};
        const int type = child ? d.i(0x5a4) : d.i(0x328);
        const int rot = child ? d.b(0x668) != 0 : d.b(0x18c) != 0;
        const int flag = child ? d.i(0x5a0) == 1 : d.i(0x324) == 1;
        if (type == 5 || type == 6) {
            // Stripe programs 0x18..0x1d by the shader key's subtype (0, 1, 2) and flag bit 0. The
            // key comes from the resource's shader table; its stripe rows (5, 0, 0, 0) and
            // (5, 4, 2, 0) have flag 0. Hypothesis: the subtype is the stripe option (data at
            // +0x55a, +0), which the history code also distinguishes (2: the matrix's Y axis).
            const int opt = d.i(d.h(0x55a));
            return 24 + 2 * std::max(0, std::min(2, opt));
        }
        for (int i = 0; i < 24; i++) {
            if (key[i][0] == type && key[i][1] == rot && key[i][2] == flag) {
                return i;
            }
        }
        return 0;
    }

    // Decode a PICA command list into register values. Each command is (value, header); the
    // header carries the register, the count of extra values and the "sequential" bit (extra
    // values go to consecutive registers instead of the same one).
    static std::map<int, uint32_t> commands(const uint8_t* raw, size_t start, size_t words) {
        std::map<int, uint32_t> regs;
        size_t o = start;
        size_t end = start + 4 * words;
        auto u32 = [&](size_t a) {
            uint32_t v;
            memcpy(&v, raw + a, 4);
            return v;
        };
        while (o + 8 <= end) {
            const uint32_t value = u32(o);
            const uint32_t header = u32(o + 4);
            o += 8;
            const int reg = header & 0xffff;
            const int extra = (header >> 20) & 0xff;
            const bool seq = header >> 31;
            std::vector<uint32_t> vals{value};
            for (int k = 0; k < extra; k++) {
                vals.push_back(u32(o + 4 * k));
            }
            o += 4 * extra;
            if (extra & 1) {
                o += 4;   // padding to 8 bytes
            }
            for (size_t k = 0; k < vals.size(); k++) {
                regs[reg + (seq ? int(k) : 0)] = vals[k];
            }
        }
        return regs;
    }

    // RGBA8 register value -> GLSL vec4 literal.
    static std::string vec4(uint32_t w) {
        std::ostringstream s;
        s.precision(9);
        s << "vec4(" << (w & 255) / 255.0 << "," << (w >> 8 & 255) / 255.0 << "," << (w >> 16 & 255) / 255.0 << ","
          << (w >> 24 & 255) / 255.0 << ")";
        return s.str();
    }

    // PICA texture combiner (TEV) -> GLSL (graphics service; documented register layout). The six
    // stages each combine up to three sources with a mode, separately for colour and alpha.
    static std::string fragmentSource(const std::map<int, uint32_t>& regs, uint32_t konstMask) {
        auto reg = [&](int r) {
            auto it = regs.find(r);
            return it == regs.end() ? 0u : it->second;
        };
        std::ostringstream g;
        g << "in vec4 vColor; in vec2 vTc0, vTc1;\nuniform sampler2D tex0, tex1; uniform vec4 "
             "konst;\n"
          << "void main(){\nvec4 t0=texture(tex0,vTc0), t1=texture(tex1,vTc1);\n"
          << "vec4 prev=vec4(0.0), buf=vec4(0.0), next=" << vec4(reg(0xfd)) << ", k;\n";
        const int bases[6] = {0xc0, 0xc8, 0xd0, 0xd8, 0xf0, 0xf8};

        auto source = [&](int src) -> std::string {
            switch (src) {
            case 0:
                return "vColor";
            case 3:
                return "t0";
            case 4:
                return "t1";
            case 13:
                return "buf";
            case 14:
                return "k";
            case 15:
                return "prev";
            default:
                return "vec4(0.0)";   // fragment lighting / texture 2 / 3: unused by the effect materials
            }
        };
        auto rgbOperand = [&](int src, int op) {
            static const char* c[16] = {"rgb", "rgb", "aaa",   "aaa",   "rrr", "rrr", nullptr, nullptr,
                                        "ggg", "ggg", nullptr, nullptr, "bbb", "bbb", nullptr, nullptr};
            if (!c[op]) {
                throw std::runtime_error("TEV colour operand");
            }
            std::string e = source(src) + "." + c[op];
            return (op & 1) ? "(vec3(1.0)-" + e + ")" : e;
        };
        auto alphaOperand = [&](int src, int op) {
            static const char* c[8] = {"a", "a", "r", "r", "g", "g", "b", "b"};
            std::string e = source(src) + "." + c[op & 7];
            return (op & 1) ? "(1.0-" + e + ")" : e;
        };
        auto combine = [&](int mode, const std::string& a, const std::string& b, const std::string& c,
                           bool rgb) -> std::string {
            const std::string half = rgb ? "vec3(0.5)" : "0.5";
            const std::string one = rgb ? "vec3(1.0)" : "1.0";
            switch (mode) {
            case 0:   // replace
                return a;
            case 1:   // modulate
                return "(" + a + "*" + b + ")";
            case 2:   // add
                return "(" + a + "+" + b + ")";
            case 3:   // add signed
                return "(" + a + "+" + b + "-" + half + ")";
            case 4:   // interpolate
                return "(" + a + "*" + c + "+" + b + "*(" + one + "-" + c + "))";
            case 5:   // subtract
                return "(" + a + "-" + b + ")";
            case 6:     // dot3 rgb
            case 7: {   // dot3 rgba
                std::string d = "(4.0*dot(" + (rgb ? a : "vec3(" + a + ")") + "-vec3(0.5)," +
                                (rgb ? b : "vec3(" + b + ")") + "-vec3(0.5)))";
                return rgb ? "vec3" + d : d;
            }
            case 8:   // multiply-add
                return "(" + a + "*" + b + "+" + c + ")";
            case 9:   // add-multiply
                return "(min(" + a + "+" + b + "," + one + ")*" + c + ")";
            default:
                throw std::runtime_error("TEV combine mode");
            }
        };

        for (int stage = 0; stage < 6; stage++) {
            const int base = bases[stage];
            const uint32_t src = reg(base);
            const uint32_t op = reg(base + 1);
            const uint32_t mode = reg(base + 2);
            const uint32_t scale = reg(base + 4);
            g << "k="
              << ((konstMask >> stage) & 1 ? (stage == 0 ? std::string("vec4(1.0)") : std::string("konst"))
                                           : vec4(reg(base + 3)))
              << ";\n";
            std::string rgb[3];
            std::string alpha[3];
            for (int i = 0; i < 3; i++) {
                rgb[i] = rgbOperand((src >> (4 * i)) & 15, (op >> (4 * i)) & 15);
                alpha[i] = alphaOperand((src >> (16 + 4 * i)) & 15, (op >> (12 + 4 * i)) & 7);
            }
            const int rs = 1 << std::min(2u, scale & 3);
            const int as = 1 << std::min(2u, (scale >> 16) & 3);
            const int amode = (mode >> 16) & 15;
            g << "{vec3 c=clamp(" << combine(mode & 15, rgb[0], rgb[1], rgb[2], true) << "*" << rs << ".0,0.0,1.0);\n";
            if (amode == 7) {
                g << "float a=c.r;\n";
            } else {
                g << " float a=clamp(" << combine(amode, alpha[0], alpha[1], alpha[2], false) << "*" << as
                  << ".0,0.0,1.0);\n";
            }
            g << " prev=vec4(c,a);}\n buf=next;\n";
            // Combiner buffer update (register 0xe0).
            const uint32_t upd = reg(0xe0);
            if (stage < 4 && (upd & (1u << (8 + stage)))) {
                g << " next.rgb=prev.rgb;\n";
            }
            if (stage < 4 && (upd & (1u << (12 + stage)))) {
                g << " next.a=prev.a;\n";
            }
        }

        // Alpha test (register 0x104).
        const uint32_t test = reg(0x104);
        if (test & 1) {
            std::ostringstream ref;
            ref.precision(9);
            ref << ((test >> 8) & 255) / 255.0;
            static const char* f[8] = {"false", "true", "==", "!=", "<", "<=", ">", ">="};
            const int func = (test >> 4) & 7;
            if (func == 0) {
                g << "discard;\n";
            } else if (func > 1) {
                g << "if(!(floor(prev.a*255.0+0.5)/255.0" << f[func] << ref.str() << "))discard;\n";
            }
        }
        g << "fragColor=prev;}\n";
        return g.str();
    }

    // The material of an emitter (or its child pass), built on first use.
    // Every emitter's materials made now (programs compiled or read from the shader cache) instead
    // of at an emitter's first draw, where compiling one stalled the frame (a headset hitch).
    void warm(const captain_toad::RecoveredEft::Resource& resource) {
        for (const auto& set : resource.sets) {
            for (const auto& d : set.emitters) {
                material(d, false);
                if (d.kind() == 1 && (d.u(0x548) & 1)) {   // the child pass (complex emitters)
                    material(d, true);
                }
            }
        }
    }

    Material& material(const captain_toad::RecoveredEft::EmitterData& d, bool child = false) {
        auto it = materials.find({d.p, child});
        if (it != materials.end()) {
            return it->second;
        }
        Material m;
        const uint8_t* raw = res->data.data();
        uint32_t base;
        memcpy(&base, raw + 0x40, 4);
        if (!child) {
            const size_t blob = base + d.u(0x158);
            if (memcmp(raw + blob, "MATC", 4)) {
                throw std::runtime_error("eft material blob missing");
            }
            m.regs = commands(raw, blob + 0xc, d.u(0x154));
            m.konstMask = d.u(0x168);
        } else {
            // The child block at data +0x70c (0x352d78 -> 0x34e108 with param_4 1): word count
            // +0x710, blob offset +0x714 ("MATC" + count, commands from +8; the decode matches the
            // traced SwoonLight child draw), constant mask +0x724.
            const size_t blob = base + d.u(0x714);
            if (memcmp(raw + blob, "MATC", 4)) {
                throw std::runtime_error("eft child material blob missing");
            }
            m.regs = commands(raw, blob + 8, d.u(0x710));
            m.konstMask = d.u(0x724);
        }
        m.shader = shaderIndex(d, child);
        for (int s = 0; s < 2; s++) {
            const int slot = child ? (s == 0 ? 0x5b0 : -1) : (s == 0 ? 0x40 : 0xbc);
            if (slot < 0) {
                m.texUsed[s] = false;
                continue;
            }
            m.texUsed[s] = d.h(slot) != 0 && d.u(slot + 0x70) != 0;
            m.texOffset[s] = d.u(slot + 0x74);
        }
        m.program = captain_toad::gpu::device().createProgram(kVertexShader, fragmentSource(m.regs, m.konstMask),
                                                              "eft material");
        return materials.emplace(std::make_pair(d.p, child), std::move(m)).first->second;
    }

    // The sampler registers of texture slot `s` (filters 0x83 / 0x93 bits 1-2, wraps bits 8-14,
    // border colour 0x81 / 0x91); an unused slot reads as black.
    captain_toad::gpu::TextureBinding binding(const Material& m, int s) const {
        auto it = textures.find(m.texOffset[s]);
        const uint32_t p = m.regs.count(s == 0 ? 0x83 : 0x93) ? m.regs.at(s == 0 ? 0x83 : 0x93) : 0;
        const bool on = m.texUsed[s] && it != textures.end() && (m.regs.count(0x80) ? (m.regs.at(0x80) >> s) & 1 : 1);
        captain_toad::gpu::TextureBinding b;
        if (!on) {
            return b;
        }
        b.texture = it->second.get();
        b.sampler.magFilter = (p >> 1) & 1 ? captain_toad::gpu::Filter::Linear : captain_toad::gpu::Filter::Nearest;
        b.sampler.minFilter = (p >> 2) & 1 ? captain_toad::gpu::Filter::Linear : captain_toad::gpu::Filter::Nearest;
        b.sampler.wrapT = captain_toad::gpu::Wrap(std::min(3u, (p >> 8) & 7));
        b.sampler.wrapS = captain_toad::gpu::Wrap(std::min(3u, (p >> 12) & 7));
        const uint32_t c = m.regs.count(s == 0 ? 0x81 : 0x91) ? m.regs.at(s == 0 ? 0x81 : 0x91) : 0;
        b.sampler.border = {(c & 255) / 255.f, (c >> 8 & 255) / 255.f, (c >> 16 & 255) / 255.f,
                            (c >> 24 & 255) / 255.f};
        return b;
    }

    // Blend (enable 0x100 bit 8, 0x101, constant 0x103), depth and colour-mask (0x107) registers
    // of a material as render state.
    static captain_toad::gpu::RenderState state(uint32_t enable, uint32_t blend, uint32_t color, uint32_t depth) {
        captain_toad::gpu::RenderState st;
        if (enable & 0x100) {
            captain_toad::gpu::BlendState& b = st.blend;
            b.enabled = true;
            auto op = [](uint32_t e) {
                return e < 5 ? captain_toad::gpu::BlendOp(e) : captain_toad::gpu::BlendOp::Add;
            };
            auto factor = [](uint32_t f) {
                return f < 15 ? captain_toad::gpu::BlendFactor(f) : captain_toad::gpu::BlendFactor::One;
            };
            b.colorOp = op(blend & 7);
            b.alphaOp = op((blend >> 8) & 7);
            b.srcColor = factor((blend >> 16) & 15);
            b.dstColor = factor((blend >> 20) & 15);
            b.srcAlpha = factor((blend >> 24) & 15);
            b.dstAlpha = factor((blend >> 28) & 15);
            b.constant = {(color & 255) / 255.f, (color >> 8 & 255) / 255.f, (color >> 16 & 255) / 255.f,
                          (color >> 24 & 255) / 255.f};
        }
        st.depth.test = depth & 1;
        st.depth.func = captain_toad::gpu::Compare((depth >> 4) & 7);
        st.depth.write = (depth >> 12) & 1;
        st.raster.colorWrite = {bool((depth >> 8) & 1), bool((depth >> 9) & 1), bool((depth >> 10) & 1),
                                bool((depth >> 11) & 1)};
        return st;
    }

    // A draw of the material with its textures and render state.
    captain_toad::gpu::Draw draw(const Material& m, captain_toad::gpu::Topology topology) const {
        const auto R = [&](int r) {
            auto it = m.regs.find(r);
            return it == m.regs.end() ? 0u : it->second;
        };
        captain_toad::gpu::Draw d;
        d.program = m.program.get();
        d.topology = topology;
        d.state = state(R(0x100), R(0x101), R(0x103), R(0x107));
        d.textures[0] = binding(m, 0);
        d.textures[1] = binding(m, 1);
        d.samplers = {{"tex0", 0}, {"tex1", 1}};
        return d;
    }

    // Post-shader vertices: clip position, colour and two texture coordinates (semantic slots).
    static captain_toad::gpu::Immediate vertexList() {
        captain_toad::gpu::Immediate v;
        v.attributes = {{captain_toad::gpu::Attribute::Position, 4, 0},
                        {captain_toad::gpu::Attribute::Color, 4, 16},
                        {captain_toad::gpu::Attribute::TexCoord0, 2, 32},
                        {captain_toad::gpu::Attribute::TexCoord1, 2, 40}};
        v.floatsPerVertex = 12;
        return v;
    }
    static void emit(captain_toad::gpu::Immediate& v, const float* o) {
        v.vertex({o[0], o[1], o[2], o[3], o[4], o[5], o[6], o[7], o[8], o[9], o[10], o[11]});
    }

    // 0x34c5f4: the stripes of a type-5 emitter. For each particle with a history of N >= 3 used
    // entries (N = min(stripe +0x1c, count)), N - 1 samples spread over the history (t = i / (N - 2),
    // entry head - round(t * (count - 2)) - 1), two vertices per sample (0x34d0a0) whose four vec4
    // go to c(4n+1)..c(4n+4), read by the stripe program through v0.x = 4n. Per stripe the uniform
    // block c76..c84 (0x27a23c): stParam, the texture channels, both colours and the emitter
    // matrix; a second pass with the other stParam for crossed stripes (stripe +4 == 1, option != 0).
    // The position offset the draw passes (0x3509c0 argument 5) is taken as zero.
    void drawStripes(const captain_toad::RecoveredEft::Emitter& e, const Material& m,
                     const captain_toad::pica::Program& prog, captain_toad::pica::Machine& vm) {
        const captain_toad::gpu::Draw strip = draw(m, captain_toad::gpu::Topology::TriangleStrip);
        using captain_toad::pica::Vec4;
        const captain_toad::RecoveredEft::EmitterData& d = e.data;
        const int so = d.h(0x55a);
        const int opt = d.i(so), cross = d.i(so + 4), fixedTex = d.i(so + 12);
        const int histLen = std::max(1, std::min(256, d.i(so + 0x10)));
        const float aStart = d.f(so + 0x14), aEnd = d.f(so + 0x18);
        const int maxN = d.i(so + 0x1c);
        const captain_toad::RecoveredEft::EmitterSet& set = *e.set;
        const float cs = d.f(0x4b8);
        const float setColor[3] = {set.color[0] * cs, set.color[1] * cs, set.color[2] * cs};
        for (const captain_toad::RecoveredEft::Particle* p = e.head; p; p = p->next) {
            if (!p->stripe) {
                continue;
            }
            const captain_toad::RecoveredEft::Stripe& s = *p->stripe;
            const int N = std::min(maxN, s.count);
            if (N < 3) {
                continue;
            }
            const float s19 = 1.0f / float(N - 2);
            float s18 = s19;
            if (fixedTex == 1 && s.frames < maxN) {
                s18 = 1.0f / float(maxN - 2);
            }
            // Uniform block (0x27a23c), colours by the emitter's colour / constant indices.
            const float* c0 = e.colorIndex == 1 ? p->color1 : p->color0;
            const float* c1 = e.konstIndex == 1 ? p->color1 : p->color0;
            const float r0 = c0[0] * e.color0.x * setColor[0], g0 = c0[1] * e.color0.y * setColor[1],
                        b0 = c0[2] * e.color0.z * setColor[2];
            const float r1 = c1[0] * e.color1.x * setColor[0], g1 = c1[1] * e.color1.y * setColor[1],
                        b1 = c1[2] * e.color1.z * setColor[2];
            const uint32_t konst =
                uint32_t(std::min(255.0f, r1 * 255.0f)) | uint32_t(std::min(255.0f, g1 * 255.0f)) << 8 |
                uint32_t(std::min(255.0f, b1 * 255.0f)) << 16 | uint32_t(std::min(255.0f, e.fade * 255.0f)) << 24;
            m.program->set("konst", (konst & 255) / 255.f, (konst >> 8 & 255) / 255.f, (konst >> 16 & 255) / 255.f,
                           (konst >> 24 & 255) / 255.f);
            vm.c[77] = {p->tex0PatU + p->tex0ScrollU, p->tex0PatV - p->tex0ScrollV, p->tex1PatU + p->tex1ScrollU,
                        p->tex1PatV - p->tex1ScrollV};
            vm.c[78] = {p->tex0ScaleU, p->tex0ScaleV, p->tex0Rot, 0.0f};
            vm.c[79] = {p->tex1ScaleU, p->tex1ScaleV, p->tex1Rot, 0.0f};
            vm.c[80] = {r0, g0, b0, e.fade};
            vm.c[81] = {r1, g1, b1, e.fade};
            for (int r = 0; r < 3; r++) {
                vm.c[82 + r] = {e.mtxSRT[size_t(r * 4)], e.mtxSRT[size_t(r * 4 + 1)], e.mtxSRT[size_t(r * 4 + 2)],
                                e.mtxSRT[size_t(r * 4 + 3)]};
            }
            // The sample vertices (0x34d0a0).
            struct Pair {
                Vec4 a[4], b[4];
            };
            std::vector<Pair> pairs;
            const float countMul = float(s.count - 2);
            for (int i = 0; i < N - 1; i++) {
                const float t = float(i) * s19;
                int idx = s.head - int(t * countMul + 0.5f) - 1;
                if (idx < 0) {
                    idx += histLen;
                }
                const captain_toad::RecoveredEft::Stripe::Entry& en = s.entries[size_t(idx)];
                const float lerp = aStart + (aEnd - aStart) * t;
                const float alpha = e.alpha * (p->alpha * set.color[3] * (lerp * e.fade));
                const float v = float(i) * s18;
                const float tex0V = p->tex0PatV + v * d.f(0x36c), tex1V = p->tex1PatV + v * d.f(0x3e4);
                Pair pr;
                pr.a[0] = {en.pos.x, en.pos.y, en.pos.z, alpha};
                pr.a[1] = {en.dir.x, en.dir.y, en.dir.z, 0.0f};
                pr.a[2] = {en.side.x, en.side.y, en.side.z, en.width};
                pr.a[3] = {d.f(0x368) + p->tex0PatU, tex0V, d.f(0x3e0) + p->tex1PatU, tex1V};
                pr.b[0] = pr.a[0];
                pr.b[1] = pr.a[1];
                pr.b[2] = {en.side.x, en.side.y, en.side.z, -en.width};
                pr.b[3] = {p->tex0PatU, tex0V, p->tex1PatU, tex1V};
                pairs.push_back(pr);
            }
            for (int pass = 0; pass < (cross == 1 && opt != 0 ? 2 : 1); pass++) {
                vm.c[76] = pass == 0 ? Vec4{1.0f, 0.0f, d.f(0x334), p->scaleX} : Vec4{0.0f, 1.0f, d.f(0x334), 1.0f};
                captain_toad::gpu::Immediate v = vertexList();
                for (const Pair& pr : pairs) {
                    for (const Vec4* vert : {pr.a, pr.b}) {
                        for (int k = 0; k < 4; k++) {
                            vm.c[1 + k] = vert[k];
                        }
                        vm.v[0] = {0.0f, 0.0f, 0.0f, 1.0f};
                        std::fill(std::begin(vm.out), std::end(vm.out), 0.0f);
                        vm.run(prog);
                        emit(v, vm.out);
                    }
                }
                captain_toad::gpu::device().draw(strip, v);
            }
        }
    }

    // Draw a frame's particle blocks, grouped by emitter and pass.
    // proj: 4x4 row-major (GL clip convention); view: 3x4 row-major world -> view; eye: world
    // camera position.
    void draw(const std::vector<captain_toad::RecoveredEft::DrawBlock>& blocks, const float proj[16],
              const float view[12], const float eye[3]) {
        if (!enabled || blocks.empty()) {
            return;
        }
        // The quad: four corners with texture coordinates, as two triangles.
        static const float cx[4] = {-0.5f, 0.5f, -0.5f, 0.5f};
        static const float cy[4] = {0.5f, 0.5f, -0.5f, -0.5f};
        static const float tu[4] = {0, 1, 0, 1};
        static const float tv[4] = {0, 0, 1, 1};
        static const int order[6] = {0, 2, 1, 1, 2, 3};
        captain_toad::pica::Machine vm;

        for (size_t i = 0; i < blocks.size();) {
            // The run of blocks of one emitter and pass.
            const captain_toad::RecoveredEft::Emitter* e = blocks[i].emitter;
            const bool child = blocks[i].child;
            size_t j = i;
            while (j < blocks.size() && blocks[j].emitter == e && blocks[j].child == child) {
                j++;
            }
            // Stripes draw whenever the emitter has particles (0x3509c0), also with data +0x15f set
            // (which only turns off the particle quads, as for the fall trail PlayerBlur).
            const bool stripeVisible =
                e && blocks[i].stripe && !e->set->stopDraw && e->ctrl && e->ctrl->visible != 0 && e->data.b(0x180) != 0;
            if (!e || !(blocks[i].stripe ? stripeVisible : child ? e->childDrawThisFrame : e->drawThisFrame)) {
                i = j;
                continue;
            }
            const captain_toad::RecoveredEft::EmitterData& d = e->data;
            if (const char* skip = getenv("DIORAMA_EFT_SKIP")) {
                if (strstr(skip, res->name(d.u(0x38)).c_str())) {   // research
                    i = j;
                    continue;
                }
            }
            Material& m = material(d, child);
            const captain_toad::pica::Program& prog = programs.at(size_t(m.shader));

            // Per-emitter uniforms.
            const float* sc = child ? e->childShaderColor : e->shaderColor;
            vm.c[82] = {sc[0], sc[1], sc[2], sc[3]};
            if (!child) {
                vm.c[83] = {d.f(0x368), d.f(0x36c), d.f(0x3e0), d.f(0x3e4)};
                vm.c[84] = {d.f(0x32c), d.f(0x330), d.f(0x334), 0.0f};
            } else {
                // 0x278e3c param_4 == 1 (traced SwoonLight child: c83 (1, 1, 0, 0), c84 0).
                vm.c[83] = {d.f(0x6d0), d.f(0x6d4), 0.0f, 0.0f};
                vm.c[84] = {d.f(0x6a4), d.f(0x6a8), 0.0f, 0.0f};
            }
            vm.c[85] = {eye[0], eye[1], eye[2], 0.0f};
            for (int r = 0; r < 4; r++) {
                vm.c[86 + r] = {proj[r * 4], proj[r * 4 + 1], proj[r * 4 + 2], proj[r * 4 + 3]};
            }
            for (int r = 0; r < 3; r++) {
                vm.c[90 + r] = {view[r * 4], view[r * 4 + 1], view[r * 4 + 2], view[r * 4 + 3]};
            }
            vm.constants(prog);

            if (blocks[i].stripe) {
                drawStripes(*e, m, prog, vm);
                i = j;
                continue;
            }

            const uint32_t k = child ? e->childKonst : e->konst;
            if (const char* dbg = getenv("DIORAMA_EFT_DEBUG")) {
                if (atoi(dbg) >= 2) {
                    fprintf(stderr, "eft draw %s: %zu blocks, shader %d, c82 %.3f %.3f %.3f %.3f konst %08x mask %x\n",
                            res->name(d.u(0x38)).c_str(), j - i, m.shader, e->shaderColor[0], e->shaderColor[1],
                            e->shaderColor[2], e->shaderColor[3], k, m.konstMask);
                }
            }
            m.program->set("konst", (k & 255) / 255.f, (k >> 8 & 255) / 255.f, (k >> 16 & 255) / 255.f,
                           (k >> 24 & 255) / 255.f);

            // One quad per particle: the ROM vertex program runs for each corner.
            captain_toad::gpu::Immediate v = vertexList();
            for (size_t b = i; b < j; b++) {
                const captain_toad::RecoveredEft::DrawBlock& o = blocks[b];
                const float* src[7] = {o.color, o.scaleRot, o.tex0, o.tex1, o.pos, o.dir, o.rot};
                for (int r = 0; r < 7; r++) {
                    vm.c[1 + r] = {src[r][0], src[r][1], src[r][2], src[r][3]};
                }
                for (int r = 0; r < 3; r++) {
                    vm.c[8 + r] = {o.mtx[r * 4], o.mtx[r * 4 + 1], o.mtx[r * 4 + 2], o.mtx[r * 4 + 3]};
                }
                float out[4][14];
                for (int q = 0; q < 4; q++) {
                    vm.v[0] = {cx[q], cy[q], 0.0f, 1.0f};
                    vm.v[1] = {tu[q], tv[q], 0.0f, 1.0f};
                    vm.v[2] = {1, 1, 1, 1};
                    std::fill(std::begin(vm.out), std::end(vm.out), 0.0f);
                    vm.run(prog);
                    std::copy(vm.out, vm.out + 14, out[q]);
                }
                for (int q : order) {
                    emit(v, out[q]);
                }
            }
            captain_toad::gpu::device().draw(draw(m, captain_toad::gpu::Topology::Triangles), v);
            i = j;
        }
    }
};
