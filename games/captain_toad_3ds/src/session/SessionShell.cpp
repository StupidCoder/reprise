// =============================================================================================
// SessionShell.cpp - the interactive session: clock, controls, inspection camera and audio
// =============================================================================================
//
// What the shell can do with a running session: advance it in real time with the player's
// controls, switch between the game camera and the free inspection camera, toggle display
// services, and pull its audio.
// =============================================================================================
#include "session/SessionState.hpp"

namespace captain_toad::session {

Session::Impl::~Impl() = default;

Session::Session(const std::filesystem::path& package) : impl_(std::make_unique<Impl>()) {
    Impl& r = *impl_;
    r.scenePath = package;
    r.loadScene();
    r.introEnabled = r.game.enabled && !getenv("DIORAMA_NO_INTRO");
    if (!r.introEnabled) {
        r.skipIntro();
    }
}

Session::~Session() = default;

// Real-time clock: the idle animation and sky advance with real time; the game runs whole 60 Hz
// frames (at most six per call, so a stall does not make the game race to catch up).
void Session::advance(double seconds, const Controls& controls) {
    Impl& r = *impl_;
    const double dt = std::min(seconds, 0.1);
    const bool introPlaying = r.introEnabled && r.intro.active();
    if (!introPlaying) {
        r.animationFrame += dt * r.character.fps;
        r.skyFrame += dt * 60;
    }
    // The controller state in the game's bit layout.
    captain_toad::RecoveredCamera::CameraPad pad;
    pad.held = controls.buttons;
    pad.cstick[0] = controls.cstick[0];
    pad.cstick[1] = controls.cstick[1];
    // C-stick digital directions (platform threshold 0.5; the HID threshold is not recovered).
    if (pad.cstick[1] > 0.5f) {
        pad.held |= 0x1000000;
    }
    if (pad.cstick[1] < -0.5f) {
        pad.held |= 0x2000000;
    }
    if (pad.cstick[0] < -0.5f) {
        pad.held |= 0x4000000;
    }
    if (pad.cstick[0] > 0.5f) {
        pad.held |= 0x8000000;
    }
    if (controls.touch && r.gameCamera) {
        pad.held |= captain_toad::RecoveredCamera::kTouch;
    }
    pad.touch[0] = controls.touchPos[0];
    pad.touch[1] = controls.touchPos[1];
    r.touchHeld = controls.touch;
    r.touchView[0] = controls.touchView[0];
    r.touchView[1] = controls.touchView[1];
    r.viewAspect = controls.viewAspect;
    r.cameraDragFrame(dt, controls.touch, controls.touchPos);
    r.playerClock += dt;
    int n = 0;
    // A small tolerance: a clock that advances by exactly 1/60 per frame must not lose a frame to
    // rounding (and then run two on the next call).
    while (r.playerClock >= 1.0 / 60 - 1e-9 && n < 6) {
        r.presentedFrame(pad, controls.stick[0], controls.stick[1]);
        r.playerClock -= 1.0 / 60;
        n++;
    }
    if (n == 6) {
        r.playerClock = 0;
    }
    r.present.alpha = float(std::clamp((r.present.sinceTick + r.playerClock) * 30.0, 0.0, 1.0));
}

std::array<int, 4> Session::gameRect(int x, int y, int w, int h) const {
    return impl_->viewRect(x, y, w, h);
}

void Session::setPresentationFraction(double fraction) {
    auto& r=*impl_;
    r.present.alpha=float(std::clamp((r.present.sinceTick+r.playerClock+std::clamp(fraction,0.0,1.0)/60.0)*30.0,0.0,1.0));
}
float Session::presentAlpha() const {
    return impl_->present.enabled ? impl_->present.alpha : 1.0f;
}

// One 60 Hz game frame, and the drawing records: a game tick (every second frame the player takes
// input) is recorded for the frames drawn after it (Presentation.hpp).
void Session::Impl::presentedFrame(const captain_toad::RecoveredCamera::CameraPad& pad, float sx, float sy) {
    const long long before = player.frames60;
    gameFrame(pad, sx, sy);
    if (player.frames60 != before && player.frames60 % 2 == 0) {
        presentCapture();
        present.sinceTick = 0;
    } else {
        present.sinceTick += 1.0 / 60;
    }
}

// The original's 5:3 frame in a view: inside it (banners, cards, menus) or covering it (wipes,
// which must close over the whole view; a circle stays round). It may reach past the view.
std::array<int, 4> Session::originalFrame(const std::array<int, 4>& view, bool cover) {
    const float aspect = 5.0f / 3.0f;
    float fw = float(view[2]), fh = float(view[3]);
    if ((float(view[2]) > float(view[3]) * aspect) != cover) {
        fw = float(view[3]) * aspect;
    } else {
        fh = float(view[2]) / aspect;
    }
    const int w = int(std::lround(fw)), h = int(std::lround(fh));
    return {view[0] + (view[2] - w) / 2, view[1] + (view[3] - h) / 2, w, h};
}

bool Session::cameraMoving() const {
    return impl_->cameraDrag.moving();
}

// ---- Smooth camera drag (enhancement, Toggle::SmoothCamera) ---------------------------------
// The original turns the camera by the touch's movement once per 30 Hz tick (0x20ad58: 0.35
// degrees per touch-screen pixel, at most 10 / 9 degrees a tick, inertia 0.8 a tick) and clicks
// (PgGyroMoving*) as the speed changes. Here the same 0.35 degrees per pixel are gathered every
// display frame, shown at once (renderView) and handed to the camera at the next tick; the
// ticket's limits apply as in the game (PlayerCamera::clampDrag). Released, the turn coasts with
// gentle inertia. The clicks stay quiet while the camera turns this way.
namespace {
constexpr float kDragDegreesPerPixel = 0.35f;   // 0x20ad58
constexpr float kDragStart = 3.0f;              // touch-screen pixels before a touch turns the camera
constexpr float kDragMaxSpeed = 300.0f;         // degrees per second (10 a tick at 30 Hz)
constexpr float kDragDecay = 5.0f;              // inertia: speed falls by e every 1 / kDragDecay s
constexpr float kDragStop = 4.0f;               // degrees per second
}   // namespace

bool Session::Impl::cameraDragUsable() const {
    const auto* cam = game.director.areaCam;
    return cameraDrag.enabled && gameCamera && game.enabled && cam && game.director.current == cam &&
           cam->dragAllowed(game.globals) && !game.goal.active() && !(introEnabled && intro.active()) && !stageMissed &&
           !stageGoalReached;
}

void Session::Impl::cameraDragFrame(double dt, bool touch, const float pos[2]) {
    auto& d = cameraDrag;
    if (auto* cam = game.director.areaCam) {
        cam->hostDrag = d.enabled;
    }
    if (!cameraDragUsable()) {
        d.pendH = d.pendV = d.velH = d.velV = 0;
        d.dragging = false;
        d.held = touch;
        d.last[0] = pos[0];
        d.last[1] = pos[1];
        return;
    }
    const float t = float(std::max(dt, 1e-4));
    if (touch && !d.held) {   // a new touch catches a coasting camera
        d.held = true;
        d.dragging = false;
        d.start[0] = d.last[0] = pos[0];
        d.start[1] = d.last[1] = pos[1];
        d.velH = d.velV = 0;
    } else if (touch) {
        const float dx = pos[0] - d.last[0], dy = pos[1] - d.last[1];
        d.last[0] = pos[0];
        d.last[1] = pos[1];
        if (!d.dragging && std::hypot(pos[0] - d.start[0], pos[1] - d.start[1]) >= kDragStart) {
            d.dragging = true;
        }
        float dh = 0, dv = 0;
        if (d.dragging) {
            dh = dx * -kDragDegreesPerPixel;
            dv = dy * kDragDegreesPerPixel;
            d.pendH += dh;
            d.pendV += dv;
        }
        // The finger's speed, smoothed over about 50 ms (what a release carries on).
        const float k = 1.0f - std::exp(-t * 20.0f);
        d.velH += (std::clamp(dh / t, -kDragMaxSpeed, kDragMaxSpeed) - d.velH) * k;
        d.velV += (std::clamp(dv / t, -kDragMaxSpeed, kDragMaxSpeed) - d.velV) * k;
    } else {
        d.held = false;
        d.dragging = false;
        d.pendH += d.velH * t;
        d.pendV += d.velV * t;
        const float f = std::exp(-t * kDragDecay);
        d.velH *= f;
        d.velV *= f;
        if (std::hypot(d.velH, d.velV) < kDragStop) {
            d.velH = d.velV = 0;
        }
    }
    const float wantV = d.pendV;
    game.director.areaCam->clampDrag(d.pendH, d.pendV);
    if (d.pendV != wantV) {
        d.velV = 0;   // against the limit: no coasting into it
    }
}

// At the start of a camera tick: the gathered turn becomes the camera's.
void Session::Impl::cameraDragHandOver() {
    auto& d = cameraDrag;
    d.turnedThisTick = false;
    if (d.pendH == 0 && d.pendV == 0) {
        return;
    }
    if (cameraDragUsable()) {
        if (present.cur.valid && !game.director.interp.active) {
            // The record this tick's blend starts from showed the turn already (drawn on top of
            // it): it keeps it, so the blend does not swing back. (While the director eases into
            // the camera the turn is not drawn on top; the blend carries it a tick later.)
            present.cur.view.eye = captain_toad::RecoveredVec::add(present.cur.view.eye, cameraDragOffset());
        }
        game.director.areaCam->applyDrag(d.pendH, d.pendV);
        d.turnedThisTick = true;
    }
    d.pendH = d.pendV = 0;
}

// How far the gathered turn moves the area camera's eye from where the last tick left it (the
// camera's own orbit, PlayerCamera::eyeAt; the target stays).
captain_toad::RecoveredVec::V3 Session::Impl::cameraDragOffset() const {
    const auto& cam = *game.director.areaCam;
    float dh = cameraDrag.pendH, dv = cameraDrag.pendV;
    cam.clampDrag(dh, dv);
    const float sign = cam.upsideDown ? -1.0f : 1.0f;
    return captain_toad::RecoveredVec::sub(cam.eyeAt(cam.outV + dv * sign, cam.outH + dh * sign), cam.eye);
}

// The view with the turn gathered since the last tick (false: the tick's view as it is).
bool Session::Impl::cameraDragView(float out[12]) const {
    const auto& d = cameraDrag;
    // While the director still eases into the area camera, its view is not the camera's own: the
    // turn shows from the next tick on (as in the original, which turns under the easing).
    if ((d.pendH == 0 && d.pendV == 0) || !cameraDragUsable() || game.director.interp.active) {
        return false;
    }
    const auto& cam = *game.director.areaCam;
    const float sign = cam.upsideDown ? -1.0f : 1.0f;
    const captain_toad::RecoveredVec::V3 eye = cam.eyeAt(cam.outV + d.pendV * sign, cam.outH + d.pendH * sign);
    captain_toad::RecoveredCamera::M34 view;
    captain_toad::RecoveredCamera::lookAt(view, eye, game.director.view.target, game.director.view.up);
    std::copy(view.m, view.m + 12, out);
    return true;
}

void Session::render(int x, int y, int w, int h) {
    impl_->render(x, y, w, h);
}

void Session::renderEyes(const std::vector<EyeView>& eyes, const std::function<void(size_t eye)>& after) {
    impl_->renderEyes(eyes, after);
}

// The course's own scenery: its static meshes (baked in world coordinates: the course map and the
// fixed objects; not the sky, characters, posed actors or shadow proxies). The collision reaches
// further (PullUpStage: x -3913..2913 against the scenery's -1754..1554).
std::array<float, 6> Session::bounds() const {
    const Impl& r = *impl_;
    std::array<float, 6> b{1e30f, 1e30f, 1e30f, -1e30f, -1e30f, -1e30f};
    auto add = [&](const float* p) {
        for (int k = 0; k < 3; k++) {
            b[size_t(k)] = std::min(b[size_t(k)], p[k]);
            b[size_t(k + 3)] = std::max(b[size_t(k + 3)], p[k]);
        }
    };
    for (const auto& m : r.meshes) {
        if (m.sky || m.character || m.actor || m.shadowOnly) {
            continue;
        }
        for (int v = m.first; v < m.first + m.count && size_t(v) < r.vertices.size(); v++) {
            add(r.vertices[size_t(v)].p);
        }
    }
    if (b[0] > b[3]) {   // no scenery: the collision
        for (size_t i = 0; i + 2 < r.collision.size(); i += 3) {
            add(&r.collision[i]);
        }
    }
    if (b[0] > b[3]) {
        b = {-500, 0, -500, 500, 500, 500};
    }
    return b;
}

void Session::setShadowResolution(int size) {
    Impl& r = *impl_;
    r.nativeShadowResolution = std::clamp(size, 256, 4096);
    r.setShadowQuality(r.highQualityShadows);
}

void Session::setShadowFilter(int radius) {
    impl_->shadowKernel = std::clamp(radius, 0, 2);
}

std::array<float, 3> Session::cameraForward() const {
    const auto& m = impl_->game.view.m;   // world -> eye rows; the eye looks along its -z
    return {-m[8], -m[9], -m[10]};
}

void Session::selectView(int view) {
    Impl& r = *impl_;
    if (view == 0) {
        r.gameCamera = true;
        return;
    }
    if (view == 4) {
        // Close-up of the protagonist at the stage start.
        r.preset(1);
        r.camera.x = -400;
        r.camera.y = -730;
        r.camera.z = 550;
        r.camera.distance = 650;
        r.camera.azimuth = 55;
        r.camera.elevation = 10;
        return;
    }
    r.preset(view);
}

bool Session::gameCameraActive() const {
    return impl_->gameCamera && impl_->game.enabled;
}

// dx right, dy up, in points.
void Session::orbit(double dx, double dy) {
    Camera& c = impl_->camera;
    c.azimuth += dx * .4;
    c.elevation = std::clamp(c.elevation + dy * .4, -85., 85.);
}

void Session::pan(double dx, double dy) {
    Camera& c = impl_->camera;
    double a = c.azimuth * M_PI / 180, scale = c.distance / 1500;
    c.x -= dx * cos(a) * scale;
    c.z += dx * sin(a) * scale;
    c.y += dy * scale;
}

// Positive steps zoom in.
void Session::zoom(double steps) {
    Camera& c = impl_->camera;
    c.distance = std::clamp(c.distance * exp(-steps * .08), 300., 12000.);
}

void Session::toggle(Toggle t) {
    Impl& r = *impl_;
    switch (t) {
    case Toggle::Collision:
        r.showCollision = !r.showCollision;
        break;
    case Toggle::Wireframe:
        r.wire = !r.wire;
        break;
    case Toggle::Lighting:
        r.lighting = !r.lighting;
        break;
    case Toggle::ShadowQuality:
        r.setShadowQuality(!r.highQualityShadows);
        break;
    case Toggle::Stereo:
        r.stereo = !r.stereo;
        break;
    case Toggle::TapToWalk:
        r.tapToWalk = !r.tapToWalk;
        break;
    case Toggle::Silhouette:
        r.silhouette = !r.silhouette;
        break;
    case Toggle::SmoothCamera:
        r.cameraDrag.enabled = !r.cameraDrag.enabled;
        break;
    case Toggle::WideView:
        r.wideView = !r.wideView;
        break;
    case Toggle::GpuSkinning:
        r.setGpuSkinning(!r.gpuSkinning);
        break;
    case Toggle::Interpolate:
        r.present.enabled = !r.present.enabled;
        for (auto& a : r.actors) {
            a.smoothSamples = r.present.enabled;
        }
        r.eft.keepPrevious = r.present.enabled;
        r.present.prev.valid = r.present.cur.valid = false;
        break;
    }
}

bool Session::enabled(Toggle t) const {
    const Impl& r = *impl_;
    switch (t) {
    case Toggle::Collision:
        return r.showCollision;
    case Toggle::Wireframe:
        return r.wire;
    case Toggle::Lighting:
        return r.lighting;
    case Toggle::ShadowQuality:
        return r.highQualityShadows;
    case Toggle::Stereo:
        return r.stereo;
    case Toggle::TapToWalk:
        return r.tapToWalk;
    case Toggle::Silhouette:
        return r.silhouette;
    case Toggle::SmoothCamera:
        return r.cameraDrag.enabled;
    case Toggle::Interpolate:
        return r.present.enabled;
    case Toggle::WideView:
        return r.wideView;
    case Toggle::GpuSkinning:
        return r.gpuSkinning;
    }
    return false;
}

void Session::set(Toggle t, bool on) {
    if (enabled(t) != on) {
        toggle(t);
    }
}

// Steps 1..5 give a separation at infinity of 1%, 1.5%, 2%, 2.5% and 3% of the view's width.
void Session::setStereoIntensity(int step) {
    impl_->stereoParallax = 0.005 * (std::clamp(step, 1, kStereoSteps) + 1);
}

std::string Session::status() const {
    const Impl& r = *impl_;
    if (r.player.enabled || !r.player.lastError.empty()) {
        return "Player: " + r.player.status();
    }
    return "";
}

// The opening has played out once the title logo's End animation has finished and the cutscene
// stream (Demo_Op01, not looping, about 45 s) has run out, which is a few seconds later. Without
// sound output the stream state does not advance, so a minute after the logo is the limit.
bool Session::Impl::openingFinished() {
    const auto& title = game.goal.titleAnim;
    if (!game.goalEnabled || title.name != "End" || !title.ended()) {
        return false;
    }
    return !snd || !snd->streamPlaying("Stm_Kp_bgm_Demo_Op01") || title.age > 60 * 30;
}

void Session::onTick(std::function<void(const StartTick&)> hook) {
    impl_->tickHook = std::move(hook);
}

bool Session::missed() const {
    return impl_->stageMissed;
}

int Session::collectedGems() const {
    int bits = 0;
    for (int id : impl_->stageWorld.counters.collectItemIds) {
        if (id >= 0 && id < 3) {
            bits |= 1 << id;
        }
    }
    return bits;
}

// The course's clear for the shell (CourseUi): with a goal demo, from GoalPose step 0 (the
// banner then appears at GoalPose step 96, as HeadCourseClear in 0x1df400); without one, the touch.
bool Session::goalReached() const {
    const Impl& r = *impl_;
    if (r.goalStarObject >= 0 && r.game.goalEnabled) {
        return r.game.goal.active() && r.game.goal.fixedCamera;
    }
    return r.stageGoalReached;
}

bool Session::finished() const {
    return impl_->openingFinished();
}

bool Session::hasSound() const {
    return impl_->snd != nullptr;
}

int Session::audioSampleRate() const {
    return int(std::lround(captain_toad::audio::kOutputRate));
}

// The audio thread pulls whole DSP frames (160 samples) from the sound system.
void Session::fillAudio(int16_t* stereo, int frames) {
    Impl& r = *impl_;
    if (!r.snd) {
        std::fill(stereo, stereo + size_t(frames) * 2, int16_t(0));
        return;
    }
    for (int i = 0; i < frames; i++) {
        if (r.audioPendingAt >= r.audioPending.size()) {
            r.audioPending.assign(captain_toad::audio::kFrameSamples * 2, 0);
            r.snd->render(r.audioPending.data(), 1);
            r.audioPendingAt = 0;
        }
        stereo[i * 2] = r.audioPending[r.audioPendingAt];
        stereo[i * 2 + 1] = r.audioPending[r.audioPendingAt + 1];
        r.audioPendingAt += 2;
    }
}

}   // namespace captain_toad::session
