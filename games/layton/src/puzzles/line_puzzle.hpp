#pragma once
// Lines between points: the second adventure's puzzle types 9 and 15 (ARM9
// overlay 37: ROM 0xad200 (+0x1ed0 bytes), loaded at 020f78a0, vtable
// 020fa504) and the first adventure's "Cut Puzzle" engine (its ARM9
// 0205c574..0205ed9c, the same engine one version earlier; see Rules).
//
// How the puzzles play
// --------------------
// The picture contains marked points. The player connects them with the
// stylus; the answer is checked when the player presses "Submit".
//
//  * Type 9, "paths" (mode 0): press on a point and drag. The path grows
//    point by point while the stylus moves; it can only run straight from
//    one point to the next (horizontally or vertically, unless the puzzle
//    allows any direction), and never through walls. Paths drawn twice are
//    erased again, so the stylus can also undo.
//  * Type 15, "lines" (mode 1): press on a point, drag to another and
//    release: a straight line is placed between them. Drawing the same line
//    again removes it, and a short tap on a line removes it too. A line that
//    runs along an existing one is merged with it. Puzzles may limit the
//    number of lines; once the limit (or a total length of 1300 pixels) is
//    reached, a "no more lines" notice appears.
//
// The answer is a list of segments between points. It is right when every
// answer segment is fully drawn and nothing else is (every drawn cell lies
// on some answer segment). With a line limit, drawing as many answer
// segments as the limit allows is enough.
//
// Internally the picture area is a grid of small cells (by default 2x2
// pixels, up to 188x188 cells). The script marks each cell as open, part of
// the drawable area, wall or point; drawing counts how many paths cover each
// cell. Script commands arrive through overlay 2 (see line_puzzle.cpp).
//
// The first adventure (Rules::Adventure1) plays the same way. Its version
// differs in details: no 1300-pixel length limit, other sounds (and a drag
// sound for every path puzzle, while the stylus moves), placed lines drawn
// one pixel lower right in a colour per line, path cells on even pixels.
// Three options of its own: a line stops at points in between (only the
// columns are compared), thin one-pixel lines, and junction markers where
// three line ends meet (with the colour picker of puzzle 86, which the
// engine around it handles). The second adventure kept the code for two of
// these options but never turns them on.
#include "system/effect_queue.hpp"
#include "system/interaction.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace picarat {

class LinePuzzle {
public:
    enum class Rules { Adventure1, Adventure2 };

    // One shape for the 3D layer: a filled rectangle (overlay 2, 02070e8c)
    // or a 2x2 quad on a line (02070fcc), in lower-screen pixels.
    struct Primitive {
        bool line = false;
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        std::uint16_t color = 0;
        // First adventure: a one-pixel line polygon from (x0, y0) to (x1, y1)
        // (ARM9 020103c8 with width 1), not a quad.
        bool thin = false;
    };

    // One 2D sprite (overlay 2, 02075e50): a point marker, the "here"
    // highlight under the stylus, or the "no more lines" notice.
    enum class Sprite { Point, Here, NoMoreLines, Junction, Picker };

    struct SpriteDraw {
        Sprite sprite;
        int x = 0, y = 0;
    };

    // A segment between two grid cells.
    struct Segment {
        int x0 = -1, y0 = -1, x1 = -1, y1 = -1;
    };

    // One entry of the first adventure's draw list, in the game's order.
    struct Item {
        bool sprite = false;
        Primitive primitive;
        SpriteDraw draw;
        bool countDepth = false; // a line drawing ends: the depth counter counts once
    };

    // The first adventure's options (Cut Puzzle +0x4fd, +0x4fe, +0x4fc).
    struct Options {
        bool stopAtPoints = false; // a line ends at the first point it passes
        bool thinLines = false;    // one-pixel lines instead of 2x2 quads
        bool junctions = false;    // markers where three or more line ends meet
    };

    // `type` is 9 (paths) or 15 (lines); `number` is the internal puzzle ID,
    // which turns on a drag sound for three puzzles.
    LinePuzzle(std::span<const std::uint8_t> script, unsigned type, unsigned number);

    // An empty engine; the setup calls below (the script's commands) follow,
    // then start().
    explicit LinePuzzle(Rules rules);

