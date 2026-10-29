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
    : track_(&files.track(course)), program_(&files.program()), course_(course) {
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
    if (r.kind != 1) {
        display_events_.push_back({false, std::uint8_t(r.code), std::uint8_t(&r - regions_.data())}); // $1120F4
    }
    if (drawbridge_ == int(&r - regions_.data())) {
        drawbridge_ = -1; // $11218E
    }
    if (r.code == 6) {
        --waves_;
    }
    if (course_ == data::Course::aerial) {
        const int self = int(&r - regions_.data());
        if (r.code == 10 && code10_ == self) {
            code10_ = -1;
        }
        for (auto& slot : codes11_13_) {
            if ((r.code == 11 || r.code == 13) && slot == self) {
                slot = -1;
                break;
            }
        }
        for (auto& slot : codes12_) {
            if (r.code == 12 && slot == self) {
                slot = -1;
                break;
            }
        }
    }
}

void Regions::bands_changed(int from, int to, Random& random) {
    bool group_full = false;
    // The list (block +0): band range (two signed bytes) and a script; $FF ends it.
    for (std::uint32_t e = block_entry(0); track_->u8(e) != 0xFF; e += 6) {
        const int first = track_->s8(e), last = track_->s8(e + 1);
        if (running(e) || !(first == to || last == to) || !(from < first || last < from)) {
            continue;
        }
        const std::uint32_t script = track_->u32(e + 2);
        if (script == 0) {
            // A group of four picked at random (block entries 3-6, 7-10,
            // 11-14 or 15-18), only while no code-12 region is registered
            // (the draw happens anyway).
            const std::uint32_t k = random.next(4);
            if (codes12_[0] < 0) {
                for (std::uint32_t i = 0; i < 4; ++i) {
                    Region* slot = free_slot();
                    if (slot == nullptr) {
                        group_full = true;
                        break;
                    }
                    slot->band_from = std::int16_t(first);
                    slot->band_to = std::int16_t(last);
                    start(*slot, block_entry(int(k * 4 + 3 + i)));
                }
                if (group_full) {
                    break;
                }
            }
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

void Regions::update(Terrain& terrain, Probe& probe) {
    terrain_ = &terrain;
    probe_ = &probe;
    // $1100A2: on Intermediate a wave (block entry 2) rolls down the long
    // ramp whenever a racing player is in zone 9 or 10 and the last wave
    // has gone far enough.
    if (course_ == data::Course::intermediate && wave_ready_) {
        for (int i = 0; i < racer_count; ++i) {
            const auto& m = racers[std::size_t(i)];
            if (m.racing && (m.feature == 10 || m.feature == 9)) {
                if (Region* slot = free_slot()) {
                    start(*slot, block_entry(2));
                }
                break;
            }
        }
    }
    // $110C80: while a wave runs, the course's routine (block entry 3)
    // first lays the ramp's animated heights flat again.
    if (waves_ != 0) {
        reset_wave_heights();
    }
    for (auto& r : regions_) {
        step(r);
    }
}

// Intermediate's wave: two routines the Track carries as code (region block
// entries 3 and 4, rebuilt here) over tables beside them.
namespace {
constexpr std::uint32_t flat_heights = 0x20;  // entry 3 +: $42 vertex codes
constexpr int flat_count = 0x42;
constexpr std::uint32_t group_kinds = 0x200;  // entry 4 +: a byte per group of three vertices (-1 ends)
constexpr std::uint32_t phase_rows = 0x21E;   // entry 4 +: 4 bytes per phase, a row per group
constexpr std::uint32_t first_codes = 0x296, second_codes = 0x24A; // words by row
constexpr std::array<std::uint32_t, 4> third_codes = {0x2E2, 0x32E, 0x354, 0x37A}; // by group kind 0-3
constexpr std::uint32_t last_first = 0x2BC, last_second = 0x270, last_third = 0x308; // the closing group
} // namespace

void Regions::reset_wave_heights() {
    const std::uint32_t from = block_entry(3) + flat_heights;
    for (int i = 0; i < flat_count && std::size_t(i) < terrain_->animated.size(); ++i) {
        terrain_->animated[std::size_t(i)] = track_->u16(from + std::uint32_t(i) * 2);
    }
}

// Entry 4 with the wave's round: the phase (rounds 0-6, then 7-10 over
// again), a group of three animated vertices per four rounds onwards: up
// to four groups get the phase's rows; a group of kind 4 or more closes
// the wave (written, then no further groups).
void Regions::draw_wave(int round) {
    const std::uint32_t code = block_entry(4);
    const int phase = round <= 6 ? round : ((round - 7) & 3) + 7;
    std::uint32_t row = code + phase_rows + std::uint32_t(phase) * 4;
    std::uint32_t kind = code + group_kinds + std::uint32_t(round >> 2);
    std::size_t at = std::size_t(round >> 2) * 3;
    for (int g = 0; g < 4; ++g, ++row, ++kind) {
        const int k = track_->s8(kind);
        if (k < 0) {
            return;
        }
        const std::uint32_t r = std::uint32_t(track_->s8(row)) * 2;
        const bool last = k > 3;
        const std::array<std::uint16_t, 3> codes = {
            track_->u16(code + (last ? last_first : first_codes) + r),
            track_->u16(code + (last ? last_second : second_codes) + r),
            track_->u16(code + (last ? last_third : third_codes[std::size_t(k)]) + r)};
        for (std::size_t i = 0; i < 3; ++i, ++at) {
            if (at < terrain_->animated.size()) {
                terrain_->animated[at] = codes[i];
            }
        }
        if (last) {
            return;
        }
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

// A patch of the animated heights ($122100 into $1007B0): first index,
// count, then the codes.
void Regions::apply_patch(std::uint32_t patch) {
    if (patch == 0) {
        return;
    }
    const int first = track_->s16(patch), count = track_->s16(patch + 2);
    for (int i = 0; i < count; ++i) {
        const std::size_t at = std::size_t(first + i);
        if (at < terrain_->animated.size()) {
            terrain_->animated[at] = track_->u16(patch + 4 + std::uint32_t(i) * 2);
        }
    }
}

// $11E848, from the step of Ultimate's code-$19 region: a finished picture
// patches the heights (the frame entry's second long), picks the cover set
// of its frame and waits for the display; a wanted one is asked for
// ($11E7B0: the entry's first long is its first map row; a -1 entry goes
// back to the list's start).
void Regions::poll_picture(Region& r) {
    if (picture_.rendering && --picture_.render_left <= 0) {
        picture_.rendering = false;
        apply_patch(track_->u32(r.frame));
        r.frame += 4;
        picture_.cover_set = int(r.frame - 8 - r.anim_frames) >> 3;
        picture_.swap_due = true;
    }
    if (picture_.wanted) {
        std::uint32_t entry = r.frame;
        if (track_->u32(entry) == 0xFFFFFFFF) {
            entry = r.anim_frames;
        }
        picture_.rendering_row = int(track_->u32(entry));
        r.frame = entry + 4;
        picture_.rendering = true;
        picture_.wanted = false;
        picture_.render_left = picture_render_time;
        if (!forced_render_times.empty()) {
            picture_.render_left = forced_render_times.front();
            forced_render_times.pop_front();
        }
    }
}

void Regions::show_rendered_picture() {
    if (picture_.swap_due) {
        picture_.swap_due = false;
        picture_.shown_row = picture_.rendering_row;
        picture_.wanted = true;
    }
}

void Regions::show_frame(Region& r) {
    if (r.code == 6) {
        draw_wave(r.loop2);
    }
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
                    // ($110A46) Code 6 in rounds $44-$53 draws the wave
                    // passing under the ramp's end. (Codes 11, 12 and 13
                    // pick other cells; not rebuilt yet.)
                    const std::uint32_t item = track_->u32(r.frame);
                    const bool wave_end = r.code == 6 && r.loop2 > 0x43 && r.loop2 < 0x54;
                    // Codes 11 and 13 by their zone, code 12 by its row.
                    constexpr std::uint32_t vacuum_floors = 0x110C48, bumper_floors = 0x110C60;
                    std::int16_t background = -1;
                    if (r.code == 11 || r.code == 13) {
                        background = std::int16_t(program_->u32(vacuum_floors + std::uint32_t(r.feature - 0x1E) * 4));
                    } else if (r.code == 12) {
                        background = std::int16_t(
                            program_->u32(bumper_floors + std::uint32_t((((r.y >> 19) - 0x66) >> 1) * 4)));
                    }
                    blits_.push_back({item, std::int16_t(r.screen_x + r.width * 8),
                                      std::int16_t(r.height * 8 - r.screen_y - view_scroll),
                                      std::int8_t(wave_end ? r.loop2 - 0x44 : -1), background});
                }
                apply_patch(track_->u32(r.frame + 4));
            }
            if (r.kind != 2) {
                return;
            }
        }
    }
    // $106A00: the region's place on the screen (the point stays in the
    // probe, which positioned sounds read).
    const auto x = std::int16_t(r.x >> 16), y = std::int16_t(r.y >> 16), z = std::int16_t(r.z >> 16);
    if (probe_ != nullptr) {
        probe_->set_point(x, y);
    }
    r.screen_x = std::int16_t(y - x + 0x88);
    r.screen_y = std::int16_t(std::int16_t(view) + z + 0x6C - std::int16_t((x + y) >> 1));
    r.shown_frame = r.frame;
}

void Regions::step(Region& r) {
    if (!r.active) {
        return;
    }
    if (r.code == 0x19) {
        poll_picture(r);
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
                if (r.code == 6 && ++r.loop2 == 0x1E) {
                    wave_ready_ = true; // the next wave may start
                }
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
                // $112004: a display object (type: the code).
                display_events_.push_back({true, std::uint8_t(r.code), std::uint8_t(&r - regions_.data())});
            }
            // $110338: some codes register the region for course rules,
            // and the script goes on. Code 3, Beginner's drawbridge, is
            // the one its occlusion rule cuts out of the marble ($110158).
            // Code 6, Intermediate's wave, stays whatever the view does,
            // holds back the next wave and counts its rounds in +$25.
            // Code $19, Ultimate's moving goal area, asks for its first
            // picture ($11E6E4).
            if (r.code == 0x19) {
                picture_.wanted = true;
                picture_.home_row = r.height;
            }
            if (r.code == 3 && drawbridge_ < 0) {
                drawbridge_ = int(&r - regions_.data());
            }
            // Codes 10-13 register for Aerial's occlusions (first free slot).
            const int self = int(&r - regions_.data());
            if (r.code == 10) {
                code10_ = self;
            }
            auto enter = [self](auto& list) {
                for (auto& slot : list) {
                    if (slot < 0) {
                        slot = self;
                        return;
                    }
                }
            };
            if (r.code == 11 || r.code == 13) {
                enter(codes11_13_);
            }
            if (r.code == 12) {
                enter(codes12_);
            }
            if (r.code == 6) {
                r.band_from = 0;
                r.band_to = 0x500;
                wave_ready_ = false;
                ++waves_;
                r.loop2 = 0;
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
            if (r.code == 10) {
                sound(0x2F, true);
            }
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
                r.pc = t.u32(r.pc);
                constexpr std::uint32_t freed_sounds = 0x1020D4; // longs by zone - $1E
                sound(int(program_->u32(freed_sounds + std::uint32_t(r.feature - 0x1E) * 4)));
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
