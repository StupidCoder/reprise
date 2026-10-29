#pragma once
// The third adventure's pictures, animations, fonts and screen positions,
// loaded from its imported pack (see src/import/adventure3_*) and decoded
// once.
//
//  * Pictures (LIMG): a tiled picture, optionally cut into parts that the
//    animations place.
//  * Animations (ani.lbin, an LSCR command list next to its pictures):
//    frames built from picture parts, and named sequences of frames with a
//    duration, a position and a touch box per sequence. The game's sprite
//    objects and buttons show a sequence by name ("hint_off", "hint_on").
//  * Fonts: NFTR, decoded by the shared decoder.
//  * Positions (rc/ge/lt3_rc/lt3_pos): named screen positions the scripts
//    give objects, on one 256x384 plane (the upper screen above the lower).
//
// File names follow the game's spelling: "?" in a path is the language
// folder ("ge"), and the game's compressed names (".cimg", ".cani", ".cbin")
// map to the pack's expanded files (".limg", a folder, ".lbin").
#include "graphics/graphics.hpp"
#include "import/a3_formats.hpp"
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace picarat::adventure3 {

struct Picture {
    Surface image;
    std::vector<Rect> parts; // in pixels
    // Translucent pictures (format 2, A3I5): each pixel's alpha 0..31;
    // empty when every shown pixel is opaque.
    std::vector<std::uint8_t> alpha;

    // 02049c14: whether the 8x8 tile holding (x, y) has any pixel.
    bool tileUsed(int x, int y) const;
};

struct Animation {
    struct Layer {
        unsigned picture = 0, part = 0;
        int x = 0, y = 0;
        unsigned flags = 0;
    };

    struct Frame {
        std::string name;
        Rect bounds;
        std::vector<Layer> layers;
    };

    struct Step {
        unsigned frame = 0, duration = 0;
        int x = 0, y = 0;
        unsigned flags = 0;
    };

    struct Sequence {
        std::string name;
        int originX = 0, originY = 0;
        Rect box; // touch box, relative to the object's anchor plus the origin
        std::vector<Step> steps;
    };

    std::vector<Picture> pictures;
    std::vector<Frame> frames;
    std::vector<Sequence> sequences;

    const Sequence* sequence(const std::string& name) const;
    // Draws one frame with its top-left at (x, y) of `image`.
    void drawFrame(Surface& image, unsigned frame, int x, int y) const;
    // Whether (x, y), relative to the frame's top-left, falls on a part of
    // the frame over a non-empty 8x8 tile of its picture (0204dd14).
    bool touches(unsigned frame, int x, int y) const;
};

struct Position {
    int x = 0, y = 0;
    int layer = 0;
    int mirror = 0; // the object's mirror flags (+0x164)
};

class Assets {
public:
    explicit Assets(std::filesystem::path root);

    // Paths as the game writes them ("nazo/?/bg_qt1.cimg").
    const Picture& picture(const std::string& path);
    const Animation& animation(const std::string& path);
    const Font& font(const std::string& name); // "font_en1"
    std::optional<Position> position(const std::string& name);

    // An LSCR file of the pack ("rc/nazo/naz_sc0/n001.lbin" or the game's
    // ".cbin" spelling).
    import::adventure3::Lscr script(const std::string& path) const;
    bool exists(const std::string& path) const;
    // A zero-terminated string in the game's code (the pack's
    // code/overlay<N>.bin loaded at `load`), such as the names of game
    // variables a routine sets.
    std::string codeText(int overlay, std::uint32_t load, std::uint32_t address) const;
    // A little-endian word there (such as a pointer in a table of names).
    std::uint32_t codeWord(int overlay, std::uint32_t load, std::uint32_t address) const;
    std::filesystem::path file(const std::string& path) const;

    const std::filesystem::path root;

private:
    std::map<std::string, Picture> pictures_;
    std::map<std::string, Animation> animations_;
    std::map<std::string, Font> fonts_;
    std::map<std::string, Position> positions_;
    bool positionsLoaded_ = false;
};

// A picture as an RGB555 surface (bit 15 = covered); in the A3I5 format,
// pixels with any alpha count as covered.
Picture decodePicture(std::span<const std::uint8_t> data);

} // namespace picarat::adventure3
