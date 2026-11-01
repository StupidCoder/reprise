// =============================================================================================
// SessionResearch.cpp - the headless research modes
// =============================================================================================
//
// These modes make the port checkable against the original: they render into offscreen
// framebuffers, run deterministically, and write images, logs and audio that the research tools
// compare with traced runs of the original (see the local research archive).
// =============================================================================================
#include "session/SessionState.hpp"
#include "platform/Offscreen.hpp"

namespace captain_toad::session {

using captain_toad::platform::offscreenTarget;

void Session::reference(const std::filesystem::path& package, const std::filesystem::path& cameraFile,
                        const std::filesystem::path& out, const ImageWriter& save) {
    Session s;
    s.impl_ = std::make_unique<Impl>();
    Impl& r = *s.impl_;
    r.scenePath = package;
    r.previewFrameFixed = true;
    // JSON: width, height, view (3x4 row-major world->view), projection (4x4 row-major), frame.
    const captain_toad::data::Value document = captain_toad::data::readJson(cameraFile);
    captain_toad::data::JsonView c = document;
    int w = c["width"].i(), h = c["height"].i();
    for (int i = 0; i < 16; i++) {
        r.referenceView[i] = (i % 4 == 3) ? (i == 15) : 0;
    }
    for (int row = 0; row < 3; row++) {
        for (int k = 0; k < 4; k++) {
            r.referenceView[k * 4 + row] = c["view"][row][k].d();
        }
    }
    for (int row = 0; row < 4; row++) {
        for (int k = 0; k < 4; k++) {
            r.referenceProjection[k * 4 + row] = c["projection"][row][k].d();
        }
    }
    r.useReferenceCamera = true;
    if (c["frame"]) {
        r.animationFrame = c["frame"].f();
    }
    if (c["shadows"]) {
        r.shadows = c["shadows"].b();
    }
    if (c["shadow_mask"]) {
        r.shadowMask = c["shadow_mask"].i();
    }
    if (c["shadow_quality"]) {
        r.setShadowQuality(c["shadow_quality"].is("high"));
    }
    if (c["shadow_resolution"]) {
        r.shadowResolution = c["shadow_resolution"].i();
    }
    if (c["filter_shadows"]) {
        r.filterShadows = c["filter_shadows"].b();
    }
    r.loadScene();
    offscreenTarget(w, h);
    r.render(0, 0, w, h);
    save(out, captain_toad::platform::readFramebuffer(w, h));
}

// Scripted play: input lines "start end device index id value" (libretro style: device 5 analog
// index 0 circle pad / index 1 C-stick, id 0 x / id 1 y with y down; device 1 joypad ids mapped
// to the game's controller bits; device 6 pointer, id 0 x / id 1 y over the reference host's
// 400 x 480 two-screen frame (-32767..32767; the touch screen is x 40..360, y 240..480), id 2
// pressed), 60 Hz frames from the stage start with the game camera; writes
// player.tsv (player and camera) and a 400x240 game-camera capture every 30 frames.
static double renderSeconds = 0;
static int renderFrames = 0;

void Session::play(const std::filesystem::path& package, const std::filesystem::path& input,
                   const std::filesystem::path& out, const ImageWriter& save) {
    Session s;
    s.impl_ = std::make_unique<Impl>();
    Impl& r = *s.impl_;
    r.scenePath = package;
    r.loadScene();
    if (!r.player.enabled) {
        throw std::runtime_error("Scene has no player");
    }
    offscreenTarget(1000, 600);
    struct Line {
        int start, end, device, index, id, value;
    };
    std::vector<Line> lines;
    int last = 0;
    std::ifstream script(input);
    if (!script) {
        fprintf(stderr, "Cannot read input script %s\n", input.string().c_str());
        exit(2);
    }   // an empty file is a valid idle script
    std::string text;
    while (std::getline(script, text)) {
        std::istringstream fields(text);
        Line l;
        if (fields >> l.start >> l.end >> l.device >> l.index >> l.id >> l.value) {
            lines.push_back(l);
            last = std::max(last, l.end);
        }
    }
    int frames = last + 120;
    if (const char* n = getenv("DIORAMA_PLAY_FRAMES")) {
        frames = atoi(n);   // longer plays (goal demo)
    }
    FILE* log = fopen((out / "player.tsv").string().c_str(), "w");
    fprintf(log, "frame\tstate\taction\tanim_frame\tx\ty\tz\tfront_x\tfront_z\tvel_x\tvel_y\tvel_z\teye_x\teye_"
                 "y\teye_z\ttarget_x\ttarget_y\ttarget_z\tfovy\n");
    // libretro joypad ids -> the game's controller bits (pad-bits-probe evidence); analog index 1 = C-stick
    const uint32_t joyBits[16] = {0x2, 0x10, 0x800,  0,      0x10000, 0x20000, 0x40000, 0x80000,
                                  0x1, 0x8,  0x2000, 0x4000, 0x4,     0x20,    0,       0x8000};
    captain_toad::gpu::Target* const gameTarget = offscreenTarget(400, 240);
    r.gameCamera = r.game.enabled;
    r.introEnabled = getenv("DIORAMA_INTRO") != nullptr;
    std::set<int> saveList;
    if (const char* l = getenv("DIORAMA_PLAY_SAVE")) {
        std::istringstream is(l);
        int f;
        while (is >> f) {
            saveList.insert(f);
        }
    }   // capture frames (default every 30)
    if (!r.introEnabled) {
        r.skipIntro();
    }
    // Research: start in a power form (DIORAMA_PLAYER_POWER=1: Mini).
    if (const char* pw = getenv("DIORAMA_PLAYER_POWER")) {
        r.setPlayerForm(atoi(pw));
    }
    // Research: start in the invincible mode (DIORAMA_PLAYER_INVINCIBLE=1).
    if (getenv("DIORAMA_PLAYER_INVINCIBLE")) {
        r.stageWorld.invincibleRequest = true;
    }
    // Research: start the protagonist elsewhere in the course (DIORAMA_PLAYER_AT=x,y,z[,yaw deg]).
    if (const char* at = getenv("DIORAMA_PLAYER_AT")) {
        captain_toad::RecoveredVec::V3 p;
        float yaw = 0;
        const int n = sscanf(at, "%f,%f,%f,%f", &p.x, &p.y, &p.z, &yaw);
        if (n >= 3) {
            const float ry = yaw * float(M_PI / 180.0);
            r.player.place(p, n == 4 ? captain_toad::RecoveredVec::V3{std::sin(ry), 0, std::cos(ry)}
                                     : r.player.body.front);
        }
    }
    // Research: display switches for the saved frames (DIORAMA_PLAY_TOGGLES=name,...: wireframe,
    // collision, unlit, original-shadows, stereo, no-silhouette, smooth-camera, interpolate: the
    // frames between ticks drawn blended, as the shell does at more than 30 frames a second).
    if (const char* list = getenv("DIORAMA_PLAY_TOGGLES")) {
        const std::string names = std::string(",") + list + ",";
        auto has = [&](const char* name) {
            return names.find(std::string(",") + name + ",") != std::string::npos;
        };
        s.set(Toggle::Wireframe, has("wireframe"));
        s.set(Toggle::Collision, has("collision"));
        s.set(Toggle::Lighting, !has("unlit"));
        s.set(Toggle::ShadowQuality, !has("original-shadows"));
        s.set(Toggle::Stereo, has("stereo"));
        s.set(Toggle::Silhouette, !has("no-silhouette"));
        s.set(Toggle::SmoothCamera, has("smooth-camera"));
        s.set(Toggle::Interpolate, has("interpolate"));
    }
    FILE* soundOut = getenv("DIORAMA_SOUND_OUT") ? fopen(getenv("DIORAMA_SOUND_OUT"), "wb") : nullptr;
    std::vector<int16_t> soundBuf;
    long soundFramesOut = 0;
    const long soundLatency = getenv("DIORAMA_SOUND_LATENCY") ? atol(getenv("DIORAMA_SOUND_LATENCY")) : 11;
    if (const char* run = getenv("DIORAMA_PAD_RUN")) {
        if (!r.padTrace.load(run)) {
            fprintf(stderr, "DIORAMA_PAD_RUN %s lacks watch 3104ede8:130\n", run);
            exit(2);
        }
    }
    bool finishedLogged = false;
    // DIORAMA_PAD_RUN record for frame fr: fr + 1 for the opening's runs (call numbering); runs
    // that record one watch per frame from a saved state need fr - 1 (DIORAMA_PAD_OFFSET=-1: the
    // reference host's first frame is 0, this loop's 1; route pullup-pickaxe-a then replays the
    // original's player path to the unit).
    const int padOffset = getenv("DIORAMA_PAD_OFFSET") ? atoi(getenv("DIORAMA_PAD_OFFSET")) : 1;
    // Research: move the protagonist during the run (DIORAMA_TELEPORT=frame:x,y,z,yaw).
    int teleportFrame = -1;
    captain_toad::RecoveredVec::V3 teleportPos;
    float teleportYaw = 0;
    if (const char* t = getenv("DIORAMA_TELEPORT")) {
        if (sscanf(t, "%d:%f,%f,%f,%f", &teleportFrame, &teleportPos.x, &teleportPos.y, &teleportPos.z, &teleportYaw) !=
            5) {
            teleportFrame = -1;
        }
    }
    // Research: the course's floors (DIORAMA_HEIGHTMAP=step[:frame]): for each grid point a
    // vertical ray through the collision at that frame; every upward face it crosses, top first
    // (route planning).
    auto floorMap = [&r](float step) {
        for (float z = -1200; z <= 1200; z += step) {
            for (float x = -1200; x <= 1200; x += step) {
                std::vector<captain_toad::RecoveredCollision::HitInfo> hits;
                captain_toad::RecoveredCollision::segmentCheck(r.player.collider.parts,
                                                               captain_toad::RecoveredVec::V3{x, 3000, z},
                                                               captain_toad::RecoveredVec::V3{0, -6000, 0}, hits);
                std::sort(hits.begin(), hits.end(), [](const auto& a, const auto& b) { return a.depth < b.depth; });
                std::string line;
                float last = 1e9f;
                for (const auto& h : hits) {
                    if (h.faceNormal.y > 0.7f && std::fabs(h.contact.y - last) > 1.0f) {
                        last = h.contact.y;
                        line += " " + std::to_string(int(std::lround(h.contact.y)));
                    }
                }
                if (!line.empty()) {
                    fprintf(stderr, "floor %5.0f %5.0f:%s\n", x, z, line.c_str());
                }
            }
        }
    };
    float floorStep = 0;
    int floorFrame = 0;
    if (const char* hm = getenv("DIORAMA_HEIGHTMAP")) {
        floorStep = std::max(10.0f, float(atof(hm)));
        if (const char* at = strchr(hm, ':')) {
            floorFrame = atoi(at + 1);
        }
        if (floorFrame <= 0) {
            floorMap(floorStep);
        }
    }
    for (int fr = 1; fr <= frames; fr++) {
        r.stageWorld.researchFrame = fr;
        if (floorStep > 0 && fr == floorFrame) {
            floorMap(floorStep);
        }
        if (fr == teleportFrame) {
            const float ry = teleportYaw * float(M_PI / 180.0);
            r.player.place(teleportPos, captain_toad::RecoveredVec::V3{std::sin(ry), 0, std::cos(ry)});
        }
        float sx = 0, sy = 0;
        captain_toad::RecoveredCamera::CameraPad c;
        bool touch = false;
        float pointer[2] = {0, 0};
        for (auto& l : lines) {
            if (fr >= l.start && fr < l.end) {
                if (l.device == 6 && l.id <= 1) {
                    pointer[l.id] = (float(l.value) + 32767.f) / 65534.f * (l.id == 0 ? 400.f : 480.f);
                }
                if (l.device == 6 && l.id == 2 && l.value) {
                    touch = true;
                }
                if (l.device == 5 && l.index == 0 && l.id == 0) {
                    sx = l.value / 32767.f;
                }
                if (l.device == 5 && l.index == 0 && l.id == 1) {
                    sy = -l.value / 32767.f;
                }
                if (l.device == 5 && l.index == 1 && l.id == 0) {
                    c.cstick[0] = l.value / 32767.f;
                }
                if (l.device == 5 && l.index == 1 && l.id == 1) {
                    c.cstick[1] = -l.value / 32767.f;
                }
                if (l.device == 1 && l.value && l.id >= 0 && l.id < 16) {
                    c.held |= joyBits[l.id];
                }
            }
        }
        if (c.cstick[1] > 0.5f) {
            c.held |= 0x1000000;
        }
        if (c.cstick[1] < -0.5f) {
            c.held |= 0x2000000;
        }
        if (c.cstick[0] < -0.5f) {
            c.held |= 0x4000000;
        }
        if (c.cstick[0] > 0.5f) {
            c.held |= 0x8000000;
        }
        if (r.padTrace.active) {
            r.padCall = size_t(fr + padOffset);
            c.held = r.padTrace.get<uint32_t>(r.padCall, 0x110);   // native frame fr = reference call fr+1
            c.cstick[0] = r.padTrace.get<float>(r.padCall, 0x124);
            c.cstick[1] = r.padTrace.get<float>(r.padCall, 0x128);
        }
        // The touch screen shows the top screen's middle at the same scale: the pointer's frame x
        // is the top screen's x, its y less 240 the top screen's y.
        r.touchHeld = touch && pointer[1] >= 240.f;
        r.touchView[0] = pointer[0] / 400.f;
        r.touchView[1] = (pointer[1] - 240.f) / 240.f;
        r.viewAspect = 5.0f / 3.0f;
        if (r.touchHeld) {
            c.held |= captain_toad::RecoveredCamera::kTouch;
            c.touch[0] = pointer[0] - 40.f;
            c.touch[1] = pointer[1] - 240.f;
        }
        const float touchPos[2] = {c.touch[0], c.touch[1]};
        r.cameraDragFrame(1.0 / 60, r.touchHeld, touchPos);   // the smooth drag (when switched on)
        r.presentedFrame(c, sx, sy);
        r.present.alpha = float(std::clamp(r.present.sinceTick * 30.0, 0.0, 1.0));
        if (!(r.introEnabled && r.intro.active())) {
            r.animationFrame += 1;
        }
        // Research sound render: DSP frames in step with the frames (547 output samples per
        // 59.83 Hz frame), with the middleware's command latency (DIORAMA_SOUND_LATENCY DSP frames).
        if (soundOut && r.snd) {
            const long target = long(double(fr) * 547.0 / 160.0) + soundLatency;
            while (soundFramesOut < target) {
                soundBuf.resize(soundBuf.size() + captain_toad::audio::kFrameSamples * 2);
                r.snd->render(soundBuf.data() + soundBuf.size() - captain_toad::audio::kFrameSamples * 2, 1);
                soundFramesOut++;
            }
        }
        if (!finishedLogged && r.openingFinished()) {   // where the shell returns to the launcher
            finishedLogged = true;
            fprintf(stderr, "opening finished at frame %d\n", fr);
        }
        const auto& game = r.game;
        const auto& player = r.player;
        if (fr % 2 == 0) {
            fprintf(log,
                    "%d\t%s\t%s\t%.4f\t%.4f\t%.4f\t%.4f\t%.5f\t%.5f\t%.5f\t%.5f\t%.5f\t%.3f\t%.3f\t%.3f\t%.3f\t%."
                    "3f\t%.3f\t%.4f\n",
                    fr, player.machine.current->name.c_str(), player.anim.action.c_str(), player.anim.frame,
                    player.body.pos.x, player.body.pos.y, player.body.pos.z, player.body.front.x, player.body.front.z,
                    player.body.vel.x, player.body.vel.y, player.body.vel.z, game.director.view.eye.x,
                    game.director.view.eye.y, game.director.view.eye.z, game.director.view.target.x,
                    game.director.view.target.y, game.director.view.target.z, game.fovy());
        }
        if (fr % 2 == 0 && getenv("DIORAMA_DEBUG") && game.goal.active() && game.goal.k % 10 == 0) {
            fprintf(stderr, "fr %d goal k %d player %s %.0f companion %s %.0f star %.0f vis %d demo %d\n", fr,
                    game.goal.k, game.goal.player.name.c_str(), game.goal.player.frame(),
                    game.goal.companion.name.c_str(), game.goal.companion.frame(), game.goal.star.frame(),
                    game.goal.starVisible, game.goal.demoStarVisible);
        }
        if (fr % 60 == 0 && getenv("DIORAMA_DEBUG")) {
            fprintf(stderr, "fr %d companion %d state %d target %d pos %.1f %.1f %.1f\n", fr, game.companion.enabled,
                    int(game.companion.c.state), game.companion.c.target, game.companion.c.pose.pos.x,
                    game.companion.c.pose.pos.y, game.companion.c.pose.pos.z);
        }
        // Research (profiling): every frame drawn at a headset eye's size (DIORAMA_PLAY_RENDER_ALL),
        // its CPU time summed and reported at the end.
        static const bool renderAll = getenv("DIORAMA_PLAY_RENDER_ALL") != nullptr;
        if (renderAll) {
            static std::unique_ptr<captain_toad::gpu::Target> eye =
                captain_toad::gpu::device().createTarget(1344, 1408);
            captain_toad::gpu::device().bindTarget(eye.get());
            const auto begin = std::chrono::steady_clock::now();
            r.render(0, 0, 1344, 1408);
            captain_toad::gpu::device().present();
            renderSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
            renderFrames++;
        }
        if (saveList.empty() ? fr % 30 == 0 : saveList.count(fr) > 0) {
            captain_toad::gpu::device().bindTarget(gameTarget);
            // Research: the saved frames from a free close-up camera instead of the game's
            // (DIORAMA_PLAY_VIEW=x,y,z,distance,azimuth,elevation).
            double view[6];
            const char* pv = getenv("DIORAMA_PLAY_VIEW");
            const bool closeUp = pv && sscanf(pv, "%lf,%lf,%lf,%lf,%lf,%lf", &view[0], &view[1], &view[2], &view[3],
                                              &view[4], &view[5]) == 6;
            if (closeUp) {
                r.gameCamera = false;
                r.camera.x = view[0];
                r.camera.y = view[1];
                r.camera.z = view[2];
                r.camera.distance = view[3];
                r.camera.azimuth = view[4];
                r.camera.elevation = view[5];
            }
            r.render(0, 0, 400, 240);
            if (closeUp) {
                r.gameCamera = r.game.enabled;
            }
            char name[32];
            snprintf(name, sizeof name, "frame-%05d.png", fr);
            save(out / name, captain_toad::platform::readFramebuffer(400, 240));
        }
    }
    if (renderFrames) {
        fprintf(stderr, "render: %d frames, %.2f ms per frame (CPU)\n", renderFrames,
                renderSeconds * 1000 / renderFrames);
    }
    if (soundOut) {
        fwrite(soundBuf.data(), 2, soundBuf.size(), soundOut);
        fclose(soundOut);
    }
    fclose(log);
    printf("Played %d frames: %s\n", frames, r.player.status().c_str());
    if (r.stageObjects) {
        printf("Course counters: coins %d, collect items %d\n", r.stageWorld.counters.coins,
               r.stageWorld.counters.collectItems);
    }
    if (r.stageGoal) {
        printf("Course %s\n", r.stageGoalReached ? "cleared (star reached)"
                              : r.stageMissed    ? "missed"
                                                 : "not finished");
    }
}

void Session::capture(const std::filesystem::path& package, const std::filesystem::path& out, const ImageWriter& save) {
    Session s;
    s.impl_ = std::make_unique<Impl>();
    Impl& r = *s.impl_;
    r.scenePath = package;
    r.previewFrameFixed = true;
    r.loadScene();
    offscreenTarget(1000, 600);
    auto shot = [&](const std::string& name) {
        r.render(0, 0, 1000, 600);
        save(out / name, captain_toad::platform::readFramebuffer(1000, 600));
    };
    for (int i = 1; i <= 3; i++) {
        r.preset(i);
        shot("view-" + std::to_string(i) + ".png");
    }
    // Research: an extra close-up (DIORAMA_CAPTURE_VIEW=x,y,z,distance,azimuth,elevation).
    if (const char* v = getenv("DIORAMA_CAPTURE_VIEW")) {
        double c[6];
        if (sscanf(v, "%lf,%lf,%lf,%lf,%lf,%lf", &c[0], &c[1], &c[2], &c[3], &c[4], &c[5]) == 6) {
            r.preset(1);
            r.camera.x = c[0];
            r.camera.y = c[1];
            r.camera.z = c[2];
            r.camera.distance = c[3];
            r.camera.azimuth = c[4];
            r.camera.elevation = c[5];
            shot("view-custom.png");
        }
    }
    r.preset(1);
    r.shadows = false;
    shot("unshadowed.png");
    r.shadows = true;
    r.shadowMask = 1;
    shot("shadow-mask.png");
    r.shadowMask = 0;
    r.setShadowQuality(false);
    shot("shadow-original.png");
    r.setShadowQuality(true);
    r.preset(1);
    r.showCollision = true;
    shot("collision.png");
    r.showCollision = false;
    r.camera = r.openingCamera;
    r.camera.x = -400;
    r.camera.y = -730;
    r.camera.z = 550;
    r.camera.distance = 650;
    r.camera.azimuth = 55;
    r.camera.elevation = 10;
    for (int frame : {0, 45, 65, 90, 135, 180}) {
        r.animationFrame = frame;
        char name[32];
        snprintf(name, sizeof name, "character-%03d.png", frame);
        shot(name);
    }
    printf("Captured 3 native views, shadow comparisons and collision overlay; %zu triangles.\n",
           r.vertices.size() / 3);
}

}   // namespace captain_toad::session
