// The screen: VDP settings, clearing, tile art and map rectangles.

#include "triple_trouble_gg/game/game.hpp"

namespace triple_trouble_gg {

namespace {
// 265A: the VDP's registers 0-10 at power-on.
constexpr Table vdp_registers{0x00, 0x265A};
// 4765: the name table offset of each 8-pixel row of the camera (2CB9).
constexpr Table name_rows{0x01, 0x4765};
} // namespace

// 2641
void Game::init_vdp_registers() {
    for (unsigned i = 0; i < video.registers.size(); ++i) {
        video.registers[i] = rom.byte(vdp_registers, i);
    }
    display.shown = video.registers[1] & 0x40;
    display.line_interrupt = video.registers[0] & 0x10;
}

// 26D6 / 26E4: register 1 bit 6.
void Game::display_on() {
    video.registers[1] |= 0x40;
    display.shown = true;
}

void Game::display_off() {
    video.registers[1] &= u8(~0x40);
    display.shown = false;
}

// 26F2: register 1 bit 5.
void Game::enable_frame_interrupt() {
    video.registers[1] |= 0x20;
}

// 7BCF: all of video memory cleared (tile art, map, sprite table); the
// display is on afterwards (26D6).
void Game::clear_vram() {
    display.level_shown = false;
    display_off();
    for (auto& t : display.tiles) {
        t.fill(0);
    }
    display.map.fill(0);
    for (auto& s : display.sprites) {
        s = {};
    }
    display_on();
}

// 7C08
void Game::clear_cram() {
    display.colors.fill(0);
}

// 273B: after a frame, the background map filled with tile C0 (whose art,
// with C1's, is cleared), then the sprite table cleared (2757).
Flow Game::clear_screen() {
    co_await next_frame();
    di();
    display.level_shown = false;
    display.map.fill(0x00C0);
    display.fill_art(0x1800, 0x40, 0);
    clear_sprites();
    co_await busy(4 + 10 + 10 + fill_clocks(0x380) + 10 + 10 + 10 + fill_clocks(0x20) +
                  sprite_clear_clocks());
}

// 2757: every sprite entry at Y F0, X 0, tile 0; uploaded at the next frame.
void Game::clear_sprites() {
    for (auto& s : sprites.table) {
        s = {0xF0, 0, 0};
    }
    video.sprites_dirty = 0xFF;
}

// 2DC7
unsigned Game::load_tiles(unsigned vram, unsigned bank, unsigned address, bool mirrored) {
    clear_decoder_buffer(); // 2DD7
    display.put_art(vram, rom.tiles(bank, address, mirrored));
    return decode_clocks(bank, address, mirrored);
}

// 2C75 (0504): the same with one byte per entry, its high byte
// `high` (D3F1); no EI at the end.
unsigned Game::draw_byte_rect(unsigned name_address, unsigned bank, unsigned address, int rows,
                              int columns, u8 high) {
    unsigned clocks = 17 + 17 + 229 + 10;
    unsigned hl = name_address;
    {
        const unsigned row_start = hl & 0xFFC0;
        const unsigned column = (((camera.x & 0xFF) >> 2 | (camera.x & 0xFF) << 6) + hl) & 0x3E;
        hl = row_start | column;
        const unsigned row = ((camera.y >> 3) & 0x1F) * 2;
        hl += rom.word(name_rows, row);
        if ((hl >> 8) >= 0x3F) {
            hl -= 0x0700;
        }
    }
    unsigned source = address;
    for (int r = 0; r < rows; ++r) {
        clocks +=
            11 + 11 + 4 + 85 + unsigned(96 * columns) + 10 + 10 + 11 + 4 + 7 + 10 + 10 + 13 - 5;
        unsigned at = hl;
        for (int c = 0; c < columns; ++c) {
            const unsigned cell = (at - 0x3800) / 2;
            display.map.at(cell) = std::uint16_t(rom.byte(bank, source) | (high << 8));
            ++source;
            at += 2;
            if ((at & 0x3F) == 0) {
                at -= 0x40;
                clocks += 135;
            }
        }
        hl += 0x40;
        if ((hl >> 8) == 0x3F) {
            hl = (hl & 0xFF) | 0x3800;
            clocks += 7;
        }
    }
    return clocks - 5;
}

// 2DC7 with D12C (loading) set: the same decode, but after every eighth
// tile counted down (and the last) the decoder waits for a frame (2E43;
// interrupts on, the frame interrupt only playing sound). The tiles appear
// as they are decoded.
Flow Game::load_tiles_slowly(unsigned vram, unsigned bank, unsigned address, bool mirrored) {
    clear_decoder_buffer(); // 2DD7
    const Bytes& art = rom.tiles(bank, address, mirrored);
    const Bytes& plain = rom.tiles(bank, address, false);
    const auto parts = decode_clock_parts(bank, address, mirrored);
    const unsigned count = unsigned(parts.tiles.size());
    unsigned clocks = parts.head;
    for (unsigned n = 0; n < count; ++n) {
        display.put_art(vram + 32 * n, std::span<const u8>(art).subspan(32 * n, 32));
        clocks += parts.tiles[n];
        if (((count - n - 1) & 7) == 0) {
            show_decoder_work(parts, plain, n, mirrored);
            co_await busy(clocks);
            clocks = 0;
            co_await next_frame(); // 084C
            di();
        }
    }
    clear_decoder_buffer(); // 2DD3
    co_await busy(clocks + parts.tail);
}

// 7A58: a list of art (bank 1A: bank byte, bit 7 mirrored, FF ends; the
// video address; the source) decoded with frame waits (D12C).
Flow Game::load_tile_list_slowly(Table list) {
    video.loading = 0xFF;
    for (;; list = list + 5) {
        const u8 bank = rom.byte(list, 0);
        if (bank == 0xFF) {
            break;
        }
        co_await load_tiles_slowly(rom.word(list, 1), bank & 0x1F, rom.word(list, 3), bank & 0x80);
    }
    video.loading = 0;
}

// The decoder's work bytes while it waits after tile `n` (2E4F): the tile
// as decoded (unmirrored; DBE6), the zero tile (DC06), its place in the
// current code byte (DC26: 1-4), the tiles still to come (DC27), the code
// and data read positions (DC29, DC2B) and the mirroring flag (DC31: the
// list's bank byte AND 80, 7A87, stored by 2DE2).
void Game::show_decoder_work(const DecodeClocks& parts, const Bytes& plain, unsigned n,
                             bool mirrored) {
    auto& w = act.work;
    std::copy_n(plain.begin() + 32 * n, 32, w.begin());
    std::fill_n(w.begin() + 0x20, 0x20, u8(0));
    const unsigned left = unsigned(parts.tiles.size()) - n - 1;
    w[0x40] = u8(n % 4 + 1);
    w[0x41] = lo(std::uint16_t(left));
    w[0x42] = hi(std::uint16_t(left));
    w[0x43] = lo(parts.codes_after[n]);
    w[0x44] = hi(parts.codes_after[n]);
    w[0x45] = lo(parts.data_after[n]);
    w[0x46] = hi(parts.data_after[n]);
    w[0x4B] = mirrored ? 0x80 : 0;
}

// 2DD7: the decoder's 77 work bytes (DBE6-DC32) cleared.
void Game::clear_decoder_buffer() {
    std::fill_n(act.work.begin(), 0x4D, u8(0));
}

// 2C3B: map entries in rows of 64 bytes. The start cell is moved by the
// camera's position (2CB9: the column by camera X / 4, wrapping within its
// row; the row by table 4765, wrapping from 3F00 back to 3800); a row
// wraps within itself at its end (2C50), and the rows wrap at 3F00 (2C68).
unsigned Game::draw_rect(unsigned name_address, unsigned bank, unsigned address, int rows,
                         int columns) {
    unsigned hl = name_address;
    {
        const unsigned row_start = hl & 0xFFC0;
        const unsigned column = (((camera.x & 0xFF) >> 2 | (camera.x & 0xFF) << 6) + hl) & 0x3E;
        hl = row_start | column;
        const unsigned row = ((camera.y >> 3) & 0x1F) * 2;
        hl += rom.word(name_rows, row);
        if ((hl >> 8) >= 0x3F) {
            hl -= 0x0700;
        }
    }
    return 17 + 229 + draw_rect_at(hl, bank, address, rows, columns); // 2CB9
}

// 2C3E: the same at a fixed place (the camera ignored).
unsigned Game::draw_rect_at(unsigned hl, unsigned bank, unsigned address, int rows, int columns) {
    unsigned clocks = 17 + 4 + 10; // CALL, EI, RET
    unsigned source = address;
    for (int r = 0; r < rows; ++r) {
        clocks +=
            11 + 11 + 4 + 85 + unsigned(99 * columns) + 10 + 10 + 11 + 4 + 7 + 10 + 10 + 13 - 5;
        unsigned at = hl;
        for (int c = 0; c < columns; ++c) {
            const unsigned cell = (at - 0x3800) / 2;
            display.map.at(cell) =
                std::uint16_t(rom.byte(bank, source) | (rom.byte(bank, source + 1) << 8));
            source += 2;
            at += 2;
            if ((at & 0x3F) == 0) {
                at -= 0x40;
                clocks += 135;
            }
        }
        hl += 0x40;
        if ((hl >> 8) == 0x3F) {
            hl = (hl & 0xFF) | 0x3800;
            clocks += 7;
        }
    }
    return clocks - 5;
}

// 327E: D181-DC97 cleared (the clock, camera, player and object state,
// colours, sprites and the decoder's buffers), and D12F.
void Game::clear_game_state() {
    freeze_level_background();
    clock = {};
    camera = {};
    level = {};
    camera_targets = {};
    palette = {};
    sprites = {};
    player_art = {};
    animation = {};
    player = {};
    act = {};
    object_art = {};
    placements = {};
    map_animations = {};
    terrain_state = {};
    player_box = {};
    objects = {};
    const u8 attribute = probe.attribute; // D100 lies below D181
    probe = {};
    probe.attribute = attribute;
    video.water_line_mode = 0;
}

// 428B: D184-D3C0 cleared: the camera and the level header.
void Game::clear_map_state() {
    freeze_level_background();
    camera = {};
    level = {};
}

// 3310: scroll and camera at the origin.
void Game::reset_camera() {
    camera.scroll_x = 0;
    camera.scroll_y = 0;
    camera.x = 0;
    camera.y = 0;
}

} // namespace triple_trouble_gg
