// Sprites: the objects' sprite table entries (2A14), its upload (27A0), the
// players' streamed art (0A0C), and the HUD ring and tile animations of
// bank 1D.

#include "triple_trouble_gg/game/game.hpp"

namespace triple_trouble_gg {

namespace {
// Sprite frames: per frame a list of offsets (bank 0D: Y, X as words;
// mirrored frames use the list 13B4 bytes further on) and a header (bank
// 0F: Y origin, X origin, address of the tile numbers).
constexpr unsigned offsets_bank = 0x0D, frames_bank = 0x0F;
constexpr unsigned mirrored_offsets = 0x13B4;
// The player's art per frame (bank 1F): bank, address, number of 64-byte
// blocks. Sonic (7FFC), Tails (81C8); the partner's (8348).
constexpr Table sonic_art{0x1F, 0x7FFC};
constexpr Table tails_art{0x1F, 0x81C8};
constexpr Table partner_art{0x1F, 0x8348};
// Bank 1D: the HUD ring's tiles (87C5) and X (87CD) per phase, and the
// phase number kept in D3DD (87D1).
constexpr Table ring_tiles{0x1D, 0x87C5};
constexpr Table ring_x{0x1D, 0x87CD};
constexpr Table ring_numbers{0x1D, 0x87D1};
// The entries the HUD ring uses (58, 59).
constexpr unsigned hud_ring = 58;
} // namespace

// 2A14: with the camera's position (2BCE) each object's frame becomes
// sprite entries: the player's at entries 0-7 (2A88), then the other 23
// slots in order. Unused entries up to 50 (63 with DBC3) are hidden (Y E0);
// the rest belong to the HUD.
void Game::build_sprites() {
    unsigned y_entry = 0, xt_entry = 0;
    build_player_sprites();
    y_entry = 8;
    xt_entry = 8;
    for (unsigned i = 1; i < objects.size(); ++i) {
        auto& o = objects[i];
        if (!o.sprite_count || u8(o.type - 1) >= 0xEF) {
            continue;
        }
        const u8 shown = o.display;
        if (shown & display_flag::flicker) {
            toggle_flicker(o);
        }
        if (shown & (display_flag::hidden | display_flag::offscreen)) {
            continue;
        }
        draw_object(o, y_entry, xt_entry);
    }
    if (y_entry > 64) {
        throw Unrecovered("more than 64 sprites (the Y entries run into DB40)");
    }
    // 2A5A: from the next entry to 50 (to 63 with DBC3) when the next is
    // at most 49 (62); LDIR writes one entry more than the count it takes.
    const unsigned last = sprites.hud_small ? 0x3E : 0x31;
    if (y_entry <= last) {
        for (unsigned e = y_entry; e <= last + 1; ++e) {
            sprites.table[e].y = 0xE0;
            sprites.table[e].placed = false;
        }
    }
    video.sprites_dirty = 0xFF;
}

// 2A88: the player's frame at entries 0-7; entries it does not use (all of
// them while hidden) get Y E1, X 0, tile 0.
void Game::build_player_sprites() {
    auto& p = objects[0];
    unsigned y_entry = 0, xt_entry = 0;
    unsigned unused = 8;
    if (p.sprite_count) {
        const u8 shown = p.display;
        if (shown & display_flag::flicker) {
            toggle_flicker(p);
        }
        if (!(shown & (display_flag::hidden | display_flag::offscreen))) {
            draw_object(p, y_entry, xt_entry);
            unused = p.sprite_count >= 8 ? 0 : 8u - p.sprite_count;
        }
    }
    for (unsigned i = 0; i < unused; ++i) {
        sprites.table[(y_entry + i) & 63].y = 0xE1;
        sprites.table[(y_entry + i) & 63].placed = false;
    }
    for (unsigned i = 0; i < unused; ++i) {
        auto& s = sprites.table[(xt_entry + i) & 63];
        s.x = 0;
        s.tile = 0;
    }
}

// 2BBA: a blinking object is hidden for two frames out of four.
void Game::toggle_flicker(Object& o) {
    ++o.flicker_ticks;
    if (o.flicker_ticks & 0x02) {
        o.display |= display_flag::hidden;
    } else {
        o.display &= u8(~display_flag::hidden);
    }
}

// 2BF5 = 2BCE (the position on screen), 2AD6 (the entries' Y) and 2B2C
// (their X and tiles). A sprite more than 16 pixels above the screen or
// below line BF gets Y E0; one off the sides X 0.
void Game::draw_object(Object& o, unsigned& y_entry, unsigned& xt_entry) {
    // 2C06: with the water line armed the same, with D133 set and
    // interrupts held off meanwhile (the line's colour change waits).
    const bool armed = video.water_line_mode == 0xFF;
    if (armed) {
        video.busy = 0xFF;
    }
    o.screen_x = std::uint16_t(o.x - camera.x);
    o.screen_y = std::uint16_t(o.y - camera.y);
    const bool mirrored = o.display & display_flag::mirrored;
    const unsigned header = o.frame_header;
    // 2AD6
    const std::uint16_t y_base = std::uint16_t(cpu_word(frames_bank, header) + o.screen_y);
    unsigned offset = o.layout;
    for (unsigned i = 0; i < o.sprite_count; ++i) {
        const std::uint16_t y = std::uint16_t(cpu_word(offsets_bank, offset) + y_base);
        offset += 4;
        const unsigned shifted = (y + 0x40u) & 0xFFFF;
        const bool visible = shifted <= 0xFF && shifted >= 0x30;
        auto& s = sprites.table[y_entry & 63];
        s.y = visible ? u8(y) : 0xE0;
        s.full_y = std::int16_t(y); // widescreen
        s.placed_y = s.y;
        s.owner = o.type;
        ++y_entry;
    }
    // 2B2C
    std::uint16_t x_origin = cpu_word(frames_bank, header + 2);
    if (mirrored) {
        x_origin = std::uint16_t(-x_origin);
    }
    unsigned tiles = cpu_word(frames_bank, header + 4);
    const std::uint16_t x_base = std::uint16_t(o.screen_x + x_origin);
    offset = o.layout + 2 + (mirrored ? mirrored_offsets : 0);
    for (unsigned i = 0; i < o.sprite_count; ++i) {
        const std::uint16_t x = std::uint16_t(cpu_word(offsets_bank, offset) + x_base);
        offset += 4;
        auto& s = sprites.table[xt_entry & 63];
        s.x = (x >> 8) ? 0 : u8(x);
        s.tile =
            u8(cpu_byte(frames_bank, tiles++) + (mirrored ? o.tile_base_mirrored : o.tile_base));
        s.full_x = std::int16_t(x); // widescreen
        s.placed_x = s.x;
        s.placed = true;
        ++xt_entry;
    }
    if (armed) {
        video.busy = 0;
    }
}

// 27A0: the sprite table goes to the chip when changed. Even and odd
// frames use different orders (the hardware shows earlier entries in front,
// and shows at most eight per line; the alternation spreads what is lost):
// with DBC0 clear the player's entries 0-7 first, then 8-63 forward on even
// frames and backward on odd ones; with DBC0 set the player's entries last
// on even frames and everything backward on odd ones.
void Game::upload_sprites() {
    if (!video.sprites_dirty) {
        return;
    }
    video.sprites_dirty = 0;
    const bool odd = ++video.sprite_parity & 1;
    std::array<unsigned, 64> order{};
    unsigned n = 0;
    if (!sprites.player_last) {
        for (unsigned e = 0; e < 8; ++e) {
            order[n++] = e;
        }
        for (unsigned k = 8; k < 64; ++k) {
            order[n++] = odd ? 63 - (k - 8) : k;
        }
    } else if (!odd) {
        for (unsigned e = 8; e < 64; ++e) {
            order[n++] = e;
        }
        for (unsigned e = 0; e < 8; ++e) {
            order[n++] = e;
        }
    } else {
        for (unsigned k = 0; k < 64; ++k) {
            order[n++] = 63 - k;
        }
    }
    for (unsigned i = 0; i < 64; ++i) {
        const auto& s = sprites.table[order[i]];
        auto& d = display.sprites[i];
        d.y = s.y;
        d.x = s.x;
        d.tile = s.tile;
        d.entry = u8(order[i]);
        // Widescreen: the full position holds while the entry has the bytes
        // it was placed with (the HUD and other code move entries by bytes).
        d.placed = s.placed && s.y == s.placed_y && s.x == s.placed_x;
        d.full_x = d.placed ? s.full_x : 0;
        d.full_y = d.placed ? s.full_y : 0;
        d.owner = d.placed ? s.owner : 0;
        d.repeat = false;
    }
}

// 0A0C: the players' art for their current frames, streamed into tiles 0-15
// (the player, 0A6C) and 16-31 (the partner, 0A12) when bit 7 asks for it,
// mirrored with bit 6. Frame 0 clears the tiles.
void Game::upload_player_art() {
    auto stream = [&](u8& mode, u8 frame, Table table, unsigned vram) {
        if ((mode & 0xA0) != 0xA0) {
            return;
        }
        if (!frame) {
            display.fill_art(vram, 0x200, 0);
            return; // 0AC9/0AD1: bit 7 stays set
        }
        const unsigned at = frame * 4u;
        const unsigned bank = rom.byte(table, at);
        const unsigned source = rom.word(table, at + 1);
        const unsigned blocks = rom.byte(table, at + 3);
        const bool mirrored = mode & 0x40;
        // 0B74 copies 64 bytes per block; 0CB9 the same through the
        // bit-reversing table at 0100.
        for (unsigned i = 0; i < blocks * 64u; ++i) {
            u8 b = rom.byte(bank, source + i);
            if (mirrored) {
                b = rom.byte(0, 0x0100 + b);
            }
            display.put_art(vram + i, std::span<const u8>(&b, 1));
        }
        mode &= 0x7F;
    };
    stream(player_art.mode, player_art.frame, objects[0].type == 1 ? sonic_art : tails_art, 0x0000);
    if ((player_art.partner_mode & 0xA0) == 0xA0 && !(objects[1].display & 0x40)) {
        stream(player_art.partner_mode, player_art.partner_frame, partner_art, 0x0200);
    }
}

// 0B01: the player's art is asked for when the frame or the mirroring
// changes.
void Game::track_player_art() {
    u8& mode = player_art.mode;
    mode = u8((mode & 0x80) | ((objects[0].display << 2) & 0x40) | 0x20);
    if (player_art.mode_seen != mode) {
        player_art.mode_seen = mode;
        mode |= 0x80;
    }
    if (player_art.frame != objects[0].frame) {
        player_art.frame = objects[0].frame;
        mode |= 0x80;
    }
}

// 0B2E: the same for the partner (type 8) while it is shown.
void Game::track_partner_art() {
    const auto& partner = objects[1];
    if (partner.type != 8 || player.partner_hidden || (partner.display & 0x40)) {
        player_art.partner_mode = 0;
        return;
    }
    u8& mode = player_art.partner_mode;
    mode = u8((mode & 0x80) | ((partner.display << 2) & 0x40) | 0x20);
    if (player_art.partner_mode_seen != mode) {
        player_art.partner_mode_seen = mode;
        mode |= 0x80;
    }
    if (player_art.partner_frame != partner.frame) {
        player_art.partner_frame = partner.frame;
        mode |= 0x80;
    }
}

// Bank 1D, 8770: every eight frames the ring animation's next phase (of
// four): its number, and its 128 bytes of art when the act has them. Not
// while the player's art waits to be streamed.
void Game::animate_tiles() {
    if (player_art.mode & 0x80) {
        return;
    }
    if (++animation.ring_ticks & 0x07) {
        return;
    }
    animation.ring_phase = u8((animation.ring_phase + 1) & 0x03);
    animation.ring_tile = rom.byte(ring_numbers, animation.ring_phase);
    if (act.variant) {
        return;
    }
    if (!animation.ring_vram) {
        return;
    }
    display.put_art(animation.ring_vram,
                    rom.bytes(0x1D, animation.ring_art + animation.ring_phase * 128u, 128));
}

// Bank 1D, 87D5: the HUD's ring (entries 58 and 59) turns with the phase.
void Game::spin_hud_ring() {
    const unsigned phase = animation.ring_phase;
    sprites.table[hud_ring].tile = rom.byte(ring_tiles, phase * 2);
    sprites.table[hud_ring + 1].tile = rom.byte(ring_tiles, phase * 2 + 1);
    sprites.table[hud_ring].x = rom.byte(ring_x, phase);
}

namespace {
// Bank 1D, 803E: the tile animations that cycle art: every `period` frames
// the next of `phases` copies (source in bank 1D, VRAM address, bytes).
// The numbers are the handlers' own operands (80A7, 80FE, ...). Those
// with `gated` (8084) wait while the player's art is being streamed.
struct Copy {
    std::uint16_t source, vram, bytes;
};

struct CyclingAnimation {
    u8 type, period, phases;
    bool gated;
    std::array<Copy, 4> copies;
    std::array<Copy, 4> second{}; // a further copy per phase (bytes 0: none)
    bool dry{};                   // not while the water line is armed (D12F)
};

constexpr CyclingAnimation cycling_animations[] = {
    {0x01,
     8,
     4,
     true,
     {{{0x8C14, 0x2E60, 0x80},
       {0x8C94, 0x2E60, 0x80},
       {0x8D14, 0x2E60, 0x80},
       {0x8D94, 0x2E60, 0x80}}}},
    {0x02,
     10,
     4,
     true,
     {{{0x8E14, 0x2D60, 0x120},
       {0x8F74, 0x2D60, 0x120},
       {0x90D4, 0x2D60, 0x120},
       {0x9234, 0x2D60, 0x120}}}},
    {0x04, 4, 2, true, {{{0x9594, 0x2960, 0x80}, {0x9614, 0x2960, 0x80}}}},
    {0x05,
     8,
     4,
     true,
     {{{0x94D4, 0x2FC0, 0x40},
       {0x9514, 0x2FC0, 0x40},
       {0x9554, 0x2FC0, 0x40},
       {0x9514, 0x2FC0, 0x40}}}},
    {0x0A, 2, 2, false, {{{0x9914, 0x1840, 0x80}, {0x9994, 0x1840, 0x80}}}},
    {0x0F,
     8,
     4,
     true,
     {{{0x96D4, 0x3360, 0x40},
       {0x9714, 0x3360, 0x40},
       {0x9754, 0x3360, 0x40},
       {0x9794, 0x3360, 0x40}}}},
    {0x12, 2, 2, false, {{{0x9C94, 0x1800, 0x40}, {0x9D14, 0x1800, 0x40}}}},
    {0x13, 2, 2, false, {{{0x9694, 0x2A60, 0x20}, {0x96B4, 0x2A60, 0x20}}}},
    {0x07, 3, 2, true, {{{0x9394, 0x3460, 0x20}, {0x93B4, 0x3460, 0x20}}}, {}, true},
    {0x08,
     8,
     2,
     true,
     {{{0x93D4, 0x37A0, 0x60}, {0x9434, 0x37A0, 0x60}}},
     {{{0x9494, 0x3360, 0x20}, {0x94B4, 0x3360, 0x20}}},
     true},
};
} // namespace

// Bank 1D, 8425 (type 0B): every 32 frames the title's line at 3C54 (12
// entries) alternates between the selection's text (table 845B: 1D:BCD0
// for 0, 1D:BD00 otherwise) and 1D:BCE8.
void Game::animate_title_text(AnimationState::Animator& a) {
    u8& phase = a.bytes[1];
    u8& counter = a.bytes[2];
    if (++counter < 0x20) {
        return;
    }
    counter = 0;
    phase = phase + 1 < 2 ? 1 : 0;
    const unsigned text =
        (phase & 1) ? 0xBCE8 : rom.word(Table{0x1D, 0x845B}, flow.next_mode_hint * 2u);
    draw_rect(0x3C54, 0x1D, text, 1, 0x0C); // 044A
}

// Bank 1D, 8155 (type 03): unless the colour cycles are off, every 4
// frames background colour 15's green and red byte (D4D1) takes the
// counter, which runs 0 to 0F and back (byte 1 bit 0: downward).
void Game::pulse_color(AnimationState::Animator& a) {
    if (act.palette_cycle_off) {
        return;
    }
    u8& direction = a.bytes[0];
    u8& value = a.bytes[1];
    u8& counter = a.bytes[2];
    if (++counter < 0x04) {
        return;
    }
    palette.dirty = 0xFF;
    counter = 0;
    palette.colors[0x1E] = value;
    if (!(direction & 1)) {
        if (++value >= 0x0F) {
            ++direction;
        }
    } else if (!--value) {
        ++direction;
    }
}

// Bank 1D, 8470 (type 0C): a hit boss flashes: every 4 frames a sprite
// colour (31 in zones 0 and 1, else 30) turns white, then the zone's flash
// colour (DBDC); after the two the animator ends (25CE).
void Game::boss_flash(AnimationState::Animator& a) {
    u8& phase = a.bytes[1];
    u8& counter = a.bytes[2];
    if (counter) {
        --counter;
        return;
    }
    counter = 0x03;
    const unsigned colour = (flow.zone == 0 || flow.zone == 1) ? 31 : 30;
    const std::uint16_t c = phase ? act.flash_color : 0x0FFF;
    palette.colors[colour * 2] = lo(c);
    palette.colors[colour * 2 + 1] = hi(c);
    palette.dirty = 0xFF;
    if (++phase >= 2) {
        a.type = 0; // 25CE (0309): the type and its first three bytes
        a.bytes[0] = a.bytes[1] = a.bytes[2] = 0;
    }
}

// Bank 1D, 84D3 (type 0D): every 8 frames background colour 13 steps
// toward 0262, then back toward black, and so on (byte 1 counts the
// turns).
void Game::breathe_color(AnimationState::Animator& a) {
    if ((video.frame_counter & 0x07) != 0x07) {
        return;
    }
    const std::uint16_t target = (a.bytes[0] & 1) ? 0x0000 : 0x0262;
    const std::uint16_t c =
        step_color(std::uint16_t(palette.colors[26] | palette.colors[27] << 8), target);
    palette.colors[26] = lo(c);
    palette.colors[27] = hi(c);
    palette.dirty = 0xFE;
    if (c == target) {
        ++a.bytes[0];
    }
}

// Bank 1D, 8515 (type 0E): background colours 14 and 15 flash in a cycle
// of four steps (handlers 8537, 8550, 8569, 8582: the frames each lasts
// and the two colours).
void Game::lightning(AnimationState::Animator& a) {
    u8& step = a.bytes[0];
    u8& wait = a.bytes[2];
    if (wait) {
        --wait;
        return;
    }

    struct Step {
        u8 frames;
        std::uint16_t colour14, colour15;
    };

    static constexpr Step steps[4] = {{0x30, 0x0000, 0x0000},
                                      {0x03, 0x0FDD, 0x0000},
                                      {0x30, 0x0000, 0x0000},
                                      {0x03, 0x0000, 0x0DFF}};
    const Step& st = steps[step & 3];
    wait = st.frames;
    step = (step & 3) == 3 ? 0 : u8(step + 1);
    palette.colors[28] = lo(st.colour14);
    palette.colors[29] = hi(st.colour14);
    palette.colors[30] = lo(st.colour15);
    palette.colors[31] = hi(st.colour15);
    palette.dirty = 0xFF;
}

// Bank 1D, 82B5 (type 9): at a zone's end the ground opens under the
// player. First: the other animators cleared, sound CF, the player falls
// (state 4B), the background's art animates (type 0A in the second slot),
// the camera stays where it is, the hole's art (1D:97D4) at tile C0 and
// the falling ground (type 4C) appear. Then every other frame for 26
// rows a row of 32 entries (1D:9A14 / 9AD4 by turns, 3 lines) is drawn at
// the place table 832C names, going back up the table.
void Game::open_ground(AnimationState::Animator& a) {
    u8& started = a.bytes[0];
    u8& rows = a.bytes[1];
    u8& ticks = a.bytes[2];
    if (!started) {
        for (unsigned i = 1; i < animation.animators.size(); ++i) {
            animation.animators[i] = {};
        }
        animation.opening = 0;
        animation.opening_row = 0;
        sound_.request(0xCF, 1);
        auto& p = objects[0];
        p.flags &= u8(~0x08);
        p.next_state = 0x4B;
        animation.animators[1].type = 0x0A;
        started = 0xFF;
        animation.opening = 0xFF;
        rows = 0x1A;
        ticks = 0x02;
        script_camera(camera.x, camera.y); // 4CDA (0321)
        animation.opening_row = 0x832C;
        display.put_art(0x1800, rom.bytes(0x1D, 0x97D4, 0x140)); // 26C8 (030C)
        // 50E1 (0354) from the frame interrupt: the spawner is whatever IX
        // holds, here the sprite table (DB00) the sprite builder (2A14)
        // left, so the spawner's tag (byte 31: its slot number + 1, 19)
        // lands in sprite entry 49's Y until the next build.
        for (unsigned i = 2; i < 18; ++i) {
            if (!objects[i].type) {
                objects[i].type = 0x4C;
                objects[i].param = 0;
                sprites.table[49].y = 0x19;
                break;
            }
        }
        return;
    }
    if (--ticks) {
        return;
    }
    ticks = 0x02;
    const unsigned place = rom.word(0x1D, animation.opening_row);
    draw_rect(place, 0x1D, (rows & 1) ? 0x9AD4 : 0x9A14, 0x03, 0x20); // 2C3B (044A)
    animation.opening_row = std::uint16_t(animation.opening_row - 2);
    if (!--rows) {
        stop_music(); // 2288 (033F)
        a.type = 0;
    }
}

// Bank 1D, 8625 (type 11): Robotnik Winter's version: the objects stop
// (D3FC), the camera stops, the second slot animates (type 12), the art
// 1D:9B94 at tile C0; then every other frame for 26 rows a row (1D:9D94 /
// 9E54 by turns) at the places of table 8332 going down; at the end the
// act ends (D144 bit 4).
void Game::freeze_over(AnimationState::Animator& a) {
    u8& started = a.bytes[0];
    u8& rows = a.bytes[1];
    u8& ticks = a.bytes[2];
    if (!started) {
        for (unsigned i = 1; i < animation.animators.size(); ++i) {
            animation.animators[i] = {};
        }
        animation.opening = 0;
        animation.opening_row = 0;
        sound_.request(0xCF, 1);
        animation.animators[1].type = 0x12;
        started = 0xFF;
        act.objects_frozen = 0xFF;
        animation.opening = 0xFF;
        rows = 0x1A;
        ticks = 0x02;
        disable_map(); // 4CCE (0312)
        animation.opening_row = 0x8332;
        display.put_art(0x1800, rom.bytes(0x1D, 0x9B94, 0x100)); // 26C8 (030C)
        return;
    }
    if (--ticks) {
        return;
    }
    ticks = 0x02;
    const unsigned place = rom.word(0x1D, animation.opening_row);
    draw_rect(place, 0x1D, (rows & 1) ? 0x9E54 : 0x9D94, 0x03, 0x20); // 2C3B (044A)
    animation.opening_row = std::uint16_t(animation.opening_row + 2);
    if (!--rows) {
        stop_music(); // 2288 (033F)
        a.type = 0;
        flow.frame_work |= 0x10;
    }
}

// Bank 1D, 85F3 (type 10): background colour 12 white for two frames,
// then black, and the animator ends (25CE).
void Game::flash_once(AnimationState::Animator& a) {
    u8& phase = a.bytes[0];
    u8& wait = a.bytes[2];
    if (wait) {
        --wait;
        return;
    }
    if (!phase) {
        wait = 0x01;
        phase = 0xFF;
        palette.colors[24] = 0xFF;
        palette.colors[25] = 0x0F;
        palette.dirty = 0x01;
        return;
    }
    palette.colors[24] = 0x00;
    palette.colors[25] = 0x00;
    palette.dirty = 0x01;
    a.type = 0; // 25CE (0309)
    a.bytes[0] = a.bytes[1] = a.bytes[2] = 0;
}

// 25DC (04DD): a colour (----BBBB GGGGRRRR) a step toward `target` in
// each component.
std::uint16_t Game::step_color(std::uint16_t c, std::uint16_t target) {
    const auto step = [](unsigned v, unsigned t) {
        return v == t ? v : v > t ? v - 1 : v + 1;
    };
    const unsigned b = step(hi(c), hi(target)) & 0xFF;
    const unsigned g = step((c >> 4) & 0x0F, (target >> 4) & 0x0F) & 0x0F;
    const unsigned r = step(c & 0x0F, target & 0x0F) & 0x0F;
    return std::uint16_t(b << 8 | g << 4 | r);
}

// 259E: an animator of `type` in the first free place.
void Game::start_animator(u8 type) {
    for (auto& a : animation.animators) {
        if (!a.type) {
            a.type = type;
            return;
        }
    }
}

// Bank 1D, 8000: the act's four tile animations (D490): not while the
// background fades or (unless the second is 0A or 12) during D144 bits 2/4.
void Game::run_animators() {
    if (act.event) {
        return;
    }
    const u8 second = animation.animators[1].type;
    if (second != 0x0A && second != 0x12) {
        if (flow.frame_work & 0x14) {
            return;
        }
        if (palette.fades[0].mode) {
            return;
        }
    }
    for (auto& a : animation.animators) {
        if (!a.type) {
            continue; // 8066
        }
        if (a.type == 0x0B) {
            animate_title_text(a); // 8425
            continue;
        }
        if (a.type == 0x03) {
            pulse_color(a); // 8155
            continue;
        }
        if (a.type == 0x0C) {
            boss_flash(a); // 8470
            continue;
        }
        if (a.type == 0x0D) {
            breathe_color(a); // 84D3
            continue;
        }
        if (a.type == 0x0E) {
            lightning(a); // 8515
            continue;
        }
        if (a.type == 0x10) {
            flash_once(a); // 85F3
            continue;
        }
        if (a.type == 0x11) {
            freeze_over(a); // 8625
            continue;
        }
        if (a.type == 0x09) {
            // 82B5. Its first pass spawns an object (50E1), which leaves
            // the loop's pointer (IY) on that object: the remaining
            // "animators" are its zero bytes, so none runs this frame.
            const bool first = !a.bytes[0];
            open_ground(a);
            if (first) {
                break;
            }
            continue;
        }
        const CyclingAnimation* c = nullptr;
        for (const auto& k : cycling_animations) {
            if (k.type == a.type) {
                c = &k;
            }
        }
        if (!c) {
            throw Unrecovered("tile animation type " + std::to_string(a.type) + " (803E)");
        }
        if (c->dry && video.water_line_mode) {
            continue;
        }
        // 8067 / 8084: the frame counter, then the next phase.
        u8& phase = a.bytes[1];
        u8& counter = a.bytes[2];
        if (++counter < c->period) {
            continue;
        }
        if (c->gated && (player_art.mode & 0x80)) {
            continue;
        }
        counter = 0;
        phase = u8(phase + 1 < c->phases ? phase + 1 : 0);
        const unsigned i = phase & (c->phases - 1);
        const Copy& copy = c->copies[i];
        display.put_art(copy.vram, rom.bytes(0x1D, copy.source, copy.bytes));
        if (c->second[i].bytes) {
            display.put_art(c->second[i].vram,
                            rom.bytes(0x1D, c->second[i].source, c->second[i].bytes));
        }
    }
}

} // namespace triple_trouble_gg

