#pragma once
#include "graphics/ds_pipeline.hpp"
#include "graphics/text.hpp"
#include "sound/sound_object.hpp"
#include "system/audio_service.hpp"
#include "system/game_types.hpp"
#include <optional>

namespace picarat {
// The toolkit the minigames and menus are built from (herb tea, camera
// repair, photo hunt, hamster, the bag and its pages, the shack, the staff
// roll). It offers what the original scenes get from overlay 2: a frame
// cycle (poll input, run the scene's logic, present), fades, two background
// pictures, two text layers, sprites (3D-drawn on the lower screen, 2D on
// the upper), sprite buttons, display windows and screen shake.
//
// Each original loop iteration is poll (0206c7b4) -> logic and drawing ->
// present (0206cfb8); scenes written with this class keep that order. Each ROM loop iteration
// is poll (0206c7b4) -> logic/draw -> present (0206cfb8); this class keeps that
// order natively. Rendering is a native composition of the original layers:
//   lower screen: BG (0206ffec), text layer +0x1c, 3D sprite polygons;
//   upper screen: BG (0207064c), text layer +0x34, 2D OAM sprites.
// Layer priorities follow 0206d9f4 (BG0 = 3D, BG1 = text): normally the 3D
// sprites cover the lower text; with priorities (1,0) the text is on top.
class SceneKit {
public:
    SceneKit(GraphicsAssets& assets, PresentFrame present, AudioService* audio, SceneKind scene);
    GraphicsAssets& assets;
    GameText text;

    GameSound* sound() { return audio_ ? audio_->sequenced() : nullptr; }

    // A game whose sound is not the overlay-2 sound object (the first
    // adventure's, sound/a1_sound.hpp) takes effects and the frame
    // tick here instead.
    struct SceneSound {
        virtual ~SceneSound() = default;
        virtual void effect(int id) = 0;
        virtual void frame() = 0;

        // A frame of a loading pause: time passes, the game's frame does not
        // run (the first adventure's main loop stands still while loading).
        virtual void idle() {}
    };

    SceneSound* sceneSound = nullptr;

    void effect(int id) {
        if (sceneSound) {
            sceneSound->effect(id);
        } else if (auto s = sound()) {
            s->playEffect(id);
        }
    } // 0207f850 + 0207f8f0 (first adventure: 020053ac)

    // --- Frame cycle.
    void poll();    // 0206c7b4: input (0206c98c keys, 0206e2c0 touch), prepare (0206d950)
    void present(); // 0206cfb8: flush, VBlank, frame counter +1 (0x2088a00)
    // A frame without the first adventure's background manager step (its
    // fades wait for VBlanks and count frames, 0200f7b4).
    void refresh();
    void wait(int count); // 0206ce18 (at least one frame; prepare from the second)
    // A loading pause: the screen holds the last presented frame for `count`
    // refreshes while the game's frame counter (`tick`, which times sprite
    // animations) stands still (the first adventure: 021ebb30 counts main
    // loop frames, measured on puzzle 2's cursor).
    void hold(int count);
    // VBlank waits without a frame (the first adventure's 02003d64): the
    // screen holds the last presented frame and the game's frame counter
    // stands still, but the sound keeps running.
    void stall(int count);
    void fadeIn();  // 02071790(step 1, interval 1), both screens
    void fadeOut(); // 02071e84(step 1, interval 1), both screens

    void setBrightness(int value) { brightness_ = lowerBrightness_ = value; } // both screens

    void setLowerBrightness(int value) { lowerBrightness_ = value; } // 0x0400006c only

    void setUpperBrightness(int value) { brightness_ = value; } // 0x0400106c only

    int lowerBrightness() const { return lowerBrightness_; }

    int upperBrightness() const { return brightness_; }

    void fadeInLower(int step = 1); // 02071d28: main screen only, (16/step)+1 iterations, then 0
    void fadeOutLower();            // 02072260(step 1): main screen only, 17 waits, then -16
    void waitRelease(); // 0206cec0: touch state machine only (no scene poll) until a release
    // Background scroll (layer manager +0x68..+0x78, set by ov16 020f0c58).
    int lowerScrollY = 0, upperScrollY = 0;
    // The first adventure's background manager (020e25a8): horizontal
    // scroll of the lower BG3 and BG2, and their automatic scrolling, which
    // present() steps on every second call (0200928c sets it, 020097dc
    // steps it). Maps are 256 pixels wide and wrap around.
    int lowerScrollX = 0, lowerOverlayScrollX = 0;

