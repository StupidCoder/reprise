#include "wings_of_fury_amiga/game/effects.hpp"
#include <algorithm>
#include <cstdint>

namespace wings::game {
namespace {

constexpr int blast_base_y = 0xC;     // $100820: y = $C + the height
constexpr int blast_frames_end = 8;   // $1006BE: frame 8 frees the slot
constexpr int fire_frames = 0x59;     // world.shp entry + frame (2..7): exp0..exp5
constexpr int water_frames = 0x65;    //                                spl0..spl5
constexpr int splash_frames = 0x67;   // $1052F8: entry + frames left: spl6 (none)..spl1
constexpr int dust_frame = 0x6E;      // ric0
constexpr int splash_y = 0xD;
constexpr int dust_y = 0xD - 4;
constexpr int island_surface = 2;
constexpr int smoke_frames = 0x60;    // entry + size (1..6): smk0..smk5
constexpr int frames_per_size = 6;    // +$12 counts 6..0 (seven frames)
constexpr std::int32_t one = 0x10000;
constexpr int bomb_pictures = 0x40;   // torpedo.shp by the game's list: bom0..bomb
constexpr int rocket_pictures = 0x4C; // rc01.. (burning)
constexpr int rocket_unlit = 0x74;    // ro01.. (before the motor lights)
constexpr int torpedo_right = 0x88, torpedo_left = 0x89;
constexpr int torpedo_running = 0xA;
constexpr int eighth_scale_weapon = 9;

// $105174: an entry at world (x, y): the screen y is the ground line + $B - y;
// in the 1/8 view both are scaled down by 8 (from the 1/8-scale table);
// skipped when the screen x is outside -$80..$1C0.
void put(IndexedCanvas& canvas, const data::Assets& assets, const EffectView& view, int entry, int x, int y) {
    const auto* shape = view.eighth_scale ? data::eighth_scale_shape(assets, entry) : data::scenery_shape(assets, entry);
    const int shift = view.eighth_scale ? 3 : 0;
    const int origin = std::int16_t(view.camera + std::int16_t(0xFF60 << shift));
    const int sx = std::int16_t(x - origin) >> shift;
    const int sy = std::int16_t(-y + 0xB + view.ground_line) >> shift;
    if (shape == nullptr || sx < -0x80 || sx > 0x1C0) return;
    canvas.draw(*shape, sx - shape->offset_x, sy - shape->offset_y);
}

// The same placement for the weapons' own pictures.
void put_weapon(IndexedCanvas& canvas, const data::Assets& assets, const EffectView& view, int entry, int x, int y) {
    const auto* shape = data::weapon_shape(assets, entry);
    const int origin = std::int16_t(view.camera + std::int16_t(0xFF60));
    const int sx = std::int16_t(x - origin);
    const int sy = std::int16_t(-y + 0xB + view.ground_line);
    if (shape == nullptr || sx < -0x80 || sx > 0x1C0) return;
    canvas.draw(*shape, sx - shape->offset_x, sy - shape->offset_y);
}

} // namespace

bool Effects::start_blast_at(int x, int height, bool water) {
    for (auto& b : objects) {
        if (b.active()) continue;
        // Only the whole parts of x and y are written; the speeds are cleared.
        b.dx = b.dy = 0;
        b.set_pixel_x(x);
        b.set_pixel_y(blast_base_y + height);
        b.picture = 0;
        b.weapon = 1;
        b.kind = FlyingObject::Kind::blast;
        b.frame = 1;
        b.surface = water ? 0 : 2;
        return true;
    }
    return false;
}

void Effects::start_splash(int x, int surface) {
    for (auto& s : splashes) {
        if (s.left != 0) continue;
        s.x = x;
        s.dust = surface == island_surface;
        s.left = s.dust ? 4 : 6;
        return;
    }
}

void Effects::start_smoke(int x, int y, int size, BeamRandom& beam) {
    x += beam.next() & 7;
    y += 6;
    start_puff(std::int32_t(x) * one, std::int32_t(y) * one, size, beam);
}

void Effects::start_puff(std::int32_t x, std::int32_t y, int size, BeamRandom& beam) {
    for (auto& puff : puffs) {
        if (puff.size != 0) continue;
        puff = Puff{};
        puff.x = x;
        puff.y = std::max(y, 16 * one); // ($105480: at least 16)
        puff.size = size;
        puff.count = frames_per_size;
        puff.dx = (beam.next() & 0xFFFF) + one;
        puff.dy = (beam.next() & 0xFFFF) + one;
        return;
    }
}

void Effects::frame_start() {
    each_object([](FlyingObject& b) {
        b.listed = b.kind;
        b.shown_x = b.pixel_x();
        b.shown_y = b.pixel_y();
    });
}

void Effects::advance() {
    // $100EE0 (the smoke): drawn where it is, then it drifts.
    for (auto& puff : puffs) {
        puff.shown_size = puff.size;
        if (puff.size == 0) continue;
        puff.shown_x = std::int16_t(puff.x >> 16);
        puff.shown_y = std::int16_t(puff.y >> 16);
        puff.x += puff.dx;
        puff.y += puff.dy;
        if (--puff.count < 0) {
            puff.count = frames_per_size;
            --puff.size;
        }
    }
    // $1052F8 (the splashes), then $1006BE (the blasts).
    for (auto& s : splashes) {
        s.shown = s.left;
        if (s.left > 0) --s.left;
    }
    each_object([](FlyingObject& b) {
        if (b.listed != FlyingObject::Kind::blast) return;
        if (++b.frame == blast_frames_end) {
            b.kind = FlyingObject::Kind::none;
            b.frame = 0;
        }
    });
}

void draw_world_sprite(IndexedCanvas& canvas, const data::Assets& assets, const EffectView& view, const WorldSprite& s) {
    put(canvas, assets, view, s.entry, s.x, s.y);
}

void draw_effects(IndexedCanvas& canvas, const data::Assets& assets, const Effects& effects, const EffectView& view,
                  const std::vector<WorldSprite>& between) {
    const auto saved = canvas.clip;
    if (view.smoke_clip_bottom >= 0) canvas.clip.bottom = view.smoke_clip_bottom;
    for (const auto& puff : effects.puffs) {
        if (puff.shown_size == 0) continue;
        // The 1/8 view has two sizes: entries $61 and $62.
        const int size = view.eighth_scale ? (puff.shown_size <= 3 ? 1 : 2) : puff.shown_size;
        put(canvas, assets, view, smoke_frames + size, puff.shown_x, puff.shown_y);
    }
    canvas.clip = saved;
    for (const auto& s : between) put(canvas, assets, view, s.entry, s.x, s.y);
    for (const auto& s : effects.splashes) {
        if (s.shown == 0) continue;
        if (s.dust) put(canvas, assets, view, dust_frame, s.x, dust_y);
        else put(canvas, assets, view, splash_frames + s.shown, s.x, splash_y);
    }
    // $1006BE: blasts, and weapons in flight (torpedo.shp; in the 1/8 view
    // every weapon is entry 9 of the 1/8-scale table, `bumb`: a dot).
    effects.each_object([&](const FlyingObject& b) {
        if (b.listed == FlyingObject::Kind::blast) {
            // (advance() has freed the slot after its last frame.)
            if (b.kind == FlyingObject::Kind::blast) {
                put(canvas, assets, view, (b.surface == 0 ? water_frames : fire_frames) + b.frame, b.shown_x, b.shown_y);
            }
            return;
        }
        if (b.listed == FlyingObject::Kind::none) return;
        if (b.listed == FlyingObject::Kind::torpedo && b.picture == torpedo_running) return;
        if (view.eighth_scale) {
            put(canvas, assets, view, eighth_scale_weapon, b.shown_x, b.shown_y);
            return;
        }
        int entry = 0;
        switch (b.listed) {
        case FlyingObject::Kind::bomb: entry = bomb_pictures + b.picture; break;
        case FlyingObject::Kind::torpedo:
            entry = std::int8_t(b.surface) < 0 ? torpedo_left : torpedo_right;
            break;
        default: entry = (b.ignition != 0 ? rocket_unlit : rocket_pictures) + b.picture; break;
        }
        put_weapon(canvas, assets, view, entry, b.shown_x, b.shown_y);
    });
}

CrewTables read_crew_tables(const amiga::LoadedImage& wings) {
    CrewTables t;
    for (std::size_t i = 0; i < t.a.size(); ++i) {
        for (std::size_t j = 0; j < 2; ++j) {
            t.a[i][j] = wings.s16(0x1155B0 + std::uint32_t(i * 4 + j * 2));
            t.b[i][j] = wings.s16(0x1155C8 + std::uint32_t(i * 4 + j * 2));
        }
    }
    return t;
}

void DeckCrew::step(const CrewTables& t, int signal) {
    const auto entry = [&](const std::array<std::array<int, 2>, 6>& table) {
        return table[std::size_t(phase) % table.size()];
    };
    if (signal != phase) {
        phase = signal;
        target_a = entry(t.a)[1];
        target_b = entry(t.b)[1];
    }
    // The next target is the entry's other end.
    const auto turn_round = [](const std::array<int, 2>& ends, int pose) { return ends[0] == pose ? ends[1] : ends[0]; };
    if (pose_a != target_a) {
        pose_a += pose_a < target_a ? 1 : -1;
    } else if (phase != 2 || pose_a == pose_b) {
        // (Signal 2 swings both flags together: the first waits for the second.)
        target_a = turn_round(entry(t.a), pose_a);
    }
    if (pose_b != target_b) {
        pose_b += pose_b < target_b ? 1 : -1;
    } else {
        target_b = turn_round(entry(t.b), pose_b);
    }
}

} // namespace wings::game
