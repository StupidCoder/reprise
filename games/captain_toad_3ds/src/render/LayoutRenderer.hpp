// =============================================================================================
// LayoutRenderer.hpp - the 2D screens of the goal sequence
// =============================================================================================
//
// The "course clear" banner and the window and text overlays of the goal sequence are 2D
// layouts (NW layout version 7 data, exported by tools/export_layouts.py): a tree of panes (null,
// picture, text, window and part panes), materials, fonts and keyframe animations (BFLAN). This
// is a presentation service: it evaluates the animations and draws the panes on the 400 x 240
// top screen. The game logic decides which layout and animation plays, and at which frame
// (Goal.hpp).
//
// Material colour model, from traced draws (evidence/runs/goal-gpu-950-a draws 183-186 text,
// goal-gpu-1020-a draws 186-187 window):
//   * stage 0 writes the texture to the combiner buffer (alpha-only formats give rgb = 1);
//   * bordered font sheets remap alpha a to rgb = 2 (a - 0.498) and alpha = 2 a (1 + 0.0784);
//   * the result interpolates black -> white per channel and is multiplied by the vertex colour
//     (with the pane alpha folded in);
//   * blending: source alpha / one minus source alpha.
// Text boxes draw the shadow and then the text, twice per frame; the first text pass uses
// white = (black.rgb, white.a) (the traced order; its cause in the text writer is not recovered).
// =============================================================================================
#pragma once
#include "data/Package.hpp"
#include "gpu/Device.hpp"
#include "gpu/Matrix.hpp"
#include <array>
#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <vector>

// One animation curve of a layout animation (Hermite, or step).
struct LayoutCurve {
    bool step = false;
    std::vector<std::array<float, 3>> keys;   // frame, value, slope

    float eval(float f) const {
        if (keys.empty()) {
            return 0;
        }
        if (step) {
            float v = keys[0][1];
            for (auto& k : keys) {
                if (f >= k[0]) {
                    v = k[1];
                }
            }
            return v;
        }
        if (f <= keys.front()[0]) {
            return keys.front()[1];
        }
        if (f >= keys.back()[0]) {
            return keys.back()[1];
        }
        for (size_t i = 1; i < keys.size(); i++) {
            if (f < keys[i][0]) {
                const auto& a = keys[i - 1];
                const auto& b = keys[i];
                float d = b[0] - a[0];
                if (d <= 0) {
                    return b[1];
                }
                float t = (f - a[0]) / d;
                float t2 = t * t;
                float t3 = t2 * t;
                return (2 * t3 - 3 * t2 + 1) * a[1] + (t3 - 2 * t2 + t) * d * a[2] + (-2 * t3 + 3 * t2) * b[1] +
                       (t3 - t2) * d * b[2];
            }
        }
        return keys.back()[1];
    }
};

struct Layout {
    struct Material {
        std::string name;
        float black[4];   // colour where the texture is 0
        float white[4];   // colour where the texture is 1
        bool fontShadow = false;
        float shadowBlack[4] = {0, 0, 0, 0};
        float shadowWhite[4] = {0, 0, 0, 1};

        struct Map {
            int texture = -1;
            int wrapS = 0;
            int wrapT = 0;
        };
        std::vector<Map> maps;

        struct SRT {
            float t[2] = {0, 0};
            float r = 0;
            float s[2] = {1, 1};
        };
        std::vector<SRT> srt;

        int alphaFunc = 1;   // PICA alpha test: 0 never 1 always 2 eq 3 ne 4 lt 5 le 6 gt 7 ge
        float alphaRef = 0;
    };

    struct Pane {
        std::string type;   // pan1 null, pic1 picture, txt1 text, wnd1 window, prt1 part
        std::string name;
        int parent = -1;
        std::vector<int> children;
        float t[3];   // translate
        float r[3];   // rotate (degrees)
        float s[2];   // scale
        float size[2];
        int origin = 0;
        float alpha = 1;
        bool visible = true;
        bool influenced = false;   // children take this pane's alpha
        float vc[4][4];            // vertex colours (LT, RT, LB, RB)
        int material = -1;
        std::array<std::array<float, 2>, 4> uv{};

        // Text.
        std::u16string text;
        float fontSize[2] = {0, 0};
        float charSpace = 0;
        float lineSpace = 0;
        float colors[2][4];   // top and bottom colour
        float shadowOffset[2] = {0, 0};
        float shadowScale[2] = {1, 1};
        float shadowColors[2][4];