    struct AutoScroll {
        bool on = false;
        int dx = 0, dy = 0;
    };

    AutoScroll lowerAutoScroll, lowerOverlayAutoScroll;
    // The first adventure's lower screen by BG priority (BGxCNT bits 0-1)
    // instead of the fixed order: entries for BG0 (3D sprites), BG1 (text),
    // BG2 (lowerOverlay) and BG3 (lowerBackground); a smaller value is
    // nearer, at equal values the lower BG number. Pictures show no colour 0
    // (their blank tile's colour) where another layer lies behind them.
    std::optional<std::array<int, 4>> lowerPriority;

    // The first adventure's palette tint of the lower pictures (02008e70):
    // every colour moves toward (r, g, b) (8-bit) by factor / 256; reloading
    // the picture clears it.
    struct Tint {
        int r = 0, g = 0, b = 0, factor = 0;
    };

    Tint lowerTint, upperTint; // upper: 02009074
    // The first adventure's blend of the lower text layer over what lies
    // beneath (0209adac with BG1 first): -1 off, else 0..16 (the text's
    // weight in sixteenths).
    int lowerTextAlpha = -1;

    // Blend weights (0209adac): (first * eva + second * evb) / 16.
    struct Blend {
        int eva = 16, evb = 0;
    };

    // The first adventure's staff roll: the upper BG2 picture (upperOverlay)
    // lies over the upper sprites and blends with them (0209adac on
    // 0x04001050: BG2 first, OBJ second): (picture * eva + below * evb) / 16.
    std::optional<Blend> upperOverlayBlend;
    // Upper sprites blended with what lies beneath (0209adac with OBJ first,
    // e.g. the first adventure's title logo over BG3): each sprite pixel
    // becomes (sprite * eva + below * evb) / 16 in 5-bit components; the
    // draw's alpha is then ignored.
    std::optional<Blend> upperSpriteBlend;
    // The lower backdrop colour (RGB555, BG palette colour 0) when set;
    // else black.
    std::optional<std::uint16_t> lowerBackdrop;
    // The two screens swapped (the first adventure's painting, 0x04000304
    // bit 15): what is drawn as "upper" shows on the touch screen.
    bool swapScreens = false;
    // A second lower picture right below lowerBackground (the second half of
    // a tall BG3 map), shown when lowerScrollY moves past the first.
    std::string lowerBackgroundBelow;
    Touch touch; // 0206e2c0 result for this iteration

    bool pressed() const { return filter_.state == 1; } // 0206e1e4

    bool released() const { return filter_.state == 3; } // 0206e1f8

    bool held() const { return filter_.state == 1 || filter_.state == 2; } // 0206e20c

    // 0206e288: position while held; otherwise the outputs keep their values.
    void position(int& x, int& y) const {
        if (held()) {
            x = filter_.x;
            y = filter_.y;
        }
    }

    void pressPosition(int& x, int& y) const {
        x = filter_.pressX;
        y = filter_.pressY;
    } // touch +0x28 / +0x2c

    static bool buttonA() { return false; } // 0206ca64: no DS buttons on the native host

    std::uint32_t tick = 0; // 0x2088a00
    // Every frame shown (presents, waits and holds alike): elapsed time.
    std::uint64_t frames = 0;
    // Research only (local probes): replaces the host sample seen by
    // poll number n, so a ROM touch timeline can be replayed per poll.
    std::function<HostInput(unsigned poll, const HostInput& host)> researchPoll;
    unsigned polls = 0;
    int detail = 0; // GameSnapshot detail (research visibility)
    // Reported as GameSnapshot::skippable: the scene ends what plays when the
    // host asks (hostInput().skip; system/game_types.hpp). Timed stretches
    // need nothing more: frame() fast-forwards them while the skip holds.
    bool skippable = false;
    // The touch-area help (TouchArea; system/game_types.hpp): what a scene
    // offers to touch, reported with the next frame and then cleared.
    std::vector<TouchArea> touchAreas;

