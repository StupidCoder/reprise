// The 3DS adventures' scene. See a56_scene.hpp.
#include "system/a56_scene.hpp"
#include "graphics/a56_camera.hpp"
#include "graphics/a56_font.hpp"
#include "graphics/a56_model.hpp"
#include "graphics/a56_text.hpp"
#include "system/a56_xq.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <utility>

namespace picarat::adventure56 {

Object::Object() {
    scale.set({1, 1, 1});
    alpha.set({1});
    colour.set({1, 1, 1});
}

Scene::Scene(const Pack& pack) : pack_(pack) {}

Object3D* Scene::object3D(std::uint32_t hash) {
    const auto it = objects3D_.find(hash);
    return it == objects3D_.end() ? nullptr : it->second.get();
}

namespace {
Affine localMatrix(const Object& o, float extra, bool scaled);

// A mesh's skinning matrices for a pose: the palette's bones (weighted
// vertices, or one bone without weights), or the bone of the mesh's own
// name ("model.part" -> bone "part": puzzle 069's pizza slices turn with
// their bones and carry the root's scale 4).
std::vector<Affine> meshSkin(const Model& model, const Mesh& mesh, const std::vector<Affine>& pose,
                             bool& weighted) {
    std::vector<Affine> skin;
    const auto& vb = mesh.vertices;
    weighted = vb.has(VertexBuffer::Bones) && vb.has(VertexBuffer::Weights);
    if (!pose.empty() && (weighted || mesh.palette.size() == 1)) {
        for (const std::uint32_t h : mesh.palette) {
            const int b = model.bone(h);
            skin.push_back(b >= 0 ? pose[std::size_t(b)] * model.bones[std::size_t(b)].inverseBind
                                  : Affine{});
        }
    } else if (!pose.empty() && mesh.palette.empty()) {
        const auto dot = mesh.name.rfind('.');
        const int b =
            model.bone(xq::hash(dot == std::string::npos ? mesh.name : mesh.name.substr(dot + 1)));
        if (b >= 0) {
            skin.push_back(pose[std::size_t(b)] * model.bones[std::size_t(b)].inverseBind);
        }
    }
    return skin;
}

// A vertex's position under its skin (a mesh bound to one bone without
// weights follows it; weighted vertices blend their bones).
std::array<float, 3> skinnedPosition(const VertexBuffer& vb, const std::vector<Affine>& skin,
                                     bool weighted, unsigned v) {
    const float* p = vb.at(VertexBuffer::Position, v);
    std::array<float, 3> out{p[0], p[1], p[2]};
    if (!skin.empty() && !weighted) {
        out = skin.front().apply(p);
    } else if (!skin.empty()) {
        const float* w = vb.at(VertexBuffer::Weights, v);
        const float* bi = vb.at(VertexBuffer::Bones, v);
        float sum[3] = {0, 0, 0}, total = 0;
        const int influences =
            std::min(vb.components[VertexBuffer::Weights], vb.components[VertexBuffer::Bones]);
        for (int k = 0; k < influences; ++k) {
            const int i = int(bi[k] + 0.5f);
            if (w[k] <= 0 || i < 0 || i >= int(skin.size())) {
                continue;
            }
            const auto q = skin[std::size_t(i)].apply(p);
            for (int c = 0; c < 3; ++c) {
                sum[c] += q[std::size_t(c)] * w[k];
            }
            total += w[k];
        }
        if (total > 0) {
            for (int c = 0; c < 3; ++c) {
                out[std::size_t(c)] = sum[c] / total;
            }
        }
    }
    return out;
}
} // namespace

// A 3D object's transform in the world: its own under its parents' (6013
// attaches it; puzzle 052's goal ladybug rides on the cob).
Affine Scene::worldMatrix(const Object& o, float extra, bool scaled) const {
    Affine m = localMatrix(o, extra, scaled);
    const Object* at = &o;
    for (int depth = 0; at->parent && depth < 16; ++depth) {
        const auto it = objects3D_.find(at->parent);
        if (it == objects3D_.end()) {
            break;
        }
        const std::uint32_t bone = at->parentBone;
        at = it->second.get();
        // Under a bone: the bone's model-space matrix in the parent's
        // current pose (puzzle 056's containers hang from the crane's
        // "arm2").
        if (bone && at->model) {
            const Model& model = *at->model;
            if (const int b = model.bone(bone); b >= 0) {
                const std::vector<Affine> pose = posedModel(*at, model);
                if (std::size_t(b) < pose.size()) {
                    m = pose[std::size_t(b)] * m;
                }
            }
        }
        m = localMatrix(*at, 1.0f, scaled) * m;
    }
    return m;
}

void Scene::detach3D(Object& o) {
    const Affine w = worldMatrix(o, 1.0f, true);
    const auto& m = w.m;
    float s[3];
    for (int c = 0; c < 3; ++c) {
        const float l = m[std::size_t(c)] * m[std::size_t(c)] +
                        m[std::size_t(4 + c)] * m[std::size_t(4 + c)] +
                        m[std::size_t(8 + c)] * m[std::size_t(8 + c)];
        s[c] = l > 0.0f ? std::sqrt(l) : 0.0f;
    }
    auto r = [&](int row, int col) {
        return s[col] != 0.0f ? m[std::size_t(row * 4 + col)] / s[col] : 0.0f;
    };
    // R = Rx * Ry * Rz: r02 = sin y, r12 = -sin x cos y, r22 = cos x cos y,
    // r01 = -cos y sin z, r00 = cos y cos z.
    const float sy = std::clamp(r(0, 2), -1.0f, 1.0f);
    const float y = std::asin(sy);
    float x, z;
    if (std::fabs(sy) < 0.99999f) {
        x = std::atan2(-r(1, 2), r(2, 2));
        z = std::atan2(-r(0, 1), r(0, 0));
    } else {
        x = std::atan2(r(2, 1), r(1, 1));
        z = 0.0f;
    }
    o.position.set({m[3] - o.offset[0], m[7] - o.offset[1], m[11] - o.offset[2]});
    o.rotation3.set({x - o.rotation3Offset[0], y - o.rotation3Offset[1], z - o.rotation3Offset[2]});
    o.scale.set({s[0], s[1], s[2]});
    o.parent = 0;
    o.parentBone = 0;
}

Object3D& Scene::create3D(const std::string& name, int screen, int priority, int kind, int level) {
    auto o = std::make_unique<Object>();
    o->is3D = true;
    o->hash = xq::hash(name);
    o->name = name;
    o->screen = screen;
    o->priority = priority;
    o->kind = kind;
    o->order = created_++;
    o->scope = level >= 0 ? level : scopeDepth_;
    Object3D& ref = *o;
    objects3D_[ref.hash] = std::move(o);
    return ref;
}

void Scene::remove3D(std::uint32_t hash) {
    objects3D_.erase(hash);
}

Resource* Scene::resource(std::uint32_t hash) {
    const auto it = resources_.find(hash);
    return it == resources_.end() ? nullptr : &it->second;
}

Resource* Scene::resource(const std::string& name) {
    return resource(xq::hash(name));
}

Resource* Scene::resource(std::uint32_t hash, Resource::Kind kind) {
    return const_cast<Resource*>(std::as_const(*this).resource(hash, kind));
}

const Resource* Scene::resource(std::uint32_t hash, Resource::Kind kind) const {
    if (const auto it = resources_.find(hash); it != resources_.end() && it->second.kind == kind) {
        return &it->second;
    }
    const auto it = otherKinds_.find({int(kind), hash});
    return it == otherKinds_.end() ? nullptr : &it->second;
}

Resource& Scene::addResource(const std::string& name, Resource r, int level) {
    // A name already loaded keeps its resource: the manager's sorted list
    // refuses a second entry of a name and releases the new one (0x200ed8;
    // the number-entry puzzles load their own "bg_b00" before the shared
    // input setup asks for the generic one).
    r.name = name;
    r.scope = level >= 0 ? level : scopeDepth_;
    const std::uint32_t h = xq::hash(name);
    // Movies (3802) keep a list of their own in the game; here they always
    // sit in the per-kind list, so the other kinds' lookups by name alone
    // never meet one ("lt5_mbc" names both the movie and its subtitles).
    if (const auto it = resources_.find(h);
        r.kind == Resource::Kind::Movie || (it != resources_.end() && it->second.kind != r.kind)) {
        // Another kind's list (see resource(hash, kind)).
        const auto [other, inserted] = otherKinds_.try_emplace({int(r.kind), h}, std::move(r));
        return other->second;
    }
    const auto [it, inserted] = resources_.try_emplace(h, std::move(r));
    return it->second;
}

namespace {

// A switch's flag word: bit 0 set = off, the saved states above it.
void pushSwitch(bool& on, Object::SwitchStack& st) {
    std::uint32_t word = st.saved << 1 | (on ? 0u : 1u);
    word = st.count < 0 ? (word >> 1 & ~1u) | (word >> 1 & 1u) : (word & 1u) | word << 1;
    on = (word & 1u) == 0;
    st.saved = word >> 1;
    ++st.count;
}

// `byWord`: the 5007 word's close tests the word, not the counter.
void popSwitch(bool& on, Object::SwitchStack& st, bool byWord = false) {
    std::uint32_t word = st.saved << 1 | (on ? 0u : 1u);
    const bool none = byWord ? word == 0 : st.count < 1;
    if (byWord && none) {
        word = 0;
    } else {
        word = none ? (word & 1u) | word << 1 : (word >> 1 & ~1u) | (word >> 1 & 1u);
    }
    on = (word & 1u) == 0;
    st.saved = word >> 1;
    --st.count;
}

} // namespace

void Scene::openScope(bool snapshot) {
    if (scopeDepth_ >= 32) {
        return;
    }
    ScopeMark mark;
    mark.snapshot = snapshot;
    for (int s = 0; s < kScreens; ++s) {
        mark.screenRig[s] = screenRig[s];
        mark.screenSky[s] = screenSky[s];
        mark.screenEnabled[s] = screenEnabled[s];
        if (s < 2) {
            mark.screenKept[s] = screenKept[s];
        }
    }
    scopes_.push_back(std::move(mark));
    for (auto* list : {&objects_, &objects3D_}) {
        for (auto& [hash, o] : *list) {
            pushSwitch(o->visible, o->visibleStack);
            pushSwitch(o->touchOn, o->touchStack);
            pushSwitch(o->switch14, o->switch14Stack);
            pushSwitch(o->drawn, o->drawnStack);
        }
    }
    ++scopeDepth_;
}

void Scene::closeScope() {
    if (scopeDepth_ <= 0 || scopes_.empty()) {
        return;
    }
    const int depth = scopeDepth_;
    // 0x2d27e0 first: the switches come back; then 0x32d338 deletes.
    for (auto* list : {&objects_, &objects3D_}) {
        for (auto& [hash, o] : *list) {
            popSwitch(o->visible, o->visibleStack);
            popSwitch(o->touchOn, o->touchStack);
            popSwitch(o->switch14, o->switch14Stack, true);
            popSwitch(o->drawn, o->drawnStack);
        }
    }
    std::erase_if(objects_, [depth](const auto& e) { return e.second->scope == depth; });
    std::erase_if(objects3D_, [depth](const auto& e) { return e.second->scope == depth; });
    std::erase_if(resources_, [depth](const auto& e) { return e.second.scope == depth; });
    std::erase_if(otherKinds_, [depth](const auto& e) { return e.second.scope == depth; });
    std::erase_if(rigs, [depth](const auto& e) { return e.second.scope == depth; });
    const ScopeMark& mark = scopes_.back();
    if (mark.snapshot) {
        for (int s = 0; s < kScreens; ++s) {
            screenRig[s] = mark.screenRig[s];
            screenSky[s] = mark.screenSky[s];
            screenEnabled[s] = mark.screenEnabled[s];
            if (s < 2) {
                screenKept[s] = mark.screenKept[s];
            }
        }
    }
    scopes_.pop_back();
    --scopeDepth_;
}

void Scene::removeResource(std::uint32_t hash) {
    resources_.erase(hash);
}

void Scene::removeResource(std::uint32_t hash, Resource::Kind kind) {
    if (const auto it = resources_.find(hash); it != resources_.end() && it->second.kind == kind) {
        resources_.erase(it);
    } else {
        otherKinds_.erase({int(kind), hash});
    }
}

Object* Scene::object(std::uint32_t hash) {
    const auto it = objects_.find(hash);
    return it == objects_.end() ? nullptr : it->second.get();
}

Object* Scene::object(const std::string& name) {
    return object(xq::hash(name));
}

Object& Scene::create(const std::string& name, int screen, int priority, int kind, int level) {
    auto o = std::make_unique<Object>();
    o->hash = xq::hash(name);
    o->name = name;
    o->screen = screen;
    o->priority = priority;
    o->kind = kind;
    o->order = created_++;
    o->listOrder = o->order;
    o->scope = level >= 0 ? level : scopeDepth_;
    Object& ref = *o;
    objects_[ref.hash] = std::move(o);
    return ref;
}

void Scene::setPriority(Object& o, int priority) {
    o.priority = priority;
    if (o.parent == 0) {
        o.listOrder = created_++;
    }
}

std::vector<Object*> Scene::screenList(int screen) const {
    std::vector<Object*> list;
    for (const auto& [hash, o] : objects_) {
        if (o->screen == screen) {
            list.push_back(o.get());
        }
    }
    std::sort(list.begin(), list.end(), screenListBefore);
    return list;
}

void Scene::remove(std::uint32_t hash) {
    objects_.erase(hash);
}

void Scene::removeAll() {
    objects_.clear();
}

Scene::Transform Scene::screenTransform(const Object& o) const {
    Transform t;
    t.x = o.position[0] + o.offset[0];
    t.y = o.position[1] + o.offset[1];
    t.sx = o.scale[0] + o.scaleOffset[0];
    t.sy = o.scale[1] + o.scaleOffset[1];
    t.angle = o.rotation[0] + o.stepAngle[0];
    const Object* at = &o;
    for (int depth = 0; at->parent && depth < 16; ++depth) {
        const auto it = objects_.find(at->parent);
        if (it == objects_.end()) {
            break;
        }
        at = it->second.get();
        const float psx = at->scale[0] + at->scaleOffset[0],
                    psy = at->scale[1] + at->scaleOffset[1];
        const float pa = at->rotation[0] + at->stepAngle[0];
        const float c = std::cos(pa), s = std::sin(pa);
        const float lx = t.x * psx, ly = t.y * psy;
        t.x = at->position[0] + at->offset[0] + lx * c - ly * s;
        t.y = at->position[1] + at->offset[1] + lx * s + ly * c;
        t.sx *= psx;
        t.sy *= psy;
        t.angle += pa;
    }
    return t;
}

std::array<float, 2> Scene::screenPosition(const Object& o) const {
    const Transform t = screenTransform(o);
    return {t.x, t.y};
}

bool Scene::shown(const Object& o) const {
    const Object* at = &o;
    for (int depth = 0; depth < 16; ++depth) {
        if (!at->visible) {
            return false;
        }
        if (!at->parent) {
            return true;
        }
        const auto it = objects_.find(at->parent);
        if (it == objects_.end()) {
            return true;
        }
        at = it->second.get();
    }
    return true;
}

void Scene::updateShared() {
    brightness[0].step();
    brightness[1].step();
    for (auto& c : cameras) {
        c.eye.step(frameStep);
        c.eyeOffset.step(frameStep);
        c.target.step(frameStep);
        c.targetOffset.step(frameStep);
        c.fov.step(frameStep);
        c.fovOffset.step(frameStep);
        c.stereoDistance.step(frameStep);
        c.stereoStrength.step(frameStep);
        c.stereoNear.step(frameStep);
        c.stereoBase.step(frameStep);
        c.shake.step(frameStep);
    }
    for (auto& [hash, rig] : rigs) {
        if (rig.clip && !rig.finished) {
            rig.time += rig.clip->rate;
            if (rig.time >= float(rig.clip->lastFrame)) {
                rig.time = float(rig.clip->lastFrame);
                rig.finished = true;
            }
        }
    }
}

void Scene::updateObject(Object& o) {
    o.position.step(frameStep);
    o.offset.step(frameStep);
    o.scale.step(frameStep);
    o.alpha.step(frameStep);
    for (auto& [hash, a] : o.meshAlpha) {
        a.step(frameStep);
    }
    o.colour.step(frameStep);
    if (o.model && !o.motion.empty()) {
        // 0x18ff9c, a character's frame (body motion layers): with the lip
        // flag (+0x770 bit 3, 6841) a motion M with a twin "M*" plays the
        // twin (its talking variant; console trace of the profiles: 6850's
        // "001_..." and then "001_...*" in the same frame); the twin's link
        // (MINF +0x48) returns to M at its end, which turns into the twin
        // again, so the twin loops. Without the flag a twin goes back to M
        // after its pass (queued at the layer, +0xc). *Not modelled*: the
        // blend frames.
        const bool twin = o.motion.back() == '*';
        if (o.lipSync && !twin) {
            if (o.model->motion(o.motion + "*")) {
                o.motion += '*';
                o.motionTime = 0;
                o.motionDone = false;
            }
        } else if (!o.lipSync && twin && o.motionDone) {
            const std::string plain = o.motion.substr(0, o.motion.size() - 1);
            if (o.model->motion(plain)) {
                o.motion = plain;
                o.motionTime = 0;
                o.motionDone = false;
            }
        }
    }
    if (o.is3D || o.model) { // a 2D object may carry a model (5800)
        o.rotation3.step(frameStep);
        o.rotation3Offset.step(frameStep);
        o.scale3Offset.step(frameStep);
        // 5818: a step that reaches the stop frame ends on it (the stop is
        // an absolute clip frame, compared with first + time: 6812 keeps the
        // frame as first + v, 0x1e447c); negative speeds wrap below 0.
        auto stopAt = [](float first, float time, float step, bool stops, float stop, float& out) {
            const float now = first + time, next = now + step;
            if (stops && ((step >= 0 && now <= stop && next >= stop) ||
                          (step < 0 && now >= stop && next <= stop))) {
                out = stop - first;
                return true;
            }
            return false;
        };
        auto wrap = [](float t, float length) {
            t = std::fmod(t, length);
            return t < 0 ? t + length : t;
        };
        for (auto& l : o.upperMotions) {
            // As layer 0 below.
            const Motion* m = o.model && !l.motion.empty() ? o.model->motion(l.motion) : nullptr;
            if (m) {
                const float length = float(std::max(1, m->last - m->first + 1));
                if (stopAt(float(m->first), l.time, l.speed * m->rate * frameStep, l.stops, l.stop,
                           l.time)) {
                    continue;
                }
                // A motion's link plays at its end (MINF +0x48; 0x370730).
                const float step = l.speed * m->rate * frameStep;
                const Motion* linked = o.model->motionByHash(m->next);
                if (linked && l.time + step >= (l.once ? length - 1 : length)) {
                    l.motion = linked->name;
                    l.time = 0;
                    l.done = false;
                    continue;
                }
                if (l.once) {
                    l.time = std::min(l.time + l.speed * m->rate * frameStep, length - 1);
                    l.done = l.time >= length - 1;
                } else {
                    l.time = wrap(l.time + l.speed * m->rate * frameStep, length);
                    l.done = l.done || l.time + l.speed * m->rate * frameStep >= length;
                }
            }
        }
        if (o.model && !o.motion.empty()) {
            if (const Motion* m = o.model->motion(o.motion)) {
                const float length = float(std::max(1, m->last - m->first + 1));
                const float step = o.motionSpeed * m->rate * frameStep;
                const Motion* linked = o.model->motionByHash(m->next);
                if (stopAt(float(m->first), o.motionTime, o.motionSpeed * m->rate * frameStep,
                           o.motionStops, o.motionStop, o.motionTime)) {
                } else if (linked && o.motion.back() != '*' &&
                           o.motionTime + step >= (o.motionOnce ? length - 1 : length)) {
                    // A motion's link plays at its end (MINF +0x48: queued when
                    // it starts, 0x2a8014, played by the layer update,
                    // 0x370730..0x370764, unless play flag 0x10 is set - not
                    // modelled). The twins keep their own rule above.
                    o.motion = linked->name;
                    o.motionTime = 0;
                    o.motionDone = false;
                } else if (o.motionOnce) {
                    o.motionTime =
                        std::min(o.motionTime + o.motionSpeed * m->rate * frameStep, length - 1);
                    o.motionDone = o.motionTime >= length - 1;
                } else {
                    // 0x36f2c4: reaching the end sets the layer's end bits
                    // (+0x59 |= 6) whether it loops or not (6813 reads them).
                    const float next = o.motionTime + o.motionSpeed * m->rate * frameStep;
                    o.motionDone = o.motionDone || next >= length;
                    o.motionTime = wrap(next, length);
                }
            }
        }
        for (auto& f : o.faces) {
            if (o.model && !f.motion.empty()) {
                if (const Motion* m = o.model->faceMotion(f.motion)) {
                    // Hypothesis: face motions hold on their last frame.
                    f.time = std::min(f.time + m->rate * f.speed * frameStep,
                                      float(std::max(0, m->last - m->first)));
                }
            }
        }
        if (o.model && !o.materialMotion.empty()) {
            // As the bone motions: once (flag 8) holds on the last frame.
            if (const Motion* m = o.model->materialMotion(o.materialMotion)) {
                const float length = float(std::max(1, m->last - m->first + 1));
                if (o.materialMotionOnce) {
                    o.materialMotionTime =
                        std::min(o.materialMotionTime + o.materialMotionSpeed * m->rate * frameStep,
                                 length - 1);
                    o.materialMotionDone = o.materialMotionTime >= length - 1;
                } else {
                    o.materialMotionTime = std::fmod(
                        o.materialMotionTime + o.materialMotionSpeed * m->rate * frameStep, length);
                }
            }
        }
        if (o.popup) {
            o.layerFrames.resize(o.popup->animations.size());
            for (std::size_t i = 0; i < o.layerFrames.size(); ++i) {
                const unsigned length = o.popup->animations[i].length;
                o.layerFrames[i] = length ? (o.layerFrames[i] + 1) % length : 0;
            }
        }
    } else {
        o.rotation.step(frameStep);
        o.scaleOffset.step(frameStep);
        o.depth.step(frameStep);
        o.depthOffset.step(frameStep);
        o.stepAngle.step(frameStep);
    }
    if (o.animation && o.playing >= 0 && o.playing < int(o.animation->animations.size())) {
        const auto& a = o.animation->animations[std::size_t(o.playing)];
        if (!o.finished) {
            o.frame += unsigned(std::max(1.0f, frameStep));
            if (a.length && o.frame >= a.length) {
                ++o.passes;
                if (o.looping) {
                    o.frame %= a.length;
                } else {
                    o.frame = a.length - 1;
                    o.finished = true;
                }
            }
        }
    }
}

std::vector<Object*> Scene::updateOrder(bool solid) const {
    std::vector<Object*> list;
    for (const auto& [hash, o] : solid ? objects3D_ : objects_) {
        list.push_back(o.get());
    }
    std::sort(list.begin(), list.end(),
              [](const Object* a, const Object* b) { return a->order < b->order; });
    return list;
}

void Scene::update() {
    updateShared();
    for (const bool solid : {false, true}) {
        for (Object* o : updateOrder(solid)) {
            if (o->updating()) {
                updateObject(*o);
            }
        }
    }
}

namespace {

// Pixel position (y down from the top-left) to clip space.
ctr3d::Vertex screenVertex(const ctr3d::Target& t, float x, float y, float u, float v) {
    ctr3d::Vertex o;
    o.x = x / float(t.width) * 2.0f - 1.0f;
    o.y = 1.0f - y / float(t.height) * 2.0f;
    o.u = u;
    o.v = v;
    return o;
}

// An object's placement: local pixels (scaled, rotated about the object's
// position) to screen pixels.
struct Placement {
    float x = 0, y = 0, sx = 1, sy = 1, c = 1, s = 0;

