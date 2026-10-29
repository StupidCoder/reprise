// Placing objects from the act's placement list (bank 1E, 8000) and
// streaming object tiles (7743).

#include "sonic2_gg/game/game.hpp"
#include <stdexcept>

namespace sonic2_gg {

// Bank 1E, 8000: walks the act's placement records (9 bytes: type, X, Y,
// display flags, parameter, two tile bases; FF ends) and creates those
// within reach of the camera that do not exist yet (D400 holds the type of
// each placed object). Reach: from 128 pixels left of/above the camera to
// about 512 pixels on, compared with byte precision.
void Game::spawn_objects() {
    if (flow.zone >= 7) {
        return;
    }
    unsigned record = act_entry(rom::placements).address;
    unsigned camera_x = (camera.x + 0xFF80u) & 0xFFFF;
    if (camera.x < 0x80) {
        camera_x = 0;
    }
    unsigned camera_y = (camera.y + 0xFF80u) & 0xFFFF;
    if (camera.y < 0x80) {
        camera_y = 0;
    }
    for (unsigned index = 0;; ++index, record += 9) {
        if (rom.byte(0x1E, record) == 0xFF) {
            return;
        }
        if (spawner.placement_spawned[index]) {
            continue;
        }
        // 804D: byte-wise window tests on X then Y. The low bytes are only
        // compared when the high byte is zero (OR A on the high byte).
        auto in_reach = [](unsigned position, unsigned camera) {
            const u8 d = u8(position >> 8), e = u8(position);
            const u8 cd = u8(camera >> 8), ce = u8(camera);
            if (d < cd) {
                return false;
            }
            if (d == 0 && e < ce) {
                return false;
            }
            const u8 top = u8(cd + 2);
            if (top < d) {
                return false;
            }
            if (top == 0 && e >= ce) {
                return false;
            }
            return true;
        };
        if ((!in_reach(rom.word(0x1E, record + 1), camera_x) ||
             !in_reach(rom.word(0x1E, record + 3), camera_y)) &&
            // Widescreen: also anything placed near the presented view.
            !in_wide_view(rom.word(0x1E, record + 1), rom.word(0x1E, record + 3), 64)) {
            continue;
        }
        if (rom.byte(0x1E, record) == 0xFE) {
            // 80A6: a routine record; only display value 0 has a routine
            // (812E). No act list uses one.
            spawner.placement_spawned[index] = 0xFF;
            if (rom.byte(0x1E, record + 5) == 0) {
                throw std::runtime_error("Placement routine record not reconstructed");
            }
            continue;
        }
        Object* o = find_free_slot(); // 0203 -> 6025
        if (!o) {
            continue;
        }
        const unsigned r = record;
        o->type = rom.byte(0x1E, r);
        spawner.placement_spawned[index] = o->type;
        o->origin_x = rom.word(0x1E, r + 1);
        o->x = o->origin_x;
        o->origin_y = rom.word(0x1E, r + 3);
        o->y = o->origin_y;
        o->display = rom.byte(0x1E, r + 5) | display_flag::offscreen;
        o->param = rom.byte(0x1E, r + 6);
        o->tile_base = rom.byte(0x1E, r + 7);
        o->tile_base_mirrored = rom.byte(0x1E, r + 8);
        o->placement = u8(index + 1);
    }
}

// 7743: copies requested object tiles (D3AB) to the tile art, four tiles per frame.
// Requests below 10 and from 20 are single blocks (bank 7 / bank 14) to
// VRAM 0A80; requests 10-1F name lists (782C) of {bank (bit 7: mirrored),
// tile count, VRAM address, source}.
void Game::stream_object_tiles() {
    if (!tiles.object_request) {
        return;
    }
    if (!tiles.object_bank) {
        start_object_tiles(); // 77D2
    }
    if (!tiles.object_request) {
        return;
    }
    if (!tiles.object_count) {
        // 77F3. At the end of a list the request is not checked again: bank
        // 0 is selected and the finish below runs a second time.
        next_object_tiles();
    }
    const u8 bank = tiles.object_bank;
    const bool mirrored = bank & 0x80;
    if (!tiles.object_count) {
        finish_object_tiles(); // 7818
        return;
    }
    const unsigned count = tiles.object_count < 4 ? tiles.object_count : 4u;
    unsigned source = tiles.object_source;
    unsigned vram = tiles.object_vram;
    for (unsigned i = 0; i < count * 32; ++i) {
        u8 v = rom.byte(bank & 0x1F, source++);
        if (mirrored) {
            v = rom.byte(rom::bit_reversed, v);
        }
        display.put_art(vram++, std::span(&v, 1));
    }
    tiles.object_source = source;
    tiles.object_vram = tiles.object_vram + 0x80u;
    const int left = int(tiles.object_count) - 4;
    tiles.object_count = u8(left < 0 ? 0 : left);
}

// 77D2
void Game::start_object_tiles() {
    const u8 request = tiles.object_request;
    if (request < 0x10) {
        // 78CF: bank 7, 6 tiles
        const unsigned source = rom.word(rom::object_tiles_07, request * 2u);
        display.put_art(84 * 32, rom.bytes(0x07, source, 0xC0)); // tiles 84-89
        finish_object_tiles();
        return;
    }
    if (request >= 0x20) {
        // 789B: bank 14, 4 tiles
        const unsigned source = rom.word(rom::object_tiles_14, (request - 0x20) * 2u);
        display.put_art(84 * 32, rom.bytes(0x14, source, 0x80)); // tiles 84-87
        finish_object_tiles();
        return;
    }
    tiles.object_list = rom.word(rom::object_tile_lists, (request - 0x10) * 2u);
    next_object_tiles();
}

// 77F3
void Game::next_object_tiles() {
    const unsigned p = tiles.object_list;
    if (rom.byte(1, p) == 0xFF) {
        finish_object_tiles();
        return;
    }
    tiles.object_bank = rom.byte(1, p);
    tiles.object_count = rom.byte(1, p + 1);
    tiles.object_vram = rom.word(1, p + 2);
    tiles.object_source = rom.word(1, p + 4);
    tiles.object_list = p + 6;
}

// 7818
void Game::finish_object_tiles() {
    tiles.object_request = 0;
    tiles.object_bank = 0;
    tiles.object_count = 0;
    tiles.object_vram = 0;
    tiles.object_source = 0;
}

} // namespace sonic2_gg
