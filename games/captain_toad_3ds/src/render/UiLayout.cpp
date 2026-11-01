// See UiLayout.hpp.
#include "render/UiLayout.hpp"

#include <algorithm>
#include <functional>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace captain_toad::ui {

namespace {

void color4(captain_toad::data::JsonView a, float* out) {
    for (int k = 0; k < 4; k++) {
        out[k] = a[k].f() / 255.f;
    }
}

// The quad of a pane of size (w, h) for its base position, in pane space (y up).
void paneQuad(int origin, float w, float h, float& x0, float& y0, float& x1, float& y1) {
    const int hx = origin & 3, vy = (origin >> 2) & 3;
    x0 = hx == 1 ? 0 : hx == 2 ? -w : -w / 2;
    x1 = x0 + w;
    y0 = vy == 1 ? 0 : vy == 2 ? h : h / 2;
    y1 = y0 - h;
}

}   // namespace

// ---- loading ------------------------------------------------------------------------------------

int Package::addTexture(const std::string& name, const std::vector<uint8_t>& rgba, int w, int h) {
    Texture x;
    x.w = x.imageW = w;
    x.h = x.imageH = h;
    x.topDown = true;
    x.texture = Layout::upload(rgba.data(), w, h);
    textures_.push_back(x);
    return textureFiles_["override:" + name] = int(textures_.size()) - 1;
}

int Package::packTexture(const std::string& name) {
    if (auto it = packTextures_.find(name); it != packTextures_.end()) {
        return it->second;
    }
    auto p = packs_.find(name);
    if (p == packs_.end()) {
        return packTextures_[name] = -1;
    }
    const captain_toad::data::Bytes rgba = captain_toad::data::readBytes(dir_ / p->second.file);
    return packTextures_[name] = addTexture(name, rgba, p->second.w, p->second.h);
}

