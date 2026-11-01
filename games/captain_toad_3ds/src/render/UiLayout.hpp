// =============================================================================================
// UiLayout.hpp - the game's 2D screens: layouts, their parts and animations, on either screen
// =============================================================================================
//
// The front end (title, course book, menus, HUD) is made of layouts (NW layout version 7,
// converted by importer/UiExport.cpp): trees of panes (null, picture, text, window and part
// panes) with materials, fonts and keyframe animations. This runtime is a presentation service
// in the role of the game's layout library: the game logic (session/FrontEnd.hpp) starts named
// animations ("actions") and sets texts and textures; the runtime evaluates and draws.
//
// Model (ROM data and traced behaviour, see .analysis/docs/frontend.md):
//   * An animation's first tag group is its slot ("MainBook", "Curtain", "SetPageType", ...).
//     Starting an animation replaces the one playing in its slot; the slots play side by side
//     (traced startAction 0x2fad5c calls: e.g. OpenBook and Curtain_INTRO on CourseBook_TOP).
//   * Frames advance by 2 per 30 Hz tick, after they are applied (as for the opening's layouts,
//     calc 0x37f328); one-time animations stop at their last frame and report the end.
//   * Each part pane holds its own instance of the part's layout with its own slots.
//   * A screen definition gives the layout a perspective projection (field of view in degrees)
//     or an orthographic one; the perspective camera looks at the layout plane from the distance
//     where one unit is one pixel (hypothesis, to be checked against captures).
//   * Pane transform: translate, then rotate Z, Y, X, then scale (hypothesis for the 3D order;
//     2D rotation matches the opening's traced layouts).
//   * Material colour model as traced for the opening (LayoutRenderer.hpp): the first texture
//     interpolates the material's black and white colours, times the vertex colour.
// Not yet covered: custom combiner stages beyond the first texture, indirect textures, per-character
// text animation and text tags (colour, ruby), the console's system font.
// =============================================================================================
#pragma once
#include "data/Package.hpp"
#include "render/LayoutRenderer.hpp"

#include <array>
#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace captain_toad::ui {

using Mat4 = std::array<float, 16>;   // column-major

inline Mat4 identity() {
    return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}

inline Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 c{};
    for (int col = 0; col < 4; col++) {
        for (int row = 0; row < 4; row++) {
            float v = 0;
            for (int k = 0; k < 4; k++) {
                v += a[k * 4 + row] * b[col * 4 + k];
            }
            c[col * 4 + row] = v;
        }
    }
    return c;
}

inline Mat4 translation(float x, float y, float z) {
    Mat4 m = identity();
    m[12] = x;
    m[13] = y;
    m[14] = z;
    return m;
}

inline Mat4 rotation(int axis, float degrees) {
    const float r = degrees * float(M_PI) / 180, c = std::cos(r), s = std::sin(r);
    Mat4 m = identity();
    const int a = (axis + 1) % 3, b = (axis + 2) % 3;
    m[a * 4 + a] = c;
    m[a * 4 + b] = s;
    m[b * 4 + a] = -s;
    m[b * 4 + b] = c;
    return m;
}

inline Mat4 scaling(float x, float y) {
    Mat4 m = identity();
    m[0] = x;
    m[5] = y;
    return m;
}

// UTF-8 -> UTF-16 code units (glyphs are indexed by UTF-16 code unit).
inline std::u16string utf16(const std::string& s) {
    return Layout::utf16(s);
}

// ---------------------------------------------------------------------------------------------
// Shared resources of a UI package: textures and fonts by name, layouts and messages.
// ---------------------------------------------------------------------------------------------

struct Texture {
    std::shared_ptr<captain_toad::gpu::Texture> texture;
    int w = 0, h = 0, imageW = 0, imageH = 0;
    bool alphaOnly = false;
    bool rotated = false;
    bool topDown = false;   // rows stored top first (texture-pack images), not as BFLIM rasters
};

