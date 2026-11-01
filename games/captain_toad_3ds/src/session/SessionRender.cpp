// =============================================================================================
// SessionRender.cpp - posing the animated models and drawing a frame
// =============================================================================================
//
// Per frame: the player's pose from the recovered animation state, the actors' clips (the
// companion, birds, grass wind, the goal star), then the frame's draws: the shadow map, the
// environment, the drop shadows, the characters and actors, the particle effects and the 2D
// layouts of the goal sequence. Draw order and material state follow traced GPU command lists.
// =============================================================================================
#include "session/SessionState.hpp"

namespace captain_toad::session {

namespace {

// The column-major matrix that moves a placed object's baked (world-space) vertices from
// its placement to `draw`: draw * inverse(placement). False when the two are the same.
bool objectDelta(const captain_toad::RecoveredVec::M34& placement, const captain_toad::RecoveredVec::M34& draw,
                 float out[16]) {
    if (!memcmp(placement.m, draw.m, sizeof placement.m)) {
        return false;
    }
    const float* p = placement.m;
    const double a = p[0], b = p[1], c = p[2], d = p[4], e = p[5], f = p[6], g = p[8], h = p[9], i = p[10];
    const double det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    const double r[9] = {(e * i - f * h) / det, (c * h - b * i) / det, (b * f - c * e) / det,
                         (f * g - d * i) / det, (a * i - c * g) / det, (c * d - a * f) / det,
                         (d * h - e * g) / det, (b * g - a * h) / det, (a * e - b * d) / det};
    double inv[12];
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++) {
            inv[row * 4 + col] = r[row * 3 + col];
        }
        inv[row * 4 + 3] = -(r[row * 3] * p[3] + r[row * 3 + 1] * p[7] + r[row * 3 + 2] * p[11]);
    }
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 4; col++) {
            double v = col == 3 ? draw.m[row * 4 + 3] : 0;
            for (int k = 0; k < 3; k++) {
                v += draw.m[row * 4 + k] * inv[k * 4 + col];
            }
            out[col * 4 + row] = float(v);
        }
    }
    out[3] = out[7] = out[11] = 0;
    out[15] = 1;
    return true;
}

}   // namespace

void Session::Impl::setShadowQuality(bool high) {
    highQualityShadows = high;
    shadowResolution = high ? nativeShadowResolution : originalShadowResolution;
    filterShadows = high;
    shadowMap.proxyCasters = !high && romShadowProxies;
    shadowDirty = true;
}

// Goal-sequence layouts over the 400x240 game view (current viewport): COURSE CLEAR, title logo, wipe.
// The original 5:3 frame in a drawn view (Session::originalFrame).
static captain_toad::gpu::Rect frameRect(const captain_toad::gpu::Rect& v, bool cover) {
    const auto f = Session::originalFrame({v.x, v.y, v.w, v.h}, cover);
    return {f[0], f[1], f[2], f[3]};
}

void Session::Impl::drawGoalLayouts(const captain_toad::gpu::Rect& view) {
    const auto& g = game.goal;
    auto set = [&](const char* name, bool shown, std::vector<std::pair<std::string, float>> anims) {
        auto it = layouts.find(name);
        if (it == layouts.end()) {
            return;
        }
        Layout& l = *it->second;
        l.shown = shown;
        l.active.clear();
        for (auto& [a, f] : anims) {
            auto an = l.animations.find(a);
            if (an != l.animations.end()) {
                l.active.push_back({&an->second, f});
            }
        }
        l.update();
    };
    // Drawing between ticks: a clip that runs on shows the frame between its last two ticks.
    auto frame = [&](int k, const std::string& name, float f) {
        const auto& p = present;
        if (!p.blended || !p.prev.valid || !p.cur.valid) {
            return f;
        }
        const auto& [n0, f0] = p.prev.layouts[k];
        const auto& [n1, f1] = p.cur.layouts[k];
        if (n0 != name || n1 != name || f1 != f || f1 < f0 || f1 - f0 > kPresentMaxFrames) {
            return f;
        }
        return f0 + (f1 - f0) * p.alpha;
    };
    const std::string clear = g.courseClearAnim.drawnName(), title = g.titleAnim.drawnName();
    const std::string wipe = g.wipeAnim.drawnName();
    set("HeadCourseClear_TOP", g.courseClearVisible,
        {{clear, frame(0, clear, g.courseClearAnim.drawnFrame())}, {"Season1", 0}});
    set("TitleLogoParts", true, {{"Season1", 0}});
    set("TitleLogoDemo_TOP", g.titleVisible, {{title, frame(1, title, g.titleAnim.drawnFrame())}});
    set("WipeCircle_TOP", g.wipeVisible, {{wipe, frame(2, wipe, g.wipeAnim.drawnFrame())}});
    captain_toad::gpu::Device& device = captain_toad::gpu::device();
    for (const char* n : {"HeadCourseClear_TOP", "TitleLogoDemo_TOP", "WipeCircle_TOP"}) {
        if (layouts.count(n)) {
            device.setViewport(frameRect(view, !strcmp(n, "WipeCircle_TOP")));   // the wipe covers
            layouts[n]->draw();
            device.setViewport(view);
        }
        if (!strcmp(n, "HeadCourseClear_TOP") && eft.enabled && g.courseClearVisible) {
            eft.draw2D();
        }
    }   // 2D effect group (UIHeadCourseClear confetti) with its layout: the set outlives the hidden layout (eft-setmtx-a to k 338) without being seen
}

// =============================================================================================
// Posing and rendering
// =============================================================================================
// Animation state of the player, NPCs and actors for the current frame (also run on every game
// tick, so effect sources such as joints are current in captures that render only some frames).
// The headlamp's spot light follows the protagonist's HeadLight joint (after each pose).
void Session::Impl::placeHeadlamp() {
    if (!picaLighting) {
        return;
    }
    // Headlamp spotLight follows the HeadLight joint; beam axis is the record's joint-space direction.
    Affine head = character.joint("HeadLight");
    float origin[3] = {0, 0, 0};
    const auto& rec = lightService.records.at("headlamp");
    float axis[3] = {(float)rec.toLight[0], (float)rec.toLight[1], (float)rec.toLight[2]};
    auto p = head.apply(origin, true);
    auto b = head.apply(axis, false);
    double n = sqrt(b[0] * b[0] + b[1] * b[1] + b[2] * b[2]);
    for (int k = 0; k < 3; k++) {
        lightService.headlampPosition[k] = p[k];
        lightService.headlampBeam[k] = b[k] / n;
    }
    lightService.headlampEnd =
        lightService.beamLength(lightService.headlampPosition, lightService.headlampBeam, rec.end);
    std::vector<std::array<double, 3>> points;
    for (const auto& c : lightService.headlampPoints) {
        const float local[3] = {float(c[0]), float(c[1]), float(c[2])};
        const auto w = head.apply(local, true);
        points.push_back({w[0], w[1], w[2]});
    }
    lightService.placeHeadlamp(points);
}

