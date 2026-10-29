#pragma once
// The 3DS adventures' scene: the named resources scripts load (pictures,
// animation sets, texts, fonts, models, pop-up layers) and the named
// objects they create on the two screens, updated and drawn once a frame.
//
// Objects are looked up by the CRC-32 of their name (the engine keeps them
// sorted by hash, 0x2d375c). Each has a screen (0 the upper 400x240, 1 the
// lower 320x240), a priority (higher numbers are drawn later), a kind
// (5002's fourth operand), a visible switch and tweened properties: the
// position (0x34c), an offset added to it for slides (0x37c), scale,
// rotation, alpha (0xac) and colour. Content comes from resources: a
// picture (5430), an animation set with the playing animation (5600 /
// 5622), a text (5900...). 2D content is drawn at position + offset in
// pixels, y down, from the screen's top-left corner.
// Reconstructed from the fifth adventure's services (see
// .analysis/adventure56/docs/scene.md).
#include "graphics/a56_assets.hpp"
#include "graphics/a56_message.hpp"
#include "graphics/a56_model.hpp"
#include "graphics/ctr3d/raster.hpp"
#include "system/a56_tween.hpp"
#include "system/a56_xq.hpp"
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace picarat::adventure56 {

struct TextList; // a56_text.hpp
struct Font;
struct Model;     // a56_model.hpp
struct CameraSet; // a56_camera.hpp
struct CameraClip;

struct Resource {
    enum class Kind : std::uint8_t { Picture, Animation, Text, Font, Model, Popup, Movie };
    Kind kind = Kind::Picture;
    std::string name, path;
    int scope = 0; // the scope depth it was made in (see Scene::openScope)
    // A pop-up's scale (+0x1a8, 3691; 1 at creation): its objects are
    // scaled by it (the pop-up object class's vtable slot 0x34141c returns
    // it as (s, s, s) where other objects return (1, 1, 1)). Scaling about
    // the camera leaves the plain view alone; puzzle 044's orbit camera
    // sees its 0.4 as nearer, larger layers.
    float popScale = 1.0f;
    // 3591 (0x129598): a float at +0x174 of an animation set's resource
    // (puzzles 103 / 140 set 1). Kept; its reader is not found yet.
    float animationValue = 0.0f;
    std::shared_ptr<ctr3d::Texture> picture;
    // 3414: a picture made as a render target (+0x8d7) of a screen; only
    // these take screen captures (5440).
    bool renderTarget = false;
    int blend = 0; // 3430: a picture's blend mode (7: multiply; others alpha)
    std::shared_ptr<AnimationSet> animation;
    std::shared_ptr<TextList> text;
    std::shared_ptr<Font> font;
    std::shared_ptr<Model> model;
    std::shared_ptr<CameraSet> cameraSet;     // 4500
    std::shared_ptr<struct Sky> sky;          // 3302
    std::shared_ptr<class MoviePlayer> movie; // 3802
};

// A screen capture (5440; object content 4, 0x2956a8, its draw 0x295648):
// while the object is drawn and `count` is not 0 (counted down when
// positive; -1 keeps capturing), it copies what its screen shows so far
// (the objects drawn before it) into the picture and sets `state` to 1
// (5441 reads it).
struct Capture {
    std::shared_ptr<ctr3d::Texture> picture;
    int count = 0;
    int state = 0;
};

struct Object {
    std::uint32_t hash = 0;
    std::string name;
    int screen = 0;
    int priority = 0;
    int kind = 0;            // 5002's fourth operand
    std::uint64_t order = 0; // creation order (the update order)
    // The place in its screen's list (0x295120: sorted by priority, an
    // object inserted after its equals; a new priority re-inserts it, 5023
    // -> 0x1c2b1c): ties of priority in drawing, touch and the list walks.
    std::uint64_t listOrder = 0;
    int scope = 0; // the scope depth it was made in (see Scene::openScope)
    // What the object shows (+0x12e, set by the content services; 5301
    // and the services check it): 0 nothing, 3 a picture (5430), 8 a
    // canvas (5500), 9 an animation set (5600), 15 a text (5900); 13 for
    // 5420's rectangle is a placeholder (its code is not read yet).
    int content = 0;
    bool visible = true; // 5005 (with the parents': Scene::shown)
    // 5006 / 6006 (vtable +0x10, 0x177444: bit 0 of +0x118): the touch
    // switch. An object takes touches only while it is shown and switched
    // on, its own flags alone (vtable +0x1c, 0x33dcf8).
    bool touchOn = true;

    bool touchable() const { return visible && touchOn; }

    // The object updates (handler, tweens, animation) only while shown and
    // switched active (5007; vtable +0x20, 0x33dd64: +0x114 and +0x11c).
    bool updating() const { return visible && switch14; }

    // 5008: drawn or not (the scripts' Nazo_ObjDrawOff calls only 5008 with
    // 0; the place cursors are created shown but not drawn). Touch handling
    // ignores it (hypothesis).
    // 5950's first value (sixth adventure 0x1af43c -> 0x24e618 -> 0x256408;
    // -999 keeps it): pixels added to every character's advance. The
    // sixth adventure's puzzle frame uses -1 for the question line outside
    // Japanese (prog_nazo CreateMsgQues).
    int letterSpacing = 0;
    // 5950's second value: the line pitch is the font's height plus this
    // (0x170578: +0x4af = height x scale + +0x4ae + value). Unset: the
    // pitch measured in the puzzles' question window (height - 1).
    std::optional<int> lineSpacing;
    bool drawn = true;
    bool switch14 = true; // 5007 / 6007 (vtable +0x14, +0x11c): the update switch

    // The four switches' saved states per open scope: each flag word
    // (+0x114 shown, +0x118 touch, +0x11c 5007, +0x120 drawn) keeps the
    // states above bit 0, with a counter (+0x124..+0x127). Opening a scope
    // saves them (0x1780f8), closing one restores them (0x177e2c); the
    // keep options (5070 / 5071, unused by the puzzles) are not modelled.
    struct SwitchStack {
        std::uint32_t saved = 0;
        int count = 0;
    };

    SwitchStack visibleStack, touchStack, switch14Stack, drawnStack;

    // A canvas's pen (canvas content +0x1ec mode, +0x1f0 width, +0x200
    // colour; 5511 / 5512 / 5521) and the box around what was drawn
    // (+0x1e4: x0, y0, x1, y1; 5501). Mode 0 draws nothing; 1 draws (the
    // circle puzzles), 16 also records strokes for the handwriting
    // recogniser (not done).
    struct Pen {
        int mode = 0;
        float width = 1;
        std::array<float, 4> colour{0, 0, 0, 1};
        bool down = false;
        float lastX = 0, lastY = 0;
        // Mode 16 (0x391a20): the strokes as (x | 0x80, y) bytes, bit 7
        // cleared on each stroke's last point at pen-up (+0x190, at most
        // 0x3ff points); the recognised character (+0x19c, 5534).
        std::vector<std::uint8_t> strokes;
        std::string recognised;
    } pen;

    struct DrawnBox {
        bool any = false;
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    } drawnBox;

    Tween<3> position, offset;
    Tween<3> scale;
    Tween<1> rotation; // radians, clockwise on the screen (hypothesis)
    Tween<1> alpha;
    Tween<3> colour;
    // 5207 / 5208 (0x200): added to the scale (hypothesis from its use:
    // pulsing balls, the intro digits growing as they fade).
    Tween<2> scaleOffset;
    // 5262 / 5263 (0x3b8) and 5267..5269 (0x3dc): the stereoscopic depth
    // and a depth offset. Values seen: backgrounds 16, windows 25, a drop
    // shadow 64, logos flying in from -500. Kept, not drawn (one image).
    Tween<1> depth, depthOffset;
    // Stepped rotation (0x284; 5235..5239): a turn in `steps` equal parts
    // of a full circle, snapping to the nearest part over `stepFrames`
    // frames; added to `rotation` when drawn.
    Tween<1> stepAngle;
    int steps = 0, stepIndex = 0;
    float stepFrames = 0;

    // A picture (5430), the whole picture or a part, and its blend mode.
    std::shared_ptr<ctr3d::Texture> picture;
    int pictureBlend = 0;

    // 5411..5413: the picture drawn as a list of rectangles instead of
    // whole: each at x, y (from the position) w x h, cut from the picture
    // at u, v (the secrets' video thumbnails from a movie's frame).
    struct PictureCell {
        float x = 0, y = 0, w = 0, h = 0, u = 0, v = 0;
    };

    std::vector<PictureCell> pictureCells;
    bool pictureCellList = false;

    // An animation set and its playing animation.
    std::shared_ptr<AnimationSet> animation;
    int playing = -1;   // animation index
    unsigned frame = 0; // frames into it
    bool looping = true;
    bool finished = false; // a non-looping animation reached its end
    int passes = 0;        // times the playing animation ran through (play record +0x9c; 5610)

    // Text (5900 and the message services): palette colours (fix.xr's
    // 16x16 picture; 1 black, 2 white, 3 the highlight of <CR>...</C>),
    // alignment (5942 centred across, 5943 centred down), a window frame
    // (5900's window animation set: nine cells LT CT RT LC CC RC LB CB RB)
    // of 5911's size, and the text's offset.
    std::shared_ptr<Font> font;
    std::string text;
    // How the text appears (graphics/a56_message.hpp; the text component's
    // timeline). Without one, the whole text shows.
    std::shared_ptr<MessageFlow> message;
    int textColour = 1, textColour2 = 0;
    // 0x177b18 (text component +0x118, as 0x172a60 writes it from the
    // palette): a colour given directly (r, g, b, a) in place of
    // textColour's; a palette colour clears it.
    std::optional<std::array<float, 4>> textRgba;
    bool centreH = false, centreV = false;
    std::shared_ptr<AnimationSet> window;
    float windowSize[2] = {};
    float textOffset[2] = {};
    // 5956: an edge around the glyphs (width 0..8, colour and alpha; the
    // intro's title: 4, black at 0.75).
    float edgeWidth = 0;
    float edgeColour[4] = {0, 0, 0, 1};
    // 5626: a string drawn with the animation set's animations as glyphs
    // ("0".."9"), each `glyphAdvance` apart.
    std::string glyphText;
    float glyphAdvance = 0, glyphLineAdvance = 0;

    // A solid rectangle (5420: x, y, width, height from the position).
    bool rectangle = false;
    float shape[4] = {};
    // An untextured quad list (content 2 built by the play loops, 0x29724c
    // begin / 0x297884 vertex / 0x2b1f30 end): four corners per quad,
    // around the outline, relative to the position; drawn in the object's
    // colour.
    std::vector<std::array<float, 2>> quads;
    // Per quad, a vertex colour multiplied in (0x28fa30: attribute 1 for
    // the vertices that follow); empty for none.
    std::vector<std::array<float, 3>> quadColours;

    // An animation set's content baked into one list of pieces (0x2aad2c
    // begin, 0x2aaad8 add by cell name, 0x188804 end): each piece one of
    // the set's cells at a local position.
    struct Piece {
        int cell = -1;
        float x = 0, y = 0;
        // 3573: the built entry's offset (0x24cfac, the entry list at
        // +0x11c; puzzle 073 shows pieces with (0, 0), hides them with
        // (0, -1000)).
        float dx = 0, dy = 0;
        // 20300 (sixth adventure, 0x1b4404 -> 0x24cfac +0xc): the built
        // entry's rotation (radians; the script gives degrees). The sense
        // (counter-clockwise on screen) is a hypothesis.
        float angle = 0;
    };

    std::vector<Piece> pieces;
    // The sixth adventure's sprite batch (3570, "#FIX_SPR_MRG_POS"): the
    // most pieces 3571 adds (0: no batch).
    int pieceCapacity = 0;

    // The sixth adventure's line set (content 0x12, class 0x1c3538; 5970 /
    // 5971 create it, 0x24eca8 / 0x1c970c): thick segments drawn as quads
    // (0x247638: centred on the segment's middle, as long as the segment
    // plus its width, as wide as the width) with the picture's uv rect.
    // 5972 starts the build (width, start point, picture, uv rect: +0xc4,
    // +0x100, +0xd0, +0xd4..), each 5973 adds a segment from the last point
    // (a positive width first restarts the line at the point: the scripts
    // make loose segments that way), 5974 ends the build. 5976 moves a
    // segment's ends, 5977 colours it (at once: vertex colours); moved
    // segments are drawn where they were at the last update (5988, or
    // every frame once 5986 switches it on, +0xcd).
    struct LineSet {
        bool tiled = false; // 5971 (+0xca): the texture repeats along a segment
        int capacity = 0;
        bool building = false, autoUpdate = false;
        float width = 0;
        float last[3] = {};
        std::shared_ptr<ctr3d::Texture> picture;
        float uv[4] = {0, 0, 0, 0}; // the rect in the picture: x, y, width, height (pixels)

        struct Segment {
            float p[2][3] = {};
            float shown[2][3] = {}; // the ends as of the last update
            float width = 0;
            float colour[4] = {1, 1, 1, 1};
        };

        std::vector<Segment> segments;
    };

    std::shared_ptr<LineSet> lines;
    // 5430 with a screen number: the object shows that (offscreen) screen's
    // image (the verdict's 3D scene, screen 4, on the top screen).
    int screenPicture = -1;
    // The picture's pivot (5430's two extra operands): drawn at -pivot.
    float pivot[2] = {};

    // 5013: drawn relative to a parent object.
    std::uint32_t parent = 0;
    // A 3D child's parent bone (6013's third operand, a name hash; 0: the
    // parent's own transform).
    std::uint32_t parentBone = 0;

    // The object's sixteen values (5002's operands; 5045 / 5046); its
    // handler blocks see them as registers 0..15.
    std::vector<xq::Value> values;

    // Touch rectangles (5051: kind, x, y, width, height relative to the
    // position; at most 16).
    struct Area {
        int kind = 0;
        float x = 0, y = 0, w = 0, h = 0;
    };

    std::vector<Area> areas;
    // Handler blocks (5032 normal/touch, 5033 normal/select/click, 5034
    // normal/slide/rotate) and the running state (0x2cd1d0).
    int handlerMode = 0;     // 1, 2, 3
    std::string handlers[6]; // by state slot: 0 normal, 1, 2, 3, 4, 5
    int handlerState = -1;

    // ---- 3D objects (6xxx services)
    bool is3D = false;

    // 6053: touch boxes in the object's own space (kind, two corners);
    // 3022 picks them with a ray from the screen's camera (0x3527e4).
    // Touch shapes (at most 8; +0x770 count, entries of 0x20 bytes at
    // +0x774: the kind reported by a hit, the type, a radius, two points).
    // Type 0 a sphere (6051, 0x338d88: centre a), 1 a capsule (6052,
    // 0x338df0: a to b), 2 a box (6053, 0x3389b4: corners a, b).
    struct Shape {
        int kind = 0;
        int type = 2;
        float radius = 0;
        float a[3] = {}, b[3] = {};
    };

    std::vector<Shape> shapes;
    Tween<3> rotation3;       // 6222 (radians, x y z)
    Tween<3> rotation3Offset; // 6227
    Tween<3> scale3Offset;    // 6207 (+0x20c; added to the scale: hypothesis)
    // The mesh layers drawn (0x2cd8cc's set-up block +0 / +2; 0xffff,
    // 0xffff for all). A room with a near layer (2121 field 10 = n) shows
    // layers 0..n-1 on "MAP_%04d" and n..63 on "MAP_%04d_NEAR" (12000).
    int layerFirst = 0, layerLast = 0xffff;
    std::shared_ptr<AnimationSet> popup;
    std::uint32_t popupResource = 0;   // the pop-up resource shown (6700)
    std::vector<unsigned> layerFrames; // per pop-up layer
    // 6800: a model; 6810 its motion (by name; it restarts), 6811 the
    // speed (the motion advances speed x its rate clip frames a frame and
    // loops; hypothesis).
    std::shared_ptr<Model> model;
    std::string motion;
    float motionSpeed = 1;
    float motionTime = 0; // clip frames into the motion
    // 6810's flags (as 5622's): 8 plays the motion once and holds its last
    // frame; then the layer stops playing (6813).
    bool motionOnce = false, motionDone = false;
    // 5818 / 6818 (0x1b2fac / 0x1b324c -> 0x1e4784: the layer record's
    // +0x28): a frame where playback halts. Hypothesis from puzzles 012 /
    // 045 (5811 speed, 5812 frame, 5818 the same frame, or 0 after speed
    // -1); a new motion (6810) clears it (hypothesis).
    bool motionStops = false;
    float motionStop = 0;

    // Motion layers 1..3 (the sixth adventure's 6810 layer operand; the
    // fields above are layer 0): each layer's tracks replace only what
    // they animate, over the layers below (puzzle 013: the shopper walks on
    // layer 0, her cart's contents loop on layer 1).
    struct MotionLayer {
        std::string motion;
        float time = 0, speed = 1;
        bool once = false, done = false;
        bool stops = false; // 5818 / 6818 (as motionStop below)
        float stop = 0;
    };

    std::array<MotionLayer, 3> upperMotions;
    // 6878 (sixth adventure): the object whose motions this one's model
    // plays instead of its own (model +0x1fc); 0 for none.
    std::uint32_t motionSource = 0;

    // 6890..6896: a merged object (content 0x10, 0x22499c) draws copies of
    // a model's meshes, each instance with its own position, rotation and
    // scale (records of 0x3c bytes: +0x10, +0x1c, +0x28; +0x38 the mesh).
    struct Merged {
        std::shared_ptr<Model> model;
        bool positionsOnly = false; // 6891's third operand: only T per copy
        bool built = false;         // 6893
        std::size_t added = 0;      // 6892 (+0x96)

        struct Instance {
            float position[3] = {0, 0, 0}, rotation[3] = {0, 0, 0}, scale[3] = {1, 1, 1};
            int mesh = 0;
        };

        std::vector<Instance> instances; // 6891's count
        Affine matrix(std::size_t i) const;
    };

    std::shared_ptr<Merged> merged;
    // 6875 (0x15d154 -> 0x18e6b8, model content +0x1e4): multiplies the
    // materials' alpha (0x192d30: material colour x object colour, alpha
    // also x this); see-through only on #FIX_IMG meshes (Scene::draw3D).
    float materialAlpha = 1;

    // 6820: face motions (texture offsets for the eyes and mouth) on three
    // layers (6820's fourth operand; the characters' 6850 uses layers 1 and
    // 2: "1nn_" and "2nn_" motions). Each layer's clip moves the
    // projections it has tracks for.
    struct FaceLayer {
        std::string motion;
        float time = 0;
        // 6821 (sixth adventure 0x198bbc -> 0x1e43e4: the layer's +0x3c):
        // frames of the clip per frame, times the motion's own rate.
        float speed = 1;
    };

    std::array<FaceLayer, 3> faces;
    // 6841: a character's lip flag (its talking mouth; content +0x770 bit
    // 3).
    bool lipSync = false;
    // 5431: a picture object's shader (0 image, 1 frame, 2 sepia).
    int pictureShader = 0;
    // 6865 (sixth adventure 0x1ad3d8 -> 0x1239dc): meshes switched off by
    // their name's hash (the mesh record's byte +0xa6); the intro's number
    // model holds every digit as its own mesh ("int03_output.a0".."c9").
    std::set<std::uint32_t> hiddenMeshes;
    // 20400 / 20401 (sixth adventure; 0x1abc40 / 0x1a7b84): a value per mesh
    // (by its name's hash; the mesh record's +0x30 controller, value +0x48),
    // tweened or set. Hypothesis: the mesh's alpha (puzzle 167 fades
    // "m0040-" between 0 and 1 over 30 frames); multiplied into the mesh.
    std::map<std::uint32_t, Tween<1>> meshAlpha;
    // 5440: the object captures its screen (content 4).
    std::shared_ptr<Capture> capture;
    // 6838 (sixth adventure 0x1990e0 -> 0x1dfba8): a value per material
    // (by its name's hash; the model's material records at +0x1c0, word
    // +0x14). Hypothesis: the material's alpha (puzzle 018 fades it over 20
    // frames); multiplied into the material's meshes.
    std::map<std::uint32_t, float> materialValues;

    // A mesh drawn with another texture (by the mesh name's hash, as
    // meshAlpha; the sixth adventure's ChangeTexture class 0x339070): its
    // texture coordinates mapped to uv x scale + offset.
    struct MeshTexture {
        std::shared_ptr<ctr3d::Texture> texture;
        float scale[2] = {1, 1};
        float offset[2] = {0, 0};
    };

    std::map<std::uint32_t, MeshTexture> meshTextures;
    // 6830: the material motion by name (XMTM: per material an alpha track,
    // the shaders' c11.w on the console), its time on the span and whether a
    // played-once one ended (6833).
    std::string materialMotion;
    float materialMotionTime = 0;
    float materialMotionSpeed = 1; // 6831 (the layer's +0x3c)
    bool materialMotionOnce = false, materialMotionDone = false;

    Object();
};

// The screens' object lists' order (0x295120): by priority, then by
// insertion (a new priority re-inserts an object after its equals).
inline bool screenListBefore(const Object* a, const Object* b) {
    return a->priority != b->priority ? a->priority < b->priority : a->listOrder < b->listOrder;
}

// A 3D object (6002; looked up in their own list, 0x2d1a40): a pop-up
// (6700) or a model (6800) placed in the screen's camera space.
// 3D objects (6002; their own list, 0x2d1a40) are Objects too: the same
// components (values, handlers, animations, tweens) with a 3D orientation,
// pop-up layers, a model and its motions. Kept as an alias.
using Object3D = Object;

// A screen's camera (0x4190 + 0x6c: one per screen). Recovered from the
// reference's RAM: a perspective projection with a vertical field of view
// of 0.4654 rad (cot 4.2193), near 10, far 10250; the eye at the origin
// looking down -z, y up. 4200 / 4220 set stereo parameters (0x4b0, 0x540).
struct Camera {
    float fovY = 0.4654f, nearZ = 10.0f, farZ = 10250.0f;
    // Eye, target and field of view: base plus offset each (see the 41xx
    // services).
    Tween<3> eye, eyeOffset, target, targetOffset;
    Tween<1> fov, fovOffset;
    Camera();
    Tween<1> stereoDistance, stereoStrength;
    float stereoDistanceOffset = 0; // 4205
    float stereoExtra[3] = {};      // 4210 (fifth adventure), 4215, 4225
    // The sixth adventure's view keeps these values as tweened slots of 36
    // bytes (set 0x2f0b84 / 0x2f0be0 / 0x2efdf4 = vtable +0xbc / +0xd4 /
    // +0xe0): +0x4d8 4230, +0x4fc 4200 (stereoDistance), +0x568 4210,
    // +0x58c 4220 (stereoStrength), +0x5b0 4215, +0x5d4 4225; 4201 / 4211
    // / 4221 read them, 4202 / 4212 / 4222 / 4232 tween them.
    Tween<1> stereoNear, stereoBase; // 4230, 4210 (sixth adventure)
    // The view's switches, bit 0 of +0x2dc / +0x2e0 / +0x2e4 (4005 / 4006 /
    // 4007, and 3010 / 3011 / 3012 for every screen): shown, touch, active.
    // 4022 reads the third. Kept; their effect on drawing is not modelled.
    bool viewShown = true, viewTouch = true, viewActive = true;
    // 4300 / 4302 (+0x570): a two-component value set or tweened together
    // with the robot's falls (2 -> 0 over 90 frames, 0.5 -> 0 over 30).
    // Hypothesis: a shake strength; kept, not drawn (its reader is not
    // found yet).
    Tween<2> shake;
    // This frame's shake offset (0x221944, +0x338; Game::cameraJitter).
    std::array<float, 2> jitter{};
};

// A sky (folders ".xk": SKY.bin, FOG.bin, SUB.bin and %03d.lgt lights).
// Only the lights are used: LGT holds a count, then per light colours and
// a direction (the verdict's s000: one white light from (-0.5, 0.71, 0.5)).
struct Sky {
    struct Light {
        float ambient[3] = {0, 0, 0}, diffuse[3] = {1, 1, 1}, specular[3] = {0, 0, 0};
        float direction[3] = {0, 1, 0}; // towards the light (world space)
    };

    std::vector<Light> lights;
};

// A camera object (4002 / 4003) with its motions (4500) and the playing
// clip (4602; 4600 reports the end).
struct CameraRig {
    // Near and far by the object's kind (4002 / 4003's second operand; the
    // factory 0x202c54 builds a class per kind, whose setup (vtable slot 7)
    // stores them at +0x330 / +0x32c): kinds 0-2 (0x1aed78, 0x1c2514,
    // 0x1c1804) 10 and 10250, kind 4 (0x1aeea4, the verdict's camera) 1
    // and 10241; kinds 3 and 5 set theirs elsewhere (kept at 10 / 10250;
    // not read yet). The console's verdict projection (GPU uniforms c0-c3)
    // confirms kind 4's.
    int kind = 0;
    int scope = 0; // the scope depth it was made in (see Scene::openScope)
    float nearZ = 10.0f, farZ = 10250.0f;
    // The camera's own values (eye, target, field of view ...): the 41xx
    // services address a screen's current camera object (0x286c28: game
    // +0x4190 +0x6c + 4 x screen, vtable +0x58 ...), so a screen showing
    // another camera (4010) shows that one's values. Kept here while the
    // rig is not on a screen (Scene::cameras holds the shown ones).
    Camera values;
    std::shared_ptr<CameraSet> set;
    const CameraClip* clip = nullptr;
    float time = 0;
    bool finished = true;
    // This frame's shake offset (0x221944; Game::cameraJitter).
    std::array<float, 2> jitter{};
};

class Scene {
public:
    explicit Scene(const Pack& pack);

    const Pack& pack() const { return pack_; }

    // ---- resources (3402 pictures, 3502 animations, 3202 texts ...)
    Resource* resource(std::uint32_t hash);
    // The resource of a kind by its name's hash: the game keeps one list
    // per kind (texts 0x2244, pictures 0x23b4, animation sets 0x246c, pop-ups
    // ...), so a pop-up and an animation set may share a name (the sixth
    // adventure's puzzle 013 loads both as "bg_a"). Null when none.
    Resource* resource(std::uint32_t hash, Resource::Kind kind);
    const Resource* resource(std::uint32_t hash, Resource::Kind kind) const;
    Resource* resource(const std::string& name);
    // `level`: the scope level it belongs to (-1: the current depth;
    // 0x3509b8: 5003 / 6003 and the game's own maps use 0, which no scope
    // close removes).
    Resource& addResource(const std::string& name, Resource resource, int level = -1);
    void removeResource(std::uint32_t hash);
    void removeResource(std::uint32_t hash, Resource::Kind kind);

    // ---- scopes (2190 / 2191: the game's vtable +0xd0 0x32f8c4 and
    // +0xd4 0x32f784). Everything made - objects, 3D objects, resources,
    // cameras - is tagged with the depth current at its creation (vtable
    // +0xdc 0x3509b8: the depth at +0x10bc; an override through +0x1dc0
    // is not modelled). Opening a scope (at most 32 deep) with a snapshot
    // (0x2d0664) keeps each screen's camera and sky and the screens'
    // switches; closing one (above the floor) deletes everything of that
    // depth from every list (0x32d338), restores the snapshot (0x2d27e0)
    // and steps back. The intro's panel, pictures and objects live in
    // IntroMain's scope and are gone before the verdict loads its own
    // "pica_bg" (console RAM after the intro: none of their names left).
    void openScope(bool snapshot);
    void closeScope();

    int scopeDepth() const { return scopeDepth_; }

    // ---- objects
    Object* object(std::uint32_t hash);
    Object* object(const std::string& name);
    // 5002: a new object (an existing one of that name is replaced).
    Object& create(const std::string& name, int screen, int priority, int kind, int level = -1);
    void remove(std::uint32_t hash);
    void removeAll();

    // ---- 3D objects and cameras
    Object3D* object3D(std::uint32_t hash);
    // A 3D object's transform in the world (its parents' over its own).
    Affine worldMatrix(const Object& o, float extra, bool scaled) const;
    // The model's bone matrices for the object's motion layers (empty when
    // layer 0's motion has no clip and no upper layer plays).
    std::vector<Affine> posedModel(const Object& o, const Model& model) const;
    // 0x294ce8: a 3D object leaves its parent keeping its place in the
    // world - the world matrix split into position, rotation (X, Y, Z as
    // the local matrix composes them) and scale.
    void detach3D(Object& o);
    Object3D& create3D(const std::string& name, int screen, int priority, int kind, int level = -1);
    void remove3D(std::uint32_t hash);
    // Screens: 0 top, 1 bottom, 2..4 offscreen images (3030 switches them
    // on; 5430 shows one on an object).
    static constexpr int kScreens = 5;
    Camera cameras[kScreens];
    std::map<std::uint32_t, CameraRig> rigs; // camera objects by name hash
    std::uint32_t screenRig[kScreens] = {};  // 4010: the camera object a screen uses
    // The screens' own built-in cameras' values while a rig is shown.
    Camera screenDefault[kScreens];
    bool screenDefaultKept[kScreens] = {};
    bool screenEnabled[kScreens] = {true, true, false, false, false};
    // 3051 (0x12abf0 -> 0x2cedcc, scene +0x63ec bits 2 / 3-4): the top or
    // bottom screen draws into a target of its own (0x2d381c), so an
    // object showing that screen (5430 obj 0 / 1) shows its last frame:
    // the horse ride's speed blur (30% of it, scaled 1.02, over the next).
    bool screenKept[2] = {};
    std::shared_ptr<Sky> screenSky[kScreens]; // 3310

    // 3022: the last pick (0x350a74): the object hit and where.
    struct Pick {
        bool hit = false, solid = false;
        std::uint32_t hash = 0;
        float point[3] = {};
        int kind = 0;
    };

    Pick pick;
    // A ray through a screen pixel against the 3D objects' touch boxes;
    // the nearest hit.
    Pick pickAt(int screen, float x, float y) const;
    // 0x27233c: whether a screen point touches a 2D object, as the area
    // kind (> 0) or 0. The point is taken into the object's own space
    // (its inverse transform, parents included). With touch areas
    // (5051) the later areas are tested first; without, the content
    // decides: an animation (0x340064) is touched where one of the
    // current frame's cells has a triangle under the point, a picture or
    // rectangle over its box (kind 1).
    int hitTest(const Object& o, float x, float y) const;
    // A line set's segment under a screen point (sixth adventure 0x1c912c
    // -> 0x36da90, 5980 and the touch test): in the object's own space, the
    // nearest segment within half its width, segments with all four
    // alphas 0 skipped; -1 for none (reconstructed: the original measures
    // the point against each segment's quad).
    int lineSegmentAt(const Object& o, float x, float y) const;
    // A screen point in an object's own space (its inverse transform,
    // parents included).
    std::array<float, 2> toLocal(const Object& o, float x, float y) const;

    // A ray through a screen point against a 3D object's collision meshes
    // (pass mask 2; skinned to its pose): the nearest hit with its point
    // and the triangle's normal (world space) and the mesh's material name
    // (the rabbit's touch test, 0x22c05c).
    struct MeshHit {
        bool hit = false;
        float distance = 0;
        std::array<float, 3> point{}, normal{};
        std::string material;
    };

    MeshHit meshPick(const Object& o, int screen, float x, float y) const;
    // 3021: the topmost 2D object of a screen under a point.
    Pick pick2DAt(int screen, float x, float y, std::uint32_t skip) const;

    // A screen's view: the camera object's clip, else the default.
    struct View {
        float eye[3] = {0, 0, 0}, forward[3] = {0, 0, -1}, right[3] = {1, 0, 0}, up[3] = {0, 1, 0};
        float fovY = 0.4654f;
    };

    View viewFor(int screen) const;
    static float rayShape(const Object::Shape& shape, const std::array<float, 3>& origin,
                          const std::array<float, 3>& dir);

    const std::map<std::uint32_t, std::unique_ptr<Object>>& objects() const { return objects_; }

    // The 2D or the 3D list (the 5xxx and 6xxx services address one each).
    const std::map<std::uint32_t, std::unique_ptr<Object>>& objects(bool solid) const {
        return solid ? objects3D_ : objects_;
    }

    Object* find(bool solid, std::uint32_t hash) { return solid ? object3D(hash) : object(hash); }

    // 5023 (0x1c2b1c): a 2D object's new priority; unparented objects go
    // after their equals in the screen's list.
    void setPriority(Object& o, int priority);
    // A screen's 2D objects in its list's order (0x3461f0 walks it).
    std::vector<Object*> screenList(int screen) const;

    // One frame: tweens and animations advance (updateShared, then every
    // updating object in updateOrder; the game interleaves the objects'
    // handlers itself).
    void update();
    // Display frames per game frame (the scene's step, 0x378900): model,
    // face and material motions advance their rate times this a frame.
    float frameStep = 1;
    void updateShared(); // brightness, cameras, camera clips
    // One object's frame (0x37b0cc / 0x37b438 per object after its
    // handler): its tweens, motion and animation.
    void updateObject(Object& o);
    // The 2D or 3D objects in the order they were made (the lists the
    // console walks).
    std::vector<Object*> updateOrder(bool solid) const;
    // Draws both screens; `brightness` per screen (-1 black .. 0 normal
    // .. 1 white, 2501 / 2502).
    void render(ctr3d::Target& top, ctr3d::Target& bottom) const;
    Tween<1> brightness[2];

    // The text colour palette (fix.xr/000.xi).
    std::shared_ptr<ctr3d::Texture> palette;
    std::array<float, 4> paletteColour(int index) const;
    // A text's size (5960): the longest line and the lines' height.
    std::array<float, 2> measure(const Object& o) const;

    // The object's position on its screen with its parents' added.
    std::array<float, 2> screenPosition(const Object& o) const;

    // Where an object is drawn: its position, scale and rotation combined
    // with its parents' (a parent's turn and scale carry its children
    // along: the intro's digits hang off rotated reference objects).
    struct Transform {
        float x = 0, y = 0, sx = 1, sy = 1, angle = 0;
    };

    Transform screenTransform(const Object& o) const;
    bool shown(const Object& o) const; // visible, and all its parents

private:
    void draw(ctr3d::Target& target, const Object& o) const;
    void renderScreen(ctr3d::Target& target, int screen) const;
    // Which meshes draw3D draws (renderScreen's two passes).
    enum class MeshPass { All, Normal, Decals, Additive };
    static MeshPass meshPassOf(const Model::Material* m);
    mutable MeshPass meshPass_ = MeshPass::All;
    mutable ctr3d::Target offscreen_[kScreens];
    mutable ctr3d::Texture offscreenTexture_[kScreens];
    mutable ctr3d::Texture keptTexture_[2]; // the last frame of a kept screen
    void drawText(ctr3d::Target& target, const Object& o, float x, float y,
                  const ctr3d::DrawState& base) const;
    void drawWindow(ctr3d::Target& target, const AnimationSet& set, float x, float y, float w,
                    float h, const ctr3d::DrawState& base) const;
    void draw3D(ctr3d::Target& target, const Object3D& o, int screen, bool flat = false) const;
    std::map<std::uint32_t, std::unique_ptr<Object>> objects3D_;
    const Pack& pack_;
    std::map<std::uint32_t, Resource> resources_;
    // Resources whose name is taken in resources_ by another kind.
    std::map<std::pair<int, std::uint32_t>, Resource> otherKinds_;
    std::map<std::uint32_t, std::unique_ptr<Object>> objects_;
    std::uint64_t created_ = 0;

    struct ScopeMark {
        bool snapshot = false;
        std::uint32_t screenRig[kScreens] = {};
        std::shared_ptr<Sky> screenSky[kScreens];
        bool screenEnabled[kScreens] = {};
        bool screenKept[2] = {};
    };

    std::vector<ScopeMark> scopes_;
    int scopeDepth_ = 0;
};

} // namespace picarat::adventure56