    std::array<float, 2> operator()(float lx, float ly) const {
        const float px = lx * sx, py = ly * sy;
        return {x + px * c - py * s, y + px * s + py * c};
    }
};

void quad(ctr3d::Target& t, const ctr3d::DrawState& s, const Placement& at, float x0, float y0,
          float x1, float y1, float u0, float v0, float u1, float v1) {
    const auto a = at(x0, y0), b = at(x1, y0), c = at(x0, y1), d = at(x1, y1);
    const ctr3d::Vertex v[4] = {
        screenVertex(t, a[0], a[1], u0, v0), screenVertex(t, b[0], b[1], u1, v0),
        screenVertex(t, c[0], c[1], u0, v1), screenVertex(t, d[0], d[1], u1, v1)};
    static const std::uint16_t kIndices[6] = {0, 1, 2, 2, 1, 3};
    ctr3d::drawTriangles(t, s, v, kIndices);
}

void quad(ctr3d::Target& t, const ctr3d::DrawState& s, float x0, float y0, float x1, float y1,
          float u0, float v0, float u1, float v1) {
    quad(t, s, Placement{}, x0, y0, x1, y1, u0, v0, u1, v1);
}

// One cell of an animation set: its mesh with the material's texture,
// moved by a key's offset and placed by the object.
void drawCell(ctr3d::Target& target, const ctr3d::DrawState& state, const Placement& at,
              const AnimationSet& set, int index, const float offset[3], float angle = 0) {
    const float ca = std::cos(angle), sa = std::sin(angle);
    if (index < 0 || index >= int(set.cells.size())) {
        return;
    }
    const auto& cell = set.cells[std::size_t(index)];
    if (cell.buffer < 0) {
        return;
    }
    const auto& vb = set.buffers[std::size_t(cell.buffer)];
    const AnimationSet::Material* m =
        cell.material >= 0 ? &set.materials[std::size_t(cell.material)] : nullptr;
    std::vector<ctr3d::Vertex> verts(vb.count);
    for (unsigned v = 0; v < vb.count; ++v) {
        const float* p = vb.at(VertexBuffer::Position, v);
        float u = 0, w = 0;
        if (vb.has(VertexBuffer::TexCoord) && m) {
            const float* uv = vb.at(VertexBuffer::TexCoord, v);
            u = uv[0] * m->uvScale[0];
            w = uv[1] * m->uvScale[1];
        }
        const float px = angle != 0 ? p[0] * ca - p[1] * sa : p[0];
        const float py = angle != 0 ? p[0] * sa + p[1] * ca : p[1];
        const auto q = at(px + offset[0], py + offset[1]);
        verts[v] = screenVertex(target, q[0], q[1], u, w);
        if (vb.has(VertexBuffer::Colour)) {
            const float* c = vb.at(VertexBuffer::Colour, v);
            verts[v].r = c[0];
            verts[v].g = c[1];
            verts[v].b = c[2];
            verts[v].a = c[3];
        }
    }
    ctr3d::DrawState cs = state;
    cs.texture = m && m->texture >= 0 ? set.textures[std::size_t(m->texture)].get() : nullptr;
    ctr3d::drawTriangles(target, cs, verts, cell.indices);
}

// The key an animation shows `frame` frames in (-1: none).
int currentKey(const AnimationSet& set, const AnimationSet::Animation& a, unsigned frame) {
    int key = -1;
    for (int k = a.first; k < a.first + a.count && k < int(set.keys.size()); ++k) {
        key = k;
        if (frame < set.keys[std::size_t(k)].duration) {
            break;
        }
        frame -= set.keys[std::size_t(k)].duration;
    }
    return key;
}

// 0x2c4b40: inside the three points' box, the triangle not of zero area, and
// on the triangle's side of every edge (edges count as inside).
bool insideTriangle(const float* a, const float* b, const float* c, float x, float y) {
    if (x < std::min({a[0], b[0], c[0]}) || x > std::max({a[0], b[0], c[0]}) ||
        y < std::min({a[1], b[1], c[1]}) || y > std::max({a[1], b[1], c[1]})) {
        return false;
    }
    const float area = (b[0] - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (b[1] - a[1]);
    if (area == 0) {
        return false;
    }
    auto side = [&](const float* p, const float* q) {
        return ((p[0] - x) * (q[1] - y) - (q[0] - x) * (p[1] - y)) * area;
    };
    return side(a, b) >= 0 && side(b, c) >= 0 && side(c, a) >= 0;
}

// Whether a point (in the animation's space) lies on a triangle of a cell
// moved by `offset` (0x142da8).
bool cellHit(const AnimationSet& set, int index, const float offset[3], float x, float y) {
    if (index < 0 || index >= int(set.cells.size())) {
        return false;
    }
    const auto& cell = set.cells[std::size_t(index)];
    if (cell.buffer < 0) {
        return false;
    }
    const auto& vb = set.buffers[std::size_t(cell.buffer)];
    for (std::size_t i = 0; i + 2 < cell.indices.size(); i += 3) {
        // The original (0x142da8) skips triangles with a repeated vertex, the
        // zero-area joins of strips, which would otherwise hit along a line.
        const auto a = cell.indices[i], b = cell.indices[i + 1], d = cell.indices[i + 2];
        if (a == b || b == d || a == d) {
            continue;
        }
        float p[3][2];
        bool valid = true;
        for (int k = 0; k < 3; ++k) {
            const unsigned v = cell.indices[i + std::size_t(k)];
            if (v >= vb.count) {
                valid = false;
                break;
            }
            const float* q = vb.at(VertexBuffer::Position, v);
            p[k][0] = q[0] + offset[0];
            p[k][1] = q[1] + offset[1];
        }
        if (valid && insideTriangle(p[0], p[1], p[2], x, y)) {
            return true;
        }
    }
    return false;
}

} // namespace

std::array<float, 2> Scene::toLocal(const Object& o, float x, float y) const {
    const Transform t = screenTransform(o);
    const float c = std::cos(t.angle), s = std::sin(t.angle);
    const float dx = x - t.x, dy = y - t.y;
    return {t.sx != 0 ? (dx * c + dy * s) / t.sx : 0, t.sy != 0 ? (-dx * s + dy * c) / t.sy : 0};
}

int Scene::lineSegmentAt(const Object& o, float x, float y) const {
    if (!o.lines) {
        return -1;
    }
    const auto [lx, ly] = toLocal(o, x, y);
    int best = -1;
    float nearest = 0;
    for (std::size_t i = 0; i < o.lines->segments.size(); ++i) {
        const auto& s = o.lines->segments[i];
        if (s.colour[3] == 0) {
            continue;
        }
        const float ax = s.shown[0][0], ay = s.shown[0][1];
        const float dx = s.shown[1][0] - ax, dy = s.shown[1][1] - ay;
        const float len2 = dx * dx + dy * dy;
        const float t =
            len2 > 0 ? std::clamp(((lx - ax) * dx + (ly - ay) * dy) / len2, 0.0f, 1.0f) : 0;
        const float d = std::hypot(lx - (ax + t * dx), ly - (ay + t * dy));
        if (d <= s.width * 0.5f && (best < 0 || d < nearest)) {
            best = int(i);
            nearest = d;
        }
    }
    return best;
}

int Scene::hitTest(const Object& o, float x, float y) const {
    const auto [lx, ly] = toLocal(o, x, y);
    if (!o.areas.empty()) {
        for (std::size_t i = o.areas.size(); i-- > 0;) {
            const auto& a = o.areas[i];
            if (lx >= a.x && lx < a.x + a.w && ly >= a.y && ly < a.y + a.h) {
                return a.kind;
            }
        }
        return 0;
    }
    if (o.lines) {
        return lineSegmentAt(o, x, y) >= 0 ? 1 : 0;
    }
    auto inBox = [&](float x0, float y0, float w, float h) {
        return lx >= x0 && lx < x0 + w && ly >= y0 && ly < y0 + h;
    };
    if (o.animation && o.playing >= 0 && o.playing < int(o.animation->animations.size())) {
        const auto& set = *o.animation;
        const int key = currentKey(set, set.animations[std::size_t(o.playing)], o.frame);
        if (key >= 0 && set.keys[std::size_t(key)].composite >= 0) {
            const auto& k = set.keys[std::size_t(key)];
            const auto& comp = set.composites[std::size_t(k.composite)];
            for (int i = comp.first + comp.count; i-- > comp.first;) {
                if (cellHit(set, set.cellList[std::size_t(i)], k.offset, lx, ly)) {
                    return 1;
                }
            }
        }
        return 0;
    }
    if (o.picture) {
        return inBox(-o.pivot[0], -o.pivot[1], float(o.picture->width), float(o.picture->height))
                   ? 1
                   : 0;
    }
    if (o.rectangle) {
        return inBox(o.shape[0], o.shape[1], o.shape[2], o.shape[3]) ? 1 : 0;
    }
    return 0;
}

void Scene::draw(ctr3d::Target& target, const Object& o) const {
    if (o.capture) {
        // 0x295648: the screen drawn so far into the picture.
        Capture& cap = *o.capture;
        if (cap.count != 0 && cap.picture) {
            if (cap.count > 0) {
                --cap.count;
            }
            ctr3d::Texture& t = *cap.picture;
            const int w = std::min(t.width, target.width), h = std::min(t.height, target.height);
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    t.pixels[std::size_t(y) * std::size_t(t.width) + std::size_t(x)] =
                        target.colour[std::size_t(y) * std::size_t(target.width) + std::size_t(x)];
                }
            }
            cap.state = 1;
        }
        return;
    }
    if (o.model) {
        // A model on a 2D object (the sixth adventure's 5800 forms).
        draw3D(target, o, o.screen, true);
    }
    const Transform world = screenTransform(o);
    const float x = world.x, y = world.y, sx = world.sx, sy = world.sy, angle = world.angle;
    const Placement at{x, y, sx, sy, std::cos(angle), std::sin(angle)};
    ctr3d::DrawState state;
    state.colour[0] = o.colour[0];
    state.colour[1] = o.colour[1];
    state.colour[2] = o.colour[2];
    state.colour[3] = o.alpha[0];
    if (state.colour[3] <= 0.0f) {
        return;
    }
    if (o.rectangle) {
        quad(target, state, at, o.shape[0], o.shape[1], o.shape[0] + o.shape[2],
             o.shape[1] + o.shape[3], 0, 0, 1, 1);
    }
    for (std::size_t i = 0; i + 4 <= o.quads.size(); i += 4) {
        ctr3d::Vertex v[4];
        for (std::size_t k = 0; k < 4; ++k) {
            const auto p = at(o.quads[i + k][0], o.quads[i + k][1]);
            v[k] = screenVertex(target, p[0], p[1], 0, 0);
        }
        static const std::uint16_t kIndices[6] = {0, 1, 2, 0, 2, 3};
        ctr3d::DrawState qs = state;
        if (i / 4 < o.quadColours.size()) {
            for (std::size_t k = 0; k < 3; ++k) {
                qs.colour[k] *= o.quadColours[i / 4][k];
            }
        }
        ctr3d::drawTriangles(target, qs, v, kIndices);
    }
    if (o.animation) {
        for (const Object::Piece& piece : o.pieces) {
            const float offset[3] = {piece.x + piece.dx, piece.y + piece.dy, 0};
            drawCell(target, state, at, *o.animation, piece.cell, offset, piece.angle);
        }
    }
    if (o.lines) {
        // 0x247638 / 0x1c2280: each segment a quad along its ends, reaching
        // half the width past each, textured with the uv rect (u along the
        // segment, v across it: hypothesis until checked against the
        // reference), coloured by the segment.
        const auto& set = *o.lines;
        ctr3d::DrawState ls = state;
        ls.texture = set.picture.get();
        static const std::uint16_t kIndices[6] = {0, 1, 2, 0, 2, 3};
        for (const auto& seg : set.segments) {
            const float x0 = seg.shown[0][0], y0 = seg.shown[0][1];
            const float x1 = seg.shown[1][0], y1 = seg.shown[1][1];
            const float w = seg.width;
            const float dx = x1 - x0, dy = y1 - y0;
            const float len = std::sqrt(dx * dx + dy * dy);
            if (w <= 0 || seg.colour[3] <= 0) {
                continue;
            }
            // A segment without length lies along x (0x247638's fallback
            // direction).
            const float ux = len > 0 ? dx / len : 1.0f, uy = len > 0 ? dy / len : 0.0f;
            const float half = (len + w) * 0.5f, hw = w * 0.5f;
            const float mx = (x0 + x1) * 0.5f, my = (y0 + y1) * 0.5f;
            const float corners[4][2] = {{mx - ux * half + uy * hw, my - uy * half - ux * hw},
                                         {mx + ux * half + uy * hw, my + uy * half - ux * hw},
                                         {mx + ux * half - uy * hw, my + uy * half + ux * hw},
                                         {mx - ux * half - uy * hw, my - uy * half + ux * hw}};
            // The rect is in the picture's pixels: x, y, width, height.
            float tu0 = 0, tv0 = 0, tu1 = 1, tv1 = 1;
            if (set.picture && set.picture->width > 0 && set.picture->height > 0) {
                const float tw = float(set.picture->width), th = float(set.picture->height);
                tu0 = set.uv[0] / tw, tv0 = set.uv[1] / th;
                tu1 = (set.uv[0] + set.uv[2]) / tw, tv1 = (set.uv[1] + set.uv[3]) / th;
                if (set.tiled && w > 0 && set.uv[3] > 0) {
                    // 0x247638's fourth row: the rect repeats along the
                    // segment, once per width (its aspect kept).
                    tu1 = tu0 + (tu1 - tu0) * ((len + w) / w) * (set.uv[3] / set.uv[2]);
                }
            }
            const float uvs[4][2] = {{tu0, tv0}, {tu1, tv0}, {tu1, tv1}, {tu0, tv1}};
            ctr3d::Vertex v[4];
            for (int k = 0; k < 4; ++k) {
                const auto p = at(corners[k][0], corners[k][1]);
                v[k] = screenVertex(target, p[0], p[1], uvs[k][0], uvs[k][1]);
            }
            ctr3d::DrawState ss = ls;
            for (int k = 0; k < 4; ++k) {
                ss.colour[k] = state.colour[k] * seg.colour[k];
            }
            ctr3d::drawTriangles(target, ss, v, kIndices);
        }
    }
    const ctr3d::Texture* shownScreen =
        o.screenPicture >= 2 && o.screenPicture < kScreens && screenEnabled[o.screenPicture]
            ? &offscreenTexture_[o.screenPicture]
        : o.screenPicture >= 0 && o.screenPicture < 2 && screenKept[o.screenPicture]
            ? &keptTexture_[o.screenPicture]
            : nullptr;
    if (shownScreen && shownScreen->width > 0) {
        const auto& t = *shownScreen;
        state.texture = &t;
        quad(target, state, at, -o.pivot[0], -o.pivot[1], float(t.width) - o.pivot[0],
             float(t.height) - o.pivot[1], 0, 0, 1, 1);
    }
    if (o.picture && o.pictureCellList) {
        state.texture = o.picture.get();
        const float tw = float(std::max(1, o.picture->width)),
                    th = float(std::max(1, o.picture->height));
        for (const auto& cell : o.pictureCells) {
            quad(target, state, at, cell.x - o.pivot[0], cell.y - o.pivot[1],
                 cell.x + cell.w - o.pivot[0], cell.y + cell.h - o.pivot[1], cell.u / tw,
                 cell.v / th, (cell.u + cell.w) / tw, (cell.v + cell.h) / th);
        }
    } else if (o.picture) {
        state.texture = o.picture.get();
        if (o.pictureBlend == 7) {
            state.blend = ctr3d::DrawState::Blend::Multiply;
        }
        quad(target, state, at, -o.pivot[0], -o.pivot[1], float(o.picture->width) - o.pivot[0],
             float(o.picture->height) - o.pivot[1], 0, 0, 1, 1);
    }
    if (o.animation && o.playing >= 0 && o.playing < int(o.animation->animations.size())) {
        const auto& set = *o.animation;
        const auto& a = set.animations[std::size_t(o.playing)];
        const int key = currentKey(set, a, o.frame);
        if (key >= 0 && set.keys[std::size_t(key)].composite >= 0) {
            const auto& k = set.keys[std::size_t(key)];
            const auto& comp = set.composites[std::size_t(k.composite)];
            for (int i = comp.first; i < comp.first + comp.count; ++i) {
                drawCell(target, state, at, set, set.cellList[std::size_t(i)], k.offset);
            }
        }
    }
    if (o.animation && !o.glyphText.empty()) {
        // 5626 (0x187ea0): the text by Shift-JIS characters (one or two
        // bytes); each picks the animation (or, in sets without
        // animations, the cell) named by its bytes - pica_num_l names its
        // full-width digits "\x82\x4f".. - `glyphAdvance` apart; a line
        // feed starts the next line, `glyphLineAdvance` down. Characters
        // the set lacks (spaces) only advance.
        const auto& set = *o.animation;
        float penX = 0, penY = 0;
        for (std::size_t i = 0; i < o.glyphText.size();) {
            const unsigned char ch = (unsigned char)o.glyphText[i];
            const bool wide = (ch >= 0x81 && ch <= 0x9f) || (ch >= 0xe0 && ch <= 0xfc);
            const std::size_t n = wide && i + 1 < o.glyphText.size() ? 2 : 1;
            const std::string name = o.glyphText.substr(i, n);
            i += n;
            if (name == "\n") {
                penX = 0;
                penY += o.glyphLineAdvance;
                continue;
            }
            const float offset[3] = {penX, penY, 0};
            penX += o.glyphAdvance;
            if (const int a = set.animationIndex(name); a >= 0) {
                const auto q = at(offset[0], offset[1]);
                Object glyph;
                glyph.animation = o.animation;
                glyph.playing = a;
                glyph.position.set({q[0], q[1], 0});
                glyph.scale.set({sx, sy, 1});
                glyph.rotation.set({angle});
                glyph.alpha.set(o.alpha.value());
                glyph.colour.set(o.colour.value());
                draw(target, glyph);
                continue;
            }
            const std::uint32_t h = xq::hash(name);
            for (std::size_t c = 0; c < set.cells.size(); ++c) {
                if (set.cells[c].hash == h) {
                    drawCell(target, state, at, set, int(c), offset);
                    break;
                }
            }
        }
    }
    if (o.font && (!o.text.empty() || o.window)) {
        drawText(target, o, x, y, state);
    }
}

