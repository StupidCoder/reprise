#pragma once
// 3D characters of the 3DS adventures (CHRC containers, folders ".xc"):
// skinned meshes with bones, materials and textures. The puzzles' robot
// parts, judge characters and the scene props are drawn from these.
//
// Files of a CHRC folder (RES.bin directories; file numbers count within
// each section type):
//   100 meshes      %03d.prm  XMPR: an XPRM (vertex + index buffer) and the
//                             mesh's bone palette (name hashes)
//   110 bones       %03d.mbn  168 bytes: name hash, parent hash, ...
//   240 textures    %03d.xi   IMGC pictures
//   290 materials   (in RES)  texture hash at word 4 (as ANMC)
//   400 motions     %03d.mtninf (MINF: a named span of a clip)
//   (300)           %03d.mtn2   XMTN clips (bone tracks)
//   140 projections %03d.txp    a material's texture transform
//   410 face motions %03d.imminf / .imm2 (XIMA: texture offsets)
// The bones' layout (42 words): 0 name hash, 1 parent hash (0 for the
// root), 2 a kind (4 seen), 3..5 local translation, 6..14 local rotation
// (3 x 3), 15..17 local scale, 18..29 the inverse bind matrix (mesh space
// to the bone's), 30..41 the bone's bind matrix; matrices are stored by
// columns (three axes, then the translation). For the robot's skinned
// bones bind x inverse bind is one constant transform (a quarter turn about
// z plus a shift), not the identity: the skeleton's rest frame differs from
// the mesh's; motions set the real pose. Without a motion the bones keep
// their rest transforms (words 3..17), composed as every frame (0x2a7418).
//
// Reconstructed from the fifth adventure's files (n001a..d, the robot).
#include "graphics/a56_mesh.hpp"
#include "graphics/ctr3d/raster.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace picarat::adventure56 {

class Pack;

// A 3 x 4 affine matrix: rows of rotation/scale, translation in column 3.
struct Affine {
    std::array<float, 12> m{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};

    std::array<float, 3> apply(const float p[3]) const;
    std::array<float, 3> rotate(const float v[3]) const;
    Affine operator*(const Affine& o) const;
};

struct Mesh {
    std::string name, material;
    // From the mesh's 15-word block (XMPR word 3 gives its offset; copied
    // by the mesh loader 0x175fb8 into the mesh at +0xb0..+0xd0): word 2
    // the technique's name hash (fix.xr section 190, e.g. "#FIX_IMG",
    // "#FIX_TON_12"; 0 none), word 13's low half the passes the mesh is
    // drawn in (+0xcc; the model draw 0x191878 tests it against 1 << pass,
    // and scenes draw pass 0): 1 for ordinary meshes, 2 for the rooms'
    // collision meshes ("_col", "_col01", "col1"; also the rabbit
    // minigame's skin layers), 0 for the puzzles' "_col" maps, 3 seen twice.
    std::uint32_t technique = 0;
    int passes = 1;
    // Word 12: the mesh's layer (a room's sky and buildings 0..3, its
    // foreground "m2xxx" meshes 4..6 in m1010b). A model object draws the
    // layers of its range only (0x2cd8cc's set-up block +0 / +2; see
    // Object::layers).
    int layer = 0;
    // The technique's combiner (fix.xr section 190 word 3), resolved by
    // loadModel; 0 when fix.xr does not list the technique.
    std::uint32_t combiner = 0;
    // The outline (silhouette files %03d.sil "XCSL": a name, the shader
    // #FIX_SIL_MDS, a mesh count (word 2), the width (words 3, 4: the
    // silhouette geometry shader's width.xy), the meshes' name hashes from
    // word 31; then the colour combiner, whose constant is the colour).
    // Width 0: no outline.
    float outlineWidth = 0;
    float outlineColour[3] = {0, 0, 0};

    // Edges for the silhouette (vertices welded by position): the two
    // triangles beside each edge (second -1 for an open edge).
    struct Edge {
        std::uint16_t a, b;
        int left, right;
    };

    std::vector<Edge> edges;
    VertexBuffer vertices;
    std::vector<std::uint16_t> indices;
    std::vector<std::uint32_t> palette; // bone name hashes; vertex bone indices index this
};

struct Bone {
    std::uint32_t hash = 0, parent = 0;
    float translation[3] = {};
    float rotation[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    float scale[3] = {1, 1, 1};
    Affine inverseBind; // words 18..29
    Affine bind;        // words 30..41
};

// A clip of bone animation (XMTN, .mtn2). The file: "XMTN", 0, a size,
// 0x28, the offset of a method-word stream (0x10), the track counts of the
// three groups (0x14 translations, 0x18 rotations, 0x1c scales), the clip
// hash (0x28) and name (0x2c), its last frame (0x54). The stream: offsets
// of the bone list (name hashes; tracks name bones by index into it), of
// channel descriptors and of the track table; the table holds the three
// groups' tracks {header, key frames, values, 0}, a header {u16 bone,
// u16 key count (bits 0..12; 0x2000 a flag)}, key frames u16, values: translations and scales three
// floats, rotations a quaternion (x, y, z, w) of four int16 / 32767.
// Poses are in the model's space (the root's children carry the mesh's
// axes), so a vertex is pose world x inverse bind x position.
struct Clip {
    std::uint32_t hash = 0;
    std::vector<std::uint32_t> bones;