void Session::Impl::poseScene(bool writeVertices) {
    if (player.enabled || !player.lastError.empty()) {
        player.restSource = characterForm != 0 && !characterAlt.skin.empty() ? &characterAlt : nullptr;
        player.apply(character, previewFrameFixed ? fmodf(animationFrame, 180.f) : -1.f);
    }
    if (!character.skin.empty()) {
        if (character.update(animationFrame, vertices, tangents, meshes, writeVertices)) {
            markVertices(character);
            shadowDirty = true;
        }
        if (character.justPosed) {
            placeHeadlamp();
        }
    }
    for (auto& g : grassWinds) {   // one recovered update per 30 Hz game frame (catch up, capped)
        long long target = (long long)floor(animationFrame / 2);
        if (g.frame > target || g.frame < target - 180) {
            g.frame = target - 1;
        }
        if (g.offsets.empty()) {
            g.offsets = actors[g.actor].windOffsets;
        }
        while (g.frame < target) {
            g.previous = g.offsets;
            g.control.update(g.offsets);
            g.frame++;
        }
        auto& shown = actors[g.actor].windOffsets;
        shown = g.offsets;
        // Drawing between ticks: the sway between the last two updates (the clock is real time).
        if (present.enabled && g.previous.size() == g.offsets.size()) {
            const float f = float(std::clamp(animationFrame / 2 - double(target), 0.0, 1.0));
            for (size_t k = 0; k < shown.size(); k++) {
                shown[k].first = g.previous[k].first + (g.offsets[k].first - g.previous[k].first) * f;
                shown[k].second = g.previous[k].second + (g.offsets[k].second - g.previous[k].second) * f;
            }
        }
    }
    if (!birdFlock.initial.empty()) {   // recovered Bird idle states, one update per 30 Hz game frame
        if (!birdFlock.gameDriven) {
            long long target = std::max(0LL, (long long)floor(animationFrame / 2));
            if (birdFlock.frame > target) {
                birdFlock.reset();
            }
            while (birdFlock.frame < target) {
                for (auto& b : birdFlock.birds) {
                    b.second.tick(birdFlock.rng);
                }
                birdFlock.frame++;
            }
        }
        // Traced yaw is atan2(-m02, m00) of the placement rows, i.e. the negated Y rotation.
        for (auto& b : birdFlock.birds) {
            auto& a = actors[b.first];
            const auto& d = b.second;
            a.setHidden(d.dead);
            a.selectClip(d.clip);
            a.setYaw(-d.yaw, d.pos);
            a.drivenFrame = d.clipFrame;
        }
    }
    if (companionActor >= 0 && game.companion.enabled) {
        auto& a = actors[companionActor];
        const auto& t = game.companion;
        a.selectCurveClip(t.c.clip);
        a.drivenFrame = t.frame;
        auto m = t.c.pose.matrix();
        Affine p;
        for (int k = 0; k < 12; k++) {
            p.m[k] = m.m[k];
        }
        a.setPlacement(p);
    }
    if (game.goalEnabled && game.goal.active()) {
        const auto& g = game.goal;   // GoalItem Get / goal demo star (Goal.hpp)
        auto drive = [&](int index, const captain_toad::RecoveredGoal::ClipClock& clock, bool visible) {
            if (index < 0) {
                return;
            }
            auto& a = actors[index];
            a.setHidden(!visible);
            if (!clock.running()) {
                return;
            }
            a.selectCurveClip(clock.name);
            a.drivenFrame = clock.frame();
            auto& m = actorClipMaterials[index];
            m.active = clock.name;
            m.frame = clock.raw();
        };
        drive(starActor, g.star, g.starVisible);
        drive(demoStarActor, g.demoStar, g.demoStarVisible);
        drive(bossBirdActor, g.bossBird, g.bossBirdVisible);
    }
    // Stage objects: the behaviour's clip, frame, placement and visibility (game/StageObjects.hpp).
    for (size_t i = 0; i < objectActor.size(); i++) {
        if (objectActor[i] < 0) {
            continue;
        }
        const auto& o = stageWorld.objects[i];
        auto& a = actors[size_t(objectActor[i])];
        a.setHidden(!o.alive || !o.visible);
        if (o.anim.running()) {
            a.selectCurveClip(o.anim.name);
            a.drivenFrame = o.anim.frame;
        }
        a.setOverlay(o.visAnim.file, o.visAnim.frame);
        Affine p;
        for (int k = 0; k < 12; k++) {
            p.m[k] = o.world.m[k];
        }
        a.setPlacement(p);
    }
    // Placed objects' meshes follow the object's life and visibility (the shadow pass too).
    for (auto& m : meshes) {
        if (m.object >= 0 && size_t(m.object) < stageWorld.objects.size()) {
            const auto& o = stageWorld.objects[size_t(m.object)];
            const bool shown = o.alive && o.visible;
            if (shown != m.objectShown) {
                m.objectShown = shown;
                shadowDirty = true;
            }
        }
    }
    {
        bool moved = false;
        for (auto& a : actors) {
            if (a.update(a.drivenFrame >= 0 ? a.drivenFrame - a.frameOffset : animationFrame * a.fps / 60.f, vertices,
                         tangents, meshes, writeVertices)) {
                markVertices(a);
                moved = true;
            }
        }
        if (moved) {
            shadowDirty = true;
        }
    }
}

// The vertices of every model posed since they were last written (poseScene poses only).
void Session::Impl::finishSkinning() {
    if (!character.skin.empty() && character.skinPending) {
        character.skinVertices(vertices, tangents);
        markVertices(character);
        shadowDirty = true;
    }
    // The other models side by side, one per core (each writes its own vertex range).
    std::vector<CharacterAnimation*> pending;
    for (auto& a : actors) {
        if (a.skinPending) {
            pending.push_back(&a);
        }
    }
    captain_toad::parallel::Pool::shared().forRange(pending.size(), 1, [&](size_t begin, size_t end) {
        for (size_t i = begin; i < end; i++) {
            pending[i]->skinVertices(vertices, tangents, false);
        }
    });
    for (auto* a : pending) {
        markVertices(*a);
        shadowDirty = true;
    }
}

// Stereo (native enhancement, not recovered: the game's own stereo camera parameters are an open
// research item). Each eye sits beside the camera along its x axis and looks through an off-axis
// frustum that shares the camera's frustum at the zero-parallax plane, which is placed at the
// camera's target (the free camera: its orbit centre). Things beyond it appear behind the screen,
// nearer things in front. stereoParallax is the separation of the eyes' images of a point at
// infinity, as a fraction of the view's width (the intensity setting, 0.01 to 0.03); the frustum
// shift and the eye offset follow from it:
//   frustum shift at the near plane  s = stereoParallax * right
//   eye offset from the camera       d = s * targetDistance / near

// Headsets: each tracked eye into its own target, the scene posed once for all of them.
void Session::Impl::renderEyes(const std::vector<Session::EyeView>& eyes, const std::function<void(size_t)>& after) {
    poseScene();
    presentBlend();
    finishSkinning();
    for (size_t i = 0; i < eyes.size(); i++) {
        eyes[i].bind();
        captain_toad::gpu::Target* target = captain_toad::gpu::device().target();
        eyeView = &eyes[i];
        eyeRepeat = i > 0;
        renderView(0, 0, target->width(), target->height(), 0);
        eyeView = nullptr;
        eyeRepeat = false;
        if (after) {
            after(i);
        }
    }
    presentRestore();
}