std::array<float, 4> Scene::paletteColour(int index) const {
    if (!palette || index < 0) {
        return {0, 0, 0, 1};
    }
    const int px = index & 15, py = index >> 4;
    if (px >= palette->width || py >= palette->height) {
        return {0, 0, 0, 1};
    }
    const auto& c = palette->pixels[std::size_t(py * palette->width + px)];
    return {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, 1.0f};
}

namespace {

// The font's line pitch (17 for the 18-pixel font, measured in the
// reference's question window).
float linePitch(const Font& f, const Object& o) {
    return o.lineSpacing ? float(f.lineHeight + *o.lineSpacing) : float(f.lineHeight - 1);
}

// Lines of display codes with their colour switches stripped: <CR> starts
// palette colour 3, </C> or </CR> returns to the text's colour.
struct Glyphs {
    std::vector<std::vector<std::pair<std::uint32_t, int>>> lines; // code, colour (-1 base)
};

// The glyphs a message has shown so far (its timeline's glyph list), or all
// of them.
Glyphs layout(const MessageFlow& m, bool all = false) {
    Glyphs g;
    g.lines.emplace_back();
    int line = 0, n = 0;
    for (const auto& gl : m.glyphs()) {
        if (!all && n++ >= m.shown()) {
            break;
        }
        while (line < gl.line) {
            g.lines.emplace_back();
            ++line;
        }
        if (!gl.ruby) {
            g.lines.back().push_back({gl.code, gl.colour});
        }
    }
    return g;
}

Glyphs layout(const std::string& text) {
    Glyphs g;
    g.lines.emplace_back();
    int colour = -1;
    const auto codes = decodeText(text);
    for (std::size_t i = 0; i < codes.size(); ++i) {
        const std::uint32_t c = codes[i];
        if (c == '<') {
            std::string tag;
            std::size_t j = i + 1;
            while (j < codes.size() && codes[j] != '>' && tag.size() < 12) {
                tag += char(codes[j]);
                ++j;
            }
            if (j < codes.size() && codes[j] == '>') {
                if (tag == "CR") {
                    colour = 3;
                } else if (!tag.empty() && tag[0] == '/') {
                    colour = -1;
                }
                i = j;
                continue;
            }
        }
        if (c == '\n') {
            g.lines.emplace_back();
            continue;
        }
        g.lines.back().push_back({c, colour});
    }
    return g;
}

// The advances of a line, with `spacing` between characters (5950).
float lineWidth(const Font& f, const std::vector<std::pair<std::uint32_t, int>>& line,
                int spacing = 0) {
    float w = 0;
    for (const auto& [c, colour] : line) {
        w += float(f.advance(c) + spacing);
    }
    return line.empty() ? w : w - float(spacing);
}

} // namespace