    // Marks a stretch skippable for as long as it lives.
    struct Skippable {
        explicit Skippable(SceneKit& kit) : kit_(kit) { kit_.skippable = true; }

        ~Skippable() { kit_.skippable = false; }

        Skippable(const Skippable&) = delete;
        Skippable& operator=(const Skippable&) = delete;

    private:
        SceneKit& kit_;
    };

    // --- Display windows (WIN0/WIN1 registers 0x04000040.., 0x04001040..).
    // Layers: BG0 = 3D (lower), BG1 = text, BG3 = background, OBJ = 2D sprites.
    enum Layer : unsigned { BG0 = 1, BG1 = 2, BG2 = 4, BG3 = 8, OBJ = 16 };

    struct Windows {
        bool enabled[2]{};
        int x1[2]{}, x2[2]{}, y1[2]{}, y2[2]{};
        unsigned inside[2]{};
        unsigned outside = 0x1f;
        bool visible(unsigned layer, int x, int y) const;
    };

    Windows windows[2]; // indexed by Screen
    // --- Backgrounds and layer priority.
    std::string lowerBackground, upperBackground;
    // Upper BG2 over the upper background, colour 0 transparent (0207090c;
    // the bonus menu's character portraits).
    std::string upperOverlay;
    // Lower BG2 at priority 0, over everything else on the lower screen,
    // colour 0 transparent (the first adventure's match puzzles' "no more
    // moves" panel, 0200a0dc layer 2; colour 0 is the picture's blank tile).
    std::string lowerOverlay;
    // Text layer +0x51: the line step of 02072ff8 (textQuestion).
    int questionLineStep = 16;
    bool textAbove3D = false; // 0206d9f4(…,1,0,…) during prize windows

    // 0206ddec display reset (subgame screen constructors): BG0/BG1/BG3 on,
    // priorities BG1 0, BG0 1, BG3 2 on both screens (text above 3D), no windows.
    void resetDisplay() {
        textAbove3D = true;
        windows[Lower] = Windows{};
        windows[Upper] = Windows{};
    }

    // --- Text layers (+0x1c lower, +0x34 upper): persistent until cleared.
    enum Screen { Lower = 0, Upper = 1 };

    void clearText(Screen s); // 02072a10 / 02072a2c
    void clearTextRect(int x, int y, int w, int h,
                       Screen s = Lower); // 02072a48 (lower) / 02072a7c (upper)
    void textCentered(int centerY, const std::string& t, int extra, int centerX,
                      Screen s); // 02072f18
    void textTitle(int y, const std::string& t, int extra, int centerX, Screen s,
                   bool fontq = false); // 02072d80 (fontq: 02072e34 flag 1)
    // 02072ff8: fontq text (layer font +0x14) from (x, y), widths 02072634;
    // characters before `skip` advance only.
    void textQuestion(int x, int y, const std::string& t, Screen s, int skip = 0);
    void textAt(int x, int y, const std::string& t, Screen s); // 02072ab0 (lower)
    // 020731b8 (upper): count characters (-1 all); the first `skip` only
    // advance 12 px; a newline steps feed + 4 + extra.
    void textTyped(int x, int y, const std::string& t, int count, int skip = 0, int extra = 0);
    // Loaded resources: system texts tx_%i (0206b2c0) and archive members (0206c8b8).
    std::string systemText(int id);
    std::string member(const std::string& directory, const std::string& name); // raw (unexpanded)

    // --- Sprite objects (ov2 020758fc 3D lower / 02076324 2D upper).
    struct SpriteObject {
        const Sprite* sprite = nullptr;
        AnimationPlayer player;
        int x = 0, y = 0;
        bool upper = false;

        bool loaded() const { return sprite != nullptr; }
    };