void Session::Impl::render(int viewX, int viewY, int w, int h) {
    poseScene();
    presentBlend();
    finishSkinning();
    if (!stereo || useReferenceCamera) {
        renderView(viewX, viewY, w, h, 0);
    } else {
        const int left = w / 2;
        renderView(viewX, viewY, left, h, -1);
        renderView(viewX + left, viewY, w - left, h, 1);
    }
    presentRestore();
}

// ---- Drawing between ticks (enhancement, Presentation.hpp) ---------------------------------

// After a game tick: what the renderer reads, posed exactly as the tick left it.
void Session::Impl::presentCapture() {
    if (!present.enabled) {
        return;
    }
    poseScene();
    PresentTick t;
    t.valid = true;
    t.view = game.director.view;
    t.player = (player.enabled || !player.lastError.empty()) && !character.localPose.empty();
    if (t.player) {
        t.playerLocal = character.localPose;
        t.playerPlacement = character.placement;
    }
    t.actors.resize(actors.size());
    for (size_t i = 0; i < actors.size(); i++) {
        const auto& a = actors[i];
        auto& r = t.actors[i];
        r.clip = a.activeCurveClip.empty() ? a.activeClip : a.activeCurveClip;
        r.frame = a.drivenFrame;
        r.placement = a.placement;
        r.hidden = a.hidden;
    }
    t.objectWorld.reserve(stageWorld.objects.size());
    t.objectDraws.resize(stageWorld.objects.size());
    for (size_t i = 0; i < stageWorld.objects.size(); i++) {
        t.objectWorld.push_back(stageWorld.objects[i].world);
        t.objectDraws[i] = stageWorld.objects[i].draws;
    }
    if (game.goalEnabled) {
        const auto& g = game.goal;
        t.layouts[0] = {g.courseClearAnim.drawnName(), g.courseClearAnim.drawnFrame()};
        t.layouts[1] = {g.titleAnim.drawnName(), g.titleAnim.drawnFrame()};
        t.layouts[2] = {g.wipeAnim.drawnName(), g.wipeAnim.drawnFrame()};
    }
    present.prev = std::move(present.cur);
    present.cur = std::move(t);
}

// Before drawing a frame between two ticks: the blended poses (the tick's own are kept aside).
void Session::Impl::presentBlend() {
    auto& p = present;
    p.blended = false;
    p.viewValid = false;
    eft.blendAlpha = 1;
    if (!p.enabled || !p.prev.valid || !p.cur.valid || p.alpha >= 1.0f) {
        return;
    }
    const float t = p.alpha;
    auto moved = [](const float* a, const float* b) {
        const float dx = a[3] - b[3], dy = a[7] - b[7], dz = a[11] - b[11];
        return dx * dx + dy * dy + dz * dz > kPresentMaxMove * kPresentMaxMove;
    };
    // The camera (a cut is not blended).
    {
        const auto d = captain_toad::RecoveredVec::sub(p.cur.view.eye, p.prev.view.eye);
        if (captain_toad::RecoveredVec::dot(d, d) < kPresentCameraCut * kPresentCameraCut &&
            std::fabs(p.cur.view.fovy - p.prev.view.fovy) < 15.0f) {
            p.view = blendPose(p.prev.view, p.cur.view, t);
            p.viewValid = true;
        }
    }
    bool changed = false;
    // The protagonist: per-bone local transforms and the placement.
    if (p.prev.player && p.cur.player && p.prev.playerLocal.size() == p.cur.playerLocal.size() &&
        character.localPose.size() == p.cur.playerLocal.size() &&
        !moved(p.prev.playerPlacement.m.data(), p.cur.playerPlacement.m.data())) {
        p.characterPose = character.pose;
        for (size_t i = 0; i < character.localPose.size(); i++) {
            character.localPose[i] = blendAffine(p.prev.playerLocal[i], p.cur.playerLocal[i], t);
        }
        character.placement = blendAffine(p.prev.playerPlacement, p.cur.playerPlacement, t);
        character.lastFrame = -1;
        character.update(animationFrame, vertices, tangents, meshes, false);   // finishSkinning skins
        changed = true;
        placeHeadlamp();
    }
    // Behaviour-driven actors (objects, the companion, birds, the goal stars): placement and the
    // clip frame, while the clip stays and the frame runs on by a little.
    p.actorPose.clear();
    p.actorLocal.clear();
    const size_t n = std::min({actors.size(), p.prev.actors.size(), p.cur.actors.size()});
    for (size_t i = 0; i < n; i++) {
        auto& a = actors[i];
        const auto& x = p.prev.actors[i];
        const auto& y = p.cur.actors[i];
        if (x.clip != y.clip || x.hidden != y.hidden || y.hidden || moved(x.placement.m.data(), y.placement.m.data())) {
            continue;
        }
        const bool frames = x.frame >= 0 && y.frame >= x.frame && y.frame - x.frame <= kPresentMaxFrames;
        const bool placed = x.placement.m != y.placement.m;
        if (!frames && !placed) {
            continue;
        }
        p.actorPose.emplace_back(i, a.pose);
        p.actorLocal.emplace_back(i, a.lastLocal);
        if (frames) {
            a.drivenFrame = x.frame + (y.frame - x.frame) * t;
        }
        if (placed) {
            a.setPlacement(blendAffine(x.placement, y.placement, t));
        }
        a.lastFrame = -1;
        a.update(a.drivenFrame >= 0 ? a.drivenFrame - a.frameOffset : animationFrame * a.fps / 60.f, vertices, tangents,
                 meshes, false);
        changed = true;
    }
    // Particles: their draw blocks between the two ticks.
    if (eft.enabled) {
        eft.blend(t, blendRows34);
    }
    // Objects drawn several times (coin stacks): each copy, while their number stays.
    p.objectDraws.resize(stageWorld.objects.size());
    for (size_t i = 0; i < stageWorld.objects.size(); i++) {
        const auto& now = stageWorld.objects[i].draws;
        auto& out = p.objectDraws[i];
        out = now;
        if (i < p.prev.objectDraws.size() && i < p.cur.objectDraws.size() && !now.empty()) {
            const auto& x = p.prev.objectDraws[i];
            const auto& y = p.cur.objectDraws[i];
            if (x.size() == y.size() && y.size() == now.size()) {
                for (size_t k = 0; k < now.size(); k++) {
                    if (!moved(x[k].m, y[k].m)) {
                        out[k] = blendM34(x[k], y[k], t);
                    }
                }
            }
        }
    }
    // Placed objects' world matrices (baked meshes are drawn moved by them).
    p.objectWorld.resize(stageWorld.objects.size());
    for (size_t i = 0; i < stageWorld.objects.size(); i++) {
        const auto& w = stageWorld.objects[i].world;
        p.objectWorld[i] = w;
        if (i < p.prev.objectWorld.size() && i < p.cur.objectWorld.size()) {
            const auto& x = p.prev.objectWorld[i];
            const auto& y = p.cur.objectWorld[i];
            if (std::equal(y.m, y.m + 12, w.m) && !std::equal(x.m, x.m + 12, y.m) && !moved(x.m, y.m)) {
                p.objectWorld[i] = blendM34(x, y, t);
                changed = true;
            }
        }
    }
    if (changed) {
        shadowDirty = true;
    }
    p.blended = true;
}