struct Material {
    std::string name;
    float black[4];
    float white[4];
    bool fontShadow = false;
    float shadowBlack[4] = {0, 0, 0, 0};
    float shadowWhite[4] = {0, 0, 0, 1};
    struct Map {
        int texture = -1;   // index into the layout's texture list
        int wrapS = 0, wrapT = 0;
        int packageTexture = -1;   // a texture set by the game (Instance::setTexture), if any
    };
    std::vector<Map> maps;
    struct SRT {
        float t[2] = {0, 0};
        float r = 0;
        float s[2] = {1, 1};
    };
    std::vector<SRT> srt;
    int alphaFunc = 1;
    float alphaRef = 0;
    int texgenSource = 0;       // first texture's coordinates: 0-2 texture coordinates, 3-5 projections
    std::vector<int> sources;   // each map's coordinate source
    // Combiner stages (BFLYT 4-byte records): stage k combines the result so far with map k + 1 by
    // its colour and alpha modes (0 replace, 1 modulate, 2 add, 3 add signed, 5 subtract).
    std::vector<std::array<int, 2>> stages;
};

struct PaneDef {
    std::string type, name;
    int parent = -1;
    std::vector<int> children;
    float t[3], r[3], s[2], size[2];
    int origin = 0;   // base position: x (0 centre, 1 left, 2 right) + 4 * y (0 centre, 1 top, 2 bottom)
    float alpha = 1;
    bool visible = true, influenced = false;
    float vc[4][4];
    int material = -1;
    std::array<std::array<float, 2>, 4> uv{};
    // Text.
    std::u16string text;
    std::string textId;
    int font = 0;
    int textPosition = 0;    // like origin: the text block's place in the box
    int textAlignment = 0;   // 0 as the position, 1 left, 2 centre, 3 right
    float fontSize[2] = {0, 0};
    float charSpace = 0, lineSpace = 0;
    float colors[2][4];
    float shadowOffset[2] = {0, 0}, shadowScale[2] = {1, 1};
    float shadowColors[2][4];
    // Window.
    float frameSize[4] = {0, 0, 0, 0};
    std::vector<int> frameMaterials;
    std::vector<int> frameFlips;   // per frame: 0 none, 1 horizontal, 2 vertical (layout data)
    int windowFlags = 0;           // bit 0 one material for all, bit 1 vertex colours for all, bits 2-3 kind
    // Part.
    std::string partLayout;
    float partScale[2] = {1, 1};
    std::map<std::string, std::u16string> partTexts;   // overridden texts of the part's panes
};

struct Animation {
    std::string name, slot;            // slot: the tag's first group (the old one-slot-per-group API)
    std::vector<std::string> groups;   // the tag's groups (BFLAN pat1)
    bool childBinding = false;         // the groups' panes' children are bound as well
    float frames = 0;
    bool loop = false;
    std::vector<Layout::Target> targets;
    std::vector<std::string> textures;
};

struct LayoutDef {
    std::string name;
    float width = 400, height = 240;
    std::vector<PaneDef> panes;
    std::vector<Material> materials;
    std::vector<int> textures;   // package texture index per layout texture
    std::vector<std::string> textureNames;
    std::vector<int> fonts;   // package font index per layout font
    std::map<std::string, Animation> animations;
    std::map<std::string, std::vector<int>> groups;   // layout groups: pane indices by group name

    int paneIndex(const std::string& n) const {
        for (size_t i = 0; i < panes.size(); i++) {
            if (panes[i].name == n) {
                return int(i);
            }
        }
        return -1;
    }
};

struct ScreenDef {
    std::string name, top, bottom;
    float topFov = 0, bottomFov = 0;
};

class Package {
public:
    explicit Package(std::filesystem::path dir) : dir_(std::move(dir)) {
        const captain_toad::data::Value index = captain_toad::data::readJson(dir_ / "ui.json");
        captain_toad::data::JsonView u = index;
        for (captain_toad::data::JsonView s : u["definition"]["ui"]["lyt"].items()) {
            ScreenDef d;
            d.name = s["name"].str();
            if (captain_toad::data::JsonView t = s["top"]) {
                d.top = t["lyt"].str();
                d.topFov = t["ortho"].b() ? 0 : t["fov"].f();
            }
            if (captain_toad::data::JsonView b = s["bot"]) {
                d.bottom = b["lyt"].str();
                d.bottomFov = b["ortho"].b() ? 0 : b["fov"].f();
            }
            screens_[d.name] = d;
        }
        for (captain_toad::data::JsonView l : u["layouts"].items()) {
            layoutFiles_[l["name"].str()] = l["file"].str();
        }
        for (const std::string& k : u["packs"].keys()) {
            captain_toad::data::JsonView t = u["packs"][k];
            packs_[k] = {t["file"].str(), t["width"].i(), t["height"].i()};
        }
    }