std::array<float, 2> Scene::measure(const Object& o) const {
    if (!o.font) {
        return {0, 0};
    }
    const Glyphs g = o.message && !o.message->empty() ? layout(*o.message, true) : layout(o.text);
    float w = 0;
    for (const auto& line : g.lines) {
        w = std::max(w, lineWidth(*o.font, line, o.letterSpacing));
    }
    return {w, float(g.lines.size()) * linePitch(*o.font, o)};
}

void Scene::drawText(ctr3d::Target& target, const Object& o, float x, float y,
                     const ctr3d::DrawState& base) const {
    const Font& font = *o.font;
    const auto size = measure(o);
    // The window frame: centred on the position, or (left-aligned texts)
    // from the position plus the offset, 8 pixels left and 3 up (measured
    // against the reference's question window at (4, 5)).
    if (o.window && o.windowSize[0] > 0 && o.windowSize[1] > 0) {
        float wx, wy;
        const float ww = o.windowSize[0], wh = o.windowSize[1];
        if (o.centreH) {
            wx = x - ww * 0.5f;
        } else {
            wx = x + o.textOffset[0] - 8;
        }
        if (o.centreV) {
            wy = y - wh * 0.5f;
        } else {
            wy = y + o.textOffset[1] - 3;
        }
        drawWindow(target, *o.window, wx, wy, ww, wh, base);
    }
    if (o.text.empty()) {
        return;
    }
    const Glyphs g = o.message && !o.message->empty() ? layout(*o.message) : layout(o.text);
    const float pitch = linePitch(font, o);
    const auto baseColour = o.textRgba ? *o.textRgba : paletteColour(o.textColour);
    // Every glyph quad of the text, drawn into `into` with `state` (the
    // glyph's colour unless `flat`).
    auto glyphs = [&](ctr3d::Target& into, const ctr3d::DrawState& state, bool flat, float dx,
                      float dy) {
        // Left-aligned glyphs start 3 right and 1 down of the position only
        // for texts 5950 never set (the puzzles' question window, where it
        // was measured); a text with 5950's values starts at the position
        // (console: the profiles' text, the rabbit's talk window and the
        // Note's titles sat 3 left of the port's). *Hypothesis*: the
        // defaults of 0x2cdd08 (1, 1, 1 without 0x287fa0) against 5950's.
        const float ox = o.lineSpacing ? 0.0f : 3.0f, oy = o.lineSpacing ? 0.0f : 1.0f;
        float penY = o.centreV ? y - size[1] * 0.5f : y + o.textOffset[1] + oy;
        for (const auto& line : g.lines) {
            float penX = o.centreH ? x - lineWidth(font, line, o.letterSpacing) * 0.5f
                                   : x + o.textOffset[0] + ox;
            penX = std::floor(penX + 0.5f);
            for (const auto& [c, colour] : line) {
                const Font::Glyph* gl = font.glyph(c);
                if (!gl) {
                    continue;
                }
                if (gl->width > 0 && gl->height > 0) {
                    const auto& sheet = *font.planes[gl->plane];
                    ctr3d::DrawState gs = state;
                    gs.texture = &sheet;
                    if (!flat) {
                        const auto rgb = colour >= 0 ? paletteColour(colour) : baseColour;
                        for (int i = 0; i < 3; ++i) {
                            gs.colour[i] = state.colour[i] * rgb[std::size_t(i)];
                        }
                    }
                    const float gx = penX + float(gl->offsetX) + dx,
                                gy = std::floor(penY) + float(gl->offsetY) + dy;
                    quad(into, gs, gx, gy, gx + float(gl->width), gy + float(gl->height),
                         float(gl->x) / float(sheet.width), float(gl->y) / float(sheet.height),
                         float(gl->x + gl->width) / float(sheet.width),
                         float(gl->y + gl->height) / float(sheet.height));
                }
                penX += float(gl->advance + o.letterSpacing);
            }
            penY += pitch;
        }
    };
    if (o.edgeWidth > 0 && o.edgeColour[3] > 0) {
        // The edge (hypothesis for the look, matched against the reference
        // intro title: solid edge colour about 3 pixels out for width 4):
        // the glyphs' coverage stamped at every offset within 0.75 x the
        // width, each stamp at the edge alpha, the stamps accumulating.
        ctr3d::Target mask(target.width, target.height);
        mask.clear({0, 0, 0, 0});
        ctr3d::DrawState ms;
        glyphs(mask, ms, true, 0, 0);
        const float radius = o.edgeWidth * 0.75f;
        const int r = int(std::ceil(radius));
        const float alpha = o.edgeColour[3];
        for (int py = 0; py < target.height; ++py) {
            for (int px = 0; px < target.width; ++px) {
                float keep = 1; // what the stamps leave of the background
                for (int dy = -r; dy <= r; ++dy) {
                    const int qy = py + dy;
                    if (qy < 0 || qy >= target.height) {
                        continue;
                    }
                    for (int dx = -r; dx <= r; ++dx) {
                        const int qx = px + dx;
                        if (qx < 0 || qx >= target.width ||
                            float(dx * dx + dy * dy) > radius * radius) {
                            continue;
                        }
                        const auto cover = mask.colour[std::size_t(qy * target.width + qx)].a;
                        if (cover) {
                            keep *= 1 - alpha * float(cover) / 255.0f;
                        }
                    }
                }
                if (keep >= 1) {
                    continue;
                }
                const float a = (1 - keep) * base.colour[3];
                auto& d = target.colour[std::size_t(py * target.width + px)];
                auto mix = [&](std::uint8_t v, float e) {
                    return std::uint8_t(std::lround(float(v) * (1 - a) + e * 255.0f * a));
                };
                d.r = mix(d.r, o.edgeColour[0]);
                d.g = mix(d.g, o.edgeColour[1]);
                d.b = mix(d.b, o.edgeColour[2]);
            }
        }
    }
    glyphs(target, base, false, 0, 0);
}

void Scene::drawWindow(ctr3d::Target& target, const AnimationSet& set, float x, float y, float w,
                       float h, const ctr3d::DrawState& base) const {
    // Nine cells: corners at their size, edges and centre stretched.
    auto cell = [&](const char* name, float x0, float y0, float x1, float y1) {
        const int a = set.animationIndex(name);
        if (a < 0 || x1 <= x0 || y1 <= y0) {
            return;
        }
        const auto& anim = set.animations[std::size_t(a)];
        if (anim.count < 1) {
            return;
        }
        const auto& key = set.keys[std::size_t(anim.first)];
        if (key.composite < 0) {
            return;
        }
        const auto& comp = set.composites[std::size_t(key.composite)];
        for (int i = comp.first; i < comp.first + comp.count; ++i) {
            const int ci = set.cellList[std::size_t(i)];
            if (ci < 0) {
                continue;
            }
            const auto& c = set.cells[std::size_t(ci)];
            if (c.buffer < 0 || c.material < 0) {
                continue;
            }
            const auto& vb = set.buffers[std::size_t(c.buffer)];
            const auto& m = set.materials[std::size_t(c.material)];
            // The cell's texture rectangle from its vertices.
            float u0 = 1e9f, v0 = 1e9f, u1 = -1e9f, v1 = -1e9f;
            for (std::uint16_t idx : c.indices) {
                const float* uv = vb.at(VertexBuffer::TexCoord, idx);
                u0 = std::min(u0, uv[0]);
                v0 = std::min(v0, uv[1]);
                u1 = std::max(u1, uv[0]);
                v1 = std::max(v1, uv[1]);
            }
            ctr3d::DrawState cs = base;
            cs.texture = m.texture >= 0 ? set.textures[std::size_t(m.texture)].get() : nullptr;
            quad(target, cs, x0, y0, x1, y1, u0 * m.uvScale[0], v0 * m.uvScale[1],
                 u1 * m.uvScale[0], v1 * m.uvScale[1]);
        }
    };
    const float e = 8;
    cell("LT", x, y, x + e, y + e);
    cell("CT", x + e, y, x + w - e, y + e);
    cell("RT", x + w - e, y, x + w, y + e);
    cell("LC", x, y + e, x + e, y + h - e);
    cell("CC", x + e, y + e, x + w - e, y + h - e);
    cell("RC", x + w - e, y + e, x + w, y + h - e);
    cell("LB", x, y + h - e, x + e, y + h);
    cell("CB", x + e, y + h - e, x + w - e, y + h);
    cell("RB", x + w - e, y + h - e, x + w, y + h);
}

Camera::Camera() {
    // The target 160 in front of the eye (the reference's puzzle camera,
    // RAM 0x8fafc5c +0x3e0 in puzzle 2); the pointer puzzles pan by moving
    // it up to 13.5 sideways.
    target.set({0, 0, -160});
    fov.set({fovY});
}