void Package::load(LayoutDef& L, captain_toad::data::JsonView d) {
    L.name = d["layout"]["name"].str();
    L.width = d["layout"]["width"].f();
    L.height = d["layout"]["height"].f();
    for (captain_toad::data::JsonView t : d["textures"].items()) {
        L.textureNames.push_back(t.str());
    }
    for (captain_toad::data::JsonView t : d["texture_files"].items()) {
        L.textures.push_back(t.isNull() ? -1 : textureByName(t));
    }
    for (captain_toad::data::JsonView f : d["font_files"].items()) {
        L.fonts.push_back(f.isNull() ? -1 : fontByName(f));
    }
    for (captain_toad::data::JsonView m : d["materials"].items()) {
        Material x;
        x.name = m["name"].str();
        color4(m["black"], x.black);
        color4(m["white"], x.white);
        for (captain_toad::data::JsonView t : m["texmaps"].items()) {
            x.maps.push_back({t["texture"].i(), t["wrap_s"].i(), t["wrap_t"].i()});
        }
        for (captain_toad::data::JsonView t : m["texsrt"].items()) {
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
        if (m["texgen"].count()) {
            x.texgenSource = m["texgen"][0]["source"].i();
        }
        for (captain_toad::data::JsonView g : m["texgen"].items()) {
            x.sources.push_back(g["source"].i());
        }
        for (captain_toad::data::JsonView t : m["tev"].items()) {
            if (t.isObject()) {
                x.stages.push_back({t["color"].i(), t["alpha"].i()});
            }
        }
        L.materials.push_back(x);
    }
    if (d["root"]) {
        addPane(L, d["root"], -1);
    }
    // Layout groups (nested groups are groups of their own).
    std::function<void(captain_toad::data::JsonView)> addGroup = [&](captain_toad::data::JsonView g) {
        std::vector<int>& panes = L.groups[g["name"].str()];
        for (captain_toad::data::JsonView p : g["panes"].items()) {
            const int i = L.paneIndex(p.str());
            if (i >= 0) {
                panes.push_back(i);
            }
        }
        for (captain_toad::data::JsonView c : g["children"].items()) {
            addGroup(c);
        }
    };
    for (captain_toad::data::JsonView g : d["groups"].items()) {
        addGroup(g);
    }
    for (const std::string& an : d["animations"].keys()) {
        captain_toad::data::JsonView a = d["animations"][an];
        Animation x;
        x.name = an;
        x.frames = a["frames"].f();
        x.loop = a["loop"].b();
        if (a["tag"] && a["tag"]["groups"].count()) {
            x.slot = a["tag"]["groups"][0].str();
            for (captain_toad::data::JsonView g : a["tag"]["groups"].items()) {
                if (!g.str().empty()) {
                    x.groups.push_back(g.str());
                }
            }
            x.childBinding = a["tag"]["child_binding"].i() != 0;
        }
        for (captain_toad::data::JsonView t : a["textures"].items()) {
            x.textures.push_back(t.str());
        }
        for (captain_toad::data::JsonView c : a["contents"].items()) {
            const bool material = c["type"].is("material");
            const std::string target = c["name"].str();
            int index = -1;
            if (material) {
                for (size_t i = 0; i < L.materials.size(); i++) {
                    if (L.materials[i].name == target) {
                        index = int(i);
                    }
                }
            } else {
                index = L.paneIndex(target);
            }
            if (index < 0) {
                continue;
            }
            for (captain_toad::data::JsonView tag : c["tags"].items()) {
                for (captain_toad::data::JsonView e : tag["entries"].items()) {
                    Layout::Target t;
                    strncpy(t.kind, tag["tag"].str().c_str(), 4);
                    t.kind[4] = 0;
                    (material ? t.material : t.pane) = index;
                    t.index = e["index"].i();
                    t.target = e["target"].i();
                    t.curve.step = e["curve"].is("step");
                    for (captain_toad::data::JsonView k : e["keys"].items()) {
                        t.curve.keys.push_back({k[0].f(), k[1].f(), k.count() > 2 ? k[2].f() : 0.f});
                    }
                    x.targets.push_back(t);
                }
            }
        }
        L.animations[an] = x;
    }
}

void Package::addPane(LayoutDef& L, captain_toad::data::JsonView p, int parent) {
    PaneDef x;
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
    for (auto& c : x.vc) {
        for (float& k : c) {
            k = 1;
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
        x.textId = p["text_id"].str();
        x.font = p["font"].i();
        x.textPosition = p["text_position"].i();
        x.textAlignment = p["text_alignment"].i();
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
        for (captain_toad::data::JsonView f : p["frames"].items()) {
            x.frameMaterials.push_back(f["material"].i());
            x.frameFlips.push_back(f["flip"].i());
        }
        x.windowFlags = p["window_flags"].i();
    }
    if (x.type == "prt1" && p["layout"]) {
        x.partLayout = p["layout"].str();
        for (int k = 0; k < 2; k++) {
            x.partScale[k] = p["part_scale_xy"][k].f();
        }
        for (captain_toad::data::JsonView o : p["overrides"].items()) {
            if (captain_toad::data::JsonView op = o["pane"]) {
                if (op["type"].is("txt1") && !op["text"].str().empty()) {
                    x.partTexts[o["name"].str()] = utf16(op["text"].str());
                }
            }
        }
    }
    L.panes.push_back(x);
    const int me = int(L.panes.size()) - 1;
    if (parent >= 0) {
        L.panes[size_t(parent)].children.push_back(me);
    }
    for (captain_toad::data::JsonView c : p["children"].items()) {
        addPane(L, c, me);
    }
}

// ---- instance -----------------------------------------------------------------------------------

Instance::Instance(Package& pkg, const std::string& layout) : pkg_(pkg), def_(&pkg.layout(layout)) {
    materials_ = def_->materials;
    panes_.resize(def_->panes.size());
    for (size_t i = 0; i < def_->panes.size(); i++) {
        const PaneDef& p = def_->panes[i];
        if (p.type == "prt1" && !p.partLayout.empty() && pkg.hasLayout(p.partLayout)) {
            auto inst = std::make_unique<Instance>(pkg, p.partLayout);
            for (const auto& [pane, text] : p.partTexts) {
                inst->setText(pane, text);
            }
            parts_[int(i)] = std::move(inst);
        }
        // Text panes with a message id show the layout's message (LayoutMessage <layout>.msbt).
        // Message ids: "#X" or "X" is label <Layout>_X of the layout's message file; ids starting
        // with '@' (@style, @unused, @raw) mark texts the game sets itself.
        if (p.type == "txt1" && !p.textId.empty() && p.textId[0] != '@') {
            const std::string id = p.textId[0] == '#' ? p.textId.substr(1) : p.textId;
            // The message file is named after the layout, or after its screen for _TOP / _BOTTOM
            // layouts (HeadMiss.msbt for HeadMiss_TOP).
            std::string file = def_->name;
            std::u16string m = pkg.message("LayoutMessage/" + file, file + "_" + id);
            for (const char* suffix : {"_TOP", "_BOTTOM", "_BOT"}) {
                const std::string sfx = suffix;
                if (m.empty() && file.size() > sfx.size() &&
                    file.compare(file.size() - sfx.size(), sfx.size(), sfx) == 0) {
                    const std::string screen = file.substr(0, file.size() - sfx.size());
                    m = pkg.message("LayoutMessage/" + screen, screen + "_" + id);
                }
            }
            if (!m.empty()) {
                texts_[int(i)] = m;
            }
        }
    }
    resetToBindPose();
    apply();
}

std::string Instance::slotOf(const std::string& tag) const {
    auto it = def_->animations.find(tag);
    return it == def_->animations.end() ? std::string() : it->second.slot;
}

Instance::Player& Instance::bind(const Animation& a, const std::string& slotKey, const std::string& group) {
    Player& p = slots_[slotKey];
    p = Player{};
    p.anim = &a;
    p.order = ++order_;
    lastSlot_ = slotKey;
    // 0x26e10c: a slot that names one of the tag's groups binds that group only; otherwise the
    // whole animation is bound.
    if (!group.empty() && std::find(a.groups.begin(), a.groups.end(), group) != a.groups.end()) {
        auto g = def_->groups.find(group);
        std::vector<bool> inGroup(def_->panes.size(), false);
        if (g != def_->groups.end()) {
            for (int i : g->second) {
                inGroup[size_t(i)] = true;
                if (a.childBinding) {
                    std::function<void(int)> mark = [&](int q) {
                        for (int c : def_->panes[size_t(q)].children) {
                            inGroup[size_t(c)] = true;
                            mark(c);
                        }
                    };
                    mark(i);
                }
            }
        }
        p.all = false;
        for (size_t ti = 0; ti < a.targets.size(); ti++) {
            const auto& t = a.targets[ti];
            bool bound = t.pane >= 0 && inGroup[size_t(t.pane)];
            if (t.material >= 0) {
                for (size_t i = 0; i < def_->panes.size() && !bound; i++) {
                    bound = inGroup[i] && def_->panes[i].material == t.material;
                }
            }
            if (bound) {
                p.bound.push_back(int(ti));
            }
        }
    }
    return p;
}

bool Instance::start(const std::string& tag, const std::string& slot, float startFrame) {
    // "part.tag" addresses the named part pane's instance (traced "counters.LifeCounter_EPISODE1"
    // on CourseBook_BOTTOM).
    if (const size_t dot = tag.find('.'); dot != std::string::npos) {
        Instance* p = part(tag.substr(0, dot));
        return p && p->start(tag.substr(dot + 1), slot, startFrame);
    }
    auto it = def_->animations.find(tag);
    if (it == def_->animations.end()) {
        return false;
    }
    // The animator list (0x37dc28): an animation that is still active in its slot is not
    // restarted (a loop keeps its phase, a running one-shot continues); only a start frame above 0
    // sets the frame (0x2fad5c / 0x3827d4).
    if (auto cur = slots_.find(slot); startFrame <= 0 && cur != slots_.end() && cur->second.anim == &it->second &&
                                      !cur->second.hold && (it->second.loop || cur->second.frame < it->second.frames)) {
        cur->second.order = ++order_;
        lastSlot_ = slot;
        return true;
    }
    Player& p = bind(it->second, slot, slot);
    p.frame = std::max(0.f, std::min(startFrame, it->second.frames));
    return true;
}

bool Instance::hold(const std::string& tag, const std::string& slot, float frame) {
    if (!start(tag, slot, frame)) {
        return false;
    }
    slots_[slot].hold = true;
    return true;
}

void Instance::stop(const std::string& tag, const std::string& slot) {
    for (auto it = slots_.begin(); it != slots_.end();) {
        if (it->second.anim && it->second.anim->name == tag && (slot.empty() || it->first == slot)) {
            it = slots_.erase(it);
        } else {
            ++it;
        }
    }
}

bool Instance::ended(const std::string& tag, float margin) const {
    const Player* p = nullptr;
    if (tag.empty()) {
        auto it = slots_.find(lastSlot_);
        p = it == slots_.end() ? nullptr : &it->second;
    } else {
        for (const auto& [slot, q] : slots_) {
            if (q.anim && q.anim->name == tag && (!p || q.order > p->order)) {
                p = &q;
            }
        }
    }
    if (!p || !p->anim) {
        return true;
    }
    if (p->anim->loop || p->hold) {
        return p->hold;
    }
    return p->ended || (margin > 0 && p->anim->frames - p->frame <= margin);
}

bool Instance::playing(const std::string& tag) const {
    for (const auto& [slot, q] : slots_) {
        if (q.anim && q.anim->name == tag && !q.ended && !q.hold) {
            return true;
        }
    }
    return false;
}

void Instance::writeVisible(const std::string& paneName, bool visible) {
    // "root" names the layout's root pane (0x2f8358 compares the name with "root").
    const int i = paneName == "root" ? (def_->panes.empty() ? -1 : 0) : def_->paneIndex(paneName);
    if (i >= 0) {
        panes_[size_t(i)].visible = visible;
    }
}

bool Instance::startAction(const std::string& tag) {
    // "part.tag" starts the tag in the named part pane's instance (traced e.g.
    // "counters.LifeCounter_EPISODE1" on CourseBook_BOTTOM).
    if (const size_t dot = tag.find('.'); dot != std::string::npos) {
        Instance* p = part(tag.substr(0, dot));
        return p && p->startAction(tag.substr(dot + 1));
    }
    auto it = def_->animations.find(tag);
    if (it == def_->animations.end()) {
        return false;
    }
    bind(it->second, it->second.slot, "");
    return true;
}

bool Instance::setFrame(const std::string& tag, float frame) {
    if (!startAction(tag)) {
        return false;
    }
    Player& p = slots_[def_->animations.at(tag).slot];
    p.frame = frame;
    p.hold = true;
    return true;
}

bool Instance::isActionEnd(const std::string& slot) const {
    auto it = slots_.find(slot);
    return it == slots_.end() || it->second.ended || (it->second.anim && it->second.anim->loop);
}

void Instance::calc(float delta) {
    apply();
    for (auto& [slot, p] : slots_) {
        if (!p.anim || p.hold || p.ended) {
            continue;
        }
        p.frame += delta;
        if (p.frame >= p.anim->frames) {
            if (p.anim->loop && p.anim->frames > 0) {
                p.frame = std::fmod(p.frame, p.anim->frames);
            } else {
                p.frame = p.anim->frames;
                p.ended = true;
            }
        }
    }
    for (auto& [i, part] : parts_) {
        part->calc(delta);
    }
}

void Instance::applyAt(float offset) {
    std::vector<std::pair<Player*, float>> saved;
    for (auto& [slot, p] : slots_) {
        if (!p.anim || p.hold || p.ended) {
            continue;
        }
        saved.emplace_back(&p, p.frame);
        p.frame = std::clamp(p.frame + offset, 0.0f, p.anim->frames);
    }
    apply();
    for (auto& [p, f] : saved) {
        p->frame = f;
    }
    for (auto& [i, part] : parts_) {
        part->applyAt(offset);
    }
}

// The panes and materials as the layout defines them (the state before any animation).
void Instance::resetToBindPose() {
    for (size_t i = 0; i < def_->panes.size(); i++) {
        const PaneDef& d = def_->panes[i];
        PaneState& s = panes_[i];
        std::copy(d.t, d.t + 3, s.t);
        std::copy(d.r, d.r + 3, s.r);
        std::copy(d.s, d.s + 2, s.s);
        std::copy(d.size, d.size + 2, s.size);
        s.alpha = d.alpha;
        s.visible = d.visible;
        std::memcpy(s.vc, d.vc, sizeof s.vc);
    }
    materials_ = def_->materials;
}

// Applies the bound animations at their current frames, in the order they were bound. Pane and
// material values persist between frames: what no animation writes keeps its last value.
void Instance::apply() {
    std::vector<const Player*> order;
    for (auto& [slot, p] : slots_) {
        if (p.anim) {
            order.push_back(&p);
        }
    }
    std::sort(order.begin(), order.end(), [](const Player* a, const Player* b) { return a->order < b->order; });
    for (const Player* pl : order) {
        const Animation& a = *pl->anim;
        for (size_t ti = 0; ti < (pl->all ? a.targets.size() : pl->bound.size()); ti++) {
            const auto& t = a.targets[pl->all ? ti : size_t(pl->bound[ti])];
            const float v = t.curve.eval(pl->frame);
            const std::string k = t.kind;
            if (t.pane >= 0) {
                PaneState& p = panes_[size_t(t.pane)];
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
                Material& m = materials_[size_t(t.material)];
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
                    auto& s = m.srt[size_t(t.index)];
                    if (t.target < 2) {
                        s.t[t.target] = v;
                    } else if (t.target == 2) {
                        s.r = v;
                    } else if (t.target < 5) {
                        s.s[t.target - 3] = v;
                    }
                } else if (k == "FLTP" && t.index < int(m.maps.size())) {
                    const int i = int(v);
                    if (i >= 0 && i < int(a.textures.size())) {
                        for (size_t j = 0; j < def_->textureNames.size(); j++) {
                            if (def_->textureNames[j] == a.textures[size_t(i)]) {
                                m.maps[size_t(t.index)].texture = int(j);
                            }
                        }
                    }
                }
            }
        }
    }
    // Language variants: panes ending in "_JP" are the Japanese texts and artwork, "_NA" the
    // others; the English release shows the "_NA" variants (hypothesis from the observed screens;
    // the code choosing them is not recovered).
    for (size_t i = 0; i < def_->panes.size(); i++) {
        const std::string& n = def_->panes[i].name;
        if (n.size() > 3 && n.compare(n.size() - 3, 3, "_JP") == 0) {
            panes_[i].visible = false;
        } else if (n.size() > 3 && n.compare(n.size() - 3, 3, "_NA") == 0) {
            panes_[i].visible = true;
        }
    }
    for (const auto& [i, vis] : visibility_) {
        panes_[size_t(i)].visible = vis;
    }
}

Instance* Instance::part(const std::string& paneName) {
    const int i = def_->paneIndex(paneName);
    auto it = parts_.find(i);
    return it == parts_.end() ? nullptr : it->second.get();
}

void Instance::setPartInstances(const std::string& paneName, std::vector<Instance*> instances) {
    const int i = def_->paneIndex(paneName);
    if (i < 0) {
        return;
    }
    if (instances.empty()) {
        partInstances_.erase(i);
    } else {
        partInstances_[i] = std::move(instances);
    }
}

void Instance::setText(const std::string& paneName, const std::u16string& text) {
    const int i = def_->paneIndex(paneName);
    if (i >= 0) {
        texts_[i] = text;
    }
}

void Instance::setVisible(const std::string& paneName, bool visible) {
    const int i = def_->paneIndex(paneName);
    if (i >= 0) {
        visibility_[i] = visible;
        panes_[size_t(i)].visible = visible;
    }
}

void Instance::setTexture(const std::string& paneName, int packageTexture) {
    const int i = def_->paneIndex(paneName);
    if (i >= 0) {
        textureOverride_[i] = packageTexture;
    }
}

bool Instance::contains(const std::string& paneName, float x, float y) const {
    const int i = def_->paneIndex(paneName);
    if (i < 0) {
        return false;
    }
    const PaneState& p = panes_[size_t(i)];
    const Mat4& m = p.world;
    const float det = m[0] * m[5] - m[4] * m[1];
    if (std::fabs(det) < 1e-6f) {
        return false;
    }
    const float px = x - m[12], py = y - m[13];
    const float lx = (m[5] * px - m[4] * py) / det, ly = (-m[1] * px + m[0] * py) / det;
    float x0, y0, x1, y1;
    paneQuad(def_->panes[size_t(i)].origin, p.size[0], p.size[1], x0, y0, x1, y1);
    return lx >= x0 && lx <= x1 && ly <= y0 && ly >= y1;
}

bool Instance::paneRect(const std::string& paneName, Mat4& world, float size[2]) const {
    const int i = def_->paneIndex(paneName);
    if (i < 0) {
        return false;
    }
    world = panes_[size_t(i)].world;
    size[0] = panes_[size_t(i)].size[0];
    size[1] = panes_[size_t(i)].size[1];
    return panes_[size_t(i)].visible;
}

// ---- drawing ------------------------------------------------------------------------------------

// The layout shader: the opening's material model (LayoutRenderer.hpp: the texture interpolates the
// material's black and white colours, times the vertex colour, then the alpha test) with up to
// three maps joined by the material's combiner stages before the interpolation.
captain_toad::gpu::Program& Instance::program() {
    const char* vs = "in vec4 inPosition;in vec4 inColor;in vec2 inTexCoord0;in vec2 inTexCoord1;in vec2 inTexCoord2;\n"
                     "out vec4 color;out vec2 texCoord0;out vec2 texCoord1;out vec2 texCoord2;\n"
                     "uniform mat4 modelViewProjectionMatrix;\n"
                     "void main(){gl_Position=modelViewProjectionMatrix*inPosition;texCoord0=inTexCoord0;"
                     "texCoord1=inTexCoord1;texCoord2=inTexCoord2;color=clamp(inColor,0.0,1.0);}";
    const char* fs =
        "in vec4 color;in vec2 texCoord0;in vec2 texCoord1;in vec2 texCoord2;\n"
        "uniform sampler2D tex,tex1,tex2;uniform int hasTexture,alphaOnly,alphaOnly1,alphaOnly2,font,alphaFunc,maps;\n"
        "uniform ivec4 modes;uniform float alphaRef;uniform vec4 black,white;\n"
        "float comb(float p,float n,int m){if(m==1)return p*n;if(m==2)return min(p+n,1.0);"
        "if(m==3)return clamp(p+n-0.5,0.0,1.0);if(m==5)return max(p-n,0.0);return n;}\n"
        "vec4 comb4(vec4 p,vec4 n,int mc,int ma){return vec4(comb(p.r,n.r,mc),comb(p.g,n.g,mc),comb(p.b,n.b,mc),"
        "comb(p.a,n.a,ma));}\n"
        "void main(){vec4 t=vec4(1.0);if(hasTexture!=0){t=texture(tex,texCoord0);if(alphaOnly!=0)t.rgb="
        "vec3(1.0);}\n"
        " if(maps>1){vec4 n=texture(tex1,texCoord1);if(alphaOnly1!=0)n.rgb=vec3(1.0);t=comb4(t,n,modes.x,"
        "modes.y);}\n"
        " if(maps>2){vec4 n=texture(tex2,texCoord2);if(alphaOnly2!=0)n.rgb=vec3(1.0);t=comb4(t,n,modes.z,"
        "modes.w);}\n"
        " vec4 b=t;if(font!=0){b.rgb=clamp(2.0*(vec3(t.a)-vec3(127.0/255.0)),0.0,1.0);b.a=clamp(2.0*(t.a*(20.0/"
        "255.0)+t.a),0.0,1.0);}\n"
        " vec4 c=(white*b+black*(vec4(1.0)-b))*color;float a=floor(c.a*255.0+0.5),r=floor(alphaRef*255.0+0.5);\n"
        " bool pass=alphaFunc==1||(alphaFunc==2&&a==r)||(alphaFunc==3&&a!=r)||(alphaFunc==4&&a<r)||(alphaFunc==5&&"
        "a<=r)||(alphaFunc==6&&a>r)||(alphaFunc==7&&a>=r);\n"
        " if(!pass)discard;fragColor=c;}";
    return captain_toad::gpu::device().sharedProgram("ui layout", vs, fs);
}

void Instance::bindMaterial(const Material& m, const float* black, const float* white, bool font,
                            const captain_toad::gpu::Texture* fontSheet) {
    captain_toad::gpu::Program& prog = program();
    static const captain_toad::gpu::Wrap wrap[] = {
        captain_toad::gpu::Wrap::ClampToEdge, captain_toad::gpu::Wrap::Repeat, captain_toad::gpu::Wrap::MirroredRepeat,
        captain_toad::gpu::Wrap::MirroredRepeat};
    bindings_ = {};
    // Maps 1 and 2 of a multi-texture material (units 1 and 2).
    int maps = 1;
    bool alphaOnlyExtra[2] = {false, false};
    if (!font) {
        for (size_t k = 1; k < 3 && k < m.maps.size() && k <= m.stages.size(); k++) {
            const Texture* t = mapTexture(m, k);
            if (!t) {
                break;
            }
            captain_toad::gpu::TextureBinding& b = bindings_[k];
            b.texture = t->texture.get();
            b.sampler.wrapS = wrap[(t->rotated ? m.maps[k].wrapT : m.maps[k].wrapS) & 3];
            b.sampler.wrapT = wrap[(t->rotated ? m.maps[k].wrapS : m.maps[k].wrapT) & 3];
            alphaOnlyExtra[k - 1] = t->alphaOnly;
            maps = int(k) + 1;
        }
    }
    bool alphaOnly = false;
    if (font) {
        bindings_[0].texture = fontSheet;
        bindings_[0].sampler.wrapS = bindings_[0].sampler.wrapT =
            captain_toad::gpu::Wrap::Repeat;   // never set: the default
    } else if (const Texture* mt = mapTexture(m)) {
        const Texture& t = *mt;
        bindings_[0].texture = t.texture.get();
        alphaOnly = t.alphaOnly;
        // Rotated storage swaps the axes the wrap modes apply to.
        bindings_[0].sampler.wrapS = wrap[(t.rotated ? m.maps[0].wrapT : m.maps[0].wrapS) & 3];
        bindings_[0].sampler.wrapT = wrap[(t.rotated ? m.maps[0].wrapS : m.maps[0].wrapT) & 3];
    }
    if (!bindings_[0].texture) {
        maps = 1;
    }
    prog.set("maps", maps);
    const int m0c = m.stages.size() > 0 ? m.stages[0][0] : 0, m0a = m.stages.size() > 0 ? m.stages[0][1] : 0;
    const int m1c = m.stages.size() > 1 ? m.stages[1][0] : 0, m1a = m.stages.size() > 1 ? m.stages[1][1] : 0;
    const int modes[4] = {m0c, m0a, m1c, m1a};
    prog.setInts("modes", modes, 4);
    prog.set("hasTexture", bindings_[0].texture != nullptr);
    prog.set("alphaOnly", alphaOnly);
    prog.set("alphaOnly1", alphaOnlyExtra[0]);
    prog.set("alphaOnly2", alphaOnlyExtra[1]);
    prog.set("font", font);
    prog.setFloats("black", black, 4);
    prog.setFloats("white", white, 4);
    prog.set("alphaFunc", font ? 1 : m.alphaFunc);
    prog.set("alphaRef", m.alphaRef);
}

// The first map's texture: one set by the game, else the layout's own.
const Texture* Instance::mapTexture(const Material& m, size_t map) const {
    if (m.maps.size() <= map) {
        return nullptr;
    }
    const auto& mp = m.maps[map];
    if (mp.packageTexture >= 0) {
        return &pkg_.texture(mp.packageTexture);
    }
    if (mp.texture >= 0 && mp.texture < int(def_->textures.size()) && def_->textures[size_t(mp.texture)] >= 0) {
        return &pkg_.texture(def_->textures[size_t(mp.texture)]);
    }
    return nullptr;
}

// The texture SRT in the layout's texture space, then the mapping onto the stored raster (rotated
// storage transposes the axes; the image occupies the top left of a padded allocation).
// The SRT scales and rotates about the texture's centre, then translates:
//   uv' = R * S * (uv - 0.5) + 0.5 + T
// (the layout library's usual texture matrix, hypothesis checked against the ROM's materials:
// the mirrored half-textures - medals, ribbons, spotlights, page shadows, translate 0.5 and scale
// 2 with mirrored wrap - only form a centred image this way, and the back button's plate
// (translate (-1, 1), scale 3) shows its corner where the button is on screen).
// Hypothesis: the SRT applies before the storage mapping.
std::array<float, 2> Instance::texSRT(const Material& m, size_t map, std::array<float, 2> uv) const {
    if (m.srt.size() > map) {
        const auto& s = m.srt[map];
        const float u = (uv[0] - 0.5f) * s.s[0], v = (uv[1] - 0.5f) * s.s[1];
        const float c = std::cos(s.r * float(M_PI) / 180), sn = std::sin(s.r * float(M_PI) / 180);
        uv = {u * c - v * sn + 0.5f + s.t[0], u * sn + v * c + 0.5f + s.t[1]};
    }
    if (const Texture* mt = mapTexture(m, map)) {
        const Texture& t = *mt;
        if (t.rotated) {
            uv = {1.0f - uv[1] * float(t.imageH) / float(t.w), 1.0f - uv[0] * float(t.imageW) / float(t.h)};
        } else if (t.topDown) {
            uv = {uv[0] * float(t.imageW) / float(t.w), 1.0f - uv[1] * float(t.imageH) / float(t.h)};
        } else {
            uv = {uv[0] * float(t.imageW) / float(t.w), 1.0f - (1.0f - uv[1]) * float(t.imageH) / float(t.h)};
        }
    }
    return uv;
}

void Instance::quad(const Mat4& M, float x0, float y0, float x1, float y1,
                    const std::array<std::array<float, 2>, 4>& uv, const float vc[4][4], float alpha) {
    const float px[4] = {x0, x1, x0, x1};
    const float py[4] = {y0, y0, y1, y1};
    const int order[6] = {0, 1, 2, 2, 1, 3};
    captain_toad::gpu::Immediate v;
    v.attributes = {{captain_toad::gpu::Attribute::Position, 4, 0},
                    {captain_toad::gpu::Attribute::Color, 4, 16},
                    {captain_toad::gpu::Attribute::TexCoord0, 2, 32},
                    {captain_toad::gpu::Attribute::TexCoord1, 2, 40},
                    {captain_toad::gpu::Attribute::TexCoord2, 2, 48}};
    v.floatsPerVertex = 14;
    for (int i : order) {
        const float x = px[i], y = py[i];
        const auto& e1 = extraUv_[0][size_t(i)];
        const auto& e2 = extraUv_[1][size_t(i)];
        const bool m1 = extraMaps_ >= 1, m2 = extraMaps_ >= 2;
        v.vertex({M[0] * x + M[4] * y + M[12], M[1] * x + M[5] * y + M[13], M[2] * x + M[6] * y + M[14],
                  M[3] * x + M[7] * y + M[15], vc[i][0], vc[i][1], vc[i][2], vc[i][3] * alpha, uv[size_t(i)][0],
                  uv[size_t(i)][1], m1 ? e1[0] : 0.f, m1 ? e1[1] : 0.f, m2 ? e2[0] : 0.f, m2 ? e2[1] : 0.f});
    }
    captain_toad::gpu::Draw d;
    d.program = &program();
    d.state.blend = captain_toad::gpu::BlendState::alpha();
    for (size_t k = 0; k < bindings_.size(); k++) {
        d.textures[k] = bindings_[k];
    }
    d.samplers = {{"tex", 0}, {"tex1", 1}, {"tex2", 2}};
    captain_toad::gpu::device().draw(d, v);
}

void Instance::draw(const Mat4& parent, float parentAlpha) {
    if (def_->panes.empty() || !shown_) {
        return;
    }
    drawPane(0, parent, parentAlpha);
}

void Instance::drawPane(int index, const Mat4& parent, float parentAlpha) {
    const PaneDef& d = def_->panes[size_t(index)];
    PaneState& p = panes_[size_t(index)];
    const Mat4 local = translation(p.t[0], p.t[1], p.t[2]) * rotation(2, p.r[2]) * rotation(1, p.r[1]) *
                       rotation(0, p.r[0]) * scaling(p.s[0], p.s[1]);
    const Mat4 M = parent * local;
    p.world = M;
    if (getenv("DIORAMA_UI_DEBUG")) {
        int depth = 0;
        for (int q = d.parent; q >= 0; q = def_->panes[size_t(q)].parent) {
            depth++;
        }
        fprintf(stderr, "%s%s %s %s vis %d alpha %.2f t %.0f %.0f %.0f r %.0f %.0f %.0f s %.2f %.2f size %.0f %.0f\n",
                std::string(size_t(depth) * 2, ' ').c_str(), def_->name.c_str(), d.type.c_str(), d.name.c_str(),
                p.visible, p.alpha, p.t[0], p.t[1], p.t[2], p.r[0], p.r[1], p.r[2], p.s[0], p.s[1], p.size[0],
                p.size[1]);
    }
    if (!p.visible) {
        return;
    }
    const float alpha = p.alpha * parentAlpha;
    float x0, y0, x1, y1;
    paneQuad(d.origin, p.size[0], p.size[1], x0, y0, x1, y1);
    if (d.type == "pic1" && d.material >= 0) {
        Material m = materials_[size_t(d.material)];
        // A texture set by the game replaces the first map's (the material's texture transform
        // still applies: e.g. the illustrations' 154 x 196 of a 256 x 256 image).
        if (auto it = textureOverride_.find(index); it != textureOverride_.end()) {
            if (m.maps.empty()) {
                m.maps.push_back({});
            }
            m.maps[0].packageTexture = it->second;
        }
        {
            bindMaterial(m, m.black, m.white, false, 0);
            std::array<std::array<float, 2>, 4> uv;
            for (int i = 0; i < 4; i++) {
                std::array<float, 2> base = d.uv[size_t(i)];
                if (m.texgenSource >= 3 && mapTexture(m)) {
                    // Projection coordinates: the texture tiles at one texel per layout unit
                    // across the pane (hypothesis from the course book's patterned backgrounds).
                    const Texture& t = *mapTexture(m);
                    const float tw = float(t.rotated ? t.imageH : t.imageW),
                                th = float(t.rotated ? t.imageW : t.imageH);
                    base = {(i & 1) ? p.size[0] / tw : 0.f, (i & 2) ? p.size[1] / th : 0.f};
                }
                uv[size_t(i)] = texSRT(m, base);
            }
            // Maps 1 and 2: their own coordinates (the pane's texture coordinates, or the
            // projection's) through their own transforms.
            extraMaps_ = 0;
            for (size_t k = 1; k < 3 && k < m.maps.size() && k <= m.stages.size() && mapTexture(m, k); k++) {
                for (int i = 0; i < 4; i++) {
                    std::array<float, 2> base = d.uv[size_t(i)];
                    if (k < m.sources.size() && m.sources[k] >= 3) {
                        const Texture& t = *mapTexture(m, k);
                        const float tw = float(t.rotated ? t.imageH : t.imageW);
                        const float th = float(t.rotated ? t.imageW : t.imageH);
                        base = {(i & 1) ? p.size[0] / tw : 0.f, (i & 2) ? p.size[1] / th : 0.f};
                    }
                    extraUv_[k - 1][size_t(i)] = texSRT(m, k, base);
                }
                extraMaps_ = int(k);
            }
            quad(M, x0, y0, x1, y1, uv, p.vc, alpha);
            extraMaps_ = 0;
        }
    } else if (d.type == "wnd1") {
        drawWindow(index, M, alpha);
    } else if (d.type == "txt1") {
        drawText(index, M, alpha);
    } else if (d.type == "prt1") {
        std::vector<Instance*> list;
        if (auto ext = partInstances_.find(index); ext != partInstances_.end()) {
            list = ext->second;
        } else if (auto it = parts_.find(index); it != parts_.end()) {
            list.push_back(it->second.get());
        }
        for (Instance* part : list) {
            // An instance position replaces the pane's translation (the pane has no rotation or
            // scale where this is used: the book's page parts).
            const Mat4 PM = part->hasOffset_
                                ? M * translation(part->offset_[0] - p.t[0], part->offset_[1] - p.t[1], -p.t[2]) *
                                      scaling(d.partScale[0], d.partScale[1])
                                : M * scaling(d.partScale[0], d.partScale[1]);
            // The part's own root pane: its visibility and alpha (pages hide their root pane).
            if (!part->def_->panes.empty() && part->shown_ && part->panes_[0].visible) {
                const float rootAlpha = alpha * part->panes_[0].alpha;
                for (int ch : part->def_->panes[0].children) {
                    part->drawPane(ch, PM, rootAlpha);
                }
            }
        }
    }
    const float childAlpha = d.influenced ? alpha : parentAlpha;
    for (int ch : d.children) {
        drawPane(ch, M, childAlpha);
    }
}

// Windows. Flags (layout data): bit 0 draws the whole window with the frame material, bit 1 gives
// the frames the pane's vertex colours, bits 2-3 the kind: 0 "around" (content inside four frame
// bands, traced for the opening), 1 horizontal (a left and a mirrored right cap from the frame
// texture around the content), 2 horizontal without content (the caps and the frame texture's
// inner edge stretched between them, e.g. the pause menu's buttons).
void Instance::drawWindow(int index, const Mat4& M, float alpha) {
    const PaneDef& d = def_->panes[size_t(index)];
    const PaneState& p = panes_[size_t(index)];
    float x0, y0, x1, y1;
    paneQuad(d.origin, p.size[0], p.size[1], x0, y0, x1, y1);
    const int kind = (d.windowFlags >> 2) & 3;
    // Flag bit 0 is set on nearly every window of the ROM, also on windows whose content has a
    // textured material of its own (the course-open spotlight's circle, shadow windows): the
    // content uses its own material when that has a texture; untextured content takes the frame
    // material's inner texel (as verified on the opening's windows). Projected content (the
    // dialogs' patterned backboards) also takes the inner texel for now: their frames blend the
    // same pattern through a second map, and layout combiners are not translated, so drawing the
    // pattern in the content alone would leave a seam at the frame.
    const bool contentTextured = d.material >= 0 && !materials_[size_t(d.material)].maps.empty() &&
                                 materials_[size_t(d.material)].texgenSource < 3;
    const bool oneMaterial = (d.windowFlags & 1) && !contentTextured;
    const bool allColors = d.windowFlags & 2;
    float white[4][4];
    for (auto& w : white) {
        for (float& x : w) {
            x = 1;
        }
    }
    const auto& frameColors = allColors ? p.vc : white;
    if (kind == 1 || kind == 2) {
        if (d.frameMaterials.empty()) {
            return;
        }
        const Material& fm = materials_[size_t(d.frameMaterials[0])];
        const float fw = std::min(d.frameSize[0], (x1 - x0) / 2);
        auto drawWith = [&](const Material& m, float qx0, float qx1, std::array<std::array<float, 2>, 4> base,
                            const float vc[4][4]) {
            bindMaterial(m, m.black, m.white, false, 0);
            std::array<std::array<float, 2>, 4> uv;
            for (int i = 0; i < 4; i++) {
                uv[size_t(i)] = texSRT(m, base[size_t(i)]);
            }
            quad(M, qx0, y0, qx1, y1, uv, vc, alpha);
        };
        // Middle: the content (kind 1) or the frame texture's inner edge (kind 2).
        if (kind == 2 || oneMaterial) {
            drawWith(fm, x0 + fw, x1 - fw, {{{1, 0}, {1, 0}, {1, 1}, {1, 1}}}, frameColors);
        } else if (d.material >= 0) {
            drawWith(materials_[size_t(d.material)], x0 + fw, x1 - fw, d.uv, p.vc);
        }
        drawWith(fm, x0, x0 + fw, {{{0, 0}, {1, 0}, {0, 1}, {1, 1}}}, frameColors);
        drawWith(fm, x1 - fw, x1, {{{1, 0}, {0, 0}, {1, 1}, {0, 1}}}, frameColors);
        return;
    }
    const float fl = d.frameSize[0], fr = d.frameSize[1], ft = d.frameSize[2], fb = d.frameSize[3];
    const float cl = x0 + fl, cr = x1 - fr, ct = y0 - ft, cb = y1 + fb;
    // Content: its own material, or with one material for all the frame material's inner texel.
    if (oneMaterial && !d.frameMaterials.empty()) {
        const Material& m = materials_[size_t(d.frameMaterials[0])];
        bindMaterial(m, m.black, m.white, false, 0);
        const std::array<float, 2> inner = texSRT(m, {1, 1});
        quad(M, cl, ct, cr, cb, {{inner, inner, inner, inner}}, p.vc, alpha);
    } else if (d.material >= 0) {
        const Material& cm = materials_[size_t(d.material)];
        bindMaterial(cm, cm.black, cm.white, false, 0);
        std::array<std::array<float, 2>, 4> uv;
        for (int i = 0; i < 4; i++) {
            std::array<float, 2> base = d.uv[size_t(i)];
            if (cm.texgenSource >= 3 && mapTexture(cm)) {
                // Projection coordinates, as for picture panes: one texel per layout unit (the
                // dialogs' patterned backboards).
                const Texture& t = *mapTexture(cm);
                const float tw = float(t.rotated ? t.imageH : t.imageW), th = float(t.rotated ? t.imageW : t.imageH);
                base = {(i & 1) ? (cr - cl) / tw : 0.f, (i & 2) ? (ct - cb) / th : 0.f};
            }
            uv[size_t(i)] = texSRT(cm, base);
        }
        quad(M, cl, ct, cr, cb, uv, p.vc, alpha);
    }
    if (d.frameMaterials.empty() || fl <= 0 || ft <= 0) {
        return;
    }
    // Frames: four pinwheel quads, each a corner with the band it continues into (top-left with
    // the top band, top-right with the right band, ...). Texture coordinates are corner-local: 0 at
    // the outer edges, 1 at the corner's inner edges, continuing linearly beyond (the sampler's
    // clamp repeats the inner column along the band). With one frame material the other corners mirror it; with four, each corner has its
    // own material and flip.
    struct Corner {
        float qx0, qy0, qx1, qy1;   // quad (y up)
        float ox, oy;               // the corner's outer point
        int frame;
        bool flipH, flipV;   // implied mirroring with a single frame material
    };
    const bool single = d.frameMaterials.size() < 4;
    const Corner corners[4] = {
        {x0, y0, cr, ct, x0, y0, 0, false, false},   // top-left + top band
        {cr, y0, x1, cb, x1, y0, 1, true, false},    // top-right + right band
        {x0, ct, cl, y1, x0, y1, 2, false, true},    // bottom-left + left band
        {cl, cb, x1, y1, x1, y1, 3, true, true},     // bottom-right + bottom band
    };
    for (const Corner& c : corners) {
        const int frame = single ? 0 : c.frame;
        const Material& fm = materials_[size_t(d.frameMaterials[size_t(frame)])];
        const int flip = single ? (c.flipH ? 1 : 0) | (c.flipV ? 2 : 0)
                                : (d.frameFlips.size() > size_t(frame) ? d.frameFlips[size_t(frame)] : 0);
        bindMaterial(fm, fm.black, fm.white, false, 0);
        const float px[4] = {c.qx0, c.qx1, c.qx0, c.qx1};
        const float py[4] = {c.qy0, c.qy0, c.qy1, c.qy1};
        std::array<std::array<float, 2>, 4> uv;
        for (int i = 0; i < 4; i++) {
            // Distance from the corner's outer edges in frame widths: linear across the quad (beyond
            // 1 the frame texture's clamp mode repeats its inner edge along the band; clamping per
            // vertex would stretch the corner over the band).
            float u = std::fabs(px[i] - c.ox) / (c.ox == x0 ? fl : fr);
            float v = std::fabs(py[i] - c.oy) / (c.oy == y0 ? ft : fb);
            // Layout orientation of the frame texture: u left to right, v top to bottom.
            if (c.ox != x0) {
                u = 1 - u;
            }
            if (c.oy != y0) {
                v = 1 - v;
            }
            if (flip & 1) {
                u = 1 - u;
            }
            if (flip & 2) {
                v = 1 - v;
            }
            uv[size_t(i)] = texSRT(fm, {u, v});
        }
        quad(M, c.qx0, c.qy0, c.qx1, c.qy1, uv, frameColors, alpha);
    }
}

// Text: lines split at line feeds; the block is placed in the box by the text position and each
// line aligned by the alignment. Glyph quads as traced for the opening (LayoutRenderer drawText).
void Instance::drawText(int index, const Mat4& M, float alpha) {
    const PaneDef& d = def_->panes[size_t(index)];
    const PaneState& p = panes_[size_t(index)];
    if (d.material < 0 || d.font < 0 || d.font >= int(def_->fonts.size()) || def_->fonts[size_t(d.font)] < 0) {
        return;
    }
    const std::u16string& text = texts_.count(index) ? texts_.at(index) : d.text;
    const Layout::Font& f = pkg_.font(def_->fonts[size_t(d.font)]);
    const Material& m = materials_[size_t(d.material)];
    const float sx = d.fontSize[0] / float(f.width), sy = d.fontSize[1] / float(f.height);
    if (getenv("DIORAMA_UI_DEBUG")) {
        fprintf(stderr, "  text %s: %zu chars, font %dx%d size %.1f %.1f, glyphs %zu, mat black a %.2f white a %.2f\n",
                d.name.c_str(), text.size(), f.width, f.height, d.fontSize[0], d.fontSize[1], f.glyphs.size(),
                m.black[3], m.white[3]);
    }
    std::vector<std::u16string> lines(1);
    for (char16_t ch : text) {
        if (ch == u'\n') {
            lines.emplace_back();
        } else {
            lines.back() += ch;
        }
    }
    auto width = [&](const std::u16string& line) {
        float w = 0;
        for (char16_t ch : line) {
            auto it = f.glyphs.find(ch);
            w += (it != f.glyphs.end() ? float(it->second.chr) : 0) * sx + d.charSpace;
        }
        return line.empty() ? 0.f : w - d.charSpace;
    };
    const float lineH = float(f.lineFeed) * sy + d.lineSpace;
    const float blockH = float(lines.size()) * lineH - d.lineSpace;
    float blockW = 0;
    for (const auto& l : lines) {
        blockW = std::max(blockW, width(l));
    }
    float bx0, by0, bx1, by1;
    paneQuad(d.origin, p.size[0], p.size[1], bx0, by0, bx1, by1);
    const int hp = d.textPosition & 3, vp = (d.textPosition >> 2) & 3;
    const float blockLeft = hp == 1 ? bx0 : hp == 2 ? bx1 - blockW : (bx0 + bx1 - blockW) / 2;
    const float blockTop = vp == 1 ? by0 : vp == 2 ? by1 + blockH : (by0 + by1 + blockH) / 2;
    const int align = d.textAlignment == 0 ? (hp == 1 ? 1 : hp == 2 ? 3 : 2) : d.textAlignment;

    auto pass = [&](const float* black, const float* white, const float colors[2][4], float ox, float oy, float scx,
                    float scy) {
        if (f.color) {
            // Colour glyphs: the sheet's colours, as a texture (no coverage remap).
            static const float none[4] = {0, 0, 0, 0}, full[4] = {1, 1, 1, 1};
            bindMaterial(m, none, full, false, nullptr);
            bindings_[0].texture = f.sheet.get();
            bindings_[0].sampler.wrapS = bindings_[0].sampler.wrapT =
                captain_toad::gpu::Wrap::Repeat;   // never set: the default
            program().set("hasTexture", 1);
            program().set("alphaOnly", 0);
        } else {
            bindMaterial(m, black, white, true, f.sheet.get());
        }
        float top = blockTop;
        for (const auto& line : lines) {
            const float lw = width(line);
            float x = align == 1 ? blockLeft : align == 3 ? blockLeft + blockW - lw : blockLeft + (blockW - lw) / 2;
            for (char16_t ch : line) {
                auto it = f.glyphs.find(ch);
                if (it == f.glyphs.end()) {
                    continue;
                }
                const Layout::Glyph& g = it->second;
                const float gx = x + (float(g.left) - 0.5f) * sx;
                const float gw = float(g.glyph + 1) * sx;
                const float gh = float(f.cellH + 1) * sy;
                // The cell's baseline row sits on the line's baseline, the font's ascent below the
                // line top (MarioFont32: ascent 31, cell baseline 24 - the NEW stamp sits 7 cell
                // rows lower than a cell at the line top; RodinFont32: 3 rows).
                const float cx = gx + gw / 2, cy = top - float(f.ascent - f.baseline) * sy - gh / 2;
                const float qx0 = cx - gw * scx / 2 + ox, qx1 = cx + gw * scx / 2 + ox;
                const float qy0 = cy + gh * scy / 2 + oy, qy1 = cy - gh * scy / 2 + oy;
                const float u0 = (g.x + 0.5f) / float(f.sheetW),
                            u1 = (g.x + 0.5f + float(g.glyph) + 1) / float(f.sheetW);
                const float v0 = (g.y + 0.5f) / float(f.sheetH),
                            v1 = (g.y + 0.5f + float(f.cellH) + 1) / float(f.sheetH);
                float vc[4][4];
                for (int k = 0; k < 4; k++) {
                    vc[0][k] = vc[1][k] = colors[0][k];
                    vc[2][k] = vc[3][k] = colors[1][k];
                }
                quad(M, qx0, qy0, qx1, qy1, {{{u0, 1 - v0}, {u1, 1 - v0}, {u0, 1 - v1}, {u1, 1 - v1}}}, vc, alpha);
                x += float(g.chr) * sx + d.charSpace;
            }
            top -= lineH;
        }
    };
    const float black[4] = {m.black[0], m.black[1], m.black[2], 0};
    if (m.fontShadow) {
        pass(m.shadowBlack, m.shadowWhite, d.shadowColors, d.shadowOffset[0], d.shadowOffset[1], d.shadowScale[0],
             d.shadowScale[1]);
    }
    pass(black, m.white, d.colors, 0, 0, 1, 1);
}

void Instance::drawScreen(float fov) {
    if (def_->panes.empty() || !shown_) {
        return;
    }
    const float w = def_->width, h = def_->height;
    Mat4 root = identity();
    captain_toad::gpu::Mat4 projection;
    if (fov > 0) {
        // Perspective: the layout plane at the distance where one unit is one pixel.
        const float t = std::tan(fov * float(M_PI) / 360);
        const float dist = (h / 2) / t;
        const float n = 10, fr = dist * 10;
        projection = captain_toad::gpu::Mat4::frustum(-n * t * w / h, n * t * w / h, -n * t, n * t, n, fr);
        root = translation(0, 0, -dist);
    } else {
        projection = captain_toad::gpu::Mat4::ortho(-w / 2, w / 2, -h / 2, h / 2, -10000, 10000);
    }
    const auto mvp = captain_toad::gpu::modelViewProjection(projection, captain_toad::gpu::Mat4());
    program().setFloats("modelViewProjectionMatrix", mvp.data(), 16);
    draw(root, 1.0f);
}

}   // namespace captain_toad::ui