    const ScreenDef* screen(const std::string& name) const {
        auto it = screens_.find(name);
        return it == screens_.end() ? nullptr : &it->second;
    }

    bool hasLayout(const std::string& name) const { return layoutFiles_.count(name) != 0; }

    const LayoutDef& layout(const std::string& name) {
        auto it = layouts_.find(name);
        if (it != layouts_.end()) {
            return *it->second;
        }
        auto f = layoutFiles_.find(name);
        if (f == layoutFiles_.end()) {
            throw std::runtime_error("Unknown layout " + name);
        }
        auto def = std::make_unique<LayoutDef>();
        load(*def, captain_toad::data::readJson(dir_ / f->second));
        return *(layouts_[name] = std::move(def));
    }

    const Texture& texture(int i) const { return textures_.at(size_t(i)); }
    const Layout::Font& font(int i) const { return fonts_.at(size_t(i)); }

    // A message (MSBT label) of the package, as UTF-16; empty when unknown. `file` is
    // "LayoutMessage/<name>" or "SystemMessage/<name>"; a bare name looks in SystemMessage.
    std::u16string message(const std::string& file, const std::string& label) {
        auto& m = messages(file.find('/') == std::string::npos ? "SystemMessage/" + file : file);
        auto it = m.find(label);
        return it == m.end() ? std::u16string() : it->second;
    }

    // Overrides a layout texture by name (course thumbnails, illustrations).
    int addTexture(const std::string& name, const std::vector<uint8_t>& rgba, int w, int h);
    // A texture of the package's texture packs ("Screenshots/PullUpStage_L"), or -1.
    int packTexture(const std::string& name);

private:
    std::map<std::string, std::u16string>& messages(const std::string& file) {
        auto it = messages_.find(file);
        if (it != messages_.end()) {
            return it->second;
        }
        auto& out = messages_[file];
        const auto path = dir_ / "messages" / (file + ".json");
        if (!std::filesystem::exists(path)) {
            return out;
        }
        const captain_toad::data::Value doc = captain_toad::data::readJson(path);
        captain_toad::data::JsonView d = doc;
        for (const std::string& label : d["labels"].keys()) {
            captain_toad::data::JsonView runs = d["texts"][d["labels"][label].i()];
            std::string text;
            for (captain_toad::data::JsonView r : runs.items()) {
                if (r.isString()) {
                    text += r.str();
                }
            }
            out[label] = utf16(text);
        }
        return out;
    }

    int textureByName(captain_toad::data::JsonView t) {
        const std::string file = t["file"].str();
        auto it = textureFiles_.find(file);
        if (it != textureFiles_.end()) {
            return it->second;
        }
        Texture x;
        x.w = t["width"].i();
        x.h = t["height"].i();
        x.imageW = t["image"][0].i();
        x.imageH = t["image"][1].i();
        const int f = t["format"].i();
        x.alphaOnly = f == 1 || f == 13;
        x.rotated = t["rotated"].b();
        const captain_toad::data::Bytes rgba = captain_toad::data::readBytes(dir_ / file);
        if (rgba.size() != size_t(x.w) * x.h * 4) {
            throw std::runtime_error("Invalid layout texture " + file);
        }
        x.texture = Layout::upload(rgba.data(), x.w, x.h);
        textures_.push_back(x);
        return textureFiles_[file] = int(textures_.size()) - 1;
    }

