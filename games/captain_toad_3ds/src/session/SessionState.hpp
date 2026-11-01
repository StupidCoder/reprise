// =============================================================================================
// SessionState.hpp - the state of a session (private to session/*.cpp)
// =============================================================================================
//
// Everything a running opening holds: the loaded scene (geometry, textures, materials), the
// recovered game objects, the effect and sound systems, and the inspection settings. The
// member functions are implemented in
//   Session.cpp         loading, effect owners, the game loop, effects and sound per tick;
//   SessionRender.cpp   posing the animated models and rendering a frame;
//   SessionResearch.cpp the headless research modes;
//   SessionShell.cpp    the interactive clock, input, inspection camera and audio output.
// =============================================================================================
#pragma once
#include "render/Bloom.hpp"
#include "session/Session.hpp"

#include "data/Package.hpp"
#include "render/SceneTypes.hpp"
#include "render/ShadowMap.hpp"
#include "gpu/Device.hpp"
#include "gpu/Matrix.hpp"
#include "render/CharacterAnimation.hpp"
#include "session/PlayerRuntime.hpp"
#include "session/OpeningGame.hpp"
#include "session/WalkPlanner.hpp"
#include "session/Presentation.hpp"
#include "render/LightService.hpp"
#include "render/MaterialAnimation.hpp"
#include "game/GrassWind.hpp"
#include "game/Bird.hpp"
#include "game/StageItems.hpp"
#include "game/StagePullOut.hpp"
#include "game/StageRadish.hpp"
#include "game/StageSwitches.hpp"
#include "game/StageEnemies.hpp"
#include "game/StageSign.hpp"
#include "game/StageSwitchParts.hpp"
#include "game/StageKyoroHei.hpp"
#include "game/StageMoking.hpp"
#include "game/StageAssist.hpp"
#include "game/StageGuide.hpp"
#include "game/PlayerWater.hpp"
#include "game/StageBlocks.hpp"
#include "game/StageDecor.hpp"
#include "game/StageLadder.hpp"
#include "game/StageBirds.hpp"
#include "game/StageNpc.hpp"
#include "render/LayoutRenderer.hpp"
#include "render/DropShadow.hpp"
#include "render/CameraMask.hpp"
#include "effects/OpeningEffects.hpp"
#include "audio/OpeningSoundKeeper.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace captain_toad::session {

// The free inspection camera (orbit around a target point).
struct Camera {
    double azimuth = -45, elevation = 15, distance = 3500, x = 125, y = -450, z = 80;
};

// A grass actor and its recovered wind controller (GrassWind.hpp).
struct GrassWindState {
    size_t actor;
    captain_toad::RecoveredGrassWind::Control control;
    long long frame = -1;
    // The control's output of the last update and of the one before (drawing between ticks).
    std::vector<std::pair<float, float>> offsets, previous;
};

// The birds share one generator, as the original shares the global xorshift128 (0x4f8064);
// tick 0 is the traced reference state (gpu-trace-opening-c), replayed deterministically. In game
// mode the birds tick with the game and flee from the player's Head sensor of the previous tick
// (captain_toad::RecoveredBird::headContact); otherwise they follow the animation clock.
struct BirdFlock {
    std::vector<std::pair<size_t, captain_toad::RecoveredBird::Bird>> birds, initial;
    captain_toad::RecoveredBird::XorShift128 rng;
    long long frame = 0;
    bool gameDriven = false, haveHead = false;
    float head[3] = {0, 0, 0};

    void reset() {
        birds = initial;
        rng = captain_toad::RecoveredBird::XorShift128{};
        frame = 0;
        haveHead = false;
    }

    void gameTick(const captain_toad::RecoveredVec::V3& pos, const captain_toad::RecoveredVec::V3& up) {
        for (auto& b : birds) {
            b.second.tick(rng);
            if (haveHead && b.second.headContact(head) && getenv("DIORAMA_DEBUG")) {
                fprintf(stderr, "bird tick %lld actor %zu flees from head %.2f %.2f %.2f\n", frame, b.first, head[0],
                        head[1], head[2]);
            }
        }
        frame++;
        head[0] = pos.x + up.x * 125.0f;
        head[1] = pos.y + up.y * 125.0f;
        head[2] = pos.z + up.z * 125.0f;
        haveHead = true;
        if (getenv("DIORAMA_DEBUG_HEAD")) {
            fprintf(stderr, "head %lld %.3f %.3f %.3f\n", frame, head[0], head[1], head[2]);
        }
    }
};

