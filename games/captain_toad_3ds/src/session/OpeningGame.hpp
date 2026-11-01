// =============================================================================================
// OpeningGame.hpp - the playable opening: camera, player, companion and goal in one tick
// =============================================================================================
//
// The opening session runs the recovered game camera (Camera.hpp) together with the recovered
// player (PlayerRuntime.hpp), the companion's rail walk (Companion.hpp) and the goal sequence
// (Goal.hpp), in the original update order.
//
// Per 30 Hz tick (execution order traced in evidence/runs: 0x42aaec camera director, then the
// player 0x15c5e4):
//   1. the camera director ticks on the player's pose of the previous tick;
//   2. the player ticks; its pad (0x2d7edc) reads this tick's view, inverted (0x2f2074), so
//      "up" on the stick always means "away from the camera";
//   3. the NPCs tick (the companion).
//
// This file is platform glue only: the scene package supplies the camera ticket data
// (tools/export_opening.py) and the platform supplies the controller state in the game's bit
// layout (captain_toad::RecoveredCamera::CameraPad).
//
// Approximation (documented): the camera target's base matrix (used only by the snap-behind) is
// the body orientation, not the pose's Euler round trip 0x2e7184 -> 0x2efcd0.
// =============================================================================================
#pragma once
#include <cmath>
#include <cstring>
#include <map>
#include "game/Camera.hpp"
#include "game/Companion.hpp"
#include "game/Goal.hpp"
#include "game/StageStart.hpp"
#include "game/Push.hpp"
#include "data/Package.hpp"

// The companion's rail guide with its clip timing: a new clip starts at frame 0 without
// advancing, then advances 2 frames per tick; looping clips wrap once, one-shot clips stop at the
// end (observed in companion-anim-b).
struct CompanionRuntime {
    bool enabled = false;
    captain_toad::RecoveredCompanion::Controller c;
    std::map<std::string, std::pair<float, bool>> clips;   // clip -> (length, loop)
    float frame = 0;
    int seenStart = 0;

    void tickAnimation() {
        auto it = clips.find(c.clip);
        const float len = it != clips.end() ? it->second.first : 1.0f;
        const bool loop = it != clips.end() && it->second.second;
        if (c.clipStarted != seenStart) {
            seenStart = c.clipStarted;
            frame = 0;
        } else {
            frame = frame + 2.0f;
            if (frame >= len) {
                frame = loop ? frame - len : len;
            }
        }
        c.clipEnded = !loop && frame >= len;
    }
};

struct OpeningGame {
    bool enabled = false;
    captain_toad::RecoveredCamera::CameraGlobals globals;
    captain_toad::RecoveredCamera::StartCamera startCam;
    captain_toad::RecoveredCamera::PlayerCamera areaCam;
    // Stage packages: one player camera per camera area (each ticket's own camera, set up from its
    // ticket at load by 0x20b184); the director switches between them (Director::tick).
    std::vector<captain_toad::RecoveredCamera::PlayerCamera> areaCams;
    captain_toad::RecoveredCamera::Director director;
    captain_toad::RecoveredCamera::CameraTarget target;
    captain_toad::RecoveredCamera::CameraSettings settings;
    captain_toad::RecoveredVec::M34 view{{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0}};
    std::vector<std::string> events;
    float nearClip = 100;
    float farClip = 115000;
    long long ticks = 0;
    CompanionRuntime companion;

    // The companion's Body sensor (the companion NPC's InitSensor: joint JointRoot, Y 30, radius
    // 90): the JointRoot channels per clip and the bind values, and the sensor centre of her
    // last tick.
    std::map<std::string, std::array<AnimationChannel, 9>> companionRoot;
    float rootDefaults[9] = {1, 1, 1, 0, 0, 0, 0, 0, 0};
    captain_toad::RecoveredVec::V3 companionSensor{};
    bool companionSensorValid = false;

    // Clip cross-fade of her JointRoot (AnimInterp): weight = 2 * ticks since the switch / (n - 1),
    // from the pose at the switch (fitted to the traced sensor at the Wait -> Walk change; that it
    // is the same model as the player's fade is a hypothesis).
    std::map<std::string, int> companionInterp;
    std::string sensorClip;
    float fadeFrom[9] = {};
    float lastLocal[9] = {};
    int fadeN = 0;
    int fadeAge = 0;
    captain_toad::RecoveredVec::V3 prevHead{};   // the player's Head sensor of the previous tick
    bool havePrevHead = false;

