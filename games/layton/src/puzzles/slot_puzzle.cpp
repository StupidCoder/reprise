// Tiles-on-slots engine (ARM9 overlay 39). See slot_puzzle.hpp for the rules.
#include "puzzles/slot_puzzle.hpp"
#include "system/script.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace picarat {

SlotPuzzle::SlotPuzzle(std::span<const std::uint8_t> script) : rules_(Rules::Adventure2) {
    ScriptContext context;
    // Tile(x, y, sprite, animation): virtual +148 (020f8190).
    context.bind(0x39, [&](CommandArguments& arguments) {
        const int x = arguments.integer();
        const int y = arguments.integer();
        const auto name = arguments.string();
        addTile(x, y, "nazo/tile/" + name, arguments.integer());
    });
    // Slot(x, y): virtual +14c, an empty labelled slot.
    context.bind(0x3a, [&](CommandArguments& arguments) {
        const int x = arguments.integer();
        addSlot(x, arguments.integer(), false);
    });
    // AnswerCount(n): virtual +150 (020f8554).
    context.bind(0x3b, [&](CommandArguments& arguments) { setAnswerCount(arguments.integer()); });
    // TileAnswer(tile, place): virtual +154 (020f82d8 -> 020f79d4).
    context.bind(0x3c, [&](CommandArguments& arguments) {
        const int tile = arguments.integer();
        addTileAnswer(tile, arguments.integer());
    });
    ScriptInterpreter(script).run(context);
}

void SlotPuzzle::addTile(int x, int y, const std::string& sprite, int animation) {
    // Second adventure 020f8190, first 0204a444 (+0x10c): at most 0x18
    // tiles; the starting place becomes a slot of its own.
    if (tiles_.size() >= 0x18) {
        return;
    }
    Tile t;
    t.x = t.homeX = x;
    t.y = t.homeY = y;
    t.sprite = sprite;
    t.animation = animation;
    tiles_.push_back(t);
    addSlot(x, y, true);
    auto& tile = tiles_.back();
    tile.slot = int(slots_.size()) - 1;
    if (tile.homeSlot == -1) {
        tile.homeSlot = tile.slot;
    }
    // Initial drawing order: tile 0 in front (020f7c68).
    order_.push_back(int(tiles_.size()) - 1);
}

void SlotPuzzle::addTileAnswer(int tile, int value) {
    // 020f82d8 -> 020f79d4 / 0204a504 -> 0205a900: at most 12 entries a
    // tile (8 in the first adventure).
    if (tile < 0 || tile >= int(tiles_.size())) {
        return;
    }
    auto& answers = tiles_[std::size_t(tile)].answers;
    if (answers.size() < (rules_ == Rules::Adventure1 ? 8u : 12u)) {
        answers.push_back(value);
    }
}

bool SlotPuzzle::step(int index, const TouchFilter& t) {
    // 0205aa50: the stylus point used is 2 pixels up and left of the
    // contact, kept 10 pixels inside the screen, and the tile is centred on
    // it.
    Tile& tile = tiles_.at(std::size_t(index));
    if (!tile.dragging) {
        return false;
    }
    if (t.state == 1 || t.state == 2) {
        const int x = std::clamp(t.x - 2, 10, 0xf0);
        const int y = std::clamp(t.y - 2, 10, 0xb4);
        tile.y = y - (tile.height >> 1);
        tile.x = x - (tile.width >> 1);
        return false;
    }
    if (t.state == 3) {
        tile.dragging = false;
        drop(tile); // virtual +0x27c, 0204a6bc
        return true;
    }
    return false;
}

void SlotPuzzle::addSlot(int x, int y, bool home) {
    if (slots_.size() >= 0x40) {
        return;
    }
    // Starting places are occupied by their tile and carry no label.
    Slot slot{x, y, home ? -1 : labels_++, home};
    occupied_[slots_.size()] = home;
    slots_.push_back(slot);
}

void SlotPuzzle::reset() {
    for (auto& tile : tiles_) {
        tile.slot = tile.homeSlot;
        tile.x = tile.homeX;
        tile.y = tile.homeY;
        tile.dragging = false;
    }
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        occupied_[i] = slots_[i].label == -1;
    }
}

void SlotPuzzle::front(int tile) {
    auto it = std::find(order_.begin() + 1, order_.end(), tile);
    if (order_.empty() || it == order_.end()) {
        return;
    }
    std::rotate(order_.begin(), it, it + 1);
}

void SlotPuzzle::drag(Tile& tile, const TouchFilter& t) {
    if (t.state == 1 || t.state == 2) {
        if (!tile.dragging) {
            return;
        }
        // The stylus position is kept 10 pixels inside the screen edges.
        const int x = std::clamp(t.x, 10, 0xf0);
        const int y = std::clamp(t.y, 10, 0xb4);
        tile.x = x - tile.grabX;
        tile.y = y - tile.grabY;
        return;
    }
    if (tile.dragging) {
        tile.dragging = false;
        drop(tile);
        sounds.play(0xf6);
    }
}

void SlotPuzzle::drop(Tile& tile) {
    // The nearest slot to the tile's corner, by single-precision distance
    // (0206b0c4); slots 1000 pixels or more away never count.
    int nearest = tile.slot;
    float best = 1000.0f;
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        const float dx = float(slots_[i].x - tile.x);
        const float dy = float(slots_[i].y - tile.y);
        const float distance = std::sqrt(dx * dx + dy * dy);
        if (distance < best) {
            nearest = int(i);
            best = distance;
        }
    }
    const int previous = tile.slot;
    if (occupied_[std::size_t(nearest)]) {
        // Occupied (possibly by this very tile): swap with the tile there,
        // which moves to the slot this tile came from.
        Tile* occupant = nullptr;
        for (auto& other : tiles_) {
            if (other.slot == nearest) {
                occupant = &other;
            }
        }
        if (!occupant) {
            throw std::runtime_error("occupied slot without a tile");
        }
        tile.x = slots_[std::size_t(nearest)].x;
        tile.y = slots_[std::size_t(nearest)].y;
        tile.slot = nearest;
        occupant->x = slots_[std::size_t(previous)].x;
        occupant->y = slots_[std::size_t(previous)].y;
        occupant->slot = previous;
        return;
    }
    occupied_[std::size_t(previous)] = false;
    tile.x = slots_[std::size_t(nearest)].x;
    tile.y = slots_[std::size_t(nearest)].y;
    tile.slot = nearest;
    occupied_[std::size_t(nearest)] = true;
}

bool SlotPuzzle::solved() const {
    // Right if any accepted arrangement matches every tile that has an entry
    // for it (see the header for the meaning of the entries).
    for (int k = 0; k < answerCount_; ++k) {
        bool matches = true;
        for (const auto& tile : tiles_) {
            if (k >= int(tile.answers.size())) {
                continue;
            }
            const int place = tile.answers[std::size_t(k)];
            const bool ok = place >= 0 ? place == slots_[std::size_t(tile.slot)].label
                                       : tile.slot == -1 - place;
            if (!ok) {
                matches = false;
            }
        }
        if (matches) {
            return true;
        }
    }
    return false;
}

} // namespace picarat