        // Window.
        float frameSize[4] = {0, 0, 0, 0};   // left, right, top, bottom
        std::vector<int> frameMaterials;

        // Part.
        std::string partLayout;
    };

    struct Texture {
        std::shared_ptr<captain_toad::gpu::Texture> texture;
        int w = 0;
        int h = 0;
        int imageW = 0;
        int imageH = 0;
        bool alphaOnly = false;
    };

    struct Glyph {
        float x;   // cell position on the sheet
        float y;
        int left;    // left bearing
        int glyph;   // glyph width
        int chr;     // advance
    };

    struct Font {
        std::shared_ptr<captain_toad::gpu::Texture> sheet;
        int sheetW = 0;
        int sheetH = 0;
        int cellW = 0;
        int cellH = 0;
        int height = 0;
        int width = 0;
        int ascent = 0;
        int lineFeed = 0;
        int baseline = 0;
        bool color = false;   // RGBA glyph sheet (icon fonts; UiLayout only)
        std::map<int, Glyph> glyphs;
    };

    // One animated value: a pane or material property selected by tag, index and target.
    struct Target {
        char kind[5];   // FLPA pane SRT, FLVC vertex colour, FLVI visibility, FLMC material colour,
                        // FLFS font shadow, FLAC alpha compare, FLTS texture SRT, FLTP texture pattern
        int pane = -1;
        int material = -1;
        int index = 0;
        int target = 0;
        LayoutCurve curve;
    };

    struct Animation {
        std::string name;
        float frames = 0;
        bool loop = false;
        std::vector<Target> targets;
        std::vector<std::string> textures;
    };

    std::string name;
    float width = 400;
    float height = 240;
    std::vector<Pane> base;    // panes as loaded
    std::vector<Pane> panes;   // panes with the animations applied
    std::vector<Material> baseMaterials;
    std::vector<Material> materials;
    std::vector<Texture> textures;
    std::vector<Font> fonts;
    std::map<std::string, Animation> animations;
    std::vector<std::pair<const Animation*, float>> active;   // applied in order each update
    bool shown = false;
    std::vector<std::string> textureNames;
    std::map<std::string, Layout*>* registry = nullptr;   // part panes draw the named layout

    // UTF-8 -> UTF-16 code units (the text panes index glyphs by UTF-16 code unit).
    static std::u16string utf16(const std::string& s) {
        std::u16string out;
        for (size_t i = 0; i < s.size();) {
            uint32_t c = uint8_t(s[i]);
            int extra = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : 0;
            c &= extra == 3 ? 0x07 : extra == 2 ? 0x0f : extra == 1 ? 0x1f : 0x7f;
            for (int k = 1; k <= extra && i + k < s.size(); k++) {
                c = (c << 6) | (uint8_t(s[i + k]) & 0x3f);
            }
            i += size_t(extra) + 1;
            if (c >= 0x10000) {
                c -= 0x10000;
                out.push_back(char16_t(0xd800 + (c >> 10)));
                out.push_back(char16_t(0xdc00 + (c & 0x3ff)));
            } else {
                out.push_back(char16_t(c));
            }
        }
        return out;
    }

    static void color4(captain_toad::data::JsonView a, float* out) {
        for (int k = 0; k < 4; k++) {
            out[k] = a[k].f() / 255.f;
        }
    }

    int paneIndex(const std::string& n) const {
        for (size_t i = 0; i < base.size(); i++) {
            if (base[i].name == n) {
                return int(i);
            }
        }
        return -1;
    }

    int materialIndex(const std::string& n) const {
        for (size_t i = 0; i < baseMaterials.size(); i++) {
            if (baseMaterials[i].name == n) {
                return int(i);
            }
        }
        return -1;
    }

    // A layout texture (RGBA8, or 8-bit alpha when `alphaOnly`), rows in stored order (first row =
    // PICA v 1) as exported; uploaded bottom-up. Alpha sheets read as (0, 0, 0, alpha).
    static std::shared_ptr<captain_toad::gpu::Texture> upload(const void* pixels, int w, int h,
                                                              bool alphaOnly = false) {
        const int bpp = alphaOnly ? 1 : 4;
        std::vector<unsigned char> rgba(size_t(w) * size_t(h) * 4);
        for (int y = 0; y < h; y++) {
            const unsigned char* row = (const unsigned char*)pixels + size_t(h - 1 - y) * size_t(w) * size_t(bpp);
            unsigned char* out = rgba.data() + size_t(y) * size_t(w) * 4;
            if (alphaOnly) {
                for (int x = 0; x < w; x++) {
                    out[x * 4 + 3] = row[x];
                }
            } else {
                memcpy(out, row, size_t(w) * 4);
            }
        }
        return captain_toad::gpu::device().createTexture(captain_toad::gpu::Format::RGBA8, w, h, rgba.data());
    }