// After drawing: the logic sees the tick's own poses again (the next poseScene re-skins).
void Session::Impl::presentRestore() {
    auto& p = present;
    if (!p.blended) {
        return;
    }
    if (!p.characterPose.empty()) {
        character.pose = std::move(p.characterPose);
        p.characterPose.clear();
        character.lastFrame = -1;
    }
    for (auto& [i, pose] : p.actorPose) {
        actors[i].pose = std::move(pose);
        actors[i].lastFrame = -1;
    }
    for (auto& [i, local] : p.actorLocal) {
        actors[i].lastLocal = std::move(local);
    }
    p.actorPose.clear();
    p.actorLocal.clear();
    p.blended = false;
    eft.blendAlpha = 1;
    shadowDirty = true;   // the shadow map shows the blended poses
}

// The game camera's view for this frame: the blend of the last two ticks' views (Interpolate),
// turned by a smooth drag's pending angles. False: the tick's view as it is.
bool Session::Impl::presentView(float out[12], float& fovy) const {
    if (!present.viewValid) {
        return cameraDragView(out);
    }
    captain_toad::RecoveredCamera::Pose v = present.view;
    const auto& d = cameraDrag;
    if ((d.pendH != 0 || d.pendV != 0) && cameraDragUsable() && !game.director.interp.active) {
        v.eye = captain_toad::RecoveredVec::add(v.eye, cameraDragOffset());
    }
    captain_toad::RecoveredCamera::M34 view;
    captain_toad::RecoveredCamera::lookAt(view, v.eye, v.target, v.up);
    std::copy(view.m, view.m + 12, out);
    fovy = v.fovy;
    return true;
}

// Level of detail (the actors' draw update 0x2d6e90 -> 0x220f68): the distance from the camera to
// the object along its LodCheckAxis axes, times tan(fovy / 2) unless LodDistanceOnly (the camera
// +0xc -> +0xa0 held 0.1262 with the game camera's fovy 14.4, observed at frame 1395 of hit2.tsv),
// against the object's two thresholds: level 0 up to the first, 1 up to the second, 2 beyond;
// a level without a model falls back to the highest one present (0x2e1764).
void Session::Impl::updateLevelOfDetail(const double eye[3], double fovyDegrees) {
    const double factor = std::tan(fovyDegrees * M_PI / 360);
    lodLevel.assign(stageWorld.objects.size(), -1);
    for (auto& m : meshes) {
        if (m.lod < 0 || m.object < 0 || size_t(m.object) >= stageWorld.objects.size()) {
            continue;
        }
        int& level = lodLevel[size_t(m.object)];
        if (level < 0) {
            const auto& w = stageWorld.objects[size_t(m.object)].world.m;
            const double d[3] = {w[3] - eye[0], w[7] - eye[1], w[11] - eye[2]};
            double sum = 0;
            for (int k = 0; k < 3; k++) {
                if (m.lodAxes & (1 << k)) {
                    sum += d[k] * d[k];
                }
            }
            double distance = std::sqrt(sum);
            if (!m.lodDistanceOnly) {
                distance *= factor;
            }
            level = distance <= m.lodThreshold[0] ? 0 : distance <= m.lodThreshold[1] ? 1 : 2;
            level = std::min(level, m.lodLevels - 1);
        }
        m.lodVisible = m.lod == level;
    }
}

void Session::Impl::setTransform(captain_toad::gpu::Program& program, const captain_toad::gpu::Mat4& modelView) const {
    const auto mvp = captain_toad::gpu::modelViewProjection(viewProjection, modelView);
    const auto mv = modelView.toFloat();
    const auto normal = captain_toad::gpu::normalMatrix(modelView);
    const auto projection = viewProjection.toFloat();
    program.setFloats("modelViewProjectionMatrix", mvp.data(), 16);
    program.setFloats("modelViewMatrix", mv.data(), 16);
    program.setFloats("normalMatrix", normal.data(), 9);
    program.setFloats("projectionMatrix", projection.data(), 16);
}

captain_toad::gpu::Draw Session::Impl::sceneDraw() const {
    captain_toad::gpu::Draw d;
    d.streams.push_back({vertexBuffer.get(),
                         0,
                         int(sizeof(Vertex)),
                         {{captain_toad::gpu::Attribute::Position, 3, 0},
                          {captain_toad::gpu::Attribute::Normal, 3, 12},
                          {captain_toad::gpu::Attribute::TexCoord0, 2, 24},
                          {captain_toad::gpu::Attribute::Color, 4, 32}}});
    d.streams.push_back({tangentBuffer.get(), 0, 12, {{captain_toad::gpu::Attribute::TexCoord1, 3, 0}}});
    if (secondUvBuffer) {
        d.streams.push_back({secondUvBuffer.get(), 0, 8, {{captain_toad::gpu::Attribute::TexCoord2, 2, 0}}});
    }
    return d;
}

// The game view in an area: centred, the shape viewShape gives, as large as fits.
std::array<int, 4> Session::Impl::viewRect(int x, int y, int w, int h) const {
    const float aspect = viewShape(float(w) / float(std::max(h, 1)));
    int vw = w, vh = h;
    if (float(w) > float(h) * aspect) {
        vw = int(std::lround(float(h) * aspect));
    } else {
        vh = int(std::lround(float(w) / aspect));
    }
    if (!wideView) {   // as before the wide view (integer 5:3 boxing)
        vw = w;
        vh = h;
        if (w * 3 > h * 5) {
            vw = h * 5 / 3;
        } else {
            vh = w * 3 / 5;
        }
    }
    return {x + (w - vw) / 2, y + (h - vh) / 2, vw, vh};
}