// A drop shadow under an actor joint (actor json drop_shadow).
struct ActorShadow {
    size_t actor;
    std::string joint;
    double radius, height, topOffset;
};

// Per actor: material animations of behaviour-started clips (e.g. GoalPoseSign) and the active one.
struct ActorClipMaterials {
    std::map<std::string, MaterialAnimationSet> sets;
    std::string active;
    double frame = 0;
};

// Render state of one scene material (PICA blend / depth registers 0x101 / 0x107).
struct MaterialState {
    bool blend, depthWrite, depthTest, offset;
    captain_toad::gpu::Compare depthFunc;
    captain_toad::gpu::BlendOp blendEq[2] = {captain_toad::gpu::BlendOp::Add, captain_toad::gpu::BlendOp::Add};
    captain_toad::gpu::BlendFactor blendFactor[4] = {
        captain_toad::gpu::BlendFactor::One, captain_toad::gpu::BlendFactor::Zero, captain_toad::gpu::BlendFactor::One,
        captain_toad::gpu::BlendFactor::Zero};
    std::vector<std::array<double, 2>> uvKeys;
    double uvDuration = 0;
};

// Research replay: the original's controller state per call (watch 3104ede8:130 of a reference
// run; +0x110 held, +0x11c/+0x120 circle pad after the SDK conversion, +0x124/+0x128 C-stick),
// replacing the scripted input so a play reproduces the run's pad exactly (DIORAMA_PAD_RUN).
struct PadTrace {
    bool active = false;
    std::vector<std::vector<uint8_t>> recs;
    size_t offset = 0;

    bool load(const char* run) {
        std::ifstream t(std::string(run) + "/watch.tsv");
        std::string a, l;
        size_t off = 0, rec = 0;
        bool found = false;
        while (t >> a >> l) {
            uint32_t A = std::stoul(a, nullptr, 16), L = std::stoul(l, nullptr, 16);
            if (A == 0x3104ede8 && L >= 0x12c) {
                offset = off + 4;
                found = true;
            }
            off += 4 + L;
            rec += 4 + L;
        }
        std::ifstream b(std::string(run) + "/watch.bin", std::ios::binary);
        std::vector<uint8_t> all((std::istreambuf_iterator<char>(b)), {});
        for (size_t q = 0; q + rec <= all.size(); q += rec) {
            recs.emplace_back(all.begin() + q, all.begin() + q + rec);
        }
        return active = found && !recs.empty();
    }

    template <class T>
    T get(size_t call, uint32_t o) const {
        T v;
        memcpy(&v, recs[std::min(call, recs.size() - 1)].data() + offset + o, sizeof v);
        return v;
    }
};

inline captain_toad::RecoveredEft::Mtx34 toMtx(const Affine& a) {
    captain_toad::RecoveredEft::Mtx34 m;
    for (int k = 0; k < 12; k++) {
        m[k] = a.m[k];
    }
    return m;
}

inline captain_toad::audio::Vec3f av(const captain_toad::RecoveredVec::V3& v) {
    return {v.x, v.y, v.z};
}

// Stream positions at the opening checkpoint (sound-dsp-a, capture timeline): Tutorial 299268 (line
// volume 84), the LightBreeze wind loop 6740; the intro runs 250 frames before the checkpoint.
constexpr double kTutorialAtCheckpoint = 299268.0, kWindAtCheckpoint = 6740.0;
// The course checkpoint (front-input-a's end, the start of the PullUpStage routes): its objects
// had run 226 ticks (run hc7: the patrolling enemy's first update at frame 1149 of front-right-a,
// the checkpoint at 1599; it reaches its traced walk step 35 at x 1240 then).
inline constexpr const char* kCheckpointStage = "PullUpStage";
constexpr int kCheckpointObjectTicks = 226;

struct Session::Impl {
    ~Impl();

    // --- the package ---------------------------------------------------------------------------
    std::filesystem::path scenePath;
    captain_toad::data::Value sceneDocument;
    captain_toad::data::JsonView scene;
    bool previewFrameFixed = false;   // capture / reference: the idle clip repeats every 180 frames