namespace triple_trouble_gg {

namespace {
// 7D1B: per art request (10 on) its list: bank (bit 7: mirrored), tile
// count, VRAM address, source; FF ends.
constexpr Table art_lists{0x01, 0x7D1B};
} // namespace

// 7C15: object art streamed in, at most four tiles per frame: a waiting
// request (D434, then D435) starts once none runs; each list entry's tiles
// are copied (through the bit-reversing table when mirrored).
void Game::stream_object_art() {
    auto& a = object_art;
    if (!a.request) {
        // 7CF9
        if (act.object_mode) {
            return;
        }
        if (a.queued) {
            a.request = a.queued;
            a.queued = 0;
        } else if (a.queued_next) {
            a.request = a.queued_next;
            a.queued_next = 0;
        }
        return;
    }
    if (!a.bank) {
        a.list = rom.word(art_lists, u8((a.request - 0x10) * 2)); // 7CA9
        next_art_block();
    }
    if (!a.request) {
        return;
    }
    if (!a.tiles_left) {
        next_art_block(); // 7CC0
    }
    if (!a.tiles_left) {
        finish_object_art(); // 7CE5 (the copy loop finds nothing to do)
        return;
    }
    const unsigned bank = a.bank & 0x1F;
    const bool mirrored = a.bank & 0x80;
    const unsigned tiles = a.tiles_left < 4 ? a.tiles_left : 4;
    for (unsigned i = 0; i < tiles * 32u; ++i) {
        u8 b = rom.byte(bank, a.source + i);
        if (mirrored) {
            b = rom.byte(0, 0x0100 + b);
        }
        display.put_art(a.vram + i, std::span<const u8>(&b, 1));
    }
    a.source = std::uint16_t(a.source + tiles * 32u);
    a.vram = std::uint16_t(a.vram + 0x80);
    a.tiles_left = a.tiles_left >= 4 ? u8(a.tiles_left - 4) : 0;
}

// 7CC0: the next list entry, or the end.
void Game::next_art_block() {
    auto& a = object_art;
    const Table entry{0x01, a.list};
    if (rom.byte(entry) == 0xFF) {
        finish_object_art();
        return;
    }
    a.bank = rom.byte(entry, 0);
    a.tiles_left = rom.byte(entry, 1);
    a.vram = rom.word(entry, 2);
    a.source = rom.word(entry, 4);
    a.list = std::uint16_t(a.list + 6);
}

// 7CE5
void Game::finish_object_art() {
    auto& a = object_art;
    a.request = 0;
    a.bank = 0;
    a.tiles_left = 0;
    a.vram = 0;
    a.source = 0;
}

} // namespace triple_trouble_gg