    // --- Setup (the script commands; second adventure numbers / first).
    // Kinds of a rectangle of cells, corners included (0x26 / 0x9f).
    void setCells(int x0, int y0, int x1, int y1, int kind);
    void addPoint(int x, int y);      // 0x28 / 0xa0
    void addAnswer(const Segment& s); // 0x29 / 0xa1, at most 32

    void setMaxLines(int n) { maxLines_ = n; } // 0x0d / 0x27

    void setOrigin(int x, int y) { // 0x10 / 0xa3
        originX_ = x;
        originY_ = y;
    }

    void setGridSize(int columns, int rows) { // 0x11 / 0xa4
        width_ = columns;
        height_ = rows;
    }

    void setCellSize(int pixels) { cellWidth_ = cellHeight_ = pixels; } // 0x12 / 0xa5

    void setLineColour(std::uint16_t c) { lineRgb_ = c; } // 0x24 / 0xac

    void setPreviewColour(std::uint16_t c) { previewRgb_ = c; } // 0x25 / 0xad

    void allowAnyDirection() { freeDirection_ = true; } // 0x27 / 0xa2

    Options options; // first adventure (command 0xaa)

    // First adventure: the colours of the line colour indices 0..4 (the
    // text palette entries 0xfc, 0xfb, 0xfa, 0xff, 0xfd) and the index new
    // lines get (+0x4d0; 4 unless the colour picker changes it).
    std::array<std::uint16_t, 5> palette{};

    void setCurrentColour(int index) { currentColor_ = index; }

    // Lists the points and selects the mode (paths or lines); `number` is
    // the internal puzzle ID (sounds).
    void start(bool lines, unsigned number);

    // First adventure 0205cd70: nothing is drawn.
    bool empty() const;

    // First adventure: this frame's drawing in the game's order (0205db78),
    // and the placed lines (0205d9d0), which its engine draws later.
    const std::vector<Item>& drawList() const { return drawList_; }

    const std::vector<Item>& lineList() const { return lineList_; }

    // First adventure: where the picker's selection mark is drawn.
    int pickerX = 200, pickerY = 0x5f;

    // One frame of input (virtual function +1f4, 020fa12c, whose input half is
    // 020f83d0). Rebuilds this frame's shapes and sprites (020f8d9c), as the
    // original does every frame.
    void input(const TouchFilter& touch);

    // Erases everything drawn (virtual function +204, 020f7f04).
    void reset();

    // Drops the active point and the preview (virtual function +20c, 020fa188).
    void cancel();

    // The answer check (virtual function +1fc, 020f8338).
    bool solved();

    // The common puzzle buttons ignore input while the drag sound of puzzles
    // 42, 91 and 160 plays (020fa12c passes !+4f3 to 020f3be0).
    bool commonInput() const { return !loopSound_; }

    // Sound effects. Second adventure: 0xf9 when a line is placed or
    // removed, 0xfd the drag sound loop of three path puzzles. First: 200
    // (0xc3 in puzzle 84) when a line starts or snaps, 0xc9 (0xc4) when one
    // is placed or removed, 0xbe the drag loop of paths.
    EffectQueue sounds;

    // The destructor (020f7b0c) fades the drag loop out over 0.125 s.
    void shutdownSounds() { sounds.stop(0xfd, 0.125f); }

    // Centres the "no more lines" notice for its sprite size (020f78a0, via
    // 02075cc0 / 02075c70).
    void setNoMoreLinesSize(int width, int height);

    const std::vector<Primitive>& primitives() const { return primitives_; }

    const std::vector<SpriteDraw>& sprites() const { return sprites_; }

    int mode() const { return mode_; }

private:
    Rules rules_ = Rules::Adventure2;
    unsigned number_ = 0;
    std::vector<Item> drawList_, lineList_;
    void emit(const Primitive& p);
    void emit(const SpriteDraw& s);
    int snapSound(bool limited) const;
    int placeSound() const;
    static constexpr int Size = 0xbc; // grids are 188x188 bytes, stored column by column
    using Grid = std::vector<std::int8_t>;

    // Cell kinds (grid +0x08).
    static constexpr std::int8_t kOpen = 0;
    static constexpr std::int8_t kArea = 1; // drawable
    static constexpr std::int8_t kWall = 2; // blocks paths and lines
    static constexpr std::int8_t kPoint = 3;
    static constexpr std::int8_t kActive = 4; // the point a path or line starts from

