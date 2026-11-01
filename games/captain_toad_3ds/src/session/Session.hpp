// =============================================================================================
// Session.hpp - one run of the opening: game state, game loop and scene rendering
// =============================================================================================
//
// A Session loads an imported scene package and runs the recovered game logic on it: the
// player, the camera director, the companion, the birds, the stage-start intro, the goal
// sequence, the particle effects and the sound. It renders through the graphics device
// (gpu/Device.hpp; the platform layer creates it) and produces the audio stream.
//
// The session owns no window and reads no device: the shell (app/) translates keyboard, mouse
// and game controller input into Controls, drives the clock and presents the frames. That keeps
// the game portable between the desktop shell, the headless research modes below and, later,
// other front ends such as a headset.
// =============================================================================================
#pragma once
#include "platform/GameImage.hpp"

#include <cstdint>
#include <array>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace captain_toad::session {

// The player's controls in the game's terms, for one 60 Hz frame.
struct Controls {
    float stick[2] = {0, 0};      // circle pad, [-1, 1], y up
    float cstick[2] = {0, 0};     // C-stick, [-1, 1], y up
    uint32_t buttons = 0;         // the game's controller bits (captain_toad::RecoveredCamera::kA, ...)
    bool touch = false;           // touch screen held
    float touchPos[2] = {0, 0};   // touch position in the 320 x 240 touch screen
    // The same touch over the 3D view a course is played in: 0..1 across the view (y down) and the
    // view's width / height (the session letterboxes the game view inside it).
    float touchView[2] = {0, 0};
    float viewAspect = 5.0f / 3.0f;
};

// Display switches of the inspection services (not game state).
enum class Toggle {
    Collision,   // collision mesh overlay
    Wireframe,
    Lighting,
    ShadowQuality,   // original (ROM resolution and proxies) or high
    Stereo,          // full side-by-side stereo (see render())
    TapToWalk,       // enhancement: tapping the ground walks the protagonist there
    Silhouette,      // the protagonist's silhouette behind walls
    SmoothCamera,    // enhancement: touch drags turn the camera at the display rate, with inertia
    Interpolate,     // enhancement: frames between the 30 Hz ticks show the blend of the last two
    WideView,        // enhancement: the game view fills screens from 4:3 to 21:9 (more view, none lost)
    GpuSkinning,     // enhancement: animated models skinned in the vertex stage (render/GpuSkin.hpp)
};

// Saves a captured image (PNG) for the research modes.
using ImageWriter = std::function<void(const std::filesystem::path&, const captain_toad::platform::Image&)>;

class Session {
public:
    // Loads the package; needs the graphics device. Starts with the stage-start intro
    // unless DIORAMA_NO_INTRO is set.
    explicit Session(const std::filesystem::path& package);
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    // Advances the clocks by `seconds` of real time and runs the 60 Hz game frames that fall due.
    void advance(double seconds, const Controls& controls);

    // Renders the view into the rectangle (x, y, w, h) of the current framebuffer, in pixels with
    // the origin at the bottom left. The game camera keeps the original 5:3 top screen.
    // With Stereo on, the rectangle's left half (x, y, w / 2, h) shows the left eye's view and the
    // right half the right eye's, each fitted as above ("full side-by-side" for stereo displays).
    void render(int x, int y, int w, int h);

    // The free inspection camera: views 1-4 (0 returns to the game camera), orbit, pan and zoom.
    void selectView(int view);
    bool gameCameraActive() const;
    void orbit(double dx, double dy);
    void pan(double dx, double dy);
    void zoom(double steps);
    void toggle(Toggle t);
    // The switch's current state, and setting it (no change when it already has that state).
    bool enabled(Toggle t) const;
    void set(Toggle t, bool on);
    // Stereo eye separation in steps 1 (gentle) to 5 (strong); 3 is the default.
    static constexpr int kStereoSteps = 5;
    void setStereoIntensity(int step);

    // True once the opening has played out: the title logo's End animation (the dot wipe that
    // leads to the book in the original) has finished and the cutscene music has stopped. The
    // shell then returns to the launcher.
    bool finished() const;