Scene::View Scene::viewFor(int screen) const {
    View v;
    const Camera& cam = cameras[screen];
    v.fovY = cam.fov[0] + cam.fovOffset[0];
    float up[3] = {0, 1, 0};
    {
        float len = 0;
        for (int i = 0; i < 3; ++i) {
            v.eye[i] = cam.eye[i] + cam.eyeOffset[i];
            v.forward[i] = cam.target[i] + cam.targetOffset[i] - v.eye[i];
            len += v.forward[i] * v.forward[i];
        }
        len = std::sqrt(len);
        if (len > 0) {
            for (float& c : v.forward) {
                c /= len;
            }
        } else {
            v.forward[0] = 0, v.forward[1] = 0, v.forward[2] = -1;
        }
    }
    if (const auto it = rigs.find(screenRig[screen]); it != rigs.end() && it->second.clip) {
        const CameraClip& clip = *it->second.clip;
        const float t = it->second.time;
        const auto e = clip.eye.at(t), g = clip.target.at(t);
        float len = 0;
        for (int i = 0; i < 3; ++i) {
            v.eye[i] = e[std::size_t(i)];
            v.forward[i] = g[std::size_t(i)] - e[std::size_t(i)];
            len += v.forward[i] * v.forward[i];
        }
        len = std::sqrt(len);
        if (len > 0) {
            for (float& c : v.forward) {
                c /= len;
            }
        }
        if (!clip.fov.values.empty()) {
            v.fovY = clip.fov.at(t)[0];
        }
        const float roll = clip.roll.values.empty() ? 0.0f : clip.roll.at(t)[0];
        up[0] = -std::sin(roll);
        up[1] = std::cos(roll);
    }
    // Camera axes: right = forward x up, true up = right x forward.
    v.right[0] = v.forward[1] * up[2] - v.forward[2] * up[1];
    v.right[1] = v.forward[2] * up[0] - v.forward[0] * up[2];
    v.right[2] = v.forward[0] * up[1] - v.forward[1] * up[0];
    const float l =
        std::sqrt(v.right[0] * v.right[0] + v.right[1] * v.right[1] + v.right[2] * v.right[2]);
    for (float& c : v.right) {
        c = l > 0 ? c / l : c;
    }
    v.up[0] = v.right[1] * v.forward[2] - v.right[2] * v.forward[1];
    v.up[1] = v.right[2] * v.forward[0] - v.right[0] * v.forward[2];
    v.up[2] = v.right[0] * v.forward[1] - v.right[1] * v.forward[0];
    return v;
}

namespace {

// An object's own transform as rows: the game's T * Rx * Ry * Rz * S
// (0x2c8500: scale, then the turns about z, y and x - radians, the rotation
// plus its offset - then the position plus its offset); `extra` multiplies
// the scale, `scaled` false leaves the scale out (for normals).
Affine localMatrix(const Object& o, float extra, bool scaled) {
    const float rx = o.rotation3[0] + o.rotation3Offset[0],
                ry = o.rotation3[1] + o.rotation3Offset[1],
                rz = o.rotation3[2] + o.rotation3Offset[2];
    Affine s, x, y, z, t;
    if (scaled) {
        s.m = {(o.scale[0] + o.scale3Offset[0]) * extra, 0, 0, 0, 0,
               (o.scale[1] + o.scale3Offset[1]) * extra, 0, 0, 0, 0,
               (o.scale[2] + o.scale3Offset[2]) * extra, 0};
    }
    x.m = {1, 0, 0, 0, 0, std::cos(rx), -std::sin(rx), 0, 0, std::sin(rx), std::cos(rx), 0};
    y.m = {std::cos(ry), 0, std::sin(ry), 0, 0, 1, 0, 0, -std::sin(ry), 0, std::cos(ry), 0};
    z.m = {std::cos(rz), -std::sin(rz), 0, 0, std::sin(rz), std::cos(rz), 0, 0, 0, 0, 1, 0};
    if (scaled) {
        t.m = {1, 0, 0, o.position[0] + o.offset[0], 0, 1, 0, o.position[1] + o.offset[1],
               0, 0, 1, o.position[2] + o.offset[2]};
    }
    return t * (x * (y * (z * s)));
}

} // namespace

// 0x159678: an instance's matrix, T * Rx * Ry * Rz * S (rotation zero: the
// scale alone), or only T for a positions-only merge.
Affine Object::Merged::matrix(std::size_t i) const {
    const Instance& in = instances[i];
    Affine m;
    if (positionsOnly) {
        m.m = {1, 0, 0, in.position[0], 0, 1, 0, in.position[1], 0, 0, 1, in.position[2]};
        return m;
    }
    const float a = in.rotation[0], b = in.rotation[1], c = in.rotation[2];
    const float sa = std::sin(a), ca = std::cos(a), sb = std::sin(b), cb = std::cos(b),
                sc = std::sin(c), cc = std::cos(c);
    const float sx = in.scale[0], sy = in.scale[1], sz = in.scale[2];
    const bool turned = a * a + b * b + c * c > 0.0f;
    m.m = {turned ? cb * cc * sx : sx,
           turned ? -cb * sc * sy : 0.0f,
           turned ? sb * sz : 0.0f,
           in.position[0],
           turned ? (sa * sb * cc + ca * sc) * sx : 0.0f,
           turned ? (-sa * sb * sc + ca * cc) * sy : sy,
           turned ? -sa * cb * sz : 0.0f,
           in.position[1],
           turned ? (-ca * sb * cc + sa * sc) * sx : 0.0f,
           turned ? (ca * sb * sc + sa * cc) * sy : 0.0f,
           turned ? ca * cb * sz : sz,
           in.position[2]};
    return m;
}

namespace {

Affine inverse(const Affine& a) {
    const auto& m = a.m;
    const float det = m[0] * (m[5] * m[10] - m[6] * m[9]) - m[1] * (m[4] * m[10] - m[6] * m[8]) +
                      m[2] * (m[4] * m[9] - m[5] * m[8]);
    Affine r;
    if (det == 0) {
        return r;
    }
    const float d = 1.0f / det;
    r.m[0] = (m[5] * m[10] - m[6] * m[9]) * d;
    r.m[1] = (m[2] * m[9] - m[1] * m[10]) * d;
    r.m[2] = (m[1] * m[6] - m[2] * m[5]) * d;
    r.m[4] = (m[6] * m[8] - m[4] * m[10]) * d;
    r.m[5] = (m[0] * m[10] - m[2] * m[8]) * d;
    r.m[6] = (m[2] * m[4] - m[0] * m[6]) * d;
    r.m[8] = (m[4] * m[9] - m[5] * m[8]) * d;
    r.m[9] = (m[1] * m[8] - m[0] * m[9]) * d;
    r.m[10] = (m[0] * m[5] - m[1] * m[4]) * d;
    for (int i = 0; i < 3; ++i) {
        r.m[std::size_t(i * 4 + 3)] =
            -(r.m[std::size_t(i * 4)] * m[3] + r.m[std::size_t(i * 4 + 1)] * m[7] +
              r.m[std::size_t(i * 4 + 2)] * m[11]);
    }
    return r;
}

} // namespace

Scene::Pick Scene::pick2DAt(int screen, float x, float y, std::uint32_t skip) const {
    Pick best;
    std::vector<const Object*> list;
    for (const auto& [hash, o] : objects_) {
        // The game walks a screen's touch list (0x3467c0); taken here as
        // the objects with touch areas or handlers (hypothesis: the
        // backgrounds are not in it - puzzle 027's stones would hit them).
        if (o->screen == screen && o->hash != skip && o->touchable() &&
            (!o->areas.empty() || o->handlerMode)) {
            list.push_back(o.get());
        }
    }
    std::sort(list.begin(), list.end(), [](const Object* a, const Object* b) {
        return a->priority != b->priority ? a->priority > b->priority : a->listOrder > b->listOrder;
    });
    for (const Object* o : list) {
        const int kind = hitTest(*o, x, y);
        if (kind > 0) {
            best.hit = true;
            best.solid = false;
            best.hash = o->hash;
            best.kind = kind;
            best.point[0] = x, best.point[1] = y, best.point[2] = 0;
            return best;
        }
    }
    return best;
}

namespace {

float dot3(const float* a, const float* b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

// The ray o + t d (t >= 0) entering a sphere; -1 when it misses (from
// inside: 0).
float raySphere(const float* c, float r, const float* o, const float* d) {
    // 0x2bf2d8: the entry along the ray; a ray that starts inside misses
    // (its entry lies behind it), so a big shape around the camera never
    // hides the objects in it.
    const float m[3] = {o[0] - c[0], o[1] - c[1], o[2] - c[2]};
    const float a = dot3(d, d), b = dot3(m, d), k = dot3(m, m) - r * r;
    if (k <= 0) {
        return -1;
    }
    const float disc = b * b - a * k;
    if (a <= 0 || b > 0 || disc < 0) {
        return -1;
    }
    return (-b - std::sqrt(disc)) / a;
}

} // namespace

// A touch shape against the ray o + t d (the object's space); the entry
// t, or -1.
float Scene::rayShape(const Object::Shape& sh, const std::array<float, 3>& origin,
                      const std::array<float, 3>& dir) {
    const float* o = origin.data();
    const float* d = dir.data();
    if (sh.type == 0) {
        return raySphere(sh.a, sh.radius, o, d);
    }
    if (sh.type == 1) {
        // The capsule: the cylinder between the ends and a sphere at each.
        float best = -1;
        auto keep = [&](float t) {
            if (t >= 0 && (best < 0 || t < best)) {
                best = t;
            }
        };
        keep(raySphere(sh.a, sh.radius, o, d));
        keep(raySphere(sh.b, sh.radius, o, d));
        const float axis[3] = {sh.b[0] - sh.a[0], sh.b[1] - sh.a[1], sh.b[2] - sh.a[2]};
        const float m[3] = {o[0] - sh.a[0], o[1] - sh.a[1], o[2] - sh.a[2]};
        const float dd = dot3(axis, axis), md = dot3(m, axis), nd = dot3(d, axis);
        const float nn = dot3(d, d), mn = dot3(m, d);
        const float a = dd * nn - nd * nd;
        const float k = dot3(m, m) - sh.radius * sh.radius;
        const float cc = dd * k - md * md;
        if (dd > 0 && std::fabs(a) > 1e-12f) {
            const float b = dd * mn - nd * md;
            const float disc = b * b - a * cc;
            if (disc >= 0) {
                const float t = (-b - std::sqrt(disc)) / a;
                const float s = md + t * nd;
                if (s >= 0 && s <= dd) {
                    keep(t);
                }
            }
        }
        return best;
    }
    // A box between the two corners (slab test). 0x150810 tests only the
    // three faces that face the ray: from inside, no hit.
    float t0 = -1e30f, t1 = 1e30f;
    for (int i = 0; i < 3; ++i) {
        const float lo = std::min(sh.a[i], sh.b[i]), hi = std::max(sh.a[i], sh.b[i]);
        if (std::fabs(d[i]) < 1e-9f) {
            if (o[i] < lo || o[i] > hi) {
                return -1;
            }
            continue;
        }
        float ta = (lo - o[i]) / d[i];
        float tb = (hi - o[i]) / d[i];
        if (ta > tb) {
            std::swap(ta, tb);
        }
        t0 = std::max(t0, ta);
        t1 = std::min(t1, tb);
        if (t0 > t1) {
            return -1;
        }
    }
    return t0 >= 0 ? t0 : -1;
}

Scene::MeshHit Scene::meshPick(const Object& o, int screen, float x, float y) const {
    MeshHit best;
    if (screen < 0 || screen >= kScreens || !o.model) {
        return best;
    }
    const View v = viewFor(screen);
    const float w = screen == 1 ? 320.0f : 400.0f, h = 240.0f;
    const float f = std::tan(v.fovY * 0.5f);
    const float nx = (x / w * 2 - 1) * f * (w / h), ny = (1 - y / h * 2) * f;
    float dir[3];
    for (int i = 0; i < 3; ++i) {
        dir[i] = v.forward[i] + v.right[i] * nx + v.up[i] * ny;
    }
    const Model& model = *o.model;
    const std::vector<Affine> pose = posedModel(o, model);
    const Affine world = worldMatrix(o, 1.0f, true);
    float nearest = 1e30f;
    for (const auto& mesh : model.meshes) {
        if ((mesh.passes & 2) == 0 || mesh.indices.empty()) {
            continue;
        }
        bool weighted = false;
        const auto skin = meshSkin(model, mesh, pose, weighted);
        const auto& vb = mesh.vertices;
        std::vector<std::array<float, 3>> pts(vb.count);
        for (unsigned i = 0; i < vb.count; ++i) {
            const auto sp = skinnedPosition(vb, skin, weighted, i);
            const float q[3] = {sp[0], sp[1], sp[2]};
            pts[i] = world.apply(q);
        }
        for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
            const auto& a = pts[mesh.indices[t]];
            const auto& b = pts[mesh.indices[t + 1]];
            const auto& c = pts[mesh.indices[t + 2]];
            // Moller-Trumbore.
            const float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
            const float e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
            const float pv[3] = {dir[1] * e2[2] - dir[2] * e2[1], dir[2] * e2[0] - dir[0] * e2[2],
                                 dir[0] * e2[1] - dir[1] * e2[0]};
            const float det = e1[0] * pv[0] + e1[1] * pv[1] + e1[2] * pv[2];
            if (std::fabs(det) < 1e-9f) {
                continue;
            }
            const float inv = 1.0f / det;
            const float tv[3] = {v.eye[0] - a[0], v.eye[1] - a[1], v.eye[2] - a[2]};
            const float u = (tv[0] * pv[0] + tv[1] * pv[1] + tv[2] * pv[2]) * inv;
            if (u < 0 || u > 1) {
                continue;
            }
            const float qv[3] = {tv[1] * e1[2] - tv[2] * e1[1], tv[2] * e1[0] - tv[0] * e1[2],
                                 tv[0] * e1[1] - tv[1] * e1[0]};
            const float vv = (dir[0] * qv[0] + dir[1] * qv[1] + dir[2] * qv[2]) * inv;
            if (vv < 0 || u + vv > 1) {
                continue;
            }
            const float d = (e2[0] * qv[0] + e2[1] * qv[1] + e2[2] * qv[2]) * inv;
            if (d <= 0 || d >= nearest) {
                continue;
            }
            nearest = d;
            best.hit = true;
            best.distance = d;
            for (int k = 0; k < 3; ++k) {
                best.point[std::size_t(k)] = v.eye[k] + dir[k] * d;
            }
            float n3[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
                           e1[0] * e2[1] - e1[1] * e2[0]};
            const float nl = std::sqrt(n3[0] * n3[0] + n3[1] * n3[1] + n3[2] * n3[2]);
            if (nl > 0) {
                // Facing the viewer.
                const float side =
                    n3[0] * dir[0] + n3[1] * dir[1] + n3[2] * dir[2] > 0 ? -1.0f : 1.0f;
                for (int k = 0; k < 3; ++k) {
                    best.normal[std::size_t(k)] = n3[k] / nl * side;
                }
            }
            best.material = mesh.material;
        }
    }
    return best;
}

