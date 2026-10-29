#include "sonic_advance_gba/game/objects.hpp"
#include "gba/bios.hpp"
#include "sonic_advance_gba/game/enemies.hpp"
#include "sonic_advance_gba/game/game.hpp"
#include "sonic_advance_gba/game/interactive.hpp"
#include "sonic_advance_gba/game/item_box.hpp"
#include "sonic_advance_gba/game/stage.hpp"
#include <cstdio>
#include <vector>

namespace sonic_advance_gba {

namespace {

constexpr u32 interactive_layouts = 0x080B7F94; // per act, run-length compressed
constexpr u32 item_layouts = 0x080B7FDC;
constexpr u32 enemy_layouts = 0x080B8024;
constexpr u32 interactive_creators = 0x080B806C; // by kind: the creating routine
constexpr u32 enemy_creators = 0x080B823C;

// A decompressed layout: size, width and height in cells, then per cell
// the offset of its record list (0: none), relative to 8 bytes before the
// table; lists end with FF.
struct ObjectLayout {
    std::vector<u8> bytes;
    u32 record_size;

    u32 word(u32 o) const {
        return u32(bytes.at(o)) | u32(bytes.at(o + 1)) << 8 | u32(bytes.at(o + 2)) << 16 |
               u32(bytes.at(o + 3)) << 24;
    }

    u32 width() const { return word(4) & 0xFFFF; }

    u32 height() const { return word(8) & 0xFFFF; }

    u32 list(u32 cx, u32 cy) const {
        const u32 o = word(0xC + 4 * (cx + cy * width()));
        return o == 0 ? 0 : 0xC + o - 8;
    }
};

std::string hex(u32 v) {
    char b[16];
    std::snprintf(b, sizeof b, "%08X", v);
    return b;
}

void spawn_interactive(Game& g, const ObjectSpawn& o) {
    const u8 kind = o.record[2];
    const u32 creator = g.assets().u32(interactive_creators + 4u * kind) & ~1u;
    if (create_interactive(g, creator, o)) {
        return;
    }
    throw Unrecovered("object kind " + std::to_string(kind) + " (" + hex(creator) + ")");
}

void spawn_item(Game& g, const ObjectSpawn& o) {
    create_item_box(g, o);
}

void spawn_enemy(Game& g, const ObjectSpawn& o) {
    const u8 kind = o.record[2];
    const u32 creator = g.assets().u32(enemy_creators + 4u * kind) & ~1u;
    if (create_enemy(g, creator, o)) {
        return;
    }
    throw Unrecovered("enemy kind " + std::to_string(kind) + " (" + hex(creator) + ")");
}

// The spawner (080088A8 for the first frame or a jump, 08008CEC after).
class Spawner final : public Process {
public:
    Spawner(Game& g, ObjectLayout interactive, ObjectLayout items, ObjectLayout enemies)
        : Process(0x2000), interactive_(std::move(interactive)), items_(std::move(items)),
          enemies_(std::move(enemies)) {
        last_x_ = g.stage().camera.x;
        last_y_ = g.stage().camera.y;
    }

    void update(Game& g) override {
        StageState& s = g.stage();
        if (s.flags & stage_flag::frozen) {
            return;
        }
        const auto& c = s.camera;
        if (!full_ && std::abs(c.x - last_x_) < 0xF9 && std::abs(c.y - last_y_) < 0xD1) {
            spawn_new_edges(g);
        } else {
            spawn_all(g);
        }
    }

private:
    // A rectangle of act pixels, clamped to the layout's extent.
    struct Area {
        s16 left, top, right, bottom;
    };

    s16 clamp(s32 v, u32 cells) const {
        s16 r = s16(v);
        if (r < 0) {
            r = 0;
        }
        if (u32(s32(r)) >= cells * 0x100) {
            r = s16(cells * 0x100 - 1);
        }
        return r;
    }

    // Every record in `area` of the three layouts, cell by cell (each cell:
    // interactive objects, item boxes, enemies).
    void spawn_area(Game& g, Area a) {
        const u32 w = interactive_.width(), h = interactive_.height();
        for (u32 cy = (u32(s32(a.top)) >> 8) & 0xFFFF; s32(cy * 0x100) < a.bottom && cy < h;
             cy = (cy + 1) & 0xFFFF) {
            for (u32 cx = (u32(s32(a.left)) >> 8) & 0xFFFF; s32(cx * 0x100) < a.right && cx < w;
                 cx = (cx + 1) & 0xFFFF) {
                spawn_cell(g, interactive_, cx, cy, a, spawn_interactive);
                spawn_cell(g, items_, cx, cy, a, spawn_item);
                spawn_cell(g, enemies_, cx, cy, a, spawn_enemy);
            }
        }
    }

