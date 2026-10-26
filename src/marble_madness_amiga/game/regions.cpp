#include "marble_madness_amiga/game/regions.hpp"
#include <stdexcept>

namespace marble_madness::game {
namespace {

// Script opcodes ($110198), one word each, then their operands.
enum Op : std::int16_t {
    keyframe = 0,      // x, y (cells), z, kind, code [, width, height, frames]: place the region
    move = 1,          // rounds, period: step x/y by the velocity, through the move frames
    animate = 2,       // rounds, period: show the animation frames
    hold = 3,          // time: wait
    loop = 4,          // count: start a loop
    loop_end = 5,
    loop2 = 6,
    loop2_end = 7,
    if_marble_near = 8, // 0, target: jump when a racing marble is within 3 cells
    jump = 9,
    call = 10,
    ret = 11,
    animation_frames = 12, // list
    move_frames = 13,      // list
    velocity = 14,         // vx, vy
    finish = 15,           // stop this region
    step_once = 16,        // add the velocity once
    wait_for_feature = 17, // feature: wait until a marble enters that feature
    if_feature_free = 18,  // target: jump when no marble is in the feature
};

} // namespace

Regions::Regions(const data::GameFiles& files, data::Course course)
    : track_(&files.track(course)), course_(course) {
    block_ = track_->u32(files.track_header(course) + 0x14);
}

std::uint32_t Regions::block_entry(int n) const {
    return track_->u32(block_ + std::uint32_t(n) * 4);
}

Region* Regions::free_slot() {
    for (auto& r : regions_) {
        if (!r.active) {
            return &r;
        }
    }
    return nullptr;
}

bool Regions::running(std::uint32_t entry) const {
    const std::uint32_t script = track_->u32(entry + 2);
    for (const auto& r : regions_) {
        if (r.active && (r.script == script || (script == 0 && r.code == 12))) {
            return true;
        }
    }
    return false;
}

void Regions::start(Region& r, std::uint32_t script) {
    r.script = script;
    r.state = 3;
    r.active = true;
}

void Regions::stop(Region& r) {
    r.active = false;
    r.state = 0;
    // Not yet rebuilt: the display-object removal for unpictured regions
    // ($1120F4) and the course-specific bookkeeping of codes 6 and 10-13.
}

void Regions::bands_changed(int from, int to) {
    // The list (block +0): band range (two signed bytes) and a script; $FF ends it.
    for (std::uint32_t e = block_entry(0); track_->u8(e) != 0xFF; e += 6) {
        const int first = track_->s8(e), last = track_->s8(e + 1);
        if (running(e) || !(first == to || last == to) || !(from < first || last < from)) {
            continue;
        }
        const std::uint32_t script = track_->u32(e + 2);
        if (script == 0) {
            // A random group of four (Aerial, Silly, Ultimate): not rebuilt yet.
            continue;
        }
        Region* slot = free_slot();
        if (slot == nullptr) {
            break;
        }
        slot->band_from = std::int16_t(first);
        slot->band_to = std::int16_t(last);
        start(*slot, script);
    }
    for (auto& r : regions_) {
        if (r.active && ((from == r.band_from && to < r.band_from) || (from == r.band_to && r.band_to < to))) {
            stop(r);
        }
    }
}

void Regions::update(Terrain& terrain) {
    terrain_ = &terrain;
    for (auto& r : regions_) {
        step(r);
    }
}

bool Regions::next_frame(Region& r) {
    r.frame += 4;
    if (r.kind != 0) {
        r.frame += 4; // pictured frames: picture and height patch
    }
    if (track_->u32(r.frame) == 0xFFFFFFFF) {
        r.frame = r.state == 2 ? r.anim_frames : r.move_frames;
        if (r.count != 0 && --r.count == 0) {
            return true;
        }
    }
    return false;
}

void Regions::show_frame(Region& r) {
    if (r.kind == 1 || r.kind == 2) {
        if (track_->u32(r.frame) == 0xFFFFFFFF) {
            return;
        }
        if (r.kind != 2 || r.state == 2 || r.state == 1) {
            // $10D4xx: a pictured region draws the frame's picture into the
            // playfield ($110A46), and a frame's second long is a patch of
            // the animated heights: first index, count, then the codes.
            if (r.active) {
                if (r.kind == 1 && r.code != 0x19) {
                    // Codes 6, 11, 12 and 13 pick other cells ($110A46);
                    // not rebuilt yet.
                    const std::uint32_t item = track_->u32(r.frame);
                    blits_.push_back({item, std::int16_t(r.screen_x + r.width * 8),
                                      std::int16_t(r.height * 8 - r.screen_y - view_scroll)});
                }
                const std::uint32_t patch = track_->u32(r.frame + 4);
                if (patch != 0) {
                    const int first = track_->s16(patch), count = track_->s16(patch + 2);
                    for (int i = 0; i < count; ++i) {
                        const std::size_t at = std::size_t(first + i);
                        if (at < terrain_->animated.size()) {
                            terrain_->animated[at] = track_->u16(patch + 4 + std::uint32_t(i) * 2);
                        }
                    }
                }
            }
            if (r.kind != 2) {
                return;
            }
        }
    }
    // $106A00: the region's place on the screen.
    const auto x = std::int16_t(r.x >> 16), y = std::int16_t(r.y >> 16), z = std::int16_t(r.z >> 16);
    r.screen_x = std::int16_t(y - x + 0x88);
    r.screen_y = std::int16_t(std::int16_t(view) + z + 0x6C - std::int16_t((x + y) >> 1));
    r.shown_frame = r.frame;
}

void Regions::step(Region& r) {
    if (!r.active) {
        return;
    }
    std::uint8_t entered = r.state;
    bool idle = false;
    while (true) {
        switch (r.state) {
        case 0:
            if (r.count != 0 && --r.count == 0 && r.active) {
                interpret(r);
            }
            idle = true;
            break;
        case 1:
            if (r.move_period != 0) {
                ++r.move_tick;
            }
            if (r.move_tick == r.move_period) {
                r.move_tick = 0;
                r.x += r.vx;
                r.y += r.vy;
                show_frame(r);
                if (next_frame(r)) {
                    interpret(r);
                }
            } else {
                idle = true;
            }
            break;
        case 2:
            if (r.anim_period != 0) {
                ++r.anim_tick;
            }
            if (r.anim_tick == r.anim_period) {
                r.anim_tick = 0;
                show_frame(r);
                if (next_frame(r)) {
                    interpret(r);
                }
            } else {
                idle = true;
            }
            break;
        case 3:
            r.pc = r.script;
            r.anim_frames = r.move_frames = r.frame = block_entry(1);
            interpret(r);
            break;
        case 4:
            r.frame = block_entry(1);
            idle = true;
            break;
        }
        // A region that has just started moving with no period moves at once.
        if (entered == r.state || r.state != 1 || r.move_period != 0) {
            break;
        }
        entered = r.state;
    }
    if (r.kind != 1 && idle) {
        show_frame(r);
    }
}

void Regions::interpret(Region& r) {
    const auto& t = *track_;
    auto word = [&] {
        const std::int16_t w = t.s16(r.pc);
        r.pc += 2;
        return w;
    };
    auto address = [&] {
        const std::uint32_t a = t.u32(r.pc);
        r.pc += 4;
        return a;
    };
    for (int guard = 0; guard < 10000; ++guard) {
        switch (word()) {
        case keyframe: {
            r.x = std::int32_t(word()) << 19;
            r.y = std::int32_t(word()) << 19;
            r.z = std::int32_t(word()) << 16;
            r.kind = std::uint8_t(word());
            r.code = std::int8_t(word());
            r.shown_frame = block_entry(1);
            if (r.kind == 1) {
                r.width = word();
                r.height = word();
                r.anim_frames = r.frame = address();
                r.screen_x = r.screen_y = 0;
                show_frame(r);
            } else {
                r.frame = block_entry(1);
                // Not yet rebuilt: the display object ($112004).
            }
            switch (r.code) {
            case 0x19: case 10: case 12: case 13: case 11: case 3: case 6:
                // Course-specific starts ($110350-$1103F6): not rebuilt yet.
                return;
            default:
                break;
            }
            break;
        }
        case move:
            r.count = word();
            r.move_period = std::uint8_t(word());
            r.frame = r.move_frames;
            r.move_tick = 0;
            r.state = 1;
            return;
        case animate:
            r.count = word();
            r.anim_period = std::uint8_t(word());
            r.frame = r.anim_frames;
            r.anim_tick = 0;
            r.state = 2;
            return;
        case hold:
            r.count = word();
            r.state = 0;
            return;
        case loop:
            r.loop = std::uint8_t(word());
            r.loop_pc = r.pc;
            break;
        case loop_end:
            if (r.loop == 0 || --r.loop != 0) {
                r.pc = r.loop_pc;
            }
            break;
        case loop2:
            r.loop2 = std::uint8_t(word());
            r.loop2_pc = r.pc;
            break;
        case loop2_end:
            if (r.loop2 == 0 || --r.loop2 != 0) {
                r.pc = r.loop2_pc;
            }
            break;
        case if_marble_near: {
            bool near = false;
            if (word() == 0) {
                // $110532: the game moves to the next racer's record only
                // after one that is racing, so a racer that is not racing
                // makes it look at the same record again.
                std::size_t at = 0;
                for (int n = 0; n < racer_count && at < racers.size(); ++n) {
                    const auto& m = racers[at];
                    if (!m.racing) {
                        continue;
                    }
                    const auto dx = std::int16_t((m.x - r.x) >> 19), dy = std::int16_t((m.y - r.y) >> 19);
                    if (dx >= 0 && dx < 3 && dy >= 0 && dy < 3) {
                        near = true; // and sound $21
                    }
                    ++at;
                }
            }
            if (near) {
                r.pc = t.u32(r.pc);
            } else {
                // The game skips only a word of the target; its low word is
                // then read as a command, which (like any unknown command,
                // see below) does nothing.
                r.pc += 2;
            }
            break;
        }
        case jump:
            r.pc = t.u32(r.pc);
            break;
        case call:
            r.return_pc = r.pc + 4;
            r.pc = t.u32(r.pc);
            break;
        case ret:
            r.pc = r.return_pc;
            break;
        case animation_frames:
            r.anim_frames = r.frame = address();
            break;
        case move_frames:
            r.move_frames = r.frame = address();
            break;
        case velocity: {
            const std::int32_t a = word(), b = word();
            r.vx = r.kind == 1 ? a : a << 8;
            r.vy = r.kind == 1 ? b : b << 8;
            break;
        }
        case finish:
            stop(r);
            return;
        case step_once:
            if (r.kind == 1) {
                r.screen_x = std::int16_t(r.screen_x + std::int16_t(r.vx));
                r.screen_y = std::int16_t(r.screen_y + std::int16_t(r.vy));
            } else {
                r.x += r.vx;
                r.y += r.vy;
            }
            break;
        case wait_for_feature:
            r.return_pc = r.pc - 2;
            r.state = 4;
            r.feature = std::uint8_t(word());
            return;
        case if_feature_free: {
            bool free = true;
            for (int n = 0; n < racer_count; ++n) {
                const auto& m = racers[std::size_t(n)];
                if (m.racing && (m.feature == r.feature || (r.feature + 1 == m.feature && m.feature > 0x1D))) {
                    free = false;
                }
            }
            if (free) {
                r.pc = t.u32(r.pc); // and a sound from $1020D4
            } else {
                r.pc += 4;
            }
            break;
        }
        default:
            break; // $1101BC: commands from $13 on are skipped
        }
    }
    throw std::runtime_error("A course script does not end.");
}

} // namespace marble_madness::game