void Session::Impl::renderView(int viewX, int viewY, int w, int h, int eye) {
    captain_toad::gpu::Device& device = captain_toad::gpu::device();
    const bool headset = eyeView != nullptr;
    const bool useGame = gameCamera && game.enabled && !useReferenceCamera && !headset;
    device.setViewport({viewX, viewY, w, h});
    {
        captain_toad::gpu::Clear clear;
        clear.color = clear.depth = clear.stencil = true;
        clear.colorValue = headset ? std::array<float, 4>{0, 0, 0, 0} : std::array<float, 4>{.12f, .16f, .19f, 1};
        device.clear(clear, {viewX, viewY, w, h});   // only this view's rectangle (the other eye may be drawn)
    }
    if (useGame) {   // the original top screen's 5:3 (or the wide view's shape), boxed
        const auto r = viewRect(viewX, viewY, w, h);
        device.setViewport({r[0], r[1], r[2], r[3]});
        w = r[2];
        h = r[3];
    }
    // The frustum's half extents at the near plane, and the stereo eye's frustum shift.
    const double near = useGame ? game.nearClip : clipNear, far = useGame ? game.farClip : clipFar;
    float drawn[12];
    float drawnFovy = game.fovy();
    const bool ownView = useGame && presentView(drawn, drawnFovy);
    const double top = near * tan((useGame ? drawnFovy : fovy) * M_PI / 360);
    // The original 5:3 view (and any free view) as ever; a wide view's extents (viewExtent).
    double right = useGame ? top * 5 / 3 : top * w / h, ky = 1;
    if (useGame && wideView) {
        float fx, fy;
        viewExtent(float(w) / float(std::max(h, 1)), fx, fy);
        right = top * double(fx);
        ky = fy;
    }
    const double shift = eye * stereoParallax * right;
    viewProjection = headset              ? captain_toad::gpu::Mat4::fromColumnMajor(eyeView->projection.data())
                     : useReferenceCamera ? captain_toad::gpu::Mat4::fromColumnMajor(referenceProjection)
                                          : captain_toad::gpu::Mat4::frustum(-right - shift, right - shift, -top * ky,
                                                                             top * ky, near, far);
    captain_toad::gpu::Mat4 view;
    if (eye) {   // the eye beside the camera: the view moves the opposite way
        const double target = useGame ? game.targetDistance() : camera.distance;
        view = captain_toad::gpu::Mat4::translation(-shift * target / near, 0, 0);
    }
    if (headset) {
        view = captain_toad::gpu::Mat4::fromColumnMajor(eyeView->view.data());
    } else if (useReferenceCamera) {
        view = captain_toad::gpu::Mat4::fromColumnMajor(referenceView);
    } else if (useGame) {
        view = view * captain_toad::gpu::Mat4::fromRows34(ownView ? drawn : game.view.m);
    } else {
        view = view * captain_toad::gpu::Mat4::translation(0, 0, -camera.distance) *
               captain_toad::gpu::Mat4::rotation(camera.elevation, 1, 0, 0) *
               captain_toad::gpu::Mat4::rotation(-camera.azimuth, 0, 1, 0) *
               captain_toad::gpu::Mat4::translation(-camera.x, -camera.y, -camera.z);
    }
    // Single precision, as the fixed-function matrix stack kept it (eye position, level of detail,
    // billboards and lights derive from it).
    for (double& x : view.m) {
        x = double(float(x));
    }
    viewTransform = view;
    const double* viewMatrix = view.m.data();
    {
        double eyePos[3];
        for (int r = 0; r < 3; r++) {
            eyePos[r] = -(viewMatrix[r * 4 + 0] * viewMatrix[12] + viewMatrix[r * 4 + 1] * viewMatrix[13] +
                          viewMatrix[r * 4 + 2] * viewMatrix[14]);
        }
        updateLevelOfDetail(eyePos, useGame || headset ? drawnFovy : fovy);
    }
    for (int c = 0; c < 3; c++) {
        for (int r = 0; r < 3; r++) {
            billboardCamera[c * 3 + r] = (float)viewMatrix[r * 4 + c];
        }
    }
    // Billboard joints face this frame's camera: re-evaluate them now that it is known (a headset:
    // once per frame, facing the first eye; re-skinning them per eye rebuilt the shadow map twice).
    for (auto& a : actors) {
        if (eyeRepeat) {
            break;
        }
        if (a.hasBillboard &&
            a.update(a.drivenFrame >= 0 ? a.drivenFrame - a.frameOffset : animationFrame * a.fps / 60.f, vertices,
                     tangents, meshes)) {
            markVertices(a);
            shadowDirty = true;
        }
    }
    // The shadow map keeps what does not move: tell it which meshes' vertices changed.
    for (const auto& [first, count] : dirtyVertices) {
        const int end = int(first) + int(count);
        for (size_t i = 0; i < meshes.size(); i++) {
            if (meshes[i].first < end && meshes[i].first + meshes[i].count > int(first)) {
                shadowMap.changed(i);
            }
        }
    }
    // Placed objects that moved (a lift rising): their baked meshes cast from where they are
    // drawn, and the shadow map redraws them while they move.
    if (shadowObjectWorld.size() != stageWorld.objects.size()) {   // the baked meshes' placements
        shadowObjectWorld.clear();
        for (const auto& o : stageWorld.objects) {
            shadowObjectWorld.push_back(o.placement);
        }
    }
    std::vector<bool> objectMoved(stageWorld.objects.size(), false);
    for (size_t i = 0; i < stageWorld.objects.size(); i++) {
        const auto& w = presentObjectWorld(i);
        if (memcmp(w.m, shadowObjectWorld[i].m, sizeof w.m) != 0) {
            shadowObjectWorld[i] = w;
            objectMoved[i] = true;
            shadowDirty = true;
        }
    }
    for (size_t i = 0; i < meshes.size(); i++) {
        const int o = meshes[i].object;
        if (o >= 0 && size_t(o) < objectMoved.size() && objectMoved[size_t(o)] && !meshes[i].actor) {
            shadowMap.changed(i);
        }
    }
    shadowMap.skin = [this](const Mesh& m, captain_toad::gpu::Draw& d, captain_toad::gpu::Program*& program,
                            bool caster) {
        if (!skinnedMesh(m)) {
            return;
        }
        program = caster ? &skinnedShared("shadow caster", ShadowMap::kCasterVertex, ShadowMap::kCasterFragment)
                         : &meshProgram(m);
        addSkin(d);
    };
    shadowMap.meshMove = [this](const Mesh& m, float out[16]) {
        if (m.object < 0 || m.actor || size_t(m.object) >= stageWorld.objects.size()) {
            return false;
        }
        return objectDelta(stageWorld.objects[size_t(m.object)].placement, presentObjectWorld(size_t(m.object)), out);
    };
    uploadVertices();
    updateSkinPalette();
    if (shadowDirty) {
        const captain_toad::gpu::Rect vp = device.viewport();
        shadowMap.build(shadowResolution, shadowDirection, vertices, *vertexBuffer, meshes, programs, textures,
                        *whiteTexture);
        shadowDirty = false;
        device.setViewport(vp);
    }
    // The stand-in lighting of untranslated materials (the fixed-function light the shaders read
    // before): the light direction in eye space, its diffuse colour and the ambient colour.
    float lightEye[4];
    {
        const auto v = view.toFloat();
        for (int r = 0; r < 4; r++) {
            float x = 0;
            for (int k = 0; k < 4; k++) {
                x += v[size_t(k * 4 + r)] * lightDirection[k];
            }
            lightEye[r] = x;
        }
    }
    const captain_toad::gpu::Sampler nearestClamp{
        captain_toad::gpu::Wrap::ClampToEdge, captain_toad::gpu::Wrap::ClampToEdge, captain_toad::gpu::Filter::Nearest,
        captain_toad::gpu::Filter::Nearest};
    const captain_toad::gpu::Sampler whiteDefault{captain_toad::gpu::Wrap::Repeat, captain_toad::gpu::Wrap::Repeat,
                                                  captain_toad::gpu::Filter::Nearest,
                                                  captain_toad::gpu::Filter::Nearest};
    bool dropShadowDrawn = false;
    // The sky model follows the camera: its model translation is the eye position (traced sky draws:
    // gpu-trace-opening-c draw 7 and goal-gpu-700-c draw 5, identity rotation, translation = eye).
    // Exported sky vertices carry the SkyList translation; shift them by eye - that translation.
    double skyShift[3] = {0, 0, 0};
    if (captain_toad::data::JsonView st = scene["sky"]["translation"]) {
        double eyeAt[3];
        for (int r = 0; r < 3; r++) {
            eyeAt[r] = -(viewMatrix[r * 4 + 0] * viewMatrix[12] + viewMatrix[r * 4 + 1] * viewMatrix[13] +
                         viewMatrix[r * 4 + 2] * viewMatrix[14]);
        }
        for (int r = 0; r < 3; r++) {
            skyShift[r] = eyeAt[r] - st[r].d();
        }
    }
    // Original order: environment, then drop-shadow volumes, then characters/actors.
    // Then, after the characters, the "$water" surfaces over a copy of everything drawn so far
    // (Shaders.cpp waterShader).
    captain_toad::gpu::Rect sceneViewport;
    int sceneViews = 1;
    // The environment's translucent surfaces (blended or not writing depth, e.g. the chameleon
    // coins) after its opaque ones, as the original's drawer sorts them; otherwise surfaces drawn
    // later in the list overwrite them.
    for (int stage = 0; stage < 4; stage++) {
        const int pass = stage <= 1 ? 0 : stage - 1;
        if (stage == 2) {   // after the environment, before the characters and actors
            drawSilhouette();
        }
        if (pass == 2) {
            bool anyWater = false;
            for (const auto& m : meshes) {
                anyWater = anyWater || (m.water && m.drawn());
            }
            if (!anyWater) {
                break;
            }
            sceneViewport = device.viewport();
            sceneViews = device.views();   // a headset's two eyes: copied side by side
            if (!sceneCopy) {
                sceneCopy = device.createTexture(captain_toad::gpu::Format::RGBA8, sceneViewport.w * sceneViews,
                                                 sceneViewport.h);
            } else if (sceneCopy->width() != sceneViewport.w * sceneViews || sceneCopy->height() != sceneViewport.h) {
                sceneCopy->resize(sceneViewport.w * sceneViews, sceneViewport.h);
            }
            device.copyToTexture(*sceneCopy, sceneViewport);
        }
        for (const auto& m : meshes) {
            if (!m.drawn() || m.shadowOnly || (headset && m.sky)) {   // a headset: the room is the sky
                continue;
            }
            if (m.water != (pass == 2)) {
                continue;
            }
            if (m.object >= 0) {   // a placed object that died or is hidden
                const auto& o = stageWorld.objects[size_t(m.object)];
                if (!o.alive || !o.visible) {
                    continue;
                }
            }
            if (pass < 2 && (m.character || m.actor) != (pass == 1)) {
                continue;
            }
            if (pass == 0 && m.translucent != (stage == 1)) {
                continue;
            }
            if (pass == 1 && !dropShadowDrawn && picaLighting && !wire) {
                const auto mvp = captain_toad::gpu::modelViewProjection(viewProjection, view);
                float origin[3] = {0, 0, 0};
                if (!character.skin.empty()) {
                    auto r = character.joint("JointRoot").apply(origin, true);
                    dropShadow.draw({r[0], r[1], r[2]}, 42.5, 100, mvp);
                }
                for (const auto& s : actorShadows) {
                    if (actors[s.actor].hidden) {
                        continue;
                    }
                    auto r = actors[s.actor].joint(s.joint).apply(origin, true);
                    dropShadow.draw({r[0], r[1] + s.topOffset, r[2]}, s.radius, s.height, mvp);
                }
                dropShadowDrawn = true;
            }
            const auto& state = materialStates.at(m.material);
            captain_toad::gpu::Draw d = sceneDraw();
            captain_toad::gpu::RenderState& rs = d.state;
            // The polygon offset applies to filled polygons (not to the wireframe view's edges).
            rs.raster.depthBias = state.offset && !wire;
            rs.raster.biasFactor = rs.raster.biasUnits = -1;
            rs.raster.wireframe = wire;
            rs.depth.write = !m.sky && state.depthWrite;
            rs.depth.func = state.depthFunc;
            rs.depth.test = !(m.sky || !state.depthTest);
            if (state.blend) {
                rs.blend.enabled = true;
                rs.blend.colorOp = state.blendEq[0];
                rs.blend.alphaOp = state.blendEq[1];
                rs.blend.srcColor = state.blendFactor[0];
                rs.blend.dstColor = state.blendFactor[1];
                rs.blend.srcAlpha = state.blendFactor[2];
                rs.blend.dstAlpha = state.blendFactor[3];
            }
            captain_toad::gpu::Program& p = meshProgram(m);
            d.program = &p;
            d.samplers = {{"albedo", 0},      {"detail", 1},       {"textureZero", 2},
                          {"shadowDepth", 3}, {"lightingLuts", 4}, {"sceneColor", 5}};
            if (skinnedMesh(m)) {
                addSkin(d);
            }
            p.set("objectAlpha", m.object >= 0 ? stageWorld.objects[size_t(m.object)].alpha : 1.0f);
            if (m.water) {
                p.set("sceneViewport", float(sceneViewport.x), float(sceneViewport.y), float(sceneViewport.w),
                      float(sceneViewport.h));
                p.set("sceneViews", float(sceneViews));
                d.textures[5] = {sceneCopy.get(), {}};
            }
            p.setFloats("fixedLightPosition", lightEye, 4);
            p.setFloats("fixedLightDiffuse", diffuseLight, 4);
            p.setFloats("fixedLightAmbient", ambientLight, 4);
            p.set("useNormalMaps", getenv("DIORAMA_NO_NORMALMAPS") ? 0 : 1);
            p.set("useShadows", shadows && lighting);
            p.set("filterShadows", filterShadows);
            p.set("shadowKernel", shadowKernel);
            p.set("showShadowMask", shadowMask);
            p.set("shadowTexel", 1.f / shadowMap.size);
            p.set("shadowBias", float((shadowMap.biasWorld > 0 ? shadowMap.biasWorld : 0.35) / shadowMap.depthSpan));
            p.setFloats("shadowTint", shadowTint, 4);
            // A placed object's texture patterns and lighting colours (its players, game/StageObjects.hpp).
            int slotTexture[3] = {m.texture0, m.texture, m.detail};   // texture slots 0, 1, 2
            LightService::ColorOverride objectColors;
            const LightService::ColorOverride* lightColors = nullptr;
            if (m.character && !protagonistColor.file.empty()) {   // the wet look
                auto c = protagonistMaterials.clips.find(protagonistColor.file);
                if (c != protagonistMaterials.clips.end()) {
                    c->second.lightColors(m.material, protagonistColor.frame, false, objectColors);
                    lightColors = &objectColors;
                }
            }
            if (m.object >= 0 && size_t(m.object) < objectMaterials.size() && objectMaterials[size_t(m.object)]) {
                const StageMaterials& sm = *objectMaterials[size_t(m.object)];
                const auto& o = stageWorld.objects[size_t(m.object)];
                if (auto c = o.patternAnim.file.empty() ? sm.clips.end() : sm.clips.find(o.patternAnim.file);
                    c != sm.clips.end()) {
                    for (int k = 0; k < 3; k++) {
                        const int t = c->second.patternTexture(m.material, k, o.patternAnim.frame, o.patternAnim.loop);
                        if (t >= 0) {
                            slotTexture[k] = t;
                        }
                    }
                }
                if (auto c = o.colorAnim.file.empty() ? sm.clips.end() : sm.clips.find(o.colorAnim.file);
                    c != sm.clips.end()) {
                    c->second.lightColors(m.material, o.colorAnim.frame, o.colorAnim.loop, objectColors);
                    lightColors = &objectColors;
                }
            }
            // Units: 0 slot 1 (albedo), 1 slot 2 (detail), 2 slot 0, 3 the shadow map, 4 the
            // lighting tables, 5 the scene copy (water).
            auto slot = [&](int k) -> captain_toad::gpu::TextureBinding {
                const bool missing = slotTexture[k] < 0;
                return {missing ? whiteTexture.get() : textures.at(slotTexture[k]).get(), meshSampler(m, k, missing)};
            };
            d.textures[0] =
                slotTexture[1] >= 0 ? slot(1) : captain_toad::gpu::TextureBinding{whiteTexture.get(), whiteDefault};
            d.textures[1] = slot(2);
            d.textures[2] = slot(0);
            d.textures[3] = {shadowMap.depth(), nearestClamp};
            d.textures[4] = {lightingLuts.get(), nearestClamp};
            p.set("useLighting", lighting);
            p.set("lutRows", float(lutRowCount));
            if (picaLighting) {
                // The light area of the drawn actor: the protagonist's position or the object's.
                const LightArea* area = nullptr;
                if (!lightService.lightAreas.empty()) {
                    if (m.character) {
                        area = lightService.areaAt({player.body.pos.x, player.body.pos.y, player.body.pos.z});
                    } else if (m.object >= 0 && size_t(m.object) < stageWorld.objects.size()) {
                        const auto& w = stageWorld.objects[size_t(m.object)].world;
                        area = lightService.areaAt({w.m[3], w.m[7], w.m[11]});
                    }
                }
                lightService.apply(p, m.material, viewMatrix, lightColors, area);
            }
            {
                auto it = materialActor.find(m.material);
                if (it != materialActor.end()) {
                    const auto& cm = actorClipMaterials[it->second];
                    if (!cm.active.empty() && cm.sets.count(cm.active)) {
                        cm.sets.at(cm.active).apply(p, m.material, cm.frame);
                    } else {
                        actorMaterials[it->second].apply(p, m.material,
                                                         animationFrame * actors[it->second].fps / 60.f +
                                                             actors[it->second].frameOffset);
                    }
                }
            }
            if (m.sky && !skyMaterialClips.empty()) {   // the stage sky: its clips from the start (60 Hz)
                skyMaterials.rest.apply(p, m.material, 0);
                for (const auto& [file, loop] : skyMaterialClips) {
                    auto c = skyMaterials.clips.find(file);
                    if (c != skyMaterials.clips.end()) {
                        c->second.apply(p, m.material, animationFrame, loop);
                    }
                }
            }
            if (m.character && !protagonistMaterials.rest.constants.empty()) {
                protagonistMaterials.rest.apply(p, m.material, 0);
                auto c = protagonistColor.file.empty() ? protagonistMaterials.clips.end()
                                                       : protagonistMaterials.clips.find(protagonistColor.file);
                if (c != protagonistMaterials.clips.end()) {
                    c->second.apply(p, m.material, protagonistColor.frame, false);
                }
            }
            if (m.object >= 0 && size_t(m.object) < objectMaterials.size() && objectMaterials[size_t(m.object)]) {
                // A placed object's material animations: the rest values, then its colour and
                // texture players (game/StageObjects.hpp Object::MaterialPlayer).
                const StageMaterials& sm = *objectMaterials[size_t(m.object)];
                const auto& o = stageWorld.objects[size_t(m.object)];
                sm.rest.apply(p, m.material, 0);
                for (const auto* player : {&o.colorAnim, &o.uvAnim}) {
                    auto c = player->file.empty() ? sm.clips.end() : sm.clips.find(player->file);
                    if (c != sm.clips.end()) {
                        c->second.apply(p, m.material, player->frame, player->loop);
                    }
                }
            }
            if (!state.uvKeys.empty()) {   // ROM sky UV scroll: linear keys over the clip duration, looped
                double f = fmod(skyFrame, state.uvDuration);
                const auto& k = state.uvKeys;
                double v = k.back()[1];
                for (size_t j = 1; j < k.size(); j++) {
                    if (f < k[j][0]) {
                        v = k[j - 1][1] + (k[j][1] - k[j - 1][1]) * (f - k[j - 1][0]) / (k[j][0] - k[j - 1][0]);
                        break;
                    }
                }
                p.set("texTranslate1U", float(v));
            }
            {
                float inv[9];
                for (int r = 0; r < 3; r++) {
                    for (int c = 0; c < 3; c++) {
                        inv[c * 3 + r] = float(viewMatrix[r * 4 + c]);   // column-major transpose of view rotation
                    }
                }
                p.setFloats("viewToWorld", inv, 9);
            }
            d.first = m.first;
            d.count = m.count;
            if (m.object >= 0) {
                // A placed object: once per drawn instance, moved from its placement.
                // Baked meshes hold the placement, posed actors the current world matrix.
                const auto& o = stageWorld.objects[size_t(m.object)];
                const auto& world = presentObjectWorld(size_t(m.object));
                const auto& base = m.actor ? world : o.placement;
                const std::vector<captain_toad::RecoveredVec::M34> once{world};
                const auto& copies = present.blended && size_t(m.object) < present.objectDraws.size()
                                         ? present.objectDraws[size_t(m.object)]
                                         : o.draws;
                for (const auto& dm : copies.empty() ? once : copies) {
                    float delta[16];
                    if (objectDelta(base, dm, delta)) {
                        float sm[16];
                        for (int col = 0; col < 4; col++) {
                            for (int row = 0; row < 4; row++) {
                                float v = 0;
                                for (int k = 0; k < 4; k++) {
                                    v += shadowMap.matrix[size_t(k * 4 + row)] * delta[col * 4 + k];
                                }
                                sm[col * 4 + row] = v;
                            }
                        }
                        p.setFloats("shadowMatrix", sm, 16);
                        setTransform(p, view * captain_toad::gpu::Mat4::fromColumnMajor(delta));
                    } else {
                        p.setFloats("shadowMatrix", shadowMap.matrix.data(), 16);
                        setTransform(p, view);
                    }
                    device.draw(d);
                }
            } else {
                p.setFloats("shadowMatrix", shadowMap.matrix.data(), 16);
                setTransform(p, m.sky
                                    ? view * captain_toad::gpu::Mat4::translation(skyShift[0], skyShift[1], skyShift[2])
                                    : view);
                device.draw(d);
            }
        }
    }
    if (eft.enabled && !wire) {   // recovered particle effects, 3D groups (0, 2, 1)
        const auto P = viewProjection.toFloat();
        float proj[16], viewRows[12], eyeAt[3];
        for (int r = 0; r < 4; r++) {
            for (int c = 0; c < 4; c++) {
                proj[r * 4 + c] = P[size_t(c * 4 + r)];
            }
        }
        for (int r = 0; r < 3; r++) {
            for (int c = 0; c < 4; c++) {
                viewRows[r * 4 + c] = (float)viewMatrix[c * 4 + r];
            }
        }
        for (int r = 0; r < 3; r++) {
            eyeAt[r] = (float)-(viewMatrix[r * 4 + 0] * viewMatrix[12] + viewMatrix[r * 4 + 1] * viewMatrix[13] +
                                viewMatrix[r * 4 + 2] * viewMatrix[14]);
        }
        eft.draw3D(proj, viewRows, eyeAt);
    }
    // The course's bloom after the 3D scene and its particles (GPU trace: draws 200 / 201 follow
    // the particles), before the 2D layouts. Post effects are left out of the wireframe view, as
    // the particles.
    if (bloom.enabled && !wire && !headset) {
        const captain_toad::gpu::Rect vp = device.viewport();
        bloom.render(vp.x, vp.y, vp.w, vp.h);
    }
    // The camera mask after the bloom (trace: draw 380 follows the bloom's 378 / 379).
    if (cameraMask.enabled && !wire && !headset) {
        const captain_toad::gpu::Rect vp = device.viewport();
        cameraMask.render(vp.x, vp.y, vp.w, vp.h);
    }
    if (showCollision && collisionBuffer) {
        captain_toad::gpu::Program& program =
            device.sharedProgram("collision overlay",
                                 "in vec3 inPosition;uniform mat4 modelViewProjectionMatrix;\n"
                                 "void main(){gl_Position=modelViewProjectionMatrix*vec4(inPosition,1.0);}",
                                 "uniform vec4 color;void main(){fragColor=color;}");
        const auto mvp = captain_toad::gpu::modelViewProjection(viewProjection, view);
        program.setFloats("modelViewProjectionMatrix", mvp.data(), 16);
        program.set("color", .15f, 1.f, .85f, .8f);
        captain_toad::gpu::Draw d;
        d.program = &program;
        d.state.depth = {true, true, captain_toad::gpu::Compare::LessEqual};
        d.state.raster.wireframe = true;
        d.state.raster.depthBias = true;
        d.state.raster.biasFactor = d.state.raster.biasUnits = -1;
        d.streams = {{collisionBuffer.get(), 0, 12, {{captain_toad::gpu::Attribute::Position, 3, 0}}}};
        d.count = int(collision.size() / 3);
        device.draw(d);
    }
    if (!guidePoints.empty()) {
        drawGuideBalloons(viewMatrix, viewProjection);
    }
    if (useGame && game.goalEnabled && game.goal.active()) {
        drawGoalLayouts(device.viewport());
        if (goalStarObject >= 0 && eft.enabled && game.goal.courseClearVisible) {
            eft.draw2D();   // a course: the banner's confetti (the banner itself is the shell's CourseUi)
        }
    }
    if (useGame && introEnabled && intro.active() && layouts.count("WipeCircleWhite_TOP")) {
        Layout& l = *layouts["WipeCircleWhite_TOP"];
        l.shown = intro.wipeVisible;
        l.active.clear();
        l.active.push_back({&l.animations.at("End"), intro.wipeFrame});
        l.update();
        const captain_toad::gpu::Rect view = device.viewport();
        device.setViewport(frameRect(view, true));
        l.draw();
        device.setViewport(view);
    }
    device.checkErrors("scene rendering");
}