Scene::Pick Scene::pickAt(int screen, float x, float y) const {
    Pick best;
    if (screen < 0 || screen >= kScreens) {
        return best;
    }
    const View v = viewFor(screen);
    // The ray through the pixel (top screen 400 x 240).
    const float w = screen == 1 ? 320.0f : 400.0f, h = 240.0f;
    const float f = std::tan(v.fovY * 0.5f);
    const float nx = (x / w * 2 - 1) * f * (w / h), ny = (1 - y / h * 2) * f;
    float dir[3];
    for (int i = 0; i < 3; ++i) {
        dir[i] = v.forward[i] + v.right[i] * nx + v.up[i] * ny;
    }
    float nearest = 1e30f;
    for (const auto& [hash, o] : objects3D_) {
        if (o->screen != screen || !o->touchable()) {
            continue;
        }
        if (o->shapes.empty()) {
            // 0x3527e4: an object without touch boxes is hit by its content
            // (vtable +0x24): a pop-up by its layers' triangles (0x341158 ->
            // 0x2c2798), the nearest along the ray; kind 1. Puzzle 002's
            // cursor falls on the background between the people.
            if (!o->popup) {
                continue;
            }
            float extra = 1.0f;
            if (const Resource* pr = resource(o->popupResource, Resource::Kind::Popup)) {
                extra = pr->popScale;
            }
            Affine m = worldMatrix(*o, 1.0f, true);
            for (int r = 0; r < 3; ++r) {
                for (int c = 0; c < 3; ++c) {
                    m.m[std::size_t(r * 4 + c)] *= extra;
                }
            }
            const Affine inv = inverse(m);
            const auto origin = inv.apply(v.eye);
            const auto d = inv.rotate(dir);
            const AnimationSet& set = *o->popup;
            for (std::size_t li = 0; li < set.animations.size(); ++li) {
                const auto& a = set.animations[li];
                unsigned t = li < o->layerFrames.size() ? o->layerFrames[li] : 0;
                int key = -1;
                for (int k = a.first; k < a.first + a.count && k < int(set.keys.size()); ++k) {
                    key = k;
                    if (t < set.keys[std::size_t(k)].duration) {
                        break;
                    }
                    t -= set.keys[std::size_t(k)].duration;
                }
                if (key < 0 || set.keys[std::size_t(key)].composite < 0) {
                    continue;
                }
                const auto& comp =
                    set.composites[std::size_t(set.keys[std::size_t(key)].composite)];
                for (int i = comp.first; i < comp.first + comp.count; ++i) {
                    const int ci = set.cellList[std::size_t(i)];
                    if (ci < 0 || set.cells[std::size_t(ci)].buffer < 0) {
                        continue;
                    }
                    const auto& cell = set.cells[std::size_t(ci)];
                    const auto& vb = set.buffers[std::size_t(cell.buffer)];
                    for (std::size_t k = 0; k + 2 < cell.indices.size(); k += 3) {
                        const float* p0 = vb.at(VertexBuffer::Position, cell.indices[k]);
                        const float* p1 = vb.at(VertexBuffer::Position, cell.indices[k + 1]);
                        const float* p2 = vb.at(VertexBuffer::Position, cell.indices[k + 2]);
                        // Moller-Trumbore, both faces.
                        const float e1[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
                        const float e2[3] = {p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]};
                        const float pv[3] = {d[1] * e2[2] - d[2] * e2[1],
                                             d[2] * e2[0] - d[0] * e2[2],
                                             d[0] * e2[1] - d[1] * e2[0]};
                        const float det = e1[0] * pv[0] + e1[1] * pv[1] + e1[2] * pv[2];
                        if (std::fabs(det) < 1e-12f) {
                            continue;
                        }
                        const float inv_det = 1.0f / det;
                        const float tv[3] = {origin[0] - p0[0], origin[1] - p0[1],
                                             origin[2] - p0[2]};
                        const float u = (tv[0] * pv[0] + tv[1] * pv[1] + tv[2] * pv[2]) * inv_det;
                        if (u < 0 || u > 1) {
                            continue;
                        }
                        const float qv[3] = {tv[1] * e1[2] - tv[2] * e1[1],
                                             tv[2] * e1[0] - tv[0] * e1[2],
                                             tv[0] * e1[1] - tv[1] * e1[0]};
                        const float w = (d[0] * qv[0] + d[1] * qv[1] + d[2] * qv[2]) * inv_det;
                        if (w < 0 || u + w > 1) {
                            continue;
                        }
                        const float tt = (e2[0] * qv[0] + e2[1] * qv[1] + e2[2] * qv[2]) * inv_det;
                        if (tt > 0 && tt < nearest) {
                            nearest = tt;
                            best.hit = true;
                            best.solid = true;
                            best.hash = o->hash;
                            best.kind = 1;
                            for (int c = 0; c < 3; ++c) {
                                best.point[c] = v.eye[c] + dir[c] * tt;
                            }
                        }
                    }
                }
            }
            continue;
        }
        const Affine inv = inverse(worldMatrix(*o, 1.0f, true));
        const auto origin = inv.apply(v.eye);
        const auto d = inv.rotate(dir);
        for (const auto& sh : o->shapes) {
            // 0x3527e4: each shape against the ray in the object's own
            // space (0x15cdc4 sphere, 0x15f83c capsule, 0x150810 box); t is
            // in units of the world ray, so hits compare across objects.
            const float t0 = rayShape(sh, origin, d);
            if (t0 >= 0 && t0 < nearest) {
                nearest = t0;
                best.hit = true;
                best.solid = true;
                best.hash = o->hash;
                best.kind = sh.kind;
                for (int i = 0; i < 3; ++i) {
                    best.point[i] = v.eye[i] + dir[i] * t0;
                }
            }
        }
    }
    return best;
}

// The models' outlines: the silhouette geometry shader (fix.xr GEO000,
// SHD000 program 11, "dmp_Silhouette") fed by #FIX_SIL_MDS (VTX012).
// Along every edge between a triangle facing the eye and one facing away
// (front faces counter-clockwise) it adds a quad: the edge, and the edge
// moved out in clip space by width x n x w (scaleByW), where n is the
// vertex's view-space normal x, y normalised and divided by w clamped to
// 15..45 (VTX012); so the outline is width / clamp(w, 15, 45) wide in
// device coordinates. The colour is the silhouette's combiner constant.
// Open edges (one triangle; the shader's openEdge path) are not drawn
// (not decoded yet).
static void drawSilhouette(ctr3d::Target& target, const ctr3d::DrawState& base, const Mesh& mesh,
                           const std::vector<ctr3d::Vertex>& verts,
                           const std::vector<std::array<float, 2>>& normals,
                           const float* colour) {
    auto facing = [&](int t) {
        const std::size_t i = std::size_t(t) * 3;
        if (i + 2 >= mesh.indices.size()) {
            return 0.0f;
        }
        const ctr3d::Vertex& a = verts[mesh.indices[i]];
        const ctr3d::Vertex& b = verts[mesh.indices[i + 1]];
        const ctr3d::Vertex& c = verts[mesh.indices[i + 2]];
        if (a.w <= 0 || b.w <= 0 || c.w <= 0) {
            return 0.0f;
        }
        const float ax = a.x / a.w, ay = a.y / a.w, bx = b.x / b.w, by = b.y / b.w, cx = c.x / c.w,
                    cy = c.y / c.w;
        return (bx - ax) * (cy - ay) - (cx - ax) * (by - ay); // > 0: counter-clockwise (front)
    };
    std::vector<ctr3d::Vertex> quads;
    std::vector<std::uint16_t> indices;
    auto moved = [&](std::uint16_t i) {
        ctr3d::Vertex v = verts[i];
        const auto& n = normals[i];
        const float l = std::sqrt(n[0] * n[0] + n[1] * n[1]);
        if (l > 0 && v.w > 0) {
            const float k = mesh.outlineWidth / std::clamp(v.w, 15.0f, 45.0f) * v.w / l;
            v.x += n[0] * k;
            v.y += n[1] * k;
        }
        v.r = colour[0], v.g = colour[1], v.b = colour[2], v.a = 1;
        return v;
    };
    for (const auto& e : mesh.edges) {
        if (e.right < 0 || e.a >= verts.size() || e.b >= verts.size() || quads.size() > 60000) {
            continue;
        }
        const float f1 = facing(e.left), f2 = facing(e.right);
        if (!((f1 > 0 && f2 <= 0) || (f1 <= 0 && f2 > 0))) {
            continue;
        }
        const auto base0 = std::uint16_t(quads.size());
        ctr3d::Vertex a = verts[e.a], b = verts[e.b];
        for (ctr3d::Vertex* v : {&a, &b}) {
            v->r = mesh.outlineColour[0], v->g = mesh.outlineColour[1],
            v->b = mesh.outlineColour[2], v->a = 1;
        }
        quads.push_back(a);
        quads.push_back(b);
        quads.push_back(moved(e.a));
        quads.push_back(moved(e.b));
        for (const int k : {0, 1, 2, 2, 1, 3}) {
            indices.push_back(std::uint16_t(base0 + k));
        }
    }
    if (indices.empty()) {
        return;
    }
    ctr3d::DrawState s = base;
    s.texture = nullptr;
    s.cull = ctr3d::DrawState::Cull::None;
    s.fragmentLighting = false;
    ctr3d::drawTriangles(target, s, quads, indices);
}

std::vector<Affine> Scene::posedModel(const Object& own, const Model& model) const {
    // A model linked to another object's (6878) plays that one's motions.
    const auto source = own.motionSource ? objects3D_.find(own.motionSource) : objects3D_.end();
    const Object* linked = source != objects3D_.end() ? source->second.get() : nullptr;
    const Object& o = linked ? *linked : own;
    std::vector<std::pair<const Clip*, float>> layers;
    bool missing = false; // layer 0's motion without a clip
    if (!o.motion.empty()) {
        if (const Motion* m = model.motion(o.motion)) {
            const Clip* clip = model.clipFor(*m);
            missing = clip == nullptr;
            layers.push_back({clip, float(m->first) + o.motionTime});
        }
    }
    bool upper = false;
    for (const auto& l : o.upperMotions) {
        if (const Motion* m = l.motion.empty() ? nullptr : model.motion(l.motion)) {
            if (const Clip* clip = model.clipFor(*m)) {
                layers.push_back({clip, float(m->first) + l.time});
                upper = true;
            }
        }
    }
    if (missing && !upper) {
        return {}; // as before: an unknown clip leaves the meshes unposed
    }
    return model.posedLayers(layers);
}