    captain_toad::RecoveredGoal::Sequence goal;
    bool goalEnabled = false;

    // After-goal demo camera (DemoCamera.bch DemoOpeningSeason1): curves relative to the goal object.
    AnimationChannel demoPos[3];
    AnimationChannel demoTarget[3];
    AnimationChannel demoFovy;

    // Camera areas of stage packages (CameraArea boxes with their player-camera tickets).
    struct Area {
        std::string id;
        int priority = -1;
        captain_toad::RecoveredVec::V3 translate, scale;
        captain_toad::RecoveredCamera::PlayerCamera::Ticket ticket;
        bool contains(const captain_toad::RecoveredVec::V3& p) const {   // AreaCubeBase: x / z centred, y from the base
            return std::fabs(p.x - translate.x) <= 500 * scale.x && p.y >= translate.y &&
                   p.y <= translate.y + 1000 * scale.y && std::fabs(p.z - translate.z) <= 500 * scale.z;
        }
    };
    std::vector<Area> areas;
    int currentArea = -1;

    static captain_toad::RecoveredCamera::PlayerCamera::Ticket readTicket(captain_toad::data::JsonView e) {
        captain_toad::RecoveredCamera::PlayerCamera::Ticket t;
        t.hasLookAt = true;
        t.lookAtPos = v3(e["look_at"]);
        t.distance = e["distance"].f();
        t.distX = e["distance_xyz"][0].f();
        t.distY = e["distance_xyz"][1].f();
        t.distZ = e["distance_xyz"][2].f();
        t.distanceDetail = e["distance_detail"].b();
        t.hasAngleV = true;
        t.vMin = e["angle_v"][0].f();
        t.vMax = e["angle_v"][1].f();
        t.hasAngleH = true;
        t.hMin = e["angle_h"][0].f();
        t.hMax = e["angle_h"][1].f();
        t.hasAngleStart = true;
        t.startV = e["angle_start"][0].f();
        t.startH = e["angle_start"][1].f();
        t.interpoleStep = e["interpole_step"].i();
        t.resetForce = e["reset_force"].b();
        t.zoomFovy = e["zoom_fovy"][0].f();
        t.zoomFovyMedium = e["zoom_fovy"][1].f();
        t.zoomFovyFar = e["zoom_fovy"][2].f();
        for (int i = 0; i < 3; i++) {
            t.follow[i] = e["follow"][i].b();
            t.useMin[i] = e["use_min"][i].b();
            t.useMax[i] = e["use_max"][i].b();
        }
        t.followOffset = v3(e["follow_offset"]);
        t.upsideDown = e["upside_down"].b();
        t.limitMin = v3(e["limit_min"]);
        t.limitMax = v3(e["limit_max"]);
        if (e["init_zoom_mode"]) {   // InitZoomMode (0 far, 2 medium; TouchPuzzleMiniStage: 0)
            t.initZoomMode = e["init_zoom_mode"].i();
        }
        return t;
    }

    // The highest-priority camera area containing the player (the area manager 0x252888); entering
    // another area requests its camera, which the director switches to with the camera's own
    // InterpoleStep.
    void selectArea(const captain_toad::RecoveredVec::V3& p) {
        int best = -1;
        for (size_t i = 0; i < areas.size(); i++) {
            // 0x252888: the highest Priority among the areas containing the point; on a tie the
            // later area wins (best.priority <= priority).
            if (areas[i].contains(p) && (best < 0 || areas[size_t(best)].priority <= areas[i].priority)) {
                best = int(i);
            }
        }
        // With no area 0x252888 clears only its own record (+0x10), not the director's slot, so
        // the camera stays; on re-entry it requests the same camera again, which the director
        // ignores (0x24dba4 switches only when the slot's entry differs from its current one,
        // +0x154). Keeping the last area is the same. (Not ported: the area's flag +0x48, which must
        // be set for an area to count; not identified.)
        if (best >= 0 && best != currentArea) {
            if (getenv("DIORAMA_CAMERA_DEBUG")) {
                fprintf(stderr, "camera area %s -> %s at (%.0f, %.0f, %.0f)\n",
                        currentArea >= 0 ? areas[size_t(currentArea)].id.c_str() : "-", areas[size_t(best)].id.c_str(),
                        p.x, p.y, p.z);
            }
            currentArea = best;
            // The area camera manager (0x252888, table built by 0x2525f0) requests the chosen
            // area's camera when it differs from the current one (areas with an equal camera are
            // linked at load and share it): 0x2f6bd8 -> 0x297d94 with steps -1. The director
            // (0x24dba4) starts it, hands the old camera's state over and eases over the new
            // camera's own InterpoleStep.
            director.areaCam = &areaCams[size_t(best)];
        }
    }