    // Path rules of the original loader: "?" is the language directory, ".spr"
    // is a 3D .arc sprite, ".sbj"/".spj" are 2D .arj sprites; x=y=0 takes "pos".
    SpriteObject load(const std::string& path, int x, int y, unsigned animation = 1);
    void select(SpriteObject& s, unsigned index);          // 02075d10 / 020763dc
    void select(SpriteObject& s, const std::string& name); // 02075dd4 / 020764a4
    void restart(SpriteObject& s);                         // 02076460
    bool variable(const SpriteObject& s, const std::string& name, int& x, int& y) const; // 02075a08
    void draw(SpriteObject& s, int alpha = 31); // 02075e50 / 02076534
    // The first adventure's mirrored sprite (020082a4 sets the flag 0200c950
    // and 0200cb3c also use; the texture is flipped, the quad stays: drawn
    // flipped left to right within its frame, hypothesis).
    void drawMirrored(SpriteObject& s, int alpha = 31);
    void drawRotated(SpriteObject& s, unsigned turns); // 02076214 (3D, centred at x/y)
    void drawScaled(SpriteObject& s, float scale);     // 02076124 (3D, centred at x/y)
    // With depthOrder: a polygon at the counter's depth that counts (as
    // sprites do), at the fixed depth 1.0, or at the counter's depth without
    // counting (several polygons sharing one step; countDepth afterwards).
    enum class Depth { Counted, Fixed, Current };
    // 02070e8c: untextured 3D rectangle (lower screen); alpha <= 0 draws nothing.
    void rectangle(int x, int y, int w, int h, std::uint16_t color, int alpha,
                   Depth depth = Depth::Counted);
    // An untextured 3D triangle (first adventure 0200f250) around (cx, cy):
    // the corners as 4.12 fractions of the screen (x / 256, y / 192), drawn
    // through the 3D pipeline (needs ds3D).
    void triangle(int cx, int cy, const std::array<std::array<int, 2>, 3>& corners,
                  std::uint16_t color, Depth depth = Depth::Counted);

    // The first adventure's 3D camera. While set, turned and scaled sprites
    // and triangles are drawn through the DS pipeline (graphics/
    // ds_pipeline.hpp) pixel for pixel, with its edge anti-aliasing
    // (DISP3DCNT bit 4); other lower-screen draws keep their direct path.
    struct Camera3D {
        dspipe::SpriteCamera camera;
        // Whether the 2D compositor mixes partly transparent 3D pixels (an
        // anti-aliased edge over no 3D pixel) with the picture beneath; off
        // when the blend registers leave the 3D layer opaque.
        bool blendOverPicture = false;
        // Whether scaled sprites take the pipeline too (the first
        // adventure's 02005bc0); the second adventure's keep their own path.
        bool scaledSprites = true;
    };

    std::optional<Camera3D> ds3D;

    // The first adventure's 3D depth (ARM9: counter 020e25a4, reset each
    // frame at 02004234). Sprites and counted rectangles lie at depth 1.0 +
    // counter / 2 and count one up; some polygons (0200f520 rectangles,
    // 0200f47c quads) lie at 1.0 and do not count, but the line drawings
    // made of them count once (countDepth). The nearer polygon (larger
    // depth) shows; at equal depth the one drawn first shows (the depth
    // test fails). With depthOrder the lower screen's 3D list is shown in
    // that order instead of the order of submission.
    bool depthOrder = false;

    void countDepth() { ++depthCounter_; }

    // Background shake of the lower screen (layer manager +0x8c): while the
    // counter runs, each frame draws two numbers from the game RNG and moves
    // BG3 by d = r % 3 - 1 on both axes.
    int lowerShake = 0;
    std::uint32_t* random = nullptr;

    // A present without a following poll: the hardware swap leaves the next
    // frame's 3D/OAM lists empty (used by loops that skip 0206c7b4).
    void clearSubmissions() { draws_.clear(); }

    // 020722fc (3D) / 02072434 (2D): digits right to left from the variable's
    // position, stepping by its spacing plane up to its maximum (default 99);
    // digit d is animation d+1, leading zeros only when padded.
    void drawNumber(SpriteObject& s, unsigned value, const std::string& name, int dx, int dy,
                    bool pad);
    int frameWidth(const SpriteObject& s) const;  // 02075cc0
    int frameHeight(const SpriteObject& s) const; // 02075c70

