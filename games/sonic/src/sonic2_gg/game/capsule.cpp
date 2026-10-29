// The capsule at the end of each zone's last act (40) and the animals it
// frees (41). It drops in when the camera reaches it, floats, and opens
// when Sonic hits it; Sonic then runs off (state 20) and the zone is
// complete.

#include "sonic2_gg/game/game.hpp"

namespace sonic2_gg {

namespace {
// 9088: per zone, the camera position while the capsule is shown (X, Y);
// it floats down to 50 below that Y.
unsigned capsule_camera_x(const Assets& rom, unsigned zone) {
    return rom.word(rom::capsule_cameras, u8(zone * 4));
}

unsigned capsule_camera_y(const Assets& rom, unsigned zone) {
    return rom.word(rom::capsule_cameras, u8(zone * 4) + 2);
}
} // namespace

// 9174: solid like a bumper; Sonic hitting it from below is sent back
// down at the same speed.
void Game::capsule_touch(Object& o) {
    bumper_object(o); // 021B
    if (!(o.touch_sides & 0x0F)) {
        return;
    }
    Object& s = sonic();
    const unsigned vy = s.vy;
    if (!(vy & 0x8000)) {
        return;
    }
    s.vy = (~(vy - 1)) & 0xFFFF;
}

// 903B (state 4): waits above the screen (40 pixels over the camera) until
// it is less than 128 pixels right of the camera's left edge (or left of
// it); then the camera is fixed for the capsule. Widescreen: above the
// presented view (it then comes down that much further).
void Game::capsule_wait(Object& o) {
    lock_camera_left(); // 0302
    o.display |= display_flag::keep_on_screen;
    o.flags |= 0x80;
    o.display &= u8(~display_flag::offscreen);
    o.y = (camera.y + 0xFFD8u - unsigned(wide_top)) & 0xFFFF;
    const unsigned dx = (unsigned(o.x) - camera.x) & 0xFFFF;
    if (unsigned(o.x) >= camera.x && dx >= 0x0080) {
        return;
    }
    o.next_state = 5;
    script_camera(capsule_camera_x(rom, flow.zone), capsule_camera_y(rom, flow.zone)); // 02F6
}

// 8FFB (state 5): comes down at 0.75 pixels per frame to its place.
void Game::capsule_descend(Object& o) {
    capsule_touch(o);
    o.vy = 0x00C0;
    move_object(o);
    const unsigned target = (capsule_camera_y(rom, flow.zone) + 0x0050) & 0xFFFF;
    if (o.y & 0x8000) {
        return;
    }
    if (target >= unsigned(o.y)) {
        return;
    }
    o.next_state = 1;
    o.vy = 0;
}

// 911C: hit by Sonic: it opens (state 2).
void Game::capsule_check_hit(Object& o) {
    if (!(o.touch_sides & 0x0F)) {
        return;
    }
    o.next_state = 2;
    sound_.request(sound::boss_hit);
}

// 912C / 914E / 913D / 9161: the float accelerates by 40 vertically and 60
// horizontally; the script picks the directions frame by frame.
void Game::capsule_float(Object& o, bool down, bool right) {
    o.vy = (unsigned(o.vy) + (down ? 0x0040u : 0xFFC0u)) & 0xFFFF;
    o.vx = (unsigned(o.vx) + (right ? 0x0060u : 0xFFA0u)) & 0xFFFF;
    move_object(o);
}

// 90DC / 90EC / 90FC / 910C (state 1)
void Game::capsule_hover(Object& o, bool down, bool right) {
    capsule_touch(o);
    capsule_check_hit(o);
    capsule_float(o, down, right);
}

// 90A4 (states 2-3): open. Sonic, once on the ground, runs off (state 20)
// with the zone jingle; the capsule keeps floating, its direction by its
// frame count (IX+1E).
void Game::capsule_open(Object& o) {
    capsule_touch(o);
    Object& s = sonic();
    if (s.state != 0x20 && !(s.flags & 0x01)) {
        s.next_state = 0x20;
        sound_.request(sound::act_clear);
    }
    const u8 phase = o.counter++;
    // 90D4: 0 and 6 down/right, 2 and 4 up/left.
    const bool down_right = (phase & 0x06) == 0 || (phase & 0x06) == 6;
    capsule_float(o, down_right, down_right);
}

// 918E: frees one of 16 animals (41) at the capsule; the kinds alternate
// per zone (table 91E1, 16 per zone).
void Game::capsule_free_animal(Object& o) {
    if (o.timer >= 0x10) {
        return;
    }
    Object* n = find_free_slot(); // 0203
    if (!n) {
        return;
    }
    n->type = object_type::animal;
    n->x = o.x;
    n->y = o.y;
    n->display = o.display & display_flag::mirrored;
    n->tile_base = o.tile_base;
    n->tile_base_mirrored = o.tile_base_mirrored;
    const u8 index = u8(u8(flow.zone << 4) + o.timer);
    ++o.timer;
    n->param = rom.byte(rom::capsule_animals, index);
}

// --- 41: an animal ----------------------------------------------------------

// 9273
void Game::animal_start(Object& o) {
    o.next_state = 1;
}

// 9278 / 9286: the animal's two frames (parameter n: frames 2n-1, 2n).
void Game::animal_frame(Object& o, bool second) {
    o.frame_ticks = 0x05;
    o.frame = u8((o.param - 1) * 2 + 1 + (second ? 1 : 0));
}

// 9295 (state 1): falls to the ground, then hops away left or right (odd
// kinds right): only the high byte of vx is set.
void Game::animal_fall(Object& o) {
    fall(o, 0x0020, 0x0400); // 020F
    move_object(o);
    const u8 type = probe_type(o, 0, 0); // 0263
    if (!(type & 0x80)) {
        return;
    }
    o.next_state = 2;
    o.vx = with_hi(o.vx, (o.param & 0x01) ? 0x02 : 0xFE);
}

// 92C0 (state 2): hops (bouncing with 40 less speed) until off screen.
void Game::animal_hop(Object& o) {
    if (o.display & display_flag::offscreen) {
        o.type = object_type::removed;
        return;
    }
    fall(o, 0x0040, 0x0600);
    move_object(o);
    if (!probe_type(o, 0, 0)) {
        return;
    }
    const unsigned vy = o.vy;
    if (vy & 0x8000) {
        return;
    }
    o.vy = vy <= 0x0040 ? 0 : (0x0040u - vy) & 0xFFFF;
}

} // namespace sonic2_gg