    void spawn_cell(Game& g, ObjectLayout& l, u32 cx, u32 cy, Area a,
                    void (*create)(Game&, const ObjectSpawn&)) {
        u32 at = l.list(cx, cy);
        if (at == 0) {
            return;
        }
        u8 index = 0;
        for (; l.bytes.at(at) != 0xFF; at += l.record_size, ++index) {
            if (s8(l.bytes[at]) < -2) {
                continue; // in use
            }
            const s32 x = s16(l.bytes[at] * 8 + cx * 0x100);
            const s32 y = s16(l.bytes[at + 1] * 8 + cy * 0x100);
            if (a.left <= x && x <= a.right && a.top <= y && y <= a.bottom) {
                create(g, {&l.bytes[at], u16(cx), u16(cy), index});
            }
        }
    }

    // 080088A8: the camera's surroundings (128 pixels to the left and
    // above, 368 to the right and 288 below the view's corner).
    void spawn_all(Game& g) {
        const auto& c = g.stage().camera;
        const u32 w = interactive_.width(), h = interactive_.height();
        spawn_area(g, {clamp(c.x - 0x80, w), clamp(c.y - 0x80, h), clamp(c.x + 0x170, w),
                       clamp(c.y + 0x120, h)});
        last_x_ = c.x;
        last_y_ = c.y;
        full_ = false;
    }

    // 08008CEC: the strips the surroundings gained since the last frame.
    void spawn_new_edges(Game& g) {
        const auto& c = g.stage().camera;
        const u32 w = interactive_.width(), h = interactive_.height();
        // The columns that came into range, at the full height.
        Area columns{};
        if (last_x_ < c.x) {
            columns.left = s16(last_x_ + 0x170);
            columns.right = s16(c.x + 0x170);
        } else {
            columns.left = s16(c.x - 0x80);
            columns.right = s16(last_x_ - 0x80);
        }
        columns.top = s16(c.y - 0x80);
        columns.bottom = s16(c.y + 0x120);
        // The rows that came into range, at the full width.
        Area rows{};
        if (last_y_ < c.y) {
            rows.top = s16(last_y_ + 0x120);
            rows.bottom = s16(c.y + 0x120);
        } else {
            rows.top = s16(c.y - 0x80);
            rows.bottom = s16(last_y_ - 0x80);
        }
        rows.left = s16(c.x - 0x80);
        rows.right = s16(c.x + 0x170);
        columns = {clamp(columns.left, w), clamp(columns.top, h), clamp(columns.right, w),
                   clamp(columns.bottom, h)};
        rows = {clamp(rows.left, w), clamp(rows.top, h), clamp(rows.right, w),
                clamp(rows.bottom, h)};
        if (c.x != last_x_ && columns.left != columns.right && columns.top != columns.bottom) {
            spawn_area(g, columns);
        }
        if (c.y != last_y_ && rows.top != rows.bottom && rows.left != rows.right) {
            spawn_area(g, rows);
        }
        last_x_ = c.x;
        last_y_ = c.y;
    }

    ObjectLayout interactive_, items_, enemies_; // +00, +04, +08
    s16 last_x_, last_y_;                        // +0C, +0E
    bool full_ = true;                           // +10 the next update spawns everything in range
};

ObjectLayout load_layout(Game& g, u32 table, u32 record_size) {
    const Assets& a = g.assets();
    const u32 data = a.u32(table + 4u * u32(s32(g.stage().zone)));
    return {gba::rl_decompress([&](u32 at) { return a.u8(at); }, data), record_size};
}

} // namespace

void start_spawner(Game& g) {
    if (g.game_mode == 6) {
        throw Unrecovered("the chao hunt spawner (08008784)");
    }
    g.scheduler().create<Spawner>(g, load_layout(g, interactive_layouts, 7),
                                  load_layout(g, item_layouts, 3),
                                  load_layout(g, enemy_layouts, 7));
}

} // namespace sonic_advance_gba