    struct Track {
        int bone = 0;
        std::vector<std::uint16_t> frames;
        std::vector<std::array<float, 4>> values;
    };

    std::vector<Track> translations, rotations, scales;
    int lastFrame = 0;
};

// A named motion (MINF, .mtninf): frames first..last of a clip, played at
// `rate` clip frames per game frame (hypothesis from the robot: 0.5).
struct Motion {
    std::string name;
    std::uint32_t hash = 0, clip = 0;
    int first = 0, last = 0;
    float rate = 1;
    // MINF +0x48: the motion that follows at the end (its name's hash; 0
    // none). The engine queues it when the motion starts (0x2a8014 ->
    // 0x2a8214, layer +0xc) and plays it at the end (0x370730..0x370764).
    std::uint32_t next = 0;
};

// A material's texture transform (.txp): the projection's name hash, the
// material's hash, an offset (words 2, 3), ..., a scale (words 7, 8).
struct TextureProjection {
    std::uint32_t hash = 0, material = 0;
    float offset[2] = {0, 0}, scale[2] = {1, 1};
};

// A face motion (XIMA, .imm2, with its .imminf span): texture offset
// tracks for projections - the eyes and mouth step through the face
// sheet's cells (one cell = 0.125 on the 512-pixel sheet). Stream layout
// as XMTN: target list (projection hashes), descriptors, track table
// {header {u16 target, u16 keys}, frames, values (two floats)}.
struct FaceClip {
    std::uint32_t hash = 0;
    std::vector<std::uint32_t> targets;

    struct Track {
        int target = 0;
        std::vector<std::uint16_t> frames;
        std::vector<std::array<float, 2>> values;
    };

    // The loader (0x1bd7xx) keeps four track lists, counted by the words at
    // 0x14, 0x18, 0x1c and 0x20, their table entries one after the other.
    // Lists 0 and 3 hold texture offsets (list 3: the eyes and mouth; list
    // 0: puzzle 029's cats, white (0, 0) and black (0, -0.5) on a texture
    // with the black cat above the white one - hypothesis: both are
    // translations); lists 1 and 2 are empty in every file of the game.
    std::vector<Track> offsets;
};

// A material motion's clip (XMTM, .mtm2; both adventures, 270 files):
// "XMTM", ..., the stream's offset (0x18; method-word compressed), the
// track count (0x1c), the clip hash (0x24, as the MINF span's) and name
// (0x28). The stream as XIMA's: target list (material name hashes),
// descriptors (always kind 07 02), track table {header {u16 target, u16
// keys (bits 0..12)}, frames, values, 0}; one float per key. Every file
// has this one channel; it animates the materials' alpha (puzzle 018: the
// glyphs' additive glow pulses 0..1; the same per-material value 6838
// sets). Seen on the console at place 1010: room m1010b's "far_01" dips
// three materials to 0.76..0.85 for a frame (the light flicker); the
// value is the material colour's alpha (the shaders' c11.w: 0.7643 on
// m1010b04-tma at the track's key 20).
struct MaterialClip {
    std::uint32_t hash = 0;
    std::vector<std::uint32_t> targets;

    struct Track {
        int target = 0;
        std::vector<std::uint16_t> frames;
        std::vector<float> values;
    };

    std::vector<Track> tracks;
};

MaterialClip parseMaterialClip(std::span<const std::uint8_t> file);

struct Model {
    std::string path; // the pack folder (research listings)
    std::vector<Mesh> meshes;
    std::vector<Bone> bones;
    std::vector<std::shared_ptr<ctr3d::Texture>> textures;

    // A material (section 290): its texture, colours and lighting table
    // (section 220, %03d.mtr "MTRC": word 2 the offset of a compressed
    // parameter block, word 4 that of a "LUTC" table). In the block, word
    // 7 says the material has its own table, words 32..34 are the emission
    // colour, 35..37 and 38..39 (hypothesis: ambient, diffuse; zero on every
    // character seen), 41..43 the specular colour. The game draws the
    // characters with the GPU's fragment lighting (read from the console's
    // command lists): global ambient = emission, the light's specular x
    // the material's, the D0 table enabled (input N.H, signed) exactly when
    // the material has its own table; without one the term is 1 (flat).
    struct Material {
        std::uint32_t hash = 0;
        int texture = -1;
        float emission[3] = {0, 0, 0}, ambient[3] = {0, 0, 0}, diffuse[3] = {0, 0, 0},
              specular[3] = {0, 0, 0};
        std::vector<float> lightTable; // 512 floats (the D0 table), or empty
        // The blend (.atr, "ATRC01", method-word block at 12, word 6 bytes
        // 0..2): equation, source and destination factor; 0xff = the
        // default. The game's factor numbers (from the reference's command
        // lists, puzzle 061): 0 zero, 1 one, 8 source alpha, 9 one minus
        // source alpha; (0, 8, 1) is additive (GPU 0x101 = 76160000).
        // Equation 2 (the PICA's reverse subtract) on "-sub" materials:
        // (2, 8, 1) = destination - source x alpha.
        std::uint8_t blendEquation = 0xff, blendSource = 0xff, blendDest = 0xff;
        // The depth offset (.atr word 3 byte 0 = 1, the amount in word 8;
        // the "-zp0-" decals: the gardens' wall shadows, the Nut Roller's
        // curve shadows) in depth-buffer units of the whole range, toward
        // the eye. The console's draws of garden 1 carry -0.2 / -0.4 x
        // 1000 / 2^24 in the depth map offset (GPU 0x4e: float24 0xae9001,
        // 0xaf9001) and come after the other meshes. *Hypothesis*: the
        // factor 1000 / 2^24 (two values seen).
        float depthOffset = 0;