    // Stage-start camera pose, kept from begin() for the end of the intro.
    captain_toad::RecoveredCamera::Pose startView;

    static captain_toad::RecoveredVec::V3 v3(captain_toad::data::JsonView a) { return {a[0].f(), a[1].f(), a[2].f()}; }

    // ---------------------------------------------------------------------------------------------
    // Loading
    // ---------------------------------------------------------------------------------------------

    // The two camera tickets of the opening: the Start camera (the view as the stage begins) and
    // the area's player camera (the camera the player controls).
    void load(captain_toad::data::JsonView gc, const captain_toad::RecoveredVec::V3& spawn) {
        using namespace captain_toad::RecoveredCamera;
        captain_toad::data::JsonView s = gc["start"];
        startCam.angleV = s["angle_v"].f();
        startCam.angleH = s["angle_h"].f();
        startCam.distance = s["distance"].f();
        startCam.lookAtPos = v3(s["look_at"]);
        startCam.lookAtOffset = v3(s["look_at_offset"]);
        startCam.isSetLookAt = !s["set_look_at"] || s["set_look_at"].b();
        startCam.enabled = true;
        startCam.fovy = s["fovy"].f();
        startCam.interpoleStep = s["interpole_step"].i();
        startCam.flag90 = false;
        startCam.startTargetPos = spawn;

        captain_toad::data::JsonView a = gc["area"];
        PlayerCamera::Ticket t;
        t.hasLookAt = true;
        t.lookAtPos = v3(a["look_at"]);
        t.distance = a["distance"].f();
        t.distX = a["distance_xyz"][0].f();
        t.distY = a["distance_xyz"][1].f();
        t.distZ = a["distance_xyz"][2].f();
        t.hasAngleV = true;
        t.vMin = a["angle_v"][0].f();
        t.vMax = a["angle_v"][1].f();
        t.hasAngleStart = true;
        t.startV = a["angle_start"][0].f();
        t.startH = a["angle_start"][1].f();
        t.interpoleStep = a["interpole_step"].i();
        t.resetForce = a["reset_force"].b();
        t.zoomFovy = a["zoom_fovy"][0].f();
        t.zoomFovyMedium = a["zoom_fovy"][1].f();
        t.zoomFovyFar = a["zoom_fovy"][2].f();
        t.followOffset = v3(a["follow_offset"]);
        // Stage packages: every camera area with its full ticket (follow, move limits, ...); the
        // start area's full ticket replaces the subset above.
        areas.clear();
        for (captain_toad::data::JsonView e : gc["areas"].items()) {
            Area ar;
            ar.id = e["id"].str();
            ar.priority = e["priority"].i();
            ar.translate = v3(e["translate"]);
            ar.scale = v3(e["scale"]);
            ar.ticket = readTicket(e);
            areas.push_back(ar);
        }
        currentArea = -1;
        areaCams.assign(areas.size(), captain_toad::RecoveredCamera::PlayerCamera());
        for (size_t i = 0; i < areas.size(); i++) {
            areaCams[i].applyTicket(areas[i].ticket, globals);
            if (areas[i].id == a["id"].str()) {
                t = areas[i].ticket;
                currentArea = int(i);
            }
        }
        areaCam.applyTicket(t, globals);

        nearClip = gc["projection"]["near"].f();
        farClip = gc["projection"]["far"].f();
        director.startCam = &startCam;
        director.areaCam = currentArea >= 0 ? &areaCams[size_t(currentArea)] : &areaCam;
        director.current = nullptr;
        enabled = true;
    }