// The protagonist's silhouette (sub-actor SilhouetteModel of his archive, InitSubActor: draw
// category シルエット[プレイヤー]; his model's "-silhouette" materials). GPU trace of
// TouchPuzzleMiniStage (frame 600 of touchpuzzle-goal-a, draw 38): his model drawn where it lies
// behind what is already drawn (depth test greater, no depth write), constant black at alpha
// 128 / 255 blended over the picture, the stencil test "not equal 0x80" replacing the stencil with
// 0x80 so overlapping parts darken once. Drawn after the environment and before the characters
// and actors (as the original, after the terrain: objects drawn later do not hide him). Native:
// his full model instead of the low-detail one (the same outline); off with the Silhouette
// setting.
void Session::Impl::drawSilhouette() {
    if (!silhouette || wire) {
        return;
    }
    bool any = false;
    for (const auto& m : meshes) {
        any = any || (m.character && m.drawn() && !m.shadowOnly);
    }
    if (!any) {
        return;
    }
    captain_toad::gpu::Device& device = captain_toad::gpu::device();
    static const char* const vertex = "in vec3 inPosition;uniform mat4 modelViewProjectionMatrix;\n"
                                      "void main(){gl_Position=modelViewProjectionMatrix*vec4(inPosition,1.0);}";
    static const char* const fragment = "void main(){fragColor=vec4(0.0,0.0,0.0,128.0/255.0);}";
    captain_toad::gpu::Program& program = device.sharedProgram("silhouette", vertex, fragment);
    const auto mvp = captain_toad::gpu::modelViewProjection(viewProjection, viewTransform);
    program.setFloats("modelViewProjectionMatrix", mvp.data(), 16);
    captain_toad::gpu::Clear clear;
    clear.stencil = true;
    device.clear(clear);
    captain_toad::gpu::Draw d;
    d.program = &program;
    d.state.depth = {true, false, captain_toad::gpu::Compare::Greater};
    d.state.stencil.enabled = true;
    d.state.stencil.func = captain_toad::gpu::Compare::NotEqual;
    d.state.stencil.reference = 0x80;
    d.state.stencil.readMask = 0xff;
    d.state.stencil.pass = captain_toad::gpu::StencilOp::Replace;
    d.state.blend = captain_toad::gpu::BlendState::alpha();
    d.streams = {{vertexBuffer.get(), 0, int(sizeof(Vertex)), {{captain_toad::gpu::Attribute::Position, 3, 0}}}};
    for (const auto& m : meshes) {
        if (m.character && m.drawn() && !m.shadowOnly) {
            captain_toad::gpu::Draw md = d;
            if (skinnedMesh(m)) {
                captain_toad::gpu::Program& skinned = skinnedShared("silhouette", vertex, fragment);
                skinned.setFloats("modelViewProjectionMatrix", mvp.data(), 16);
                md.program = &skinned;
                addSkin(md);
            }
            md.first = m.first;
            md.count = m.count;
            device.draw(md);
        }
    }
}

}   // namespace captain_toad::session