        // The table at a signed input (-1..1), interpolated.
        float lookup(float x) const;
    };

    std::vector<Material> materials;

    std::vector<Clip> clips;
    std::vector<Motion> motions;
    std::vector<TextureProjection> projections;
    std::vector<FaceClip> faceClips;
    std::vector<Motion> faceMotions; // MINF spans of the face clips
    // Material motions (%03d.mtminf MINF spans of the XMTM .mtm2 clips; 6830).
    std::vector<Motion> materialMotions;
    std::vector<MaterialClip> materialClips;

    // Motion files attached to the model resource (3730, 0x2c9c98: eight
    // slots at +0x384 with fill stamps at +0x570): their motions and clips
    // play on this model's bones (the rabbits' "mini/ra101mot_select").
    std::array<std::shared_ptr<const Model>, 8> motionSlots;
    std::array<std::uint32_t, 8> slotStamps{};
    std::uint32_t slotClock = 0; // +0x56c

    // Attaches a motion file to a slot; with a negative slot the empty slot
    // filled longest ago (none empty: false; already attached: true).
    bool attachMotions(std::shared_ptr<const Model> file, int slot);

    int bone(std::uint32_t hash) const;
    const Material* material(const std::string& name) const;
    // A motion by name: the model's own, else the attached files' in slot
    // order.
    const Motion* motion(const std::string& name) const;
    // By its name's hash (a motion's link, Motion::next).
    const Motion* motionByHash(std::uint32_t hash) const;
    // Every bone's model-space matrix at a frame of a motion (fractional
    // frames interpolate); empty when the motion's clip is missing.
    std::vector<Affine> pose(const Motion& motion, float frame) const;
    // Every bone's model-space matrix from the rest transforms alone: the
    // game composes the skeleton every frame (0x2a7418) and a motion only
    // replaces the locals it animates, so a model without a motion still
    // follows its bones (puzzle 029's dice: bone scale 2).
    std::vector<Affine> restPose() const;
    // Both: the clip's tracks over the rest transforms (none: rest only).
    std::vector<Affine> posed(const Clip* clip, float frame) const;
    // Motion layers (the sixth adventure's 6810 layer operand): each
    // layer's tracks replace only the channels they animate, later layers
    // over earlier ones, all over the rest transforms.
    std::vector<Affine> posedLayers(std::span<const std::pair<const Clip*, float>> layers) const;
    // A motion's clip (the model's own clips, then the attached files').
    const Clip* clipFor(const Motion& motion) const;
    const Motion* faceMotion(const std::string& name) const;
    const Motion* materialMotion(const std::string& name) const;
    // A material's alpha in a material motion at a clip frame (linear
    // between keys: hypothesis, the reference's glows fade smoothly); false
    // when the motion has no track for the material.
    bool materialAlphaAt(const Motion& motion, float frame, std::uint32_t material,
                         float& alpha) const;
    // A material's texture offset at a frame of a face motion (or its
    // projection's own offset without one).
    // `targeted`: set when the face clip has a track for the material's
    // projection.
    std::array<float, 2> textureOffset(std::uint32_t material, const Motion* face, float frame,
                                       bool* targeted = nullptr) const;
};

FaceClip parseFaceClip(std::span<const std::uint8_t> data);

Clip parseClip(std::span<const std::uint8_t> data);
Motion parseMotion(std::span<const std::uint8_t> data);

Mesh parseMesh(std::span<const std::uint8_t> data);
Bone parseBone(std::span<const std::uint8_t> data);
// A "LUTC" lighting table (a material's, or fix.xr's %03d.lut): 512
// floats (values, then differences), or empty when it is not one.
std::vector<float> parseLightTable(std::span<const std::uint8_t> lutc);
// A table at a signed input (-1..1), interpolated (max(0, x) without one).
float lookupLightTable(std::span<const float> table, float x);

std::shared_ptr<Model> loadModel(const Pack& pack, const std::filesystem::path& folder);

} // namespace picarat::adventure56