    // Goal sequence parameters (scene.json goal, tools/export_opening.py) and clip lengths.
    void loadGoal(captain_toad::data::JsonView g, const std::map<std::string, float>& lengths) {
        if (!g) {
            return;
        }
        auto& p = goal.p;
        p.position = v3(g["position"]);
        p.starRoot = v3(g["star_root"]);
        p.starBodyOffset = g["star_body"]["offset"].f();
        p.starBodyRadius = g["star_body"]["radius"].f();
        p.headOffset = g["player_head"]["offset"].f();
        p.headRadius = g["player_head"]["radius"].f();
        p.eyeUp = g["camera"]["eye_up"].f();
        p.eyeFront = g["camera"]["eye_front"].f();
        p.targetUp = g["camera"]["target_up"].f();
        p.fovy = g["camera"]["fovy"].f();
        p.courseClearStep = g["demo_steps"]["course_clear"].i();
        p.endStep = g["demo_steps"]["end"].i();
        p.pose = g["clips"]["pair"].str();
        p.signPose = g["clips"]["sign"].str();
        if (g["clear_after_demo"]) {
            p.clearAfterDemo = g["clear_after_demo"].b();
        }
        if (g["pair"]) {
            p.pair = g["pair"].b();
        }
        if (g["rotate_y"]) {
            p.rotateY = g["rotate_y"].f();
        }
        if (g["clips"]["star_sign"].isString()) {
            p.starSign = g["clips"]["star_sign"].str();
        }
        if (g["clips"]["mini"].isString()) {
            p.miniPose = g["clips"]["mini"].str();
        }
        goal.clipLength = lengths;
        goalEnabled = true;
    }

    void loadDemoCamera(captain_toad::data::JsonView c) {
        if (!c) {
            return;
        }
        for (int i = 0; i < 3; i++) {
            demoPos[i] = readChannel(c["position"][i]);
            demoTarget[i] = readChannel(c["target"][i]);
        }
        demoFovy = readChannel(c["fovy"]);
    }

    // The companion (actors/companion.json rail_guide) and her clip lengths.
    void loadCompanion(captain_toad::data::JsonView actor) {
        captain_toad::data::JsonView g = actor["rail_guide"];
        if (!g) {
            return;
        }
        std::vector<captain_toad::RecoveredRail::RailPoint> points;
        for (captain_toad::data::JsonView p : (g["points"]).items()) {
            points.push_back({v3(p["pos"]), v3(p["handle_in"]), v3(p["handle_out"])});
        }
        auto& c = companion.c;
        c.rail.build(points, g["closed"].b());
        c.mover.rail = &c.rail;
        c.mover.forward = true;
        c.mover.place(0.0f);
        for (auto& p : points) {
            c.points.push_back(p.pos);
        }
        c.waitTicks = g["wait_ticks"].i();
        // Controller +0x1c: her placement front, the direction she turns to at the rail's end (End
        // 0x427978): the front column (0x3f4474) of the placement rotation about +Y (map Rotate Y)
        // as a quaternion; traced 0xbf6ba634, 0x80000000, 0x3ec80dea for -67 (companion-end-c596).
        {
            const float h = g["map_rotate_y"].f() * captain_toad::RecoveredCompanion::kD2R * 0.5f;
            captain_toad::RecoveredCompanion::Pose placement;
            placement.q[0] = 0.0f;
            placement.q[1] = captain_toad::RecoveredMath::sinf(h);
            placement.q[2] = 0.0f;
            placement.q[3] = captain_toad::RecoveredMath::cosf(h);
            const captain_toad::RecoveredVec::M34 m = placement.matrix();
            c.endDir = {m.m[2], m.m[6], m.m[10]};
        }
        for (const std::string& n : actor["curve_clips"].keys()) {
            companion.clips[n] = {actor["curve_clips"][n]["frames"].f(), actor["curve_clips"][n]["loop"].b()};
        }
        for (const std::string& k : actor["anim_interp"].keys()) {
            companionInterp[k] = actor["anim_interp"][k].i();
        }
        for (captain_toad::data::JsonView b : (actor["bones"]).items()) {
            if (!b["name"].is("JointRoot")) {
                continue;
            }
            for (int i = 0; i < 3; i++) {
                rootDefaults[i] = b["scale"][i].f();
                rootDefaults[3 + i] = b["rotation"][i].f();
                rootDefaults[6 + i] = b["translation"][i].f();
            }
        }
        for (const std::string& n : actor["curve_clips"].keys()) {
            companionRoot[n] = readBoneChannels(actor["curve_clips"][n]["tracks"]["JointRoot"]);
        }
        companion.enabled = true;
    }

    // ---------------------------------------------------------------------------------------------
    // The companion
    // ---------------------------------------------------------------------------------------------

