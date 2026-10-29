#pragma once
// The fourth adventure's pictures, animation packs, fonts, data lists and
// scripts, loaded from its imported pack (see src/import/adventure4_*) and
// decoded once.
//
//  * Pictures (.ci packs: IMG.lt picture + IMG.lp palette): a tiled picture
//    and the rectangles ("parts") animations cut from it.
//  * Animation packs (.ca: AS.lb description, ATnn.lt pictures, APnn.lp
//    palettes): images built from picture parts, composites of images and
//    timed sequences of either, each by name ("touch", "hint_off0").
//  * Fonts (.cf): one bit per pixel glyphs with their metrics.
//  * Data lists (.cb) and scripts (.cq).
//
// File names follow the game's spelling: "@" in a path is the language
// folder ("ge"); a pack (.ci, .ca, .cse) is a folder in the imported pack
// and other files are expanded (.cq -> .lq, .cb -> .lb, .cf -> .lf). The
// game reads main/ (lt4_main_ge.fa) and sub/ (lt4_sub.fa; lt4_seq.fa, the
// copy mounted when the boot's integrity check fails, is not used).
#include "graphics/graphics.hpp"
#include "import/a4_formats.hpp"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace picarat::adventure4 {

// A decoded picture (GTXT with its GPLT palette).
struct Picture {
    unsigned width = 0, height = 0;
    unsigned format = 0; // the GTXT format (0 direct .. 5 A3I5)
    // RGB555 per pixel, bit 15 set where the picture has a pixel (palette
    // index 0 of the paletted formats is empty).
    std::vector<std::uint16_t> pixels;
    // Alpha 0..31 per pixel for the translucent formats (A3I5, A5I3);
    // empty when every shown pixel is opaque.
    std::vector<std::uint8_t> alpha;
    std::vector<Rect> parts; // in pixels
};

// An animation pack's description (AS.lb, run by ov6's handler table at
// 020b6768: 1000 header 020921f8, 1010 image 02092364, 1012 cell
// 020926f8, 1020 composite 0209276c, 1022 its cell 02092ae4, 1030 sequence
// 02092b58, 1032 step 02092f48, 1040 combinations 02092fe0, 1050 palette
// 020932cc, 1051 picture 0209337c).
struct Animation {
    // Bounds as the game stores them: x1, y1, x2, y2.
    struct Box {
        int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    };

    struct Cell {
        int x = 0, y = 0;
        unsigned index = 0; // a part (images) or an image (composites)
    };

    // 1010: an image built from parts of the pack's pictures.
    struct Image {
        std::string name;
        int picture = 0; // which ATnn picture (operand 1)
        Box box;
        std::vector<Cell> cells;
    };

    // 1020: a composite of images.
    struct Composite {
        std::string name;
        Box box;
        std::vector<Cell> cells;
    };

    // 1030: a timed sequence of composites.
    struct Step {
        int dx = 0, dy = 0;
        unsigned index = 0;    // a composite
        unsigned start = 0;    // frames before this step
        unsigned duration = 0; // frames
    };

    struct Sequence {
        std::string name;
        Box box;
        // Operands 5 and 6 (record +0x10/+0x12, 02092b58): a position that
        // 5205 moves objects to and 4761 reads.
        int x = 0, y = 0;
        int next = -1; // operand 7: a sequence to continue with (-1 none)
        unsigned length = 0;
        std::vector<Step> steps;
    };

    struct Combination {
        std::string name;
        std::vector<std::uint8_t> values;
    };

    std::vector<Picture> pictures; // ATnn with their palettes
    std::vector<Image> images;
    std::vector<Composite> composites;
    std::vector<Sequence> sequences;
    std::vector<Combination> combinations;
    std::vector<int> header; // the 1000 command's operands

    const Image* image(const std::string& name) const;
    const Composite* composite(const std::string& name) const;
    const Sequence* sequence(const std::string& name) const;
};

// One glyph of a GFNT font: a one-bit mask and its metrics (ov6 0209afb8).
struct FontGlyph {
    unsigned code = 0;
    unsigned left = 0, advance = 0, width = 0; // x offset, pen advance, mask width
    std::vector<std::uint8_t> mask;            // width x cell height, 0/1
};

struct Font {
    unsigned cellWidth = 0, cellHeight = 0;
    std::map<unsigned, FontGlyph> glyphs;
    const FontGlyph* glyph(unsigned code) const;
};

class Assets {
public:
    explicit Assets(std::filesystem::path root);

    // Paths as the game writes them ("menu/@/nazo_menu.ca").
    const Picture& picture(const std::string& path);
    const Animation& animation(const std::string& path);
    const Font& font(const std::string& path); // "fnt/font_en1.cf"
    import::adventure4::DataList data(const std::string& path);
    // A script by the game's path ("evt/prg/lt4_prog_nazo.cq" reads
    // sub/evt/prg/lt4_prog_nazo.lq; other scripts come from main/).
    std::vector<std::uint8_t> scriptBytes(const std::string& path) const;
    bool exists(const std::string& path) const;
    // The pack file a game path names, or empty when there is none.
    std::filesystem::path file(const std::string& path) const;
    // A file inside a GFSP pack the game names ("dcm/dcm_en.cb" is the
    // pack's folder dcm/dcm_en).
    std::vector<std::uint8_t> packMember(const std::string& pack, const std::string& member) const;
    // A word or zero-terminated string of the game's code (the pack's
    // code/overlay<N>.bin loaded at `load`, or code/arm9.bin for -1).
    std::uint32_t codeWord(int overlay, std::uint32_t load, std::uint32_t address) const;
    std::string codeText(int overlay, std::uint32_t load, std::uint32_t address) const;
    // The whole program file (code/overlay<N>.bin, or arm9.bin for -1).
    const std::vector<std::uint8_t>& code(int overlay) const;

    const std::filesystem::path root;
    std::string language = "ge";
    // The language's number in the game's table (ov6 020b51ec: "jp", "en",
    // "sp", "fr", "it", "ge", "du", "uk", "ko", "ch"); game +0x18.
    int languageIndex() const;

private:
    std::map<std::string, std::unique_ptr<Picture>> pictures_;
    std::map<std::string, std::unique_ptr<Animation>> animations_;
    std::map<std::string, std::unique_ptr<Font>> fonts_;
    std::map<std::string, std::vector<std::uint8_t>> dataFiles_;
    mutable std::map<int, std::vector<std::uint8_t>> code_;
};

// Decodes a GTXT picture with a GPLT palette.
Picture decodePicture(std::span<const std::uint8_t> picture, std::span<const std::uint8_t> palette);
Font decodeFont(std::span<const std::uint8_t> data);

} // namespace picarat::adventure4