    // --- game objects ----------------------------------------------------------------------------
    PlayerRuntime player;
    OpeningGame game;
    bool gameCamera = true;
    uint32_t prevHeld = 0;
    int companionActor = -1;
    std::vector<GrassWindState> grassWinds;
    BirdFlock birdFlock;
    captain_toad::RecoveredStageStart::Demo intro;
    bool introEnabled = false;
    bool stageObjectsPreRolled = false;
    std::function<void(const Session::StartTick&)> tickHook;
    void reportTick() {
        if (tickHook) {
            const bool on = introEnabled && intro.active();
            tickHook({on ? intro.tick : -1, introEnabled && intro.turnStarted});
        }
    }

    // --- models and their presentation ---------------------------------------------------------
    // Touch hints (StageGuide.hpp): the GUIDE_BALLOON 2D actor (scene "actor2d"), a sprite drawn
    // in the scene at each GuideAppearPoint while its nerves show it.
    struct GuideSprite {
        struct Key {
            float dy, sx, sy, color[4];
        };
        std::map<std::string, std::vector<Key>> sequences;
        std::map<std::string, bool> loops;
        std::map<std::string, std::unique_ptr<captain_toad::gpu::Texture>> textures;
        float quad[4] = {-32, -32, 32, 32};
    } guideSprite;
    std::vector<size_t> guidePoints;   // the GuideAppearPoint objects
    void guideSetup(captain_toad::data::JsonView actor2d);
    float guideScale(int iconType) const;
    void drawGuideBalloons(const double* view, const captain_toad::gpu::Mat4& projection);
    std::vector<std::unique_ptr<Layout>> layoutStore;
    std::map<std::string, Layout*> layouts;   // goal-sequence 2D layouts by name; part panes resolve here
    DropShadow dropShadow;
    std::vector<CharacterAnimation> actors;   // the companion, birds, star, grass; each owns a skin range
    std::vector<MaterialAnimationSet> actorMaterials;
    std::vector<ActorClipMaterials> actorClipMaterials;
    int starActor = -1, demoStarActor = -1, bossBirdActor = -1;
    int goalStarObject = -1, goalDemoObject = -1;   // a course's GoalItem and its demo star (stage objects)
    std::map<int, size_t> materialActor;            // material index -> actor
    // Stage packages: the material animations of each model (scene "material_animations"): the rest
    // values of its animated materials and its clips by file name; per placed object, its model's.
    struct StageMaterials {
        MaterialAnimationSet rest;
        std::map<std::string, MaterialAnimationSet> clips;
    };
    std::map<std::string, StageMaterials> stageMaterials;
    std::vector<const StageMaterials*> objectMaterials;
    StageMaterials skyMaterials;                                  // the stage sky's (scene sky.material_animations)
    std::vector<std::pair<std::string, bool>> skyMaterialClips;   // playing: file, looping
    std::vector<ActorShadow> actorShadows;
    // Stage packages: the course has a goal demo (scene.json goal), and its Get has happened.
    bool stageGoal = false;
    bool stageGoalReached = false;
    bool stageMissed = false;
    // Stage packages: the placed objects and their recovered behaviours (game/StageObjects.hpp);
    // partObject maps the player collider's parts to their objects (-1: none).
    captain_toad::RecoveredStage::World stageWorld;
    bool stageObjects = false;
    std::vector<int> partObject;
    std::vector<int> partSlot;   // the object's part slot (exporter "slot"; -1: its world matrix)
    std::vector<std::pair<size_t, std::string>> meshObjectIds;               // loading: mesh index -> object id
    std::vector<std::pair<size_t, std::string>> actorObjectIds;              // loading: actor index -> object id
    std::vector<std::map<std::string, std::pair<float, bool>>> actorClips;   // per actor: clip lengths
    std::vector<std::map<std::string, std::pair<float, bool>>> actorVisibilityClips;   // per actor: overlays
    std::vector<int> objectActor;   // object index -> actor index (-1)
    void loadStageObjects();
    void stageObjectsTick();
    void applySceneStop(const std::string& table, const std::string& reaction);
    void courseGoalMirror();
    // The pull protocol around the player's tick (Session.cpp): the Hold handler's request before
    // the action machine runs, the pull message and the reply after it.
    int pullTarget = -1;        // player +0x2a4: the object the request reserved
    bool playerBound = false;   // a ladder holds the player (StageObjects World::bind)
    void stagePullRequest(uint32_t trig);
    void stagePullAfterTick();
    // Carrying (game/PlayerCarry.hpp, game/StageRadish.hpp): the held object (+0x1c0), ticks held
    // (+0x1d0), the throw's release delay (+0x1cc), voices for the player's sound entry.
    int heldObject = -1;
    int heldTicks = 0;
    int throwDelay = 0;
    std::vector<std::string> playerVoices;
    std::vector<std::string> playerEffects;   // effects of the player's hit reactions this tick
    // The protagonist and the water (game/PlayerWater.hpp): wet state and the surface detector's
    // last point; the player's hit reactions of this tick (着地, 水柱ヒット[水面], ...) with the
    // point a reaction happens at (DirectPos effects).
    captain_toad::RecoveredPlayerWater::Wet playerWet;
    CameraMask cameraMask;   // the course's edge tint (scene.json "camera_mask", render/CameraMask.hpp)
    Bloom bloom;             // the course's bloom (scene.json "bloom", render/Bloom.hpp)
    // The protagonist's colour animation (WetStart / WetEnd of both forms' materials, started by
    // the wet state) and its player.
    StageMaterials protagonistMaterials;
    captain_toad::RecoveredStage::Object::MaterialPlayer protagonistColor;
    captain_toad::RecoveredVec::V3 prevWaterPoint{};
    bool havePrevWaterPoint = false;
    struct PlayerReaction {
        std::string name;
        bool hasAt = false;
        captain_toad::RecoveredVec::V3 at{};
    };
    std::vector<PlayerReaction> playerReactions;
    bool playerReactionHasAt = false;
    captain_toad::RecoveredVec::V3 playerReactionAt{};
    int damageInvalid = 0;
    int blinkPhase = 0;   // the damage blink's 6-tick cycle (0x1bc94c, player +0x110 +0x18)
    int invincibleTimer = 0;
    std::string invincibleBgm,
        clearJingle;   // BgmPlayInfoList "Invincible" (Ws_Kp_bgm_Pick)                  // the invincible timer (+0x5c object +0x24)
    int pickaxeObject =
        -2;   // the "player/pickaxe" object (-1 none)                    // the invulnerability counter (object 0x4cbef8 +8)
    void stageCarryTick(uint32_t trig);
    void stageCarryRelease();
    bool stageGroundGrab();
    void playerPowerTick();
    void invincibleTick();
    void setPlayerForm(int form);
    void damageBlinkTick();
    void updateLevelOfDetail(const double eye[3], double fovyDegrees);
    std::vector<int> lodLevel;   // per stage object: its level of detail this frame
    void showPlayerForm(int form);
    void transformStep();
    int transformCount = 0;   // the power change flicker (player +0x10c +0x14)
    int transformFrom = 0, transformTo = 0;
    bool transformRequest = false;
    void holdMatrix(float out[12]) const;
    void stageHoldPlace();
    // The touch screen in a course (the mouse or a finger over the game view). A press picks the
    // nearest touch point along its ray (World::pickScreenPoint) and the object gets the touch
    // after this tick's objects ran. Enhancement (tapToWalk, not in the original): a tap that no
    // object took, on walkable ground, walks the protagonist there by steering his stick.
    bool touchHeld = false;           // this frame's touch (Controls)
    float touchView[2] = {0, 0};      // the touched point over the view, 0..1 (y down)
    float viewAspect = 5.0f / 3.0f;   // the view's width / height
    bool tapToWalk = true;
    bool silhouette = true;   // the protagonist's silhouette behind walls (drawSilhouette)
    void drawSilhouette();
    bool touchWasHeld = false;
    float touchStart[2] = {0, 0};   // in top-screen pixels (400 x 240)
    bool touchStartInView = false;
    bool touchMoved = false;   // moved 16 or more pixels: a slide (the camera), not a tap
    bool touchTaken = false;   // an object reacted to this touch
    int touchTicks = 0;
    int pendingScreenTouch = -1;
    int assistTarget = -1;     // the touch's collision target (touch pointer +0x9c; object index)
    bool assistSent = false;   // the hold message went out for this touch (+0x117)
    struct WalkTarget {
        bool active = false;
        captain_toad::RecoveredVec::V3 pos;
        int ticks = 0, stalled = 0;
        float best = 1e30f;
    } walkTo;
    float lastCam[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    // Enhancement (with tapToWalk): a double tap on an object walks the protagonist to it and
    // presses the action button once the game itself would take the press (a lever or plant he
    // can pull, a turnip he can pick up); while he carries something, a double tap turns him toward
    // the tapped point and throws it there.
    struct TouchIntent {
        enum class Kind { None, Interact, Throw } kind = Kind::None;
        int object = -1;
        captain_toad::RecoveredVec::V3 point;
        int ticks = 0, stalled = 0;
        float best = 1e30f;
    } touchIntent;
    // Enhancement: drawing between the game ticks (Presentation.hpp; SessionRender.cpp present*).
    struct Present {
        bool enabled = false;
        PresentTick prev, cur;
        double sinceTick = 0;   // seconds from the last tick to this frame
        float alpha = 1;        // where this frame lies between prev (0) and cur (1)
        // While drawing a blended frame: the objects' world matrices, the protagonist's and actors'
        // exact poses to restore afterwards.
        bool blended = false;
        std::vector<captain_toad::RecoveredVec::M34> objectWorld;
        std::vector<std::vector<captain_toad::RecoveredVec::M34>> objectDraws;
        std::vector<Affine> characterPose;
        std::vector<std::pair<size_t, std::vector<Affine>>> actorPose;
        std::vector<std::pair<size_t, std::vector<std::array<float, 9>>>> actorLocal;
        captain_toad::RecoveredCamera::Pose view;   // the view drawn
        bool viewValid = false;
    } present;
    void presentCapture();
    void presentedFrame(const captain_toad::RecoveredCamera::CameraPad& pad, float sx, float sy);
    void placeHeadlamp();
    void presentBlend();
    void presentRestore();
    const captain_toad::RecoveredVec::M34& presentObjectWorld(size_t object) const {
        return present.blended && object < present.objectWorld.size() ? present.objectWorld[object]
                                                                      : stageWorld.objects[object].world;
    }
    // Enhancement: the smooth camera drag (SessionShell.cpp).
    struct CameraDrag {
        bool enabled = false;
        bool held = false, dragging = false;
        float start[2] = {0, 0}, last[2] = {0, 0};   // touch-screen pixels
        float pendH = 0, pendV = 0;                  // degrees gathered since the last tick
        float velH = 0, velV = 0;                    // degrees per second
        bool turnedThisTick = false;
        bool moving() const { return dragging || velH != 0 || velV != 0 || pendH != 0 || pendV != 0; }
    } cameraDrag;
    bool cameraDragUsable() const;
    void cameraDragFrame(double dt, bool touch, const float pos[2]);
    void cameraDragHandOver();
    bool cameraDragView(float out[12]) const;
    bool presentView(float out[12], float& fovy) const;
    captain_toad::RecoveredVec::V3 cameraDragOffset() const;
    WalkRoute walkRoute;   // the route of the walk in progress (tap to walk or the double tap's)
    bool routeAim(const captain_toad::RecoveredVec::V3& target, captain_toad::RecoveredVec::V3& aim, float& progress);
    bool routeClimb(float& sx, float& sy, float& best, int& stalled);
    std::vector<LadderSpan> ladderSpans();
    struct TapAim {
        int object = -1;
        captain_toad::RecoveredVec::V3 point;
        bool hasPoint = false;
    } firstTap;
    long lastTapTick = -1000;   // the previous single tap (game ticks), where it was, its aim
    float lastTap[2] = {0, 0};
    // Enhancement: the wide view (Toggle::WideView). The original view is 5:3 (the top screen's
    // 400 x 240); a wider one adds view at the sides (the same vertical field of view), a narrower
    // one above and below (the same horizontal one), so the original picture is always all there.
    // Beyond 4:3 and 21:9 the view is boxed.
    bool wideView = false;
    static constexpr float kOriginalAspect = 5.0f / 3.0f;
    float viewShape(float areaAspect) const {   // the drawn view's width / height
        return wideView ? std::clamp(areaAspect, 4.0f / 3.0f, 21.0f / 9.0f) : kOriginalAspect;
    }
    // The view's half extents as multiples of tan(fovy / 2): (aspect, 1) or (5:3, 5:3 / aspect).
    static void viewExtent(float aspect, float& kx, float& ky) {
        kx = std::max(aspect, kOriginalAspect);
        ky = kx / aspect;
    }
    std::array<int, 4> viewRect(int x, int y, int w, int h) const;
    bool touchPixel(float out[2]) const;
    bool touchRay(const float px[2], captain_toad::RecoveredVec::V3& origin, captain_toad::RecoveredVec::V3& dir) const;
    bool screenPixel(const captain_toad::RecoveredVec::V3& p, float out[2], float* scale = nullptr) const;
    int touchObjectAt(const float px[2]) const;
    TapAim touchAim(const float px[2]) const;
    bool touchDoubleTap(const TapAim& aim);
    void touchTick();
    void walkStick(float& sx, float& sy);
    uint32_t touchButtons(float& sx, float& sy);
    OpeningEffects eft;   // the recovered particle effects
    LightService lightService;
    bool picaLighting = false;
    int lutRowCount = 28;
    CharacterAnimation character;      // the protagonist's model of the current power form
    CharacterAnimation characterAlt;   // the other form's model (Mini / Super), swapped in on a change
    int characterForm = 0;             // the form whose model is in `character`
    // The player's model container switch (0x1bd1c4): the other form's model takes over.
    void setCharacterForm(int form) {
        if (form == characterForm || characterAlt.skin.empty()) {
            return;
        }
        std::swap(character, characterAlt);
        characterForm = form;
        showCharacter();
    }
    // The protagonist's meshes: the current form's model, unless hidden (the damage blink).
    bool characterHidden = false;
    void showCharacter() {
        for (auto& m : meshes) {
            if (m.characterForm >= 0) {
                m.visible = m.characterForm == characterForm && !characterHidden;
            }
        }
    }
    float animationFrame = 0;

    // --- shadows ---------------------------------------------------------------------------------
    // Shadow quality (native setting): original = ROM resolution (512), unfiltered, ROM _shd proxy
    // casters; high = native resolution (2048), 5x5 PCF, full visible geometry as casters.
    ShadowMap shadowMap;
    std::vector<captain_toad::RecoveredVec::M34> shadowObjectWorld;   // the objects' matrices at the last shadow check
    bool shadows = true, filterShadows = true, shadowDirty = true;
    int shadowResolution = 2048;
    bool highQualityShadows = true, romShadowProxies = false;
    int originalShadowResolution = 512, nativeShadowResolution = 2048;
    int shadowMask = 0;
    float shadowTint[4] = {}, shadowDirection[4] = {};

    // --- view and scene buffers ------------------------------------------------------------------
    Camera camera, openingCamera;
    double fovy = 30, clipNear = 100, clipFar = 115000;
    float ambientLight[4] = {0, 0, 0, 1}, diffuseLight[4] = {0, 0, 0, 1}, lightDirection[4] = {0, 1, 0, 0};
    std::vector<Vertex> vertices;
    std::vector<float> collision, tangents;
    std::vector<float> secondUv;   // stage packages: the second UV set (uv1.bin), when a material reads it
    float secondDirection[4] = {0, 1, 0, 0};
    // GPU copies: the vertex streams (skinned ranges uploaded again when they move: dirtyVertices,
    // first vertex -> count), the textures, the material programs and the lighting tables.
    std::unique_ptr<captain_toad::gpu::Buffer> vertexBuffer, tangentBuffer, secondUvBuffer, collisionBuffer;
    std::map<size_t, size_t> dirtyVertices;
    void markVertices(const CharacterAnimation& a) {
        size_t& n = dirtyVertices[size_t(a.first)];
        n = std::max(n, a.skin.size());
    }
    void uploadVertices();
    std::unique_ptr<captain_toad::gpu::Texture> lightingLuts;
    std::unique_ptr<captain_toad::gpu::Texture> sceneCopy;   // the water pass's copy of the scene drawn so far
    std::vector<Mesh> meshes;
    std::vector<std::unique_ptr<captain_toad::gpu::Texture>> textures;
    std::vector<std::unique_ptr<captain_toad::gpu::Program>> programs;
    double skyFrame = 775, skyStart = 775;
    std::vector<MaterialState> materialStates;
    std::unique_ptr<captain_toad::gpu::Texture> whiteTexture;
    bool wire = false, showCollision = false, lighting = true;
    bool stereo = false;
    double stereoParallax = 0.02;   // see SessionRender.cpp
    bool useReferenceCamera = false;
    double referenceView[16] = {}, referenceProjection[16] = {};

    // --- effects ---------------------------------------------------------------------------------
    int playerFloorAttr = -1, playerFloorPrev = -1;        // collider ground attribute (this / previous tick)
    int playerFloorPart = -1, playerFloorPartPrev = -1;    // its collision part (course packages)
    std::vector<std::vector<std::string>> partMaterials;   // course packages: per part, attribute -> MaterialCode
    // The floor MaterialCode under the protagonist (this tick or the previous one).
    std::string floorMaterial(bool previous) const {
        const int a = previous ? playerFloorPrev : playerFloorAttr;
        const int p = previous ? playerFloorPartPrev : playerFloorPart;
        if (!partMaterials.empty()) {
            if (p < 0 || size_t(p) >= partMaterials.size() || a < 0 || size_t(a) >= partMaterials[size_t(p)].size()) {
                return "";
            }
            return partMaterials[size_t(p)][size_t(a)];
        }
        return a >= 0 && size_t(a) < eft.floorMaterials.size() ? eft.floorMaterials[size_t(a)] : std::string();
    }
    std::map<size_t, bool> birdFleeing, birdFlyWait;
    std::map<size_t, float> birdRestYaw;
    bool headlampOn = false;
    bool objectsStarted = false;
    std::string playerEffectAction;
    int companionEffectClip = -1;

    // --- research replay -------------------------------------------------------------------------
    PadTrace padTrace;
    size_t padCall = 0;

    // --- sound -----------------------------------------------------------------------------------
    std::unique_ptr<captain_toad::audio::SoundSystem> snd;
    std::unique_ptr<captain_toad::OpeningSoundKeeper::Keeper> sndKeeper;
    bool soundStarted = false;
    std::string playerSoundAction;
    float playerSoundPrev = 0;
    int cameraSoundBand = -1;
    long soundFrameNo = 0;
    uint32_t checkpointGameRng[4] = {};
    bool haveCheckpointGameRng = false;
    int atmosphereHandle = -1;
    void playAtmosphere();
    // Course packages: the stage's BGM stream and stage-start sound (sound/sedb.json), the placed
    // objects' effect / sound owners ("obj:<id>") and their hit reaction tables (effects.json).
    std::string stageBgm, stageStartSound;
    struct HitReaction {
        std::string effect, sound, shake;
        float shakeDistance = 0;
        int stopFrames = 0;              // StopSceneFrame
        bool stopForHitEffect = false;   // IsStopSceneForHitEffect: the stop's mode 2
    };
    struct SceneStop {
        int length = 0;          // [0]
        int mode = 0;            // [1]
    } sceneStop;                 // the scene stop object (scene +0x74 -> +0x14; 0x22e73c / 0x22e76c / 0x221fec)
    float carryGrabFrame = -1;   // PullOutSuccessCarry's frame before the player's update (-1: other clip)
    std::map<std::string, std::map<std::string, HitReaction>> hitReactions;   // model -> name -> entry
    struct PendingReaction {
        size_t object;
        std::string name;
        bool hasAt = false;
        captain_toad::RecoveredVec::V3 at;
    };
    std::vector<int> reactionAtObject;   // per object: an emission at a reaction's place is under way
    captain_toad::RecoveredVec::V3 reactionAt;
    std::vector<PendingReaction> effectReactions, soundReactions;
    std::vector<std::tuple<size_t, std::string, int>> programSounds;   // World::sound requests (choice)
    // The walk controller's dash voice (0x29a394: walk level 3 and more than 5 units since the
    // previous tick -> "DashLoopStartPre" once, until the run ends).
    bool dashVoicePlayed = false;
    // NPC pushes (the companion's law, Push.hpp): the player's contact sensors of the previous tick.
    std::array<captain_toad::RecoveredVec::V3, 3> prevPlayerSensors{};
    bool havePrevPlayerSensors = false;
    captain_toad::RecoveredVec::V3 dashPrevPos;
    std::vector<char> objectEffectOwner, objectSoundOwner;   // per object: registered with the keepers
    std::vector<unsigned> objectActionSeen;                  // per object: the action start count last seen
    std::vector<char> objectAliveSeen;                       // effects: the object was alive last tick
    void registerObjectOwners();
    std::string companionGoalClip;
    float companionGoalPrev = 0;
    std::string debugCompanionClip;
    // The original's previous angles (+0x14c, +0x150) still hold the defaults when the area camera
    // takes over from the start camera (traced: one PgGyroMovingFast at the switch, frame 27).
    float cameraSoundLastV = 30, cameraSoundLastH = 45;
    std::vector<int16_t> audioPending;   // audio thread: the rest of the last DSP frame
    size_t audioPendingAt = 0;

    // --- the interactive clock -------------------------------------------------------------------
    double playerClock = 0;

    // Session.cpp
    void registerEffectOwners();
    void loadScene();
    void loadSound();
    void preset(int n);
    void cameraMatrix(float cam[12]);
    void effectsTick();
    void startStageSound(bool withIntro);
    void soundTick();
    void soundFrame();
    void gameFrame(captain_toad::RecoveredCamera::CameraPad c, float sx, float sy);
    void skipIntro();
    bool openingFinished();
    void logFloor(bool allHits);
    // --- GPU skinning (SessionSkin.cpp, render/GpuSkin.hpp) ------------------------------------
    int shadowKernel = 2;   // Session::setShadowFilter
    bool gpuSkinning = false;
    struct SkinRange {
        int base = 0;   // the model's first palette entry (its bones' skinning matrices, then the bones)
        int bones = 0;
        uint32_t version = ~0u;   // the pose in the palette
        Affine placement;
    };
    std::map<int, SkinRange> skinRanges;   // by the model's first vertex
    std::vector<float> skinPalette;        // RGBA32F texels, 3 per entry
    std::unique_ptr<captain_toad::gpu::Texture> skinBones;
    std::unique_ptr<captain_toad::gpu::Buffer> skinBuffer;   // per vertex: 4 palette indices, 4 weights
    std::vector<char> meshSkinned;                           // per mesh: inside a GPU-skinned range
    std::string materialVertexSource;                        // shaders/terrain.vert (the material programs')
    std::vector<std::string> materialFragments;
    std::vector<std::unique_ptr<captain_toad::gpu::Program>> skinnedPrograms;   // per material, made on first use
    std::map<std::string, captain_toad::gpu::Program*> skinnedSharedPrograms;   // by name (device-owned)
    std::vector<CharacterAnimation*> skinnedModels();
    void setGpuSkinning(bool on);
    void updateSkinPalette();   // the palette of the models posed since; before drawing
    bool skinnedMesh(const Mesh& m) const;
    captain_toad::gpu::Program& meshProgram(const Mesh& m);   // the material's program, its skinned variant if needed
    captain_toad::gpu::Program& skinnedShared(const std::string& name, std::string_view vertex,
                                              std::string_view fragment);
    void addSkin(captain_toad::gpu::Draw& d) const;   // the palette indices / weights stream and the palette texture

    // SessionRender.cpp
    void setShadowQuality(bool high);
    void drawGoalLayouts(const captain_toad::gpu::Rect& view);
    void poseScene(bool writeVertices = false);   // joints; vertices: finishSkinning
    void finishSkinning();
    void render(int viewX, int viewY, int w, int h);
    // One view into (viewX, viewY, w, h): eye 0 the camera itself, -1 / +1 the left / right eye.
    void renderView(int viewX, int viewY, int w, int h, int eye);
    // Headsets (Session::renderEyes): the tracked eye renderView draws instead of the game camera.
    const Session::EyeView* eyeView = nullptr;
    bool eyeRepeat = false;   // a second eye of the same frame: billboards and the shadow map stay
    void renderEyes(const std::vector<Session::EyeView>& eyes, const std::function<void(size_t)>& after);
    // The view being drawn (renderView): its projection and view transform (world -> eye).
    captain_toad::gpu::Mat4 viewProjection, viewTransform;
    // The transform uniforms of a scene program for a model-view (the clip transform, the
    // model-view, its normal matrix and the projection).
    void setTransform(captain_toad::gpu::Program& program, const captain_toad::gpu::Mat4& modelView) const;
    // A draw reading the scene's vertex streams (position, normal, texture coordinates, colour,
    // tangent, second texture coordinates).
    captain_toad::gpu::Draw sceneDraw() const;
};

}   // namespace captain_toad::session
