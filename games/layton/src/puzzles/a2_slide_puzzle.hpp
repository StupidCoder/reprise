#pragma once
// Sliding blocks (puzzle type 25, class lt2qSlide2Object).
// ARM9 overlay 47: ROM 0xb6a00 (+0x1178 bytes), loaded at 020f78a0, vtable 020f8fc4.
//
// How the puzzle plays
// --------------------
// Blocks of various shapes lie in a walled box. The player drags a block
// with the stylus; it slides freely but cannot pass through walls or other
// blocks, gliding along them when pushed diagonally. On release it snaps to
// the nearest cell. The goal is to move particular blocks to particular
// cells. As soon as every goal is met the puzzle is solved and judged 45
// frames later; there is no
// "Submit" button (virtual function +220 returns 0). The game counts moves.
//
// The parameter script describes everything (commands 0x4e..0x53, handled
// by overlay 2 at 0207a428..0207a6b4 and passed to virtual +194..+1a8): the
// board grid, wall rectangles, goals, up to four block sprites, and the
// blocks, each a list of cell rectangles.
//
// Collisions are tested in pixels: every block and wall is a set of pixel
// rectangles (inclusive bounds) derived from its cells.
#include "system/effect_queue.hpp"
#include "system/interaction.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace picarat {

class SlidePuzzle {
public:
    // A rectangle of cells.
    struct Rect {
        std::int16_t x = 0, y = 0, width = 0, height = 0;
    };

    // A block (0xbc-byte record at +0x164).
    struct Piece {
        std::vector<Rect> rects;                 // +0x00: its cells, relative (at most 16)
        std::int16_t sprite = 0;                 // +0x82: index into sprites()
        std::string held, idle;                  // +0x84 / +0x94: animation names
        std::int16_t initialX = 0, initialY = 0; // +0xa4: starting cell
        float displayX = 0, displayY = 0;        // +0xa8: where it is drawn (eased)
        std::int16_t cellX = 0, cellY = 0;       // +0xb0: its cell (nearest while dragged)
        std::int16_t pixelX = 0, pixelY = 0;     // +0xb4: its logical pixel position
        std::int16_t grabX = 0, grabY = 0;       // +0xb8: its cell when it was picked up
    };

    // Block `piece` must end at cell (x, y).
    struct Goal {
        std::int16_t piece = 0, x = 0, y = 0;
    };

    // `internalId` selects one of two "solved" sounds.
    explicit SlidePuzzle(std::span<const std::uint8_t> script, unsigned internalId = 0);

    // Sound effects: 0x109 loops while any block is gliding and fades out
    // over 0.1 s when all stop (020f8a78); 0x10b when solved (0x10a for the
    // puzzles with internal IDs 0x15, 0x76 and 0x98; 020f8734).
    EffectQueue sounds;

    // All blocks back to their starting cells (virtual function +204, 020f7b30).
    void reset();

    // One frame of input (020f8734).
    void update(const TouchFilter& touch);

    // Eases the drawn positions towards the logical ones (020f8a78); the
    // puzzle scene draws the blocks.
    void animate();

    // Solved and 45 more frames have passed (virtual function +1fc, 020f7bec).
    // Also stops the sliding sound.
    bool complete();

    // The common puzzle buttons stop working once solved (020f7c5c passes 0
    // to 020f3be0).
    bool commonInput() const { return state_ != kSolved; }

    const std::vector<Piece>& pieces() const { return pieces_; }

    const std::vector<std::string>& sprites() const { return sprites_; }

    int selected() const { return selected_; }

    int moves() const { return moves_; }

private:
    static constexpr int kIdle = 0;
    static constexpr int kDragging = 1;
    static constexpr int kSolved = 2;

    std::vector<std::string> sprites_;       // +0x6c: "nazo/slide/<name>", at most 4
    std::vector<Rect> walls_;                // +0x7e: in cells, at most 16
    std::vector<Goal> goals_;                // +0x100: at most 16
    std::vector<Piece> pieces_;              // +0x164: at most 16
    std::int16_t selected_ = -1;             // +0xd26: block being dragged
    std::int16_t originX_ = 0, originY_ = 0; // +0xd28: top-left of the board
    std::int16_t columns_ = 0, rows_ = 0;
    std::int16_t cellWidth_ = 1, cellHeight_ = 1; // +0xd30
    int state_ = kIdle;                           // +0xd34
    std::int16_t timer_ = 0;                      // +0xd38: frames since solved
    std::int16_t moves_ = 0;                      // +0xd3a
    std::int16_t touchX_ = -1, touchY_ = -1;      // +0xd3c
    std::int16_t offsetX_ = 0, offsetY_ = 0;      // +0xd40: stylus offset from the block
    bool sliding_ = false;                        // +0xd44: sound 0x109 is playing
    bool alternateSolved_ = false;                // internal ID (+7e88) is 0x15, 0x76 or 0x98

    // Cell to pixel and pixel to cell (020f7ab8 / 020f7acc / 020f7ae0 / 020f7b08).
    int pixelX(int cell) const;
    int pixelY(int cell) const;
    int cellX(int pixel) const;
    int cellY(int pixel) const;

    // Picks up the block covering the pressed cell (020f7ebc).
    bool pick(int x, int y);

    // Whether the held block's pixel rectangles, moved by (dx, dy), are clear
    // of every wall and other block (020f808c).
    bool fits(int dx, int dy, const std::vector<std::array<int, 4>>& rects) const;

    // Reduces a wanted movement to how far the block can actually slide
    // (020f8298).
    void move(int& dx, int& dy, const std::vector<std::array<int, 4>>& rects) const;

    // Moves the held block towards the stylus (020f8530).
    void drag(int dx, int dy);
};

} // namespace picarat
