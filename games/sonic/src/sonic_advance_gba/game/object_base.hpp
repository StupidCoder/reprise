#pragma once
#include "sonic_advance_gba/game/game.hpp"
#include "sonic_advance_gba/game/objects.hpp"
#include "sonic_advance_gba/game/scheduler.hpp"

// The bases the layouts' interactive objects and enemies share.
namespace sonic_advance_gba {

// What every interactive object keeps (+00-+0B): its layout record (whose x
// byte it takes, FE while it lives), the cell and its index.
class Interactive : public Process {
public:
    Interactive(const ObjectSpawn& o, u16 priority = 0x2000)
        : Process(priority), spawn_(o), x_byte_(o.record[0]) {
        x_ = s16(x_byte_ * 8 + o.cell_x * 0x100);
        y_ = s16(o.record[1] * 8 + o.cell_y * 0x100);
        o.record[0] = 0xFE;
    }

protected:
    void give_back() { spawn_.record[0] = x_byte_; }

    // Within 128 pixels of the view's left, top and right (or 0x120 below
    // its top).
    static bool near_view(s16 sx, s16 sy) {
        return u16(sx + 0x80) < 0x1F1 && sy + 0x80 >= 0 && sy < 0x121;
    }

    ObjectSpawn spawn_;
    u8 x_byte_; // +08
    s16 x_, y_; // the position in the act
};

// What every enemy keeps (+00-+0B): its layout record (whose x byte it
// takes, FE while it lives), the cell and its index, and its sprite (+0C)
// with tile memory of its own, freed when it ends (08009670).
class Enemy : public Process {
public:
    Enemy(Game& g, const ObjectSpawn& o, u32 tiles)
        : Process(0x2000), game_(g), spawn_(o), x_byte_(o.record[0]) {
        base_x_ = x_byte_ * 8 + o.cell_x * 0x100;
        base_y_ = o.record[1] * 8 + o.cell_y * 0x100;
        o.record[0] = 0xFE;
        tiles_ = g.sprite_tiles.allocate(tiles);
    }

    void on_destroy() override { game_.sprite_tiles.free(tiles_); }

protected:
    // Leaves the layout to the spawner again.
    void give_back() { spawn_.record[0] = x_byte_; }

    s8 param(unsigned i) const { return s8(spawn_.record[i]); }

    Game& game_;
    ObjectSpawn spawn_;
    u8 x_byte_; // +08
    s32 base_x_, base_y_;
    u32 tiles_{}; // +10
};

} // namespace sonic_advance_gba
