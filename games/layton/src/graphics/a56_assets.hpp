#pragma once
// Game resources of the 3DS adventures read from an imported pack: files by
// the games' own paths, pictures as textures, and the RES containers inside
// packs (2D animation sets, ANMC).
//
// Paths: scripts name resources like "menu/@/nazo_intro" or "nazo/cmn/x";
// '@' stands for the language folder ("ge" on the German cartridges) and
// the resource kind adds the extension (.xa animations, .xi pictures, .xp
// pop-up layers, .xc models). The importer keeps the three archives as
// arc/a, arc/b and arc/ge with packs expanded into folders named like the
// pack file.
//
// RES containers (RES.bin of a pack, a method-word stream): "XXXX00", u16
// string pool / 4, u16 ?, then two directories (u16 offset / 4, u16 count)
// of sections {u16 offset / 4, u16 count, u16 type, u16 entry size}; the
// first directory holds the shared parts (textures 240, materials 290, and
// for models more), the second the kind's own (for ANMC: 100 vertex
// buffers, 200 cells, 300 cell list, 310 composites, 400 keys, 410
// animations). Names are CRC-32 hashes plus offsets into the string pool.
// Reader: 0x29f6b4 (shared part), 0x19b194 (ANMC).
#include "graphics/a56_mesh.hpp"
#include "graphics/ctr3d/raster.hpp"
#include "import/a56_formats.hpp"
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace picarat::adventure56 {

class Pack {
public:
    explicit Pack(std::filesystem::path root, std::string language = "ge");

    const std::filesystem::path& root() const { return root_; }

    const std::string& language() const { return language_; }

    // The file or pack folder for a game path with its extension, or empty.
    std::filesystem::path find(const std::string& path, const std::string& extension) const;
    // A whole file.
    Bytes read(const std::filesystem::path& file) const;

private:
    std::filesystem::path root_;
    std::string language_;
};

// A RES container's sections.
class ResContainer {
public:
    explicit ResContainer(Bytes data);

    struct Section {
        std::size_t offset = 0;
        unsigned count = 0, type = 0, size = 0;
    };

    // The section of a type (either directory), or null.
    const Section* section(unsigned type) const;
    std::uint32_t word(const Section& s, unsigned entry, unsigned index) const;
    float real(const Section& s, unsigned entry, unsigned index) const;
    std::string text(std::uint32_t offset) const; // from the string pool
    std::string magic() const;                    // "ANMC", "POPC", "CHRC" ...

private:
    Bytes data_;
    std::vector<Section> sections_;
    std::size_t strings_ = 0;
};

// A 2D animation set (.xa pack with an ANMC container), or a pop-up's
// layers (.xp, POPC: the same parts with 3D vertices and projective
// texture coordinates; each animation is a layer).
struct AnimationSet {
    bool popup = false;
    std::vector<std::shared_ptr<ctr3d::Texture>> textures; // section 240, files %03d.xi

    struct Material { // section 290
        std::uint32_t hash = 0;
        int texture = -1;
        float uvScale[2] = {1, 1};
    };

    std::vector<Material> materials;
    std::vector<VertexBuffer> buffers; // section 100, files %03d.pvb

    struct Cell { // section 200: a mesh piece, file %03d.pbi
        std::uint32_t hash = 0;
        int material = -1, buffer = -1;
        std::vector<std::uint16_t> indices;
        float box[6] = {};
    };

    std::vector<Cell> cells;
    std::vector<int> cellList; // section 300

    struct Composite { // section 310: cells drawn together
        std::uint32_t hash = 0;
        std::string name;
        // Word 2 of the 36-byte records: the hash the engine's composite
        // lookup sorts by (0x2c54d8, record +0x68), the group's name
        // without the "_NN" suffix ("b3" for b3_00 and b3_01); the
        // 32-byte records have none (then the composite's own hash).
        std::uint32_t key = 0;
        int first = 0, count = 0; // in cellList
        float box[6] = {};
    };

    std::vector<Composite> composites;

    struct Key { // section 400
        int composite = -1;
        unsigned duration = 0; // frames
        float offset[3] = {};
    };

    std::vector<Key> keys;

    struct Animation { // section 410
        std::uint32_t hash = 0;
        std::string name;
        int first = 0, count = 0; // in keys
        std::uint32_t flags = 0;
        float origin[3] = {};
        float box[6] = {};
        unsigned length = 0; // sum of the keys' durations
    };

    std::vector<Animation> animations;

    const Animation* animation(const std::string& name) const;
    int animationIndex(const std::string& name) const;
};

// A texture entry's wrap modes (its word 2).
void applyTextureMode(ctr3d::Texture& t, std::uint32_t mode);

std::shared_ptr<ctr3d::Texture> loadTexture(const Pack& pack, const std::filesystem::path& file);
std::shared_ptr<AnimationSet> loadAnimationSet(const Pack& pack,
                                               const std::filesystem::path& folder);

} // namespace picarat::adventure56