    // Controller start (0x1c7e14): at the rail start, facing the player.
    void startCompanion(const captain_toad::RecoveredVec::V3& playerPos) {
        if (!companion.enabled) {
            return;
        }
        auto& c = companion.c;
        captain_toad::RecoveredVec::V3 d = captain_toad::RecoveredVec::sub(playerPos, c.points[0]);
        captain_toad::RecoveredVec::normalize(d);
        c.pose.setFront(d);
        c.pose.pos = c.mover.pos;
        c.setState(captain_toad::RecoveredCompanion::Controller::Wait);
        c.startClip("Wait");
        // Reference checkpoint state (traced, 335c1370+0x10: Wait frame 120 at call 0, 122 after
        // the first tick; the clip advances before the frame is read); started during the stage
        // intro. The controller state too: Wait step 61 (traced 33770870+0xc), so the clip is not
        // restarted.
        c.step = 61;
        companion.seenStart = c.clipStarted;
        companion.frame = 120.0f;
    }

    // NPC part of one 30 Hz tick (after the player tick).
    void npcTick(const captain_toad::RecoveredVec::V3& playerPos) {
        if (!companion.enabled) {
            return;
        }
        if (!goal.active() || goal.companionMoves()) {
            companion.c.tick(playerPos);
            companion.tickAnimation();
        }
        updateCompanionSensor();
        if (goal.companion.running()) {
            // Goal poses (Goal.hpp): the goal sequence takes over her clip and position.
            companion.c.clip = goal.companion.name;
            companion.frame = goal.companion.frame();
            if (goal.companionPlaced) {
                companion.c.pose.pos = goal.p.position;
                float q[4] = {0, 0, 0, 1};
                memcpy(companion.c.pose.q, q, sizeof q);
            }
        }
    }

    // The companion's Body sensor centre after her tick: (JointRoot world) * (0, 30, 0).
    void updateCompanionSensor() {
        auto it = companionRoot.find(companion.c.clip);
        if (it == companionRoot.end()) {
            companionSensorValid = false;
            return;
        }
        float v[9];
        for (int k = 0; k < 9; k++) {
            v[k] = it->second[k].evaluate(companion.frame, rootDefaults[k]);
        }
        if (companion.c.clip != sensorClip) {
            if (!sensorClip.empty()) {
                memcpy(fadeFrom, lastLocal, sizeof fadeFrom);
                auto f = companionInterp.find(companion.c.clip);
                fadeN = f != companionInterp.end()
                            ? f->second
                            : (companionInterp.count("Default") ? companionInterp["Default"] : 3);
                fadeAge = 0;
            }
            sensorClip = companion.c.clip;
        } else {
            ++fadeAge;
        }
        if (fadeN > 1) {
            const float w = std::min(1.0f, 2.0f * float(fadeAge) / float(fadeN - 1));
            for (int k = 0; k < 9; k++) {
                v[k] = fadeFrom[k] + (v[k] - fadeFrom[k]) * w;
            }
            if (w >= 1.0f) {
                fadeN = 0;
            }
        }
        memcpy(lastLocal, v, sizeof lastLocal);
        Affine local = Affine::trs(v, v + 3, v + 6);   // the parent AllRoot is identity in every clip
        const float off[3] = {0, 30, 0};
        auto jl = local.apply(off, true);
        const auto m = companion.c.pose.matrix();
        captain_toad::RecoveredVec::V3 p{jl[0], jl[1], jl[2]};
        companionSensor = captain_toad::RecoveredVec::mulPoint(m, p);
        companionSensorValid = true;
    }

    // The companion pushes the player away when they bump into each other: the player's Head
    // sensor (r55, +125 along up) is pushed by the companion's Body sensor (r90), message handler
    // 0x1574dc -> 0x2db770 (max 20, no velocity cut); traced on push-exec-a (calls 247, 249).
    // Uses the companion's sensor from her previous tick (the player ticks first). Returns
    // whether there is a push, and the push vector.
    bool companionPush(const captain_toad::RecoveredVec::V3& pos, const captain_toad::RecoveredVec::V3& up,
                       const captain_toad::RecoveredVec::V3& gravity, captain_toad::RecoveredVec::V3& push) {
        using namespace captain_toad::RecoveredVec;
        if (!companion.enabled || !companionSensorValid) {
            return false;
        }
        const V3 head = add(pos, scale(up, 125.0f));
        const bool hit = havePrevHead && captain_toad::RecoveredPush::overlaps(prevHead, 55.0f, companionSensor, 90.0f);
        prevHead = head;
        havePrevHead = true;
        return hit && captain_toad::RecoveredPush::push(head, 55.0f, companionSensor, 90.0f, gravity, 20.0f, push);
    }

    // ---------------------------------------------------------------------------------------------
    // Cameras
    // ---------------------------------------------------------------------------------------------