void Scene::draw3D(ctr3d::Target& target, const Object3D& o, int screen, bool flat) const {
    if (!o.popup && !o.model && !o.animation && !o.picture && !o.merged) {
        return;
    }
    if (flat && !o.model) {
        return;
    }
    const Camera& camera = cameras[screen];
    const View view = viewFor(screen);
    const float* eye = view.eye;
    const float* forward = view.forward;
    const float* right = view.right;
    const float* trueUp = view.up;
    const float fovY = view.fovY;
    const float f = 1.0f / std::tan(fovY * 0.5f);
    const float aspect = float(target.width) / float(target.height);
    // Near and far: the screen's camera object's (by its kind), else the
    // screen camera's.
    float n = camera.nearZ, fz = camera.farZ;
    if (const auto it = rigs.find(screenRig[screen]); screenRig[screen] && it != rigs.end()) {
        n = it->second.nearZ;
        fz = it->second.farZ;
    }
    // A pop-up object's extra scale (Resource::popScale).
    float extra = 1.0f;
    if (o.popupResource) {
        if (const Resource* pr = resource(o.popupResource, Resource::Kind::Popup)) {
            extra = pr->popScale;
        }
    }
    // The object's transform (localMatrix: the game's order) under its
    // parents'; normals turn by the same without the scales.
    const Affine world = worldMatrix(o, extra, true);
    const Affine turn = worldMatrix(o, 1.0f, false);
    auto rotate = [&](float& x, float& y, float& z) {
        const float v[3] = {x, y, z};
        const auto r = turn.rotate(v);
        x = r[0], y = r[1], z = r[2];
    };
    // Flat: a model on a 2D object (sixth adventure 0x12195c, content 13).
    // The object's 2D matrix with y and z of the scale negated (model y up,
    // screen y down) and the screen's centre added to the translation (the
    // table at 0x42387c: 200, 120 for the upper screen, 160, 120 for the
    // lower, 200, 120 offscreen). Drawn orthographically in pixels;
    // hypothesis for the depth range (+-1024). Matches the reference's
    // shopper in puzzle 013. Not modelled yet: the 2D object's own 3D
    // rotation fields (+0x284 / +0x2e8), which the scenes seen leave at 0.
    const Transform flatAt = flat ? screenTransform(o) : Transform{};
    const float flatZ = o.scale[2];
    const float flatC = std::cos(flatAt.angle), flatS = std::sin(flatAt.angle);
    auto project = [&](const float* p, ctr3d::Vertex& v) {
        if (flat) {
            const float X = p[0] * flatAt.sx, Y = -p[1] * flatAt.sy, Z = -p[2] * flatZ;
            const float cx = o.screen == 1 ? 160.0f : 200.0f, cy = 120.0f;
            const float px = flatAt.x + cx + X * flatC - Y * flatS;
            const float py = flatAt.y + cy + X * flatS + Y * flatC;
            v.x = px / float(target.width) * 2.0f - 1.0f;
            v.y = 1.0f - py / float(target.height) * 2.0f;
            v.z = std::clamp(Z / 1024.0f, -1.0f, 1.0f);
            v.w = 1.0f;
            return;
        }
        const auto w = world.apply(p);
        const float x = w[0] - eye[0], y = w[1] - eye[1], z = w[2] - eye[2];
        const float vx = x * right[0] + y * right[1] + z * right[2];
        const float vy = x * trueUp[0] + y * trueUp[1] + z * trueUp[2];
        const float vz = -(x * forward[0] + y * forward[1] + z * forward[2]);
        v.x = f / aspect * vx;
        v.y = f * vy;
        v.z = (vz * (fz + n) / (n - fz)) + (2 * fz * n / (n - fz));
        v.w = -vz;
    };
    // Lighting (the models' fragment lighting as the console's command
    // lists set it; see Model::Material): the screen's sky (3310) gives the
    // lights. Per vertex the normal's N.L and N.H (H halfway between the
    // light and the direction to the eye) are passed on; the rasterizer
    // forms (emission + ambient + diffuse x N.L + specular x D0(N.H)) x
    // texture per pixel. The first light only (every scene seen has one).
    // Without a sky the model is drawn as textured.
    const Sky* sky = screenSky[screen].get();
    const Sky::Light* mainLight = sky && !sky->lights.empty() ? &sky->lights.front() : nullptr;
    auto toWorld = [&](const float* p) {
        return world.apply(p);
    };
    auto light = [&](const float* normal, const float* position, ctr3d::Vertex& v) {
        if (!mainLight) {
            return;
        }
        float nx = normal[0], ny = normal[1], nz = normal[2];
        rotate(nx, ny, nz);
        const float l = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (l > 0) {
            nx /= l, ny /= l, nz /= l;
        }
        const auto w = toWorld(position);
        float view[3] = {eye[0] - w[0], eye[1] - w[1], eye[2] - w[2]};
        if (flat) { // the viewer straight in front (orthographic)
            view[0] = 0, view[1] = 0, view[2] = 1;
        }
        const float vl = std::sqrt(view[0] * view[0] + view[1] * view[1] + view[2] * view[2]);
        float h[3];
        for (int k = 0; k < 3; ++k) {
            h[k] = mainLight->direction[k] + (vl > 0 ? view[k] / vl : 0.0f);
        }
        const float hl = std::sqrt(h[0] * h[0] + h[1] * h[1] + h[2] * h[2]);
        v.shade = hl > 0 ? (nx * h[0] + ny * h[1] + nz * h[2]) / hl : 0.0f;
        v.shade2 = nx * mainLight->direction[0] + ny * mainLight->direction[1] +
                   nz * mainLight->direction[2];
    };
    // The draw state's fragment lighting for a material.
    auto shadeState = [&](ctr3d::DrawState& ds, const Model::Material* material) {
        if (!mainLight || !material) {
            return;
        }
        ds.fragmentLighting = true;
        for (int k = 0; k < 3; ++k) {
            ds.litPrimary[k] = material->emission[k] + mainLight->ambient[k] * material->ambient[k];
            ds.litDiffuse[k] = mainLight->diffuse[k] * material->diffuse[k];
            ds.litSpecular[k] = mainLight->specular[k] * material->specular[k];
        }
        ds.d0Table = material->lightTable.size() >= 512 ? material->lightTable.data() : nullptr;
    };
    // The later passes draw model meshes only.
    const bool decalsOnly = meshPass_ == MeshPass::Decals || meshPass_ == MeshPass::Additive;
    if (!flat && !decalsOnly && o.picture && o.picture->width > 0) {
        // A picture on a 3D object (6430): a quad of its pixels around the
        // pivot in the object's x, y (as the 2D animations placed in 3D).
        const float w = float(o.picture->width), h = float(o.picture->height);
        const float corners[4][3] = {{-o.pivot[0], -o.pivot[1], 0},
                                     {w - o.pivot[0], -o.pivot[1], 0},
                                     {-o.pivot[0], h - o.pivot[1], 0},
                                     {w - o.pivot[0], h - o.pivot[1], 0}};
        ctr3d::Vertex quad[4];
        for (int k = 0; k < 4; ++k) {
            project(corners[k], quad[k]);
            quad[k].u = k % 2 ? 1.0f : 0.0f;
            quad[k].v = k / 2 ? 1.0f : 0.0f;
        }
        ctr3d::DrawState ps;
        ps.texture = o.picture.get();
        ps.depthTest = true;
        ps.depthWrite = true;
        ps.colour[0] = o.colour[0], ps.colour[1] = o.colour[1], ps.colour[2] = o.colour[2];
        ps.colour[3] = o.alpha[0];
        if (o.pictureBlend == 7) {
            ps.blend = ctr3d::DrawState::Blend::Multiply;
        }
        static const std::uint16_t kQuad[6] = {0, 1, 2, 2, 1, 3};
        ctr3d::drawTriangles(target, ps, quad, kQuad);
    }
    if (!flat && !decalsOnly && o.animation && o.playing >= 0 &&
        o.playing < int(o.animation->animations.size())) {
        // A 2D animation placed in 3D (6600 / 6622): its cells' pixels are
        // the object's local x, y (the scale flips y: 0.15, -0.15).
        const auto& set = *o.animation;
        const auto& a = set.animations[std::size_t(o.playing)];
        const int key = currentKey(set, a, o.frame);
        if (key >= 0 && set.keys[std::size_t(key)].composite >= 0) {
            const auto& k = set.keys[std::size_t(key)];
            const auto& comp = set.composites[std::size_t(k.composite)];
            ctr3d::DrawState state;
            state.colour[0] = o.colour[0];
            state.colour[1] = o.colour[1];
            state.colour[2] = o.colour[2];
            state.colour[3] = o.alpha[0];
            for (int i = comp.first; i < comp.first + comp.count; ++i) {
                const int ci = set.cellList[std::size_t(i)];
                if (ci < 0 || set.cells[std::size_t(ci)].buffer < 0) {
                    continue;
                }
                const auto& cell = set.cells[std::size_t(ci)];
                const auto& vb = set.buffers[std::size_t(cell.buffer)];
                const AnimationSet::Material* m =
                    cell.material >= 0 ? &set.materials[std::size_t(cell.material)] : nullptr;
                std::vector<ctr3d::Vertex> verts(vb.count);
                for (unsigned v = 0; v < vb.count; ++v) {
                    const float* p = vb.at(VertexBuffer::Position, v);
                    const float local[3] = {p[0] + k.offset[0], p[1] + k.offset[1], 0};
                    project(local, verts[v]);
                    if (vb.has(VertexBuffer::TexCoord) && m) {
                        const float* uv = vb.at(VertexBuffer::TexCoord, v);
                        verts[v].u = uv[0] * m->uvScale[0];
                        verts[v].v = uv[1] * m->uvScale[1];
                    }
                }
                ctr3d::DrawState cs = state;
                cs.texture =
                    m && m->texture >= 0 ? set.textures[std::size_t(m->texture)].get() : nullptr;
                ctr3d::drawTriangles(target, cs, verts, cell.indices);
            }
        }
    }
    const bool merged = o.merged && o.merged->built && o.merged->model;
    if (o.model || merged) {
        // The model, posed by its motion, with the depth buffer; a merged
        // object (6890..6896) draws one mesh per instance instead.
        const Model& model = merged ? *o.merged->model : *o.model;
        ctr3d::DrawState state;
        state.depthTest = true;
        state.depthWrite = true;
        state.cull = ctr3d::DrawState::Cull::Back;
        // The pose: bone matrices x inverse binds, per palette entry.
        const std::vector<Affine> pose = merged ? std::vector<Affine>{} : posedModel(o, model);

        // The parts to draw: every mesh, or for a merged object each
        // instance's mesh with the instance's matrix as its one bone
        // (0x159678: T * Rx * Ry * Rz * S, or only T for a positions-only
        // merge; the baked buffer tags each vertex with its instance).
        struct Part {
            const Mesh* mesh;
            bool instance;
            Affine matrix;
        };

        std::vector<Part> parts;
        if (merged) {
            const auto& g = *o.merged;
            for (std::size_t i = 0; i < g.added && i < g.instances.size(); ++i) {
                const auto& in = g.instances[i];
                if (in.mesh < 0 || in.mesh >= int(model.meshes.size())) {
                    continue;
                }
                parts.push_back({&model.meshes[std::size_t(in.mesh)], true, g.matrix(i)});
            }
        } else {
            for (const auto& mesh : model.meshes) {
                if (!o.hiddenMeshes.empty() && o.hiddenMeshes.count(xq::hash(mesh.name))) {
                    continue; // 6865
                }
                if (mesh.layer < o.layerFirst || mesh.layer > o.layerLast) {
                    continue; // 0x2cd8cc: another object draws this layer
                }
                if ((mesh.passes & 1) == 0) {
                    // The model draw (0x191878) tests the mesh's pass mask
                    // (+0xcc) against 1 << pass (0x192d94, 0x1930f4 /
                    // 0x193888); scenes draw pass 0, so the rooms'
                    // collision meshes (mask 2) are left out.
                    continue;
                }
                parts.push_back({&mesh, false, Affine{}});
            }
            // The console draws a model's meshes layer by layer (the
            // model draw walks per-pass groups, param_1[pass * 3 + 0x30]):
            // the show stage's command list (console frame 1200 of the
            // show) has stage (layer 0), k01..k22 (1), shadow (2),
            // sankaku (3), while the file lists shadow and sankaku before
            // the backdrop; room m1010b (story checkpoint s3) draws its
            // layers 0..6 in order too. Inside a layer the file order is
            // kept, which matches except m1010b's layer 3 (console m1250,
            // m1300, m1150, box, kemuri; the rule is open). The group
            // builder itself is not decoded.
            std::stable_sort(parts.begin(), parts.end(), [](const Part& a, const Part& b) {
                return a.mesh->layer < b.mesh->layer;
            });
        }
        if (meshPass_ != MeshPass::All) {
            // Only this pass's meshes (renderScreen).
            std::erase_if(parts, [&](const Part& p) {
                return meshPassOf(model.material(p.mesh->material)) != meshPass_;
            });
        }
        for (const Part& part : parts) {
            const Mesh& mesh = *part.mesh;
            const auto& vb = mesh.vertices;
            const Model::Material* m = model.material(mesh.material);
            bool weighted = false;
            std::vector<Affine> skin;
            if (part.instance) {
                skin.push_back(part.matrix);
            } else {
                skin = meshSkin(model, mesh, pose, weighted);
            }
            std::vector<ctr3d::Vertex> verts(vb.count);
            // The view-space normals' x, y, for the silhouette.
            const bool outlined =
                mesh.outlineWidth > 0 && !mesh.edges.empty() && vb.has(VertexBuffer::Normal);
            std::vector<std::array<float, 2>> silNormal(outlined ? vb.count : 0);
            for (unsigned v = 0; v < vb.count; ++v) {
                const auto sp = skinnedPosition(vb, skin, weighted, v);
                float skinned[3] = {sp[0], sp[1], sp[2]};
                project(skinned, verts[v]);
                if (vb.has(VertexBuffer::TexCoord)) {
                    const float* uv = vb.at(VertexBuffer::TexCoord, v);
                    verts[v].u = uv[0];
                    verts[v].v = uv[1];
                }
                if (vb.has(VertexBuffer::Colour)) {
                    const float* c = vb.at(VertexBuffer::Colour, v);
                    verts[v].r = c[0];
                    verts[v].g = c[1];
                    verts[v].b = c[2];
                    verts[v].a = c[3];
                }
                if (vb.has(VertexBuffer::Normal)) {
                    const float* n = vb.at(VertexBuffer::Normal, v);
                    float normal[3] = {n[0], n[1], n[2]};
                    if (!skin.empty() && !weighted) {
                        const auto r = skin.front().rotate(n);
                        const float len = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
                        if (len > 0) {
                            normal[0] = r[0] / len, normal[1] = r[1] / len, normal[2] = r[2] / len;
                        }
                    } else if (!skin.empty()) {
                        // The strongest bone's rotation (normals need no
                        // more precision for diffuse light).
                        const float* w = vb.at(VertexBuffer::Weights, v);
                        const float* bi = vb.at(VertexBuffer::Bones, v);
                        int best = 0;
                        const int influences = std::min(vb.components[VertexBuffer::Weights],
                                                        vb.components[VertexBuffer::Bones]);
                        for (int k = 1; k < influences; ++k) {
                            best = w[k] > w[best] ? k : best;
                        }
                        const int i = int(bi[best] + 0.5f);
                        if (i >= 0 && i < int(skin.size())) {
                            const auto r = skin[std::size_t(i)].rotate(n);
                            normal[0] = r[0], normal[1] = r[1], normal[2] = r[2];
                        }
                    }
                    light(normal, skinned, verts[v]);
                    if (outlined) {
                        float nx = normal[0], ny = normal[1], nz = normal[2];
                        rotate(nx, ny, nz);
                        silNormal[v] = {nx * right[0] + ny * right[1] + nz * right[2],
                                        nx * trueUp[0] + ny * trueUp[1] + nz * trueUp[2]};
                    }
                }
            }
            if (m && !model.projections.empty()) {
                // The projection's own offset; each face layer whose clip has
                // a track for it replaces it (later layers last).
                auto offset = model.textureOffset(m->hash, nullptr, 0.0f);
                for (const auto& f : o.faces) {
                    const Motion* face = f.motion.empty() ? nullptr : model.faceMotion(f.motion);
                    if (!face) {
                        continue;
                    }
                    bool targeted = false;
                    const auto v =
                        model.textureOffset(m->hash, face, float(face->first) + f.time, &targeted);
                    if (targeted) {
                        offset = v;
                    }
                }
                if (offset[0] != 0 || offset[1] != 0) {
                    for (auto& vx : verts) {
                        vx.u -= offset[0];
                        vx.v -= offset[1];
                    }
                }
            }
            ctr3d::DrawState ms = state;
            if (m) {
                ms.depthOffset = 2.0f * m->depthOffset; // the whole range is 2 here (-1..1)
            }
            // The shaders' material colour (c11; 0x192cbc..0x192d38): the
            // material's colour x the object's colour, its alpha x the
            // object's alpha x 6875's factor (+0x1e4).
            for (int k = 0; k < 3; ++k) {
                ms.colour[k] *= o.colour[std::size_t(k)];
            }
            // 6875's factor makes only the #FIX_IMG meshes (rooms, props:
            // combiner alpha = primary x texture) see-through; a toon
            // mesh's combiner takes its alpha from its constant (place
            // 1010's command list: Comani's #FIX_TON draws carry c11.w 0.6
            // from 6875 and stay opaque on the console).
            static const std::uint32_t kImageCombinerAlpha = xq::hash("#FIX_IMG");
            ms.colour[3] *=
                o.alpha[0] * (mesh.combiner == kImageCombinerAlpha ? o.materialAlpha : 1.0f);
            if (!o.meshAlpha.empty()) {
                if (const auto a = o.meshAlpha.find(xq::hash(mesh.name)); a != o.meshAlpha.end()) {
                    ms.colour[3] *= a->second[0]; // 20400 / 20401
                }
            }
            ms.texture =
                m && m->texture >= 0 ? model.textures[std::size_t(m->texture)].get() : nullptr;
            if (!o.meshTextures.empty()) {
                if (const auto t = o.meshTextures.find(xq::hash(mesh.name));
                    t != o.meshTextures.end() && t->second.texture) {
                    ms.texture = t->second.texture.get();
                    for (auto& vx : verts) {
                        vx.u = vx.u * t->second.scale[0] + t->second.offset[0];
                        vx.v = vx.v * t->second.scale[1] + t->second.offset[1];
                    }
                }
            }
            if (m && m->blendEquation == 0 && m->blendSource == 8 && m->blendDest == 1) {
                ms.blend = ctr3d::DrawState::Blend::Add; // .atr (0, 8, 1)
            } else if (m && m->blendEquation == 0 && m->blendSource == 1 && m->blendDest == 0) {
                ms.blend = ctr3d::DrawState::Blend::None; // (0, 1, 0): opaque
            } else if (m && m->blendEquation == 0 && m->blendSource == 1 && m->blendDest == 1) {
                ms.blend =
                    ctr3d::DrawState::Blend::AddOne; // (0, 1, 1): the horse ride's light shafts
            } else if (m && m->blendEquation == 2 && m->blendSource == 8 && m->blendDest == 1) {
                // (2, 8, 1): reverse subtract, the "-sub-" materials (the
                // sixth adventure's globe shading, puzzle 001).
                ms.blend = ctr3d::DrawState::Blend::Subtract;
            }
            if (m && !o.materialMotion.empty()) {
                // 6830's material motion: the clip's alpha for this material.
                float alpha = 1;
                if (const Motion* mm = model.materialMotion(o.materialMotion);
                    mm && model.materialAlphaAt(*mm, float(mm->first) + o.materialMotionTime,
                                                m->hash, alpha)) {
                    ms.colour[3] *= alpha;
                }
            }
            if (m && !o.materialValues.empty()) {
                if (const auto a = o.materialValues.find(m->hash); a != o.materialValues.end()) {
                    ms.colour[3] *= a->second; // 6838
                }
            }
            // The "#FIX_IMG" combiner (the rooms' and props' techniques
            // #FIX_IMG, #FIX_MDS_IMG0, #FIX_IMG_MDS): stage 0 primary x
            // texture 0, doubled; its shaders (VTX003 for #FIX_SPR) pass
            // 0.5 x the material colour (c11) x the vertex colour, so the
            // pixel is vertex colour x material colour x texture and the
            // fragment lighting the console leaves set up is not used
            // (place 1010's command lists: tev0 source 0x30 / 0x0e30,
            // scale 2).
            static const std::uint32_t kImageCombiner = xq::hash("#FIX_IMG");
            if (vb.has(VertexBuffer::Normal) && mesh.combiner != kImageCombiner) {
                shadeState(ms, m);
            }
            ctr3d::drawTriangles(target, ms, verts, mesh.indices);
            if (outlined) {
                const float* colour = mesh.outlineColour;
                if (const auto oc = o.outlineColours.find(mesh.outline);
                    oc != o.outlineColours.end()) {
                    colour = oc->second.data(); // 6870
                }
                drawSilhouette(target, state, mesh, verts, silNormal, colour);
            }
        }
        if (!o.popup) {
            return;
        }
    }
    if (!o.popup || decalsOnly) {
        return;
    }
    const auto& set = *o.popup;
    ctr3d::DrawState state;
    state.depthTest = false;
    for (std::size_t li = 0; li < set.animations.size(); ++li) {
        const auto& a = set.animations[li];
        unsigned t = li < o.layerFrames.size() ? o.layerFrames[li] : 0;
        int key = -1;
        for (int k = a.first; k < a.first + a.count && k < int(set.keys.size()); ++k) {
            key = k;
            if (t < set.keys[std::size_t(k)].duration) {
                break;
            }
            t -= set.keys[std::size_t(k)].duration;
        }
        if (key < 0 || set.keys[std::size_t(key)].composite < 0) {
            continue;
        }
        const auto& comp = set.composites[std::size_t(set.keys[std::size_t(key)].composite)];
        for (int i = comp.first; i < comp.first + comp.count; ++i) {
            const int ci = set.cellList[std::size_t(i)];
            if (ci < 0 || set.cells[std::size_t(ci)].buffer < 0) {
                continue;
            }
            const auto& cell = set.cells[std::size_t(ci)];
            const auto& vb = set.buffers[std::size_t(cell.buffer)];
            const AnimationSet::Material* m =
                cell.material >= 0 ? &set.materials[std::size_t(cell.material)] : nullptr;
            std::vector<ctr3d::Vertex> verts(vb.count);
            for (unsigned v = 0; v < vb.count; ++v) {
                project(vb.at(VertexBuffer::Position, v), verts[v]);
                if (vb.has(VertexBuffer::TexCoord) && m) {
                    const float* uv = vb.at(VertexBuffer::TexCoord, v);
                    // Projective coordinates (s, t, q): the GPU divides
                    // per pixel; dividing per vertex bent the layers.
                    verts[v].q = vb.components[VertexBuffer::TexCoord] >= 3 ? uv[2] : 1.0f;
                    verts[v].u = uv[0] * m->uvScale[0];
                    verts[v].v = uv[1] * m->uvScale[1];
                }
            }
            ctr3d::DrawState cs = state;
            cs.texture =
                m && m->texture >= 0 ? set.textures[std::size_t(m->texture)].get() : nullptr;
            ctr3d::drawTriangles(target, cs, verts, cell.indices);
        }
    }
}