    // ---------------------------------------------------------------------------------------------
    // Loading
    // ---------------------------------------------------------------------------------------------

    void load(captain_toad::data::JsonView d, const std::filesystem::path& package) {
        name = d["layout"]["name"].str();
        width = d["layout"]["width"].f();
        height = d["layout"]["height"].f();
        for (captain_toad::data::JsonView t : d["textures"].items()) {
            textureNames.push_back(t.str());
        }

        // Textures.
        for (captain_toad::data::JsonView t : (d["texture_files"]).items()) {
            const captain_toad::data::Bytes rgba = captain_toad::data::readBytes(package / t["file"].str());
            Texture x;
            x.w = t["width"].i();
            x.h = t["height"].i();
            x.imageW = t["image"][0].i();
            x.imageH = t["image"][1].i();
            int f = t["format"].i();
            x.alphaOnly = f == 1 || f == 13;   // A8, A4
            if (rgba.size() != size_t(x.w) * x.h * 4) {
                throw std::runtime_error("Invalid layout texture");
            }
            x.texture = upload(rgba.data(), x.w, x.h);
            textures.push_back(x);
        }

        // Fonts.
        for (captain_toad::data::JsonView fr : (d["font_files"]).items()) {
            const captain_toad::data::Value fontDocument = captain_toad::data::readJson(package / fr["file"].str());
            captain_toad::data::JsonView f = fontDocument;
            Font x;
            captain_toad::data::JsonView s = f["sheet"];
            x.sheetW = s["width"].i();
            x.sheetH = s["height"].i();
            x.cellW = s["cell_width"].i();
            x.cellH = s["cell_height"].i();
            x.baseline = s["baseline"].i();
            x.height = f["height"].i();
            x.width = f["width"].i();
            x.ascent = f["ascent"].i();
            x.lineFeed = f["line_feed"].i();
            const captain_toad::data::Bytes a = captain_toad::data::readBytes(package / f["sheets"][0].str());
            if (a.size() != size_t(x.sheetW) * x.sheetH) {
                throw std::runtime_error("Invalid font sheet");
            }
            x.sheet = upload(a.data(), x.sheetW, x.sheetH, true);
            for (const std::string& code : f["glyphs"].keys()) {
                captain_toad::data::JsonView g = f["glyphs"][code];
                x.glyphs[std::atoi(code.c_str())] = {g["x"].f(), g["y"].f(), g["left"].i(), g["glyph"].i(),
                                                     g["char"].i()};
            }
            fonts.push_back(x);
        }

        // Materials.
        for (captain_toad::data::JsonView m : (d["materials"]).items()) {
            Material x;
            x.name = m["name"].str();
            color4(m["black"], x.black);
            color4(m["white"], x.white);
            for (captain_toad::data::JsonView t : (m["texmaps"]).items()) {
                x.maps.push_back({t["texture"].i(), t["wrap_s"].i(), t["wrap_t"].i()});
            }
            for (captain_toad::data::JsonView t : (m["texsrt"]).items()) {
                Material::SRT s;
                s.t[0] = t["translate"][0].f();
                s.t[1] = t["translate"][1].f();
                s.r = t["rotate"].f();
                s.s[0] = t["scale"][0].f();
                s.s[1] = t["scale"][1].f();
                x.srt.push_back(s);
            }
            if (captain_toad::data::JsonView ac = m["alpha_compare"]) {
                x.alphaFunc = ac["func"].i();
                x.alphaRef = ac["ref"].f();
            }
            if (captain_toad::data::JsonView fs = m["font_shadow"]) {
                x.fontShadow = true;
                for (int k = 0; k < 3; k++) {
                    x.shadowBlack[k] = fs["black"][k].f() / 255.f;
                }
                x.shadowBlack[3] = 0;
                color4(fs["white"], x.shadowWhite);
            }
            baseMaterials.push_back(x);
        }

        // Pane tree.
        addPane(d["root"], -1);

        // Animations.
        for (const std::string& an : d["animations"].keys()) {
            captain_toad::data::JsonView a = d["animations"][an];
            Animation x;
            x.name = an;
            x.frames = a["frames"].f();
            x.loop = a["loop"].b();
            for (captain_toad::data::JsonView t : a["textures"].items()) {
                x.textures.push_back(t.str());
            }
            for (captain_toad::data::JsonView c : (a["contents"]).items()) {
                const bool material = c["type"].is("material");
                std::string target = c["name"].str();
                for (captain_toad::data::JsonView tag : (c["tags"]).items()) {
                    for (captain_toad::data::JsonView e : (tag["entries"]).items()) {
                        Target t;
                        strncpy(t.kind, tag["tag"].str().c_str(), 4);
                        t.kind[4] = 0;
                        if (material) {
                            t.material = materialIndex(target);
                        } else {
                            t.pane = paneIndex(target);
                        }
                        t.index = e["index"].i();
                        t.target = e["target"].i();
                        t.curve.step = e["curve"].is("step");
                        for (captain_toad::data::JsonView k : (e["keys"]).items()) {
                            t.curve.keys.push_back({k[0].f(), k[1].f(), k.count() > 2 ? k[2].f() : 0.f});
                        }
                        x.targets.push_back(t);
                    }
                }
            }
            animations[x.name] = x;
        }
        panes = base;
        materials = baseMaterials;
    }