    int fontByName(captain_toad::data::JsonView fr) {
        const std::string file = fr["file"].str();
        auto it = fontFiles_.find(file);
        if (it != fontFiles_.end()) {
            return it->second;
        }
        const captain_toad::data::Value doc = captain_toad::data::readJson(dir_ / file);
        captain_toad::data::JsonView f = doc;
        Layout::Font x;
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
        const captain_toad::data::Bytes a = captain_toad::data::readBytes(dir_ / f["sheets"][0].str());
        x.color = f["color"].b();
        x.sheet = Layout::upload(a.data(), x.sheetW, x.sheetH, !x.color);
        for (const std::string& code : f["glyphs"].keys()) {
            captain_toad::data::JsonView g = f["glyphs"][code];
            x.glyphs[std::atoi(code.c_str())] = {g["x"].f(), g["y"].f(), g["left"].i(), g["glyph"].i(), g["char"].i()};
        }
        fonts_.push_back(x);
        return fontFiles_[file] = int(fonts_.size()) - 1;
    }

    void load(LayoutDef& L, captain_toad::data::JsonView d);
    void addPane(LayoutDef& L, captain_toad::data::JsonView p, int parent);

    std::filesystem::path dir_;
    std::map<std::string, ScreenDef> screens_;
    std::map<std::string, std::string> layoutFiles_;
    std::map<std::string, std::unique_ptr<LayoutDef>> layouts_;
    std::vector<Texture> textures_;
    std::map<std::string, int> textureFiles_;
    std::vector<Layout::Font> fonts_;
    std::map<std::string, int> fontFiles_;
    std::map<std::string, std::map<std::string, std::u16string>> messages_;
    struct PackEntry {
        std::string file;
        int w, h;
    };
    std::map<std::string, PackEntry> packs_;
    std::map<std::string, int> packTextures_;
};

// ---------------------------------------------------------------------------------------------
// A layout instance: pane and material state, animation slots and part instances.
// ---------------------------------------------------------------------------------------------

class Instance {
public:
    Instance(Package& pkg, const std::string& layout);

    const LayoutDef& def() const { return *def_; }

    // ---- The game's layout actions (layout actor 0x2fad5c and its queries) ----------------------
    // Starts `tag` in `slot` (0x2fad5c). The slot names a group of the animation's tag: the
    // animation is bound to that group's panes; an empty or unknown slot binds the whole
    // animation (0x26e10c). Each slot holds one animation; a start replaces the slot's previous
    // one (0x26e0ec), and pane values persist after an animation is replaced or stopped. Returns
    // false (no effect) when the layout has no such animation: the code also calls tags of the
    // original layouts that the redesigned ones lack.
    bool start(const std::string& tag, const std::string& slot = "", float startFrame = 0);
    // Binds `tag` held at `frame` without playing it (0x2d5318 / 0x26df5c).
    bool hold(const std::string& tag, const std::string& slot, float frame);
    // Stops the animations of `tag` (0x2f32f4); the panes keep their values.
    void stop(const std::string& tag, const std::string& slot = "");   // "": every slot
    // True when `tag`'s animation has ended (or is within `margin` frames of its end); without a
    // tag, the most recently started animation (0x2fa6c4). True when nothing is bound.
    bool ended(const std::string& tag = "", float margin = 0) const;
    // True while `tag` plays (bound and not ended) (0x2fa980).
    bool playing(const std::string& tag) const;
    // Writes a pane's visibility (0x2f8358): animations may change it again later.
    void writeVisible(const std::string& paneName, bool visible);
    // A pane's current visibility flag (0x2aa998); false when there is no such pane.
    bool paneVisible(const std::string& paneName) const {
        const int i = paneName == "root" ? (def_->panes.empty() ? -1 : 0) : def_->paneIndex(paneName);
        return i >= 0 && panes_[size_t(i)].visible;
    }
    // An actor's layout is drawn only after the actor appeared (0x2faafc sets +0x34).
    void setShown(bool shown) { shown_ = shown; }
    bool shown() const { return shown_; }

    // ---- Simplified API (one slot per first tag group) ------------------------------------------
    // Starts an animation by tag (no effect when the layout lacks it); returns false then.
    bool startAction(const std::string& tag);
    // Starts the animation at a given frame and holds it there (one-frame "Set..." states).
    bool setFrame(const std::string& tag, float frame);
    // True when the slot's one-time animation has reached its end (or nothing plays in it).
    bool isActionEnd(const std::string& slot) const;
    // The slot of an animation tag ("" when unknown).
    std::string slotOf(const std::string& tag) const;
    // Applies the playing frames to the panes, then advances them by `delta` frames.
    void calc(float delta = 2);
    // Enhancement (drawing between ticks): applies the playing animations `offset` frames from
    // their current frames (clamped to the animation; ended and held ones as they are) without
    // moving them on. The next calc applies the real frames again.
    void applyAt(float offset);