    // --- Sprite buttons (ov2 0208003c..02080690): Button state machine and
    // its sprite animations (pressed +0x28, idle +0x2c, click +0x30).
    struct SpriteButton {
        SpriteObject sprite;
        Button button;
        int x = 0, y = 0;
        unsigned pressedAnim = 0, idleAnim = 0, clickAnim = 0;
        bool exists = false;
        int pressX = 0, pressY = 0; // +0x14 / +0x18: sprite offset while pressed inside
    };

    SpriteButton button(const std::string& path, int x, int y, int sound,
                        const std::string& positionVariable = "pos");
    void setButtonAnimations(SpriteButton& b, const std::string& pressed, const std::string& idle,
                             const std::string& click);           // 0208070c
    void setButtonIdle(SpriteButton& b, const std::string& idle); // 020806e8
    void updateButton(SpriteButton& b);                           // 02080300
    void drawButton(SpriteButton& b);                             // 020805e0

    bool clicked(SpriteButton& b) { return b.exists && b.button.consumeClick(); } // 02080690

    // 020806c4: a click as soon as it happens (no wait for the click animation).
    bool clickedNow(SpriteButton& b) {
        if (!b.exists || b.button.state != Button::State::Clicked) {
            return false;
        }
        b.button.state = Button::State::Idle;
        return true;
    }

    std::string language = "ge"; // 0208398c

    // The scene reported to the host with each frame (e.g. Name: the host
    // passes typed characters in HostInput::name).
    void setScene(SceneKind scene) { scene_ = scene; }

    SceneKind scene() const { return scene_; }

    // For scenes with their own text and touch code (the first adventure):
    // the persistent text layer of a screen, and the host's raw stylus
    // sample of this frame.
    Surface& textLayer(Screen s) { return s == Lower ? lowerText_ : upperText_; }

    const HostInput& hostInput() const { return input_; }

    // Whether a present shows the sprites and background scroll of the
    // previous one (the second adventure's swap latency, measured). The
    // first adventure's puzzle scenes show a frame's sprites with its text
    // (measured, puzzle 23).
    bool spriteLatency = true;
    // Native full-screen painter used instead of the layer composition while
    // set (e.g. the ov2 judgment sequence 0206b4fc on its own images).
    std::function<void(Surface&)> painter;
    // Lower BG0 frame of the 3D engine (hamster walk): r|g<<8|b<<16|a<<24 with
    // 6-bit colour and 5-bit alpha per pixel; empty when no models are drawn.
    // Latched by present() like the 3D sprites (one swap per present).
    std::vector<std::uint32_t> layer3D;
    std::string localized(std::string path) const;

private:
    struct Draw {
        const Surface* image;
        int x, y, alpha;
        bool upper;
        int turns = -1;
        const std::vector<Rect>* chunks = nullptr;
        float scale = 0;
        int w = 0, h = 0;
        std::uint16_t fill = 0;
        const std::vector<Rect>* quads = nullptr;
        int z = 0; // depthOrder: depth in half units
        bool triangle = false;
        std::array<std::array<int, 2>, 3> corners{}; // triangle: 4.12 screen fractions
        bool mirrored = false;                       // the image flipped left to right in its box
    }; // scale > 0: 02076124; image null: 02070e8c (or a triangle)

    int depthCounter_ = 0;
    void submit(Draw d, Depth depth = Depth::Counted);
    void drawSprite(SpriteObject& s, int alpha, bool mirrored);
    std::vector<Draw> latch() const;
    PresentFrame present_;
    AudioService* audio_;
    SceneKind scene_;
    TouchFilter filter_;
    HostInput input_;
    std::vector<Draw> draws_, shown_; // submissions / last swapped
    std::vector<std::uint32_t> shown3D_;
    Surface lowerText_, upperText_;
    int brightness_ = -16, lowerBrightness_ = -16;
    const Font& font();
    const Font& fontq();
    void glyphs(Surface& layer, const std::string& t, int x, int y, bool q = false);
    int lineWidth(const std::string& line, bool q = false);
    Surface compose();
    void frame(bool swap);
    int shownLowerScroll_ = 0, shownUpperScroll_ = 0, shake_ = 0, pendingShake_ = 0;
    std::uint32_t ownRandom_ = 0;
    unsigned presents_ = 0;
};
} // namespace picarat