// The console's draw classes (the sixth adventure's garden 1 mid-bloom,
// console frame 400 of runs/l6/gpu4): all other meshes first; then the
// meshes with a depth offset (the "-zp0-" decals, also from objects created
// earlier); then the additive ones (.atr (0, 8, 1): GPU blend 86160400),
// the flowers' bloom effects. Those draw without the depth test there
// (depth/colour mask 1f50); the aquarium's additive glass (puzzle 126)
// stays behind its rim on the console, so the test is kept (open: .atr
// word 2, 00ff01ff / 01ff01ff on the effects, 01ffffff on the others).
// *Hypothesis*: the class decides the order across objects in general
// (one list seen per game).
Scene::MeshPass Scene::meshPassOf(const Model::Material* m) {
    if (m && m->blendEquation == 0 && m->blendSource == 8 && m->blendDest == 1) {
        return MeshPass::Additive;
    }
    return m && m->depthOffset > 0 ? MeshPass::Decals : MeshPass::Normal;
}

void Scene::renderScreen(ctr3d::Target& target, int screen) const {
    // A screen's 3D objects first, then its 2D objects over them, each in
    // priority order (higher later; 2D ties in the screen list's order,
    // 3D ties in creation order). Hypothesis
    // from the captures: the help (2D 131/132) and the dimming rectangle
    // (2D 122) cover the pop-up frame (3D 200); no 2D object of the top
    // screen has been seen under a 3D one.
    struct Item {
        int priority;
        std::uint64_t order;
        const Object* flat;
        const Object3D* solid;
    };

    std::vector<Item> items;
    for (const auto& [hash, o] : objects_) {
        if (o->screen == screen && shown(*o) && o->drawn) {
            items.push_back({o->priority, o->listOrder, o.get(), nullptr});
        }
    }
    for (const auto& [hash, o] : objects3D_) {
        // 6008 hides a 3D object as 5008 a 2D one (puzzle 061's staff: 6008 0
        // after creation, 1 at its merge motion; hidden on the console).
        if (o->screen == screen && o->visible && o->drawn) {
            items.push_back({o->priority, o->order, nullptr, o.get()});
        }
    }
    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        if ((a.solid != nullptr) != (b.solid != nullptr)) {
            return a.solid != nullptr;
        }
        return a.priority != b.priority ? a.priority < b.priority : a.order < b.order;
    });
    // The 3D objects (sorted first) in three passes over all of them (see
    // meshPassOf): the other meshes, the decals, the additive ones.
    auto firstFlat =
        std::find_if(items.begin(), items.end(), [](const Item& i) { return i.flat != nullptr; });
    for (const MeshPass pass : {MeshPass::Normal, MeshPass::Decals, MeshPass::Additive}) {
        meshPass_ = pass;
        for (auto it = items.begin(); it != firstFlat; ++it) {
            draw3D(target, *it->solid, screen);
        }
    }
    meshPass_ = MeshPass::All;
    for (auto it = firstFlat; it != items.end(); ++it) {
        draw(target, *it->flat);
    }
}

void Scene::render(ctr3d::Target& top, ctr3d::Target& bottom) const {
    // Offscreen screens first (their images may be shown on the others).
    for (int s = 2; s < kScreens; ++s) {
        if (!screenEnabled[s]) {
            continue;
        }
        if (offscreen_[s].width != top.width || offscreen_[s].height != top.height) {
            offscreen_[s].resize(top.width, top.height);
        }
        // 0x32e504 / 0x32e5d4: a screen switched on clears to transparent
        // black, so an object showing it covers only what was drawn.
        offscreen_[s].clear({0, 0, 0, 0});
        renderScreen(offscreen_[s], s);
        auto& t = offscreenTexture_[s];
        t.width = offscreen_[s].width;
        t.height = offscreen_[s].height;
        t.pixels = offscreen_[s].colour;
        t.linear = false;
    }
    renderScreen(top, 0);
    renderScreen(bottom, 1);
    // A kept screen's frame is what the next frame's objects show of it.
    ctr3d::Target* drawn[2] = {&top, &bottom};
    for (int s = 0; s < 2; ++s) {
        auto& t = keptTexture_[s];
        if (!screenKept[s]) {
            t = {};
            continue;
        }
        t.width = drawn[s]->width;
        t.height = drawn[s]->height;
        t.pixels = drawn[s]->colour;
        t.linear = false;
    }
    // Screen brightness: towards black below 0, towards white above.
    ctr3d::Target* targets[2] = {&top, &bottom};
    for (int s = 0; s < 2; ++s) {
        const float b = brightness[s][0];
        if (b == 0.0f) {
            continue;
        }
        for (auto& c : targets[s]->colour) {
            auto f = [b](std::uint8_t v) {
                const float x = b < 0 ? float(v) * (1.0f + b) : float(v) + (255.0f - float(v)) * b;
                return std::uint8_t(std::clamp(x, 0.0f, 255.0f) + 0.5f);
            };
            c.r = f(c.r);
            c.g = f(c.g);
            c.b = f(c.b);
        }
    }
}

} // namespace picarat::adventure56