    // A part pane's instance (by the part pane's name), or nullptr.
    Instance* part(const std::string& paneName);
    // The position (layout units) of this instance when it is drawn as a part, replacing the part
    // pane's own translation (the book's page actors' x / y at +0x68 / +0x6c, set by the book
    // 0x137050; the right page pane's own (15, 4) does not show in the original).
    void setOffset(float x, float y) {
        offset_[0] = x;
        offset_[1] = y;
        hasOffset_ = true;
    }
    // Draws these instances (in order) at the part pane instead of its own instance; an empty
    // list restores the pane's own part.
    void setPartInstances(const std::string& paneName, std::vector<Instance*> instances);
    void setText(const std::string& paneName, const std::u16string& text);
    void setVisible(const std::string& paneName, bool visible);
    // Replaces the texture of a picture pane's material (first map).
    void setTexture(const std::string& paneName, int packageTexture);
    // True when the layout point (x, y) of an orthographic screen lies in the pane's rectangle as
    // last drawn (touch hit tests on "Hit" bounding panes).
    bool contains(const std::string& paneName, float x, float y) const;
    // The pane's world matrix of the last draw (for touch hit tests), and its size.
    bool paneRect(const std::string& paneName, Mat4& world, float size[2]) const;

    // Draws into the current viewport: perspective (fov degrees) or orthographic (fov 0).
    void drawScreen(float fov);
    void draw(const Mat4& parent, float parentAlpha);

private:
    struct Player {
        const Animation* anim = nullptr;
        float frame = 0;
        bool hold = false;
        bool ended = false;
        uint64_t order = 0;
        std::vector<int> bound;   // indices of the animation's targets bound by the slot's group
        bool all = true;          // bound to the whole animation
    };
    struct PaneState {
        float t[3], r[3], s[2], size[2];
        float alpha;
        bool visible;
        float vc[4][4];
        Mat4 world;
    };

    void apply();
    void resetToBindPose();
    Player& bind(const Animation& a, const std::string& slotKey, const std::string& group);
    void drawPane(int index, const Mat4& parent, float parentAlpha);
    void bindMaterial(const Material& m, const float* black, const float* white, bool font,
                      const captain_toad::gpu::Texture* fontSheet);
    const Texture* mapTexture(const Material& m, size_t map = 0) const;
    std::array<float, 2> texSRT(const Material& m, std::array<float, 2> uv) const { return texSRT(m, 0, uv); }
    std::array<float, 2> texSRT(const Material& m, size_t map, std::array<float, 2> uv) const;
    // Texture coordinates of maps 1 and 2 for the next quad (multi-texture materials).
    std::array<std::array<float, 2>, 4> extraUv_[2];
    int extraMaps_ = 0;
    void quad(const Mat4& M, float x0, float y0, float x1, float y1, const std::array<std::array<float, 2>, 4>& uv,
              const float vc[4][4], float alpha);
    void drawWindow(int index, const Mat4& M, float alpha);
    void drawText(int index, const Mat4& M, float alpha);
    static captain_toad::gpu::Program& program();
    // The bound material's textures (units 0-2).
    std::array<captain_toad::gpu::TextureBinding, 3> bindings_{};

    Package& pkg_;
    const LayoutDef* def_;
    std::vector<PaneState> panes_;
    std::vector<Material> materials_;
    std::map<std::string, Player> slots_;
    uint64_t order_ = 0;
    std::string lastSlot_;   // the slot of the most recent start (isActionEnd without a tag)
    bool shown_ = true;
    std::map<int, std::unique_ptr<Instance>> parts_;
    std::map<int, std::vector<Instance*>> partInstances_;
    float offset_[2] = {0, 0};
    bool hasOffset_ = false;
    std::map<int, std::u16string> texts_;
    std::map<int, bool> visibility_;
    std::map<int, int> textureOverride_;
};

}   // namespace captain_toad::ui