    // After-goal demo camera at the sequence's camera frame (eye / target = goal + curves, up +Y).
    void demoCamera() {
        const float f = goal.demoCameraFrame();
        const auto& g = goal.p.position;
        captain_toad::RecoveredVec::V3 eye{g.x + demoPos[0].evaluate(f, 0), g.y + demoPos[1].evaluate(f, 0),
                                           g.z + demoPos[2].evaluate(f, 0)};
        captain_toad::RecoveredVec::V3 target{g.x + demoTarget[0].evaluate(f, 0), g.y + demoTarget[1].evaluate(f, 0),
                                              g.z + demoTarget[2].evaluate(f, 0)};
        director.view.eye = eye;
        director.view.target = target;
        director.view.up = {0, 1, 0};
        director.view.fovy = demoFovy.evaluate(f, 0.5236f) * float(180.0 / M_PI);
        captain_toad::RecoveredCamera::lookAt(view, eye, target, director.view.up);
        ++ticks;
    }

    // Stage-start demo camera (StageStart.hpp).
    void introCamera(const captain_toad::RecoveredStageStart::Demo& intro) {
        director.view.eye = intro.eye;
        director.view.target = intro.target;
        director.view.up = {0, 1, 0};
        director.view.fovy = intro.fovy;
        captain_toad::RecoveredCamera::lookAt(view, intro.eye, intro.target, director.view.up);
    }

    void endIntro() {
        director.view = startView;
        captain_toad::RecoveredCamera::lookAt(view, startView.eye, startView.target, startView.up);
    }

    // Fixed goal demo camera (goal object 0x1ec824 offsets; up +Y).
    void goalCamera() {
        captain_toad::RecoveredVec::V3 eye, target;
        goal.cameraPose(eye, target);
        director.view.eye = eye;
        director.view.target = target;
        director.view.up = {0, 1, 0};
        director.view.fovy = goal.p.fovy;
        captain_toad::RecoveredCamera::lookAt(view, eye, target, director.view.up);
        ++ticks;
    }

    // Stage start: the Start ticket camera becomes current (after the stage-start demo).
    void begin(const captain_toad::RecoveredVec::V3& playerPos) {
        captain_toad::RecoveredCamera::Context c = context({});
        target.pos = playerPos;
        startCam.start(c);
        director.current = &startCam;
        startCam.calc(c);
        director.view = startCam.pose();
        director.fovy = startCam.fovy;
        director.interp.lastEye = director.view.eye;
        director.interp.lastTarget = director.view.target;
        director.interp.lastUp = director.view.up;
        startView = director.view;
        captain_toad::RecoveredCamera::lookAt(view, director.view.eye, director.view.target, director.view.up);
    }

    captain_toad::RecoveredCamera::Context context(const captain_toad::RecoveredCamera::CameraPad& pad) {
        captain_toad::RecoveredCamera::Context c;
        c.pad = pad;
        c.settings = settings;
        c.globals = &globals;
        c.target = &target;
        c.events = &events;
        return c;
    }

    // Camera part of one 30 Hz tick. pos / front / up: the player's pose of the previous tick.
    void cameraTick(const captain_toad::RecoveredCamera::CameraPad& pad, const captain_toad::RecoveredVec::V3& pos,
                    const captain_toad::RecoveredVec::V3& front, const captain_toad::RecoveredVec::V3& up) {
        if (!enabled) {
            return;
        }
        selectArea(pos);
        using namespace captain_toad::RecoveredVec;
        target.pos = pos;
        V3 side = cross(up, front);
        normalize(side);
        V3 u = cross(front, side);
        normalize(u);
        target.baseMtx = {{side.x, u.x, front.x, pos.x, side.y, u.y, front.y, pos.y, side.z, u.z, front.z, pos.z}};
        director.tick(context(pad));
        captain_toad::RecoveredCamera::lookAt(view, director.view.eye, director.view.target, director.view.up);
        ++ticks;
    }

    // The camera's world matrix, for the player's stick-to-world conversion.
    void padCamera(float cam[12]) const { captain_toad::RecoveredCamera::cameraToWorld(view, cam); }

    float fovy() const { return director.view.fovy; }
    // Distance from the eye to the camera's target (where stereo views put zero parallax).
    float targetDistance() const {
        const auto& v = director.view;
        const float dx = v.target.x - v.eye.x, dy = v.target.y - v.eye.y, dz = v.target.z - v.eye.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }
};