    void addPane(captain_toad::data::JsonView p, int parent) {
        Pane x;
        x.type = p["type"].str();
        x.name = p["name"].str();
        x.parent = parent;
        for (int k = 0; k < 3; k++) {
            x.t[k] = p["translate"][k].f();
            x.r[k] = p["rotate"][k].f();
        }
        for (int k = 0; k < 2; k++) {
            x.s[k] = p["scale"][k].f();
            x.size[k] = p["size"][k].f();
        }
        x.origin = p["origin"].i();
        x.alpha = p["alpha"].f() / 255.f;
        x.visible = p["visible"].b();
        x.influenced = p["influenced_alpha"].b();
        for (int c = 0; c < 4; c++) {
            for (int k = 0; k < 4; k++) {
                x.vc[c][k] = 1;
            }
        }
        if (captain_toad::data::JsonView vc = p["vertex_colors"]) {
            for (int c = 0; c < 4; c++) {
                color4(vc[c], x.vc[c]);
            }
        }
        if (p["material"]) {
            x.material = p["material"].i();
        }
        if (captain_toad::data::JsonView tc = p["texcoords"]) {
            if (tc.count()) {
                for (int c = 0; c < 4; c++) {
                    for (int k = 0; k < 2; k++) {
                        x.uv[c][k] = tc[0][c][k].f();
                    }
                }
            }
        }
        if (x.type == "txt1") {
            x.text = utf16(p["text"].str());
            for (int k = 0; k < 2; k++) {
                x.fontSize[k] = p["font_size"][k].f();
                x.shadowOffset[k] = p["shadow_offset"][k].f();
                x.shadowScale[k] = p["shadow_scale"][k].f();
                color4(p["colors"][k], x.colors[k]);
                color4(p["shadow_colors"][k], x.shadowColors[k]);
            }
            x.charSpace = p["char_space"].f();
            x.lineSpace = p["line_space"].f();
        }
        if (x.type == "prt1" && !p["layout"].isNull()) {
            x.partLayout = p["layout"].str();
        }
        if (x.type == "wnd1") {
            for (int k = 0; k < 4; k++) {
                x.frameSize[k] = p["frame_size"][k].f();
            }
            captain_toad::data::JsonView c = p["content"];
            x.material = c["material"].i();
            for (int i = 0; i < 4; i++) {
                color4(c["vertex_colors"][i], x.vc[i]);
            }
            if (c["texcoords"].count()) {
                for (int i = 0; i < 4; i++) {
                    for (int k = 0; k < 2; k++) {
                        x.uv[i][k] = c["texcoords"][0][i][k].f();
                    }
                }
            }
            for (captain_toad::data::JsonView f : (p["frames"]).items()) {
                x.frameMaterials.push_back(f["material"].i());
            }
        }
        base.push_back(x);
        int me = int(base.size()) - 1;
        if (parent >= 0) {
            base[parent].children.push_back(me);
        }
        for (captain_toad::data::JsonView c : (p["children"]).items()) {
            addPane(c, me);
        }
    }

