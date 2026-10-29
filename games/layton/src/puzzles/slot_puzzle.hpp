#pragma once
// Tiles on slots: the second adventure's puzzle type 11 (class
// lt2qTileObject, ARM9 overlay 39: ROM 0xafa00 (+0xa0c bytes), loaded at
// 020f78a0, vtable 020f85c4) and the first adventure's "Tile" engine (its
// ARM9 02049d20..0204ab48 with the tile pieces 0205a710..0205ac40).
//
// How the puzzle plays
// --------------------
// Picture tiles lie at their starting places beside a set of empty slots.
// The player drags tiles with the stylus; a released tile snaps to the
// nearest slot (its starting place counts as a slot too). Dropping a tile on
// an occupied slot swaps the two tiles. When the player presses "Submit", the
// arrangement is compared with the accepted answers.
//
// The parameter script creates the tiles (command 0x39), the empty slots
// (0x3a), the number of accepted arrangements (0x3b) and, per tile, where it
// must be in each arrangement (0x3c).
//
// Empty slots are labelled 0, 1, 2, ... in the order they are created. An
// answer entry for a tile is either a label (the tile must be in the slot
// with that label) or a negative value −1 − n meaning "in slot number n"
// (any slot, counted in creation order including the tiles' starting places).
//
// The tile picked up last is drawn in front.
//
// The first adventure (Rules::Adventure1) differs in the handling: a picked
// tile is centred on the stylus (2 pixels up and left of it), the tile
// highest in creation order under the stylus is picked, tiles are drawn in
// creation order with the dragged one last (nothing is moved to the front
// for good), and a tile has at most 8 answer entries. Its engine adds
// rotation, a restart button and the memory of the arrangement between
// tries (a1_tile.cpp).
#include "system/effect_queue.hpp"
#include "system/interaction.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace picarat {

class SlotPuzzle {
public:
    enum class Rules { Adventure1, Adventure2 };

    // Sound effects: 0xf5 a tile is picked up (020f7998), 0xf6 dropped (020f7ae0).
    EffectQueue sounds;

    explicit SlotPuzzle(std::span<const std::uint8_t> script);

    // An empty engine; the setup calls (the script's commands) follow.
    explicit SlotPuzzle(Rules rules) : rules_(rules) {}

    // --- Setup (second adventure commands / first).
    // A tile at (x, y), whose starting place becomes a slot (0x39 / 0x73).
    void addTile(int x, int y, const std::string& sprite, int animation);
    // A slot; `home` for a tile's starting place (0x3a / 0x74).
    void addSlot(int x, int y, bool home);

    void setAnswerCount(int n) { answerCount_ = n; } // 0x3b / 0x76

    // The tile's place in the next accepted arrangement (0x3c / 0x75).
    void addTileAnswer(int tile, int place);

    struct Tile {
        int x = 0, y = 0;          // +0x00: current position
        int homeX = 0, homeY = 0;  // starting position
        int homeSlot = -1;         // +0x10: the slot at the starting position
        std::vector<int> answers;  // +0x14: one entry per accepted arrangement (at most 12)
        bool dragging = false;     // +0x54
        std::string sprite;        // +0x58: "nazo/tile/<name>"
        int animation = 0;         // which animation of the sprite shows this tile
        int slot = -1;             // +0x60: slot the tile is in
        int grabX = 0, grabY = 0;  // +0x64: stylus offset from the tile's corner
        int width = 0, height = 0; // first adventure: the sprite's frame size
    };

    // Every tile back to its starting place (virtual function +204, 020f7db8).
    void reset();

    // One frame of input (020f7eb8). On a new press, the frontmost tile under
    // the stylus is picked up; `hit(i)` tests tile i's sprite at the touch
    // position (sprite hit test 02075f44 -> 0207430c). Then every tile follows
    // or is dropped.
    template <class Hit>
    void update(const TouchFilter& touch, Hit hit);

    // First adventure, 0204a7f8: on a new press, the tile highest in
    // creation order with `hit(i)` is picked up (true when one was).
    template <class Hit>
    bool pick(const TouchFilter& touch, Hit hit);
    // First adventure 0205aa50: one tile follows the stylus or is dropped
    // (true when it was dropped this frame).
    bool step(int tile, const TouchFilter& touch);

    // Matches one of the accepted arrangements (virtual function +1fc, 020f7e34).
    bool solved() const;

    const std::vector<Tile>& tiles() const { return tiles_; }

    Tile& tile(int index) { return tiles_.at(std::size_t(index)); }

    int slotCount() const { return int(slots_.size()); }

    std::array<bool, 0x40>& occupancy() { return occupied_; }

    // Tile indices front to back (+0x45c).
    const std::vector<int>& order() const { return order_; }

private:
    Rules rules_ = Rules::Adventure2;

    struct Slot {
        int x = 0, y = 0;
        int label = -1;    // -1 for a tile's starting place
        bool home = false; // a tile's starting place
    };

    std::vector<Tile> tiles_;           // at most 0x18
    std::vector<Slot> slots_;           // at most 0x40 (+0xd0 / +0x2d0 / +0x418)
    std::array<bool, 0x40> occupied_{}; // +0x3d8
    int labels_ = 0;                    // +0x3d4: labels handed out so far
    int answerCount_ = 0;               // +0x458: accepted arrangements
    std::vector<int> order_;

    // Moves a tile to the front of the drawing order (020f812c).
    void front(int tile);

    // Snaps a released tile to the nearest slot (020f7ff0).
    void drop(Tile& tile);

    // Follows the stylus while held; drops on release (input half of 020f7ae0).
    void drag(Tile& tile, const TouchFilter& touch);
};

template <class Hit>
bool SlotPuzzle::pick(const TouchFilter& t, Hit hit) {
    if (t.state != 1) {
        return false;
    }
    for (int i = int(tiles_.size()); i-- > 0;) {
        if (hit(i)) {
            tiles_[std::size_t(i)].dragging = true;
            return true;
        }
    }
    return false;
}

template <class Hit>
void SlotPuzzle::update(const TouchFilter& t, Hit hit) {
    // TouchFilter states: 1 pressed, 2 held, 3 released.
    if (t.state == 1) {
        for (std::size_t k = 0; k < order_.size(); ++k) {
            const int i = order_[k];
            if (!hit(i)) {
                continue;
            }
            sounds.play(0xf5);
            auto& tile = tiles_[std::size_t(i)];
            tile.grabX = t.x - tile.x;
            tile.grabY = t.y - tile.y;
            tile.dragging = true; // 020f7a84
            front(i);
            break;
        }
    }
    // Back to front, as the original walks its list.
    for (auto k = order_.size(); k-- > 0;) {
        drag(tiles_[std::size_t(order_[k])], t);
    }
}

} // namespace picarat