    // Stage packages: true once the player has touched the course's star (GoalItem contact); the
    // shell then plays the course clear.
    bool goalReached() const;
    // The camera is being turned by a smooth touch drag or still coasting: worth drawing at the
    // display's full rate.
    bool cameraMoving() const;
    // The game view's rectangle in a w x h area (x, y from the bottom left, w, h): the original's
    // 5:3, or with Toggle::WideView the area's own shape between 4:3 and 21:9.
    std::array<int, 4> gameRect(int x, int y, int w, int h) const;
    // The original's 5:3 frame in a view rectangle (x, y, w, h): inside it, or covering it.
    static std::array<int, 4> originalFrame(const std::array<int, 4>& view, bool cover);
    // Drawing between ticks: where the next frame lies between the last two game ticks (0..1;
    // 1 without Toggle::Interpolate).
    float presentAlpha() const;
    void setPresentationFraction(double fraction);
    // Stage packages: true once the protagonist is inside one of the course's DeathAreas (a fall);
    // the shell then plays the miss. (The player's death states are not ported yet.)
    bool missed() const;
    // Stage packages: the gems (CollectItemId 0-2) taken in this attempt, as bits.
    int collectedGems() const;

    // Stage start, for an overlay that runs with the game ticks (the course's title card): the
    // hook is called after every 30 Hz game tick with the intro's tick (0: before the stage
    // scene's first update; -1 once the intro is over or when there is none) and whether the
    // demo is turning (from Turn's step 0).
    struct StartTick {
        int introTick = -1;
        bool turnStarted = false;
    };
    void onTick(std::function<void(const StartTick&)> hook);

    // ---- headsets (src/xr): the course as a model on a real table ---------------------------
    // A tracked eye instead of the game camera. `view` is rigid (rotation and translation, in course
    // units: the headset scales the eye's position, not the view); `projection` is an OpenGL-style
    // frustum (clip depth -w..w). `bind` makes the eye's target current.
    struct EyeView {
        std::array<double, 16> view{};         // course -> eye, column-major
        std::array<double, 16> projection{};   // eye -> clip, column-major
        std::function<void()> bind;
    };
    // Draws the course for each eye over the whole bound target, cleared to transparent black,
    // without the sky and the screen-space passes (bloom, camera mask, the 2D layouts and wipes):
    // the room shows around it. The scene is posed once for all eyes. `after(eye)` runs with the
    // eye's target still bound (the headset adds its own drawings and the alpha there).
    void renderEyes(const std::vector<EyeView>& eyes, const std::function<void(size_t eye)>& after = {});
    // The shadow filter's radius in shadow-map texels with high-quality shadows (2: 5 x 5 bilinear
    // comparisons, the default; 1: 3 x 3, a third of the texture reads, for headsets).
    void setShadowFilter(int radius);
    // The high-quality shadow map's size in texels (the package's native resolution by default).
    void setShadowResolution(int size);
    // The course's extent: its collision (else its meshes without the sky), min x, y, z, max x, y, z.
    std::array<float, 6> bounds() const;
    // The game camera's viewing direction in the course (its forward axis).
    std::array<float, 3> cameraForward() const;

    // A one-line player status for the shell.
    std::string status() const;

    // Audio: true when the package has sound. fillAudio() renders interleaved stereo 16-bit
    // frames at audioSampleRate(); it is called from the audio thread.
    bool hasSound() const;
    int audioSampleRate() const;
    void fillAudio(int16_t* stereo, int frames);

    // Research modes (headless, deterministic): renders and logs into `out`. They need the
    // graphics device and write images through `save`.
    //   capture:   the inspection views, shadow comparisons, collision overlay and idle frames;
    //   play:      replays an input script from the opening checkpoint (player.tsv, captures,
    //              optional audio via DIORAMA_SOUND_OUT);
    //   reference: one image from a traced original camera (JSON).
    static void capture(const std::filesystem::path& package, const std::filesystem::path& out,
                        const ImageWriter& save);
    static void play(const std::filesystem::path& package, const std::filesystem::path& input,
                     const std::filesystem::path& out, const ImageWriter& save);
    static void reference(const std::filesystem::path& package, const std::filesystem::path& camera,
                          const std::filesystem::path& out, const ImageWriter& save);

    struct Impl;

private:
    Session() = default;
    std::unique_ptr<Impl> impl_;
};

}   // namespace captain_toad::session
