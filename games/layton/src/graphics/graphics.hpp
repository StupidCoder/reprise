#pragma once
// Images, sprites and fonts as the game uses them, and the 2D drawing
// operations the scenes build their screens from.
//
// The DS shows two 256x192 screens. Scenes draw both into one 256x384
// Surface: the upper screen in rows 0..191, the lower (touch) screen below.
// Colours are the DS's 15-bit RGB555; bit 15 marks a pixel as covered
// (opaque) rather than being part of the colour.
//
// GraphicsAssets loads the imported pack (see src/import/): backgrounds
// (.l2bg), sprites (.l2sp) and fonts (.l2ft), each decoded once and cached.
#include "system/interaction.hpp"
#include "system/resources.hpp"
#include <map>

namespace picarat {
// A picture: width x height RGB555 pixels.
struct Surface {
    unsigned width = 0, height = 0;
    std::vector<std::uint16_t> pixels; // RGB555; bit 15 is coverage, not colour.
};

// A sprite: its images, named animations over them, and named "variables"
// (small tables of numbers the game uses for positions, spacing and so on).
// A sprite may name a child sprite drawn attached to it.
struct Sprite {
    struct Attachment {
        std::int16_t x = 0, y = 0;
        unsigned animation = 0;
    };

    std::vector<Surface> images;
    std::vector<std::vector<Rect>> chunks; // Original texture-quad bounds, needed at rotated seams.
    std::vector<std::vector<Rect>>
        quads; // Every frame's texture chunks (x, y, stored size), for 02076124.
    std::vector<std::vector<AnimationStep>> animations;
    std::map<std::string, unsigned> animationNames;
    std::map<std::string, std::array<std::int16_t, 8>> variables;
    std::string child;
    std::vector<Attachment> attachments;
};

// One character of a font: an alpha mask and its metrics.
struct Glyph {
    unsigned width, height;
    int left, advance;
    Bytes alpha;
};

// A font: glyphs by character code and, for two fonts, the separate width
// table the game uses to lay out text.
struct Font {
    std::map<unsigned, Glyph> glyphs;
    Bytes textWidths;
};

// Loads and caches the pack's backgrounds, sprites and fonts.
class GraphicsAssets {
public:
    explicit GraphicsAssets(std::filesystem::path path) : root(std::move(path)) {}

    const Surface& background(const std::string& name);

    // Tile 0 of a background (8x8; null for .arb pictures): what the BG's
    // 256x256 map shows outside the picture.
    const Surface* backgroundTile(const std::string& name) {
        background(name);
        auto it = backgroundTiles_.find(name);
        return it == backgroundTiles_.end() ? nullptr : &it->second;
    }

    const Sprite& sprite(const std::string& name);
    const Font& font(const std::string& name);
    std::array<Bytes, 4> keyboard() const;
    std::filesystem::path root;

private:
    std::map<std::string, Surface> backgrounds_, backgroundTiles_;
    std::map<std::string, Sprite> sprites_;
    std::map<std::string, Font> fonts_;
};

// Draws `source` at (x, y), skipping uncovered pixels; the weights (out of
// 16) blend it with what is underneath.
void blit(Surface& destination, const Surface& source, int x, int y, int foregroundWeight = 16,
          int backgroundWeight = 0);
// Narrow native service for the narration's axis-aligned, nonoverlapping 3D
// sprite polygons over a 2D background. Not a general DS polygon renderer.
void blitNarrationPolygon(Surface& destination, const Surface& source, int x, int y, int alpha);
// One untextured translucent 3D-layer pixel (vertex colour RGB555, alpha
// 1..31) blended over the 2D layer beneath, as calibrated against ROM captures.
std::uint16_t blend3D(std::uint16_t color, int alpha, std::uint16_t behind);
// A sprite turned about its middle (ov2 0206ecfc), drawn chunk by chunk
// through the DS pipeline (graphics/ds_pipeline.hpp): chunks are the
// frame's texture chunks (x, y, stored size); none means one chunk.
void blitRotated(Surface& destination, const Surface& source, int centerX, int centerY,
                 float degrees, int clipTop = 0, std::span<const Rect> chunks = {});
// The same with the original u16 angle (65536 per turn), as 02076214 passes it.
void blitRotatedTurns(Surface& destination, const Surface& source, int centerX, int centerY,
                      unsigned turns, int clipTop = 0, std::span<const Rect> chunks = {});
void drawGlyph(Surface& destination, const Font& font, unsigned character, int x, int y);
// The DS master brightness: -16 (black) .. 0 .. +16 (white), separately for
// the upper and lower screen.
void applyBrightness(Surface& image, int top, int bottom);
Bytes rgba(const Surface& image);
void writePPM(const Surface& image, const std::filesystem::path& path);
} // namespace picarat