    // Modes (+0x10).
    static constexpr int kPaths = 0; // type 9
    static constexpr int kLines = 1; // type 15

    std::int8_t& at(Grid& g, int x, int y) { return g[std::size_t(x * Size + y)]; }

    // Per-cell grids: +0x00 how many paths cover the cell, +0x04 the line
    // that owns it (lines mode), +0x08 its kind, +0x0c marks left by the
    // answer check.
    Grid coverage_, owner_, cells_, checked_;
    int mode_ = kPaths;                       // +0x10
    std::vector<std::pair<int, int>> points_; // +0x14: all point cells, row by row
    int originX_ = 0, originY_ = 0;           // +0x1c: top-left of the grid in pixels
    int width_ = Size, height_ = Size;        // +0x24: grid size in cells
    int cellWidth_ = 2, cellHeight_ = 2;      // +0x2c: cell size in pixels
    int maxLines_ = 0x80;                     // +0x34: line limit (0x80 = none)
    Segment preview_;                         // +0x38: the segment under the stylus
    bool previewActive_ = false;              // +0x48
    bool freeDirection_ = false;              // +0x49: any direction allowed (command 0x27)
    bool snapped_ = false;                    // +0x4a: the preview ends on a point
    bool noMoreLines_ = false;                // +0x4b: show the notice
    std::vector<Segment> answer_;             // +0x4c: at most 32 (count at +0x24c)
    int touchX_ = -1, touchY_ = -1;           // +0x250
    std::array<Segment, 32> lines_;           // +0x258: placed lines (lines mode)
    std::array<int, 32> lineColor_{};         // +0x458: -1 marks a free slot
    int currentColor_ = 4;                    // +0x4d8
    std::uint16_t lineRgb_ = 0;               // +0x4dc: colour of drawn lines (command 0x24)
    std::uint16_t previewRgb_ = 0;            // +0x4de: colour of the preview (command 0x25)
    bool loopFlag_ = false;                   // +0x4f2: puzzle 42, 91 or 160
    bool loopSound_ = false;                  // +0x4f3: drag loop 0xfd playing

    // Stops the drag loop (020f8f70 / 020f83d0; first adventure: 02005594,
    // every path puzzle).
    void stopLoop() {
        if (rules_ == Rules::Adventure1) {
            if (loopSound_) {
                sounds.stop(0xbe);
            }
            loopSound_ = false;
            return;
        }
        if (loopFlag_ && loopSound_) {
            sounds.stop(0xfd);
        }
        loopSound_ = false;
    }

    int heldFrames_ = 0;            // +0x4f4: frames the stylus has been down
    float lastDistance_ = 0;        // +0x4f8
    int noMoreX_ = 0, noMoreY_ = 0; // position of the "no more lines" notice
    std::vector<Primitive> primitives_;
    std::vector<SpriteDraw> sprites_;

    void markPoints();                          // 020f7c38: every point back to plain
    int toggle(int x0, int y0, int x1, int y1); // 020f7ccc: remove or add a line
    int lineCount() const;                      // 020f7dcc
    // 020f7fe4: whether every cell of a segment is covered (marking it), or,
    // with `stop`, whether the segment avoids the cell `stopAt`.
    bool covered(int x0, int y0, int x1, int y1, bool stop, std::pair<int, int> stopAt);
    bool allMarked() const;                          // 020f82c4
    void draw(bool preview);                         // 020f8d9c
    void drawCells();                                // 020f87c4
    void drawLines();                                // 020f8c44
    float totalLength() const;                       // 020f8bfc
    bool snap(int x, int y);                         // 020f8f70: what the stylus position means
    void setPreview(int x0, int y0, int x1, int y1); // 020f9500
    // 020f9594: walks the cells between two points; draws (preview) or
    // changes coverage and lines.
    bool walk(int x0, int y0, int x1, int y1, bool preview);
    // A line of 2x2 quads (020f8cb4 / overlay 2, 02070fcc; first adventure
    // 020101a4).
    void lineQuads(int x0, int y0, int x1, int y1, std::uint16_t color);
    // A placed or previewed line: quads, or a thin line with that option.
    void line(int x0, int y0, int x1, int y1, std::uint16_t color);

    // First adventure 0200f520 takes half coordinates: rectangles start on
    // even pixels.
    int even(int v) const { return rules_ == Rules::Adventure1 ? v & ~1 : v; }
};

} // namespace picarat