    // ---------------------------------------------------------------------------------------------
    // Animation
    // ---------------------------------------------------------------------------------------------

    // The base state, then each active animation at its frame (later entries override earlier ones).
    void update() {
        panes = base;
        materials = baseMaterials;
        for (auto& [a, frame] : active) {
            for (const auto& t : a->targets) {
                const float v = t.curve.eval(frame);
                const std::string k = t.kind;
                if (t.pane >= 0) {
                    Pane& p = panes[t.pane];
                    if (k == "FLPA") {
                        if (t.target < 3) {
                            p.t[t.target] = v;
                        } else if (t.target < 6) {
                            p.r[t.target - 3] = v;
                        } else if (t.target < 8) {
                            p.s[t.target - 6] = v;
                        } else if (t.target < 10) {
                            p.size[t.target - 8] = v;
                        }
                    } else if (k == "FLVC") {
                        if (t.target < 16) {
                            p.vc[t.target / 4][t.target % 4] = v / 255.f;
                        } else if (t.target == 16) {
                            p.alpha = v / 255.f;
                        }
                    } else if (k == "FLVI") {
                        p.visible = v != 0;
                    }
                }
                if (t.material >= 0) {
                    Material& m = materials[t.material];
                    if (k == "FLMC") {
                        if (t.target < 4) {
                            m.black[t.target] = v / 255.f;
                        } else if (t.target < 8) {
                            m.white[t.target - 4] = v / 255.f;
                        }
                    } else if (k == "FLFS") {
                        if (t.target < 3) {
                            m.shadowBlack[t.target] = v / 255.f;
                        } else if (t.target < 7) {
                            m.shadowWhite[t.target - 3] = v / 255.f;
                        }
                    } else if (k == "FLAC") {
                        if (t.target == 0) {
                            m.alphaRef = v;
                        }
                    } else if (k == "FLTS" && t.index < int(m.srt.size())) {
                        auto& s = m.srt[t.index];
                        if (t.target < 2) {
                            s.t[t.target] = v;
                        } else if (t.target == 2) {
                            s.r = v;
                        } else if (t.target < 5) {
                            s.s[t.target - 3] = v;
                        }
                    } else if (k == "FLTP" && t.index < int(m.maps.size())) {
                        int i = int(v);
                        if (i >= 0 && i < int(a->textures.size())) {
                            for (size_t j = 0; j < textureNames.size(); j++) {
                                if (textureNames[j] == a->textures[i]) {
                                    m.maps[t.index].texture = int(j);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // ---------------------------------------------------------------------------------------------
    // Drawing
    // ---------------------------------------------------------------------------------------------

    // The layout program (one for all layouts) and the draw its material binding prepares.
    static captain_toad::gpu::Program& program() {
        return captain_toad::gpu::device().sharedProgram(
            "layout",
            "in vec2 inPosition;in vec2 inTexCoord0;in vec4 inColor;out vec2 texCoord;out vec4 color;\n"
            "uniform mat4 modelViewProjectionMatrix;\n"
            "void main(){gl_Position=modelViewProjectionMatrix*vec4(inPosition,0.0,1.0);texCoord=inTexCoord0;"
            "color=clamp(inColor,0.0,1.0);}",
            "in vec2 texCoord;in vec4 color;\n"
            "uniform sampler2D tex;uniform int hasTexture,alphaOnly,font,alphaFunc;uniform float alphaRef;uniform vec4 "
            "black,white;\n"
            "void main(){vec4 "
            "t=vec4(1.0);if(hasTexture!=0){t=texture(tex,texCoord);if(alphaOnly!=0)t.rgb=vec3(1.0);}\n"
            " vec4 "
            "b=t;if(font!=0){b.rgb=clamp(2.0*(vec3(t.a)-vec3(127.0/255.0)),0.0,1.0);b.a=clamp(2.0*(t.a*(20.0/"
            "255.0)+t.a),0.0,1.0);}\n"
            " vec4 c=(white*b+black*(vec4(1.0)-b))*color;float a=floor(c.a*255.0+0.5),r=floor(alphaRef*255.0+0.5);\n"
            " bool "
            "pass=alphaFunc==1||(alphaFunc==2&&a==r)||(alphaFunc==3&&a!=r)||(alphaFunc==4&&a<r)||(alphaFunc==5&&a<=r)||"
            "(alphaFunc==6&&a>r)||(alphaFunc==7&&a>=r);\n"
            " if(!pass)discard;fragColor=c;}");
    }
    captain_toad::gpu::TextureBinding binding_;   // the bound material's texture

    // 2D affine product: x' = a0 x + a1 y + a2, y' = a3 x + a4 y + a5.
    static void mul(const float a[6], const float b[6], float o[6]) {
        float r[6] = {a[0] * b[0] + a[1] * b[3], a[0] * b[1] + a[1] * b[4], a[0] * b[2] + a[1] * b[5] + a[2],
                      a[3] * b[0] + a[4] * b[3], a[3] * b[1] + a[4] * b[4], a[3] * b[2] + a[4] * b[5] + a[5]};
        memcpy(o, r, sizeof r);
    }

    void bindMaterial(const Material& m, const float* black, const float* white, bool font,
                      const captain_toad::gpu::Texture* fontSheet) {
        captain_toad::gpu::Program& prog = program();
        binding_ = {};
        bool alphaOnly = false;
        if (font) {
            binding_.texture = fontSheet;
            binding_.sampler.wrapS = binding_.sampler.wrapT =
                captain_toad::gpu::Wrap::Repeat;   // never set: the default
        } else if (!m.maps.empty() && m.maps[0].texture >= 0) {
            const auto& t = textures.at(m.maps[0].texture);
            binding_.texture = t.texture.get();
            alphaOnly = t.alphaOnly;
            static const captain_toad::gpu::Wrap wrap[] = {
                captain_toad::gpu::Wrap::ClampToEdge, captain_toad::gpu::Wrap::Repeat,
                captain_toad::gpu::Wrap::MirroredRepeat, captain_toad::gpu::Wrap::MirroredRepeat};
            binding_.sampler.wrapS = wrap[m.maps[0].wrapS & 3];
            binding_.sampler.wrapT = wrap[m.maps[0].wrapT & 3];
        }
        prog.set("hasTexture", binding_.texture != nullptr);
        prog.set("alphaOnly", alphaOnly);
        prog.set("font", font);
        prog.setFloats("black", black, 4);
        prog.setFloats("white", white, 4);
        prog.set("alphaFunc", font ? 1 : m.alphaFunc);
        prog.set("alphaRef", m.alphaRef);
    }

    // Rotated-texture mapping, then the texture SRT (traced window content: u' = (u - t) * s).
    std::array<float, 2> texSRT(const Material& m, std::array<float, 2> uv) const {
        if (!m.maps.empty() && m.maps[0].texture >= 0) {
            const Texture& t = textures.at(m.maps[0].texture);
            uv = {1.0f - uv[1] * t.imageH / t.w, 1.0f - uv[0] * t.imageW / t.h};
        }
        if (m.srt.empty()) {
            return uv;
        }
        const auto& s = m.srt[0];
        float u = uv[0] - s.t[0];
        float v = uv[1] - s.t[1];
        const float c = std::cos(s.r * float(M_PI) / 180);
        const float sn = std::sin(s.r * float(M_PI) / 180);
        float ru = (u - 0.5f) * c - (v - 0.5f) * sn + 0.5f;
        float rv = (u - 0.5f) * sn + (v - 0.5f) * c + 0.5f;
        if (s.r == 0) {
            ru = u;
            rv = v;
        }
        return {ru * s.s[0], rv * s.s[1]};
    }

    // Quad corners LT, RT, LB, RB in pane space with OpenGL texture coordinates. Layout texture
    // coordinates are PICA coordinates on the stored raster (u along the columns, v up from the
    // last stored row): traced window texels match this mapping exactly (goal-gpu-1020-a), so
    // textures are uploaded as stored and layout coordinates are passed through.
    void quad(const float M[6], const float x0, const float y0, const float x1, const float y1,
              const std::array<std::array<float, 2>, 4>& uv, const float vc[4][4], float alpha) {
        const float px[4] = {x0, x1, x0, x1};
        const float py[4] = {y0, y0, y1, y1};
        const int order[6] = {0, 1, 2, 2, 1, 3};
        captain_toad::gpu::Immediate v;
        v.attributes = {{captain_toad::gpu::Attribute::Position, 2, 0},
                        {captain_toad::gpu::Attribute::TexCoord0, 2, 8},
                        {captain_toad::gpu::Attribute::Color, 4, 16}};
        v.floatsPerVertex = 8;
        for (int i : order) {
            v.vertex({M[0] * px[i] + M[1] * py[i] + M[2], M[3] * px[i] + M[4] * py[i] + M[5], uv[i][0], uv[i][1],
                      vc[i][0], vc[i][1], vc[i][2], vc[i][3] * alpha});
        }
        captain_toad::gpu::Draw d;
        d.program = &program();
        d.state.blend = captain_toad::gpu::BlendState::alpha();
        d.textures[0] = binding_;
        d.samplers = {{"tex", 0}};
        captain_toad::gpu::device().draw(d, v);
    }

    // Draw a pane and its children. `parentAlpha` is the product of the influencing ancestors.
    void drawPane(int index, const float parent[6], float parentAlpha) {
        const Pane& p = panes[index];
        if (!p.visible) {
            return;
        }
        if (getenv("DIORAMA_LAYOUT_DEBUG")) {
            fprintf(stderr, "pane %s %s alpha %.3f inherited %.3f\n", p.type.c_str(), p.name.c_str(), p.alpha,
                    parentAlpha);
        }
        const float rz = p.r[2] * float(M_PI) / 180;
        const float c = std::cos(rz);
        const float s = std::sin(rz);
        const float local[6] = {c * p.s[0], -s * p.s[1], p.t[0], s * p.s[0], c * p.s[1], p.t[1]};
        float M[6];
        mul(parent, local, M);
        const float alpha = p.alpha * parentAlpha;
        const float hw = p.size[0] / 2;   // origin 0: centre (traced window content)
        const float hh = p.size[1] / 2;
        if (p.type == "pic1" && p.material >= 0) {
            const Material& m = materials[p.material];
            bindMaterial(m, m.black, m.white, false, 0);
            std::array<std::array<float, 2>, 4> uv;
            for (int i = 0; i < 4; i++) {
                uv[i] = texSRT(m, p.uv[i]);
            }
            quad(M, -hw, hh, hw, -hh, uv, p.vc, alpha);
        } else if (p.type == "wnd1") {
            drawWindow(p, M, alpha);
        } else if (p.type == "txt1") {
            drawText(p, M, alpha);
        } else if (p.type == "prt1" && registry) {
            auto it = registry->find(p.partLayout);
            if (it != registry->end() && !it->second->panes.empty()) {
                Layout& part = *it->second;
                for (int ch : part.panes[0].children) {
                    part.drawPane(ch, M, alpha);
                }
            }
        }
        // "Influenced alpha": the children take this pane's alpha.
        const float childAlpha = p.influenced ? alpha : parentAlpha;
        for (int ch : p.children) {
            drawPane(ch, M, childAlpha);
        }
    }

    // A window with one frame material ("around" layout, traced goal-gpu-1020-a): the content quad
    // and four pinwheel quads (each a corner plus an edge band). The frame texture coordinates run
    // from 1 at the content edge to 2 at the outer edge (frame material SRT translate -1), flipped
    // per quad.
    void drawWindow(const Pane& p, const float M[6], float alpha) {
        const float hw = p.size[0] / 2;
        const float hh = p.size[1] / 2;
        const float cl = -hw + p.frameSize[0];
        const float cr = hw - p.frameSize[1];
        const float ct = hh - p.frameSize[2];
        const float cb = -hh + p.frameSize[3];
        const Material& cm = materials[p.material];
        bindMaterial(cm, cm.black, cm.white, false, 0);
        std::array<std::array<float, 2>, 4> uv;
        for (int i = 0; i < 4; i++) {
            uv[i] = texSRT(cm, p.uv[i]);
        }
        quad(M, cl, ct, cr, cb, uv, p.vc, alpha);
        if (p.frameMaterials.empty()) {
            return;
        }
        const Material& fm = materials[p.frameMaterials[0]];
        bindMaterial(fm, fm.black, fm.white, false, 0);
        const float fw = p.frameSize[0];
        const float fh = p.frameSize[2];
        float white[4][4];
        for (auto& w : white) {
            for (float& x : w) {
                x = 1;
            }
        }
        // Linear in position across each band: 1 at the content edge, 2 at the outer edge,
        // measured towards the band's corner (sx, sy: corner direction).
        auto band = [&](float x0, float y0, float x1, float y1, float sx, float sy) {
            std::array<std::array<float, 2>, 4> t;
            const float px[4] = {x0, x1, x0, x1};
            const float py[4] = {y0, y0, y1, y1};
            for (int i = 0; i < 4; i++) {
                t[i] = {1.0f + (sx > 0 ? px[i] - cr : cl - px[i]) / fw, 1.0f + (sy > 0 ? py[i] - ct : cb - py[i]) / fh};
            }
            quad(M, x0, y0, x1, y1, t, white, alpha);
        };
        band(cl, hh, hw, ct, 1, 1);       // top band + top-right corner
        band(cr, ct, hw, -hh, 1, -1);     // right band + bottom-right corner
        band(-hw, cb, cr, -hh, -1, -1);   // bottom band + bottom-left corner
        band(-hw, hh, cl, cb, -1, 1);     // left band + top-left corner
    }

    // A single-line, centred text box: shadow pass then text pass, twice (see the file header).
    void drawText(const Pane& p, const float M[6], float alpha) {
        if (p.material < 0 || fonts.empty()) {
            return;
        }
        const Font& f = fonts[0];
        const Material& m = materials[p.material];
        const float sx = p.fontSize[0] / f.width;
        const float sy = p.fontSize[1] / f.height;
        float total = 0;
        for (char16_t ch : p.text) {
            auto it = f.glyphs.find(ch);
            total += (it != f.glyphs.end() ? it->second.chr : 0) * sx + p.charSpace;
        }
        total -= p.charSpace;

        auto pass = [&](const float* black, const float* white, const float colors[2][4], float ox, float oy, float scx,
                        float scy) {
            bindMaterial(m, black, white, true, f.sheet.get());
            float x = -total / 2;
            const float top = f.lineFeed * sy / 2;
            for (char16_t ch : p.text) {
                auto it = f.glyphs.find(ch);
                if (it == f.glyphs.end()) {
                    continue;
                }
                const Glyph& g = it->second;
                // Traced glyph quads (goal-gpu-950-a): glyph width + 1 texels from cell + 0.5, cell
                // height + 1 rows, the quad starting half a texel left of the glyph's left offset.
                const float gx = x + (g.left - 0.5f) * sx;
                const float gw = (g.glyph + 1) * sx;
                const float gh = (f.cellH + 1) * sy;
                const float cx = gx + gw / 2;
                const float cy = top - gh / 2;
                const float x0 = cx - gw * scx / 2 + ox;
                const float x1 = cx + gw * scx / 2 + ox;
                const float y0 = cy + gh * scy / 2 + oy;
                const float y1 = cy - gh * scy / 2 + oy;
                const float u0 = (g.x + 0.5f) / f.sheetW;
                const float u1 = (g.x + 0.5f + g.glyph + 1) / f.sheetW;
                const float v0 = (g.y + 0.5f) / f.sheetH;
                const float v1 = (g.y + 0.5f + f.cellH + 1) / f.sheetH;
                float vc[4][4];
                for (int k = 0; k < 4; k++) {
                    vc[0][k] = vc[1][k] = colors[0][k];
                    vc[2][k] = vc[3][k] = colors[1][k];
                }
                quad(M, x0, y0, x1, y1, {{{u0, 1 - v0}, {u1, 1 - v0}, {u0, 1 - v1}, {u1, 1 - v1}}}, vc, alpha);
                x += g.chr * sx + p.charSpace;
            }
        };

        // Traced text constants: black alpha 0 (the material's black alpha is unused), shadow black
        // alpha 0.
        const float black[4] = {m.black[0], m.black[1], m.black[2], 0};
        for (int twice = 0; twice < 2; twice++) {
            if (m.fontShadow) {
                pass(m.shadowBlack, m.shadowWhite, p.shadowColors, p.shadowOffset[0], p.shadowOffset[1],
                     p.shadowScale[0], p.shadowScale[1]);
            }
            float white[4] = {m.white[0], m.white[1], m.white[2], m.white[3]};
            if (twice == 0) {
                for (int k = 0; k < 3; k++) {
                    white[k] = m.black[k];
                }
            }
            pass(black, white, p.colors, 0, 0, 1, 1);
        }
    }

    // Draw into the current viewport (the 400 x 240 top screen), y up, origin at the centre.
    void draw() {
        if (!shown || panes.empty()) {
            return;
        }
        const auto mvp = captain_toad::gpu::modelViewProjection(
            captain_toad::gpu::Mat4::ortho(-width / 2, width / 2, -height / 2, height / 2, -1, 1),
            captain_toad::gpu::Mat4());
        program().setFloats("modelViewProjectionMatrix", mvp.data(), 16);
        const float I[6] = {1, 0, 0, 0, 1, 0};
        drawPane(0, I, 1.0f);
    }
};
