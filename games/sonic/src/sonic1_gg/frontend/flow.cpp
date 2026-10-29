// The frontend screens before an act: the SEGA logo with its sampled voice,
// the title screen with the attract demos, and the world map. Also the shared
// palette fade used between all screens.

#include "sonic1_gg/frontend/flow.hpp"
#include "sonic1_gg/audio/voice.hpp"
#include <algorithm>
#include <stdexcept>

namespace sonic1_gg {
void FrontEnd::load_screen(unsigned id, unsigned background, unsigned sprites) {
    screen.patterns = assets_.screens.at(id).patterns;
    screen.tiles = assets_.screens.at(id).tiles;
    screen.palette = assets_.palette(background, sprites);
    screen.sprites.clear();
    screen.enabled = true;
}

void FrontEnd::reset() {
    act = demo_cycle = ticks = scene_ticks = logo_step = step_ticks = 0;
    lives = 3;
    demo = false;
    logo_x = 44;
    voice_requested = false;
    sound_events = {{SoundEvent::Kind::Stop, 0}};
    load_screen(0, 0x12, 0x12);
    scene = Scene::Logo;
}

void FrontEnd::patch(unsigned address, unsigned attributes) {
    // 0612: X/Y tile coordinates, a row of one-byte tile IDs, FF terminator.
    unsigned x = assets_.byte(address++), y = assets_.byte(address++), offset = (y * 32 + x) * 2;
    for (unsigned count = 0; count < 256; ++count) {
        unsigned value = assets_.byte(address++);
        if (value == 255) {
            return;
        }
        screen.tiles.at(offset++) = value;
        screen.tiles.at(offset++) = attributes;
    }
    throw std::runtime_error("Unterminated frontend tile row");
}

void FrontEnd::sprite_mapping(unsigned address, int x, int y) {
    // 2F07: three rows of six 8x16 sprites. FE skips a cell; a row whose
    // first byte is FF terminates. Preserve the original coarse clipping.
    if (x < 0 || x >= 208 || y < -24 || y >= 168) {
        return;
    }
    for (int row = 0; row < 3; ++row) {
        if (assets_.byte(address + row * 6) == 255) {
            break;
        }
        for (int col = 0; col < 6; ++col) {
            unsigned tile = assets_.byte(address + row * 6 + col);
            if (tile < 254) {
                screen.sprites.push_back({(x + col * 8) & 255, y + row * 16, tile});
            }
        }
    }
}

void FrontEnd::logo_trademark() {
    patch(0x1faa);
    // Supplied Japan/USA cartridge observed with the emulator's export-region
    // port bit set (1E70). Keep this explicit instead of guessing from language.
    patch(0x1fa5);
}

void FrontEnd::logo_tick(bool start) {
    struct Step {
        unsigned duration, mapping;
        int dx, wave_origin;
        bool reveal;
    };

    // 1D38-1E49, expressed as the thirteen visible motions/poses. These are
    // authored operations over extracted sprite data, not captured video frames.
    static constexpr Step steps[] = {
        {16, 0x1f27, 4, 44, true},    {8, 0x1f39, 0, 0, false},  {16, 0x1f27, 4, 108, true},
        {8, 0x1f39, 0, 0, false},     {10, 0x1f4b, 0, 0, false}, {8, 0x1f5d, 0, 0, false},
        {17, 0x1f6f, -4, 108, false}, {8, 0x1f5d, 0, 0, false},  {15, 0x1f6f, -4, 44, false},
        {8, 0x1f5d, 0, 0, false},     {10, 0x1f4b, 0, 0, false}, {20, 0x1f81, 0, 0, false},
        {80, 0x1f81, 0, 0, false}};
    auto reveal = [&] {
        int edge = (logo_x + 24) & 255;
        if (edge < 72 || edge >= 200) {
            return;
        }
        int col = edge / 8 - 9;
        for (int row = 0; row < 6; ++row) {
            int index = 0x252 + col * 2 + row * 64;
            screen.tiles.at(index) = col + row * 16;
            screen.tiles.at(index + 1) = 0;
        }
    };
    const auto& step = steps[logo_step];
    if (start && step.dx) { // 1E4A: finish the reveal immediately; retain end pose.
        screen.sprites.clear();
        if (step.reveal) {
            sprite_mapping(0x1f93, logo_x + 24, 71);
        }
        do {
            reveal();
            logo_x = (logo_x + 8) & 255;
        } while (((logo_x + 24) & 255) < 200);
        logo_trademark();
        logo_x = 44;
        logo_step = 12;
        step_ticks = 0;
        return;
    }
    screen.sprites.clear();
    int y = 70;
    if (step.reveal) {
        reveal();
        sprite_mapping(0x1f93, logo_x + 24, 71);
    }
    if (step.dx) {
        int value = assets_.byte(0x1f14 + ((logo_x - step.wave_origin) & 255) / 4);
        y += value < 128 ? value : value - 256;
    }
    sprite_mapping(step.mapping, logo_x, y);
    logo_x += step.dx;
    if (++step_ticks < step.duration) {
        return;
    }
    step_ticks = 0;
    ++logo_step;
    if (logo_step == 3) {
        logo_trademark();
    }
    if (logo_step == 12) {
        scene = Scene::Voice;
        scene_ticks = 0;
        voice_requested = true;
        // IRQs are disabled for this sample routine; round its cycle count
        // up to the next host simulation boundary. Audio keeps subframe timing.
        voice_ticks_ = (LogoVoice(assets_.voice).duration + 59735) / 59736;
    } else if (logo_step == 13) {
        fade(Scene::Title, assets_.palette(0x16, 0x16));
    }
}

void FrontEnd::fade(Scene next, Bytes target) {
    scene = Scene::Fade;
    scene_ticks = 0;
    after_fade_ = next;
    fade_target_ = std::move(target);
}

void FrontEnd::show_title() {
    scene = Scene::Title;
    scene_ticks = blink = 0;
    demo = false;
    act = 0;
    lives = 3;
    load_screen(1, 9, 9);
    patch(0x444c);
    patch(0x4451);
    animation_pointer = 0x43dd;
    animation_remaining = 1;
    mapping = 0;
    sound_events.push_back({SoundEvent::Kind::Music, unsigned(Music::Title)});
}

void FrontEnd::show_map(unsigned index, unsigned count, bool automatic) {
    act = index;
    lives = count;
    demo = automatic;
    scene_ticks = map_ticks_ = map_intro_ticks_ = 0;
    if (index == 18) {
        scene = Scene::Black;
        screen.enabled = false;
        screen.sprites.clear();
        return;
    } // 0C05 skips the map for the flyby.
    if (index > 17) {
        throw std::runtime_error("Invalid ordinary-act map index");
    }
    const bool second = index >= 9;
    load_screen(second ? 3 : 2, 0x16, 0x16);

    animation_pointer = assets_.word(0x0ea8 + index * 3);
    animation_remaining = 1;
    mapping = 0;
    fade(Scene::Map, assets_.palette(second ? 12 : 10, second ? 13 : 11));
}

void FrontEnd::map_lives() {
    screen.sprites.clear();
    screen.sprites.push_back({72, 151, 128 + 2 * ((lives / 10) % 10)});
    screen.sprites.push_back({80, 151, 128 + 2 * (lives % 10)});
}

void FrontEnd::map_tick(bool start, bool action, bool directions) {
    map_lives();
    if (!map_ticks_) {
        patch(assets_.word(0x1163 + act * 2), 0x10);
        ++map_ticks_;
        return;
    }
    // 0D76: Act 3's map has two character entrances before the route.
    // 0EA1 loads HL=(D20F), then INC (HL): it does NOT increment the
    // animation counter. With that word zero it writes to ROM, retaining the
    // first pose. Preserve the observed instruction, including this quirk.
    if (assets_.byte(0x0ea8 + act * 3 + 2) == 1 && map_intro_ticks_ < 370) {
        if (start || action || directions) {
            map_intro_ticks_ = 370;
            return;
        }
        if (map_intro_ticks_ < 256) {
            sprite_mapping(assets_.word(0x0e0f), 220 - int(map_intro_ticks_), 50);
        } else {
            sprite_mapping(assets_.word(0x0e17), int(map_intro_ticks_) - 266, 80);
        }
        ++map_intro_ticks_;
        return;
    }
    // The common 0D1A route loop has 300 updates. A five-byte record contains
    // sprite mapping, X, Y, and duration; zero duration redirects to a record.
    if (!--animation_remaining) {
        for (unsigned guard = 0;; ++guard) {
            if (guard > 16) {
                throw std::runtime_error("Map route has no timed pose");
            }
            unsigned next = assets_.word(animation_pointer);
            unsigned x = assets_.byte(animation_pointer + 2),
                     y = assets_.byte(animation_pointer + 3);
            unsigned duration = assets_.byte(animation_pointer + 4);
            if (!duration) {
                animation_pointer = next;
                continue;
            }
            mapping = next;
            route_x = x;
            route_y = y;
            animation_remaining = duration;
            animation_pointer += 5;
            break;
        }
    }
    sprite_mapping(mapping, route_x, route_y);
    if (++map_ticks_ >= 301 || start || action) {
        if (demo && (start || action)) {
            demo = false;
            sound_events.push_back({SoundEvent::Kind::Stop, 0});
            load_screen(0, 0x12, 0x12);
            scene = Scene::Logo;
            scene_ticks = logo_step = step_ticks = 0;
            logo_x = 44;
        } else {
            fade(Scene::Black, assets_.palette(0x16, 0x16));
        }
    }
    (void)directions;
}

void FrontEnd::finish_act(Bytes palette) {
    screen.palette = std::move(palette);
    fade(Scene::Results, assets_.palette(0x16, 0x16));
}

void FrontEnd::finish_life(Bytes palette, bool game_over) {
    // 13E4 / 1819: the same 0AA3 fade as an act exit. A restart then holds
    // black for 60 waits (13EA; D205 bit 0 is set while dead) and stops the
    // sound; game over stops the sound and enters 11E1 directly.
    screen.palette = std::move(palette);
    fade(game_over ? Scene::GameOver : Scene::Black, assets_.palette(0x16, 0x16));
}

void FrontEnd::enter_special(unsigned index, unsigned count) {
    act = index;
    lives = count;
    demo = false;
    fade(Scene::Gameplay, assets_.palette(0x17, 0x17)); // 1797, after ordinary tally.
}

void FrontEnd::finish_demo() {
    ++demo_cycle;
    sound_events = {{SoundEvent::Kind::Fade, 40}};
    fade(Scene::Logo, assets_.palette(0x16, 0x16));
}

void FrontEnd::tick(bool start, bool action, bool directions) {
    sound_events.clear();
    voice_requested = false;
    ++ticks;
    ++scene_ticks;
    switch (scene) {
    case Scene::Logo:
        logo_tick(start);
        break;
    case Scene::Voice:
        if (!--voice_ticks_) {
            scene = Scene::Logo;
            scene_ticks = 0;
        }
        break;
    case Scene::Fade:
        // 0AB7: four waits unchanged, then sixteen one-nibble palette steps,
        // each held for four waits. Background and sprite palettes fade together.
        if (scene_ticks >= 4 && ((scene_ticks - 4) % 4) == 0) {
            for (unsigned i = 0; i < 64; ++i) {
                int low = screen.palette[i] & 15, high = screen.palette[i] >> 4;
                int a = fade_target_[i] & 15, b = fade_target_[i] >> 4;
                low += (low < a) - (low > a);
                high += (high < b) - (high > b);
                screen.palette[i] = low | (high << 4);
            }
        }
        if (scene_ticks == 68) {
            if (after_fade_ == Scene::Title) {
                show_title();
            } else if (after_fade_ == Scene::Logo) {
                sound_events.push_back({SoundEvent::Kind::Stop, 0});
                load_screen(0, 0x12, 0x12);
                scene = Scene::Logo;
                scene_ticks = logo_step = step_ticks = 0;
                logo_x = 44;
            } else {
                scene = after_fade_;
                scene_ticks = 0;
                if (scene == Scene::Black) {
                    screen.enabled = false;
                    screen.sprites.clear();
                }
                if (scene == Scene::Map) {
                    sound_events.push_back({SoundEvent::Kind::Music, unsigned(Music::Map)});
                }
                if (scene == Scene::GameOver) {
                    sound_events.push_back({SoundEvent::Kind::Stop, 0});
                }
            }
        }
        break;
    case Scene::Title:
        blink = (blink + 1) % 100;
        patch(blink < 64 ? 0x43b3 : 0x43c8);
        if (!--animation_remaining) {
            mapping = assets_.word(animation_pointer);
            animation_remaining = assets_.byte(animation_pointer + 2);
            animation_pointer += 3;
            if (!animation_remaining) {
                sound_events.push_back({SoundEvent::Kind::Stop, 0});
                show_map((demo_cycle & 1) ? 6 : 0, 3, true);
                break;
            }
        }
        screen.sprites.clear();
        sprite_mapping(mapping, 136, 32);
        if (start) {
            sound_events.push_back({SoundEvent::Kind::Stop, 0});
            show_map(0, 3, false);
        }
        break;
    case Scene::Map:
        map_tick(start, action, directions);
        break;
    case Scene::Black:
        if (scene_ticks == 60) {
            sound_events.push_back({SoundEvent::Kind::Stop, 0});
            scene = Scene::Gameplay;
            scene_ticks = 0;
        }
        break;
    case Scene::Gameplay:
    case Scene::Results:
    case Scene::GameOver:
        break;
    }
}
} // namespace sonic1_gg
