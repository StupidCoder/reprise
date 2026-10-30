#include "beach_volley_amiga/front/ending.hpp"
#include "platform/files.hpp"
#include <stdexcept>
#include <string>

namespace beach_volley::front {
namespace {

constexpr int row_bytes = 40, rows = 600, plane_bytes = row_bytes * rows; // $5DC0
constexpr std::uint32_t figures_address = 0x5C290;                         // where final is loaded
constexpr std::uint32_t path = 0x4A714;     // (x, y) words, $FFFF ends: the figures' way round
constexpr std::uint32_t pictures = 0x49C70; // the figures' pictures by frame, -1 ends
constexpr std::uint32_t cycle = 0x49A26;    // colours 8-15, 16 sets of eight
constexpr std::uint32_t copper_colours = 0x49FB4; // the copper list's colour values, a long apart

// Blanks the program's CPU loops and waits take, measured on the original
// (game.md "Ending").
constexpr int start_blanks = 156; // $49842: after the song starts
constexpr int scroll_blanks = 1;  // $4985C: a row a frame
constexpr int hold_blanks = 316;  // $49894
constexpr int scroll_rows = 201;

std::vector<std::uint8_t> read_installed(const std::filesystem::path& content, const std::string& name) {
    try {
        return platform::read_file(content / name);
    } catch (const std::exception&) {
        throw std::runtime_error("The imported file " + name + " is missing; import the disk again.");
    }
}

} // namespace

EndingFiles load_ending_files(const std::filesystem::path& content) {
    EndingFiles f{data::CodeFile(read_installed(content, "final.code"), data::final_base),
                  read_installed(content, "trophe"), read_installed(content, "final"),
                  sound::Song::parse(read_installed(content, "musicfinal")), read_installed(content, "instruments")};
    if (f.trophy.size() < 3 * 0x2BA4 || f.figures.size() < 8 * 0x800)
        throw std::runtime_error("The ending's pictures are not as expected.");
    return f;
}

Ending::Ending(const EndingFiles& files, amiga::PaulaChannels& paula)
    : files_(&files), paula_(&paula),
      // final.code's copy of the player: notes and effects on all four channels.
      player_(sound::read_player_tables(files.code, 0x4A572, 0x4A5AE, files.instruments.size()), 0xF, 0xF, true),
      screen_(std::size_t(plane_bytes * 4)) {
    for (std::uint32_t i = 0; i < 16; ++i) colours_[i] = files.code.u16(copper_colours + i * 4);
    for (int n = 0; n < 2; ++n) {
        const std::uint32_t record = 0x4A6B8 + std::uint32_t(n) * 20;
        figures_[std::size_t(n)] = {std::int16_t(files.code.u16(record)), std::int16_t(files.code.u16(record + 2)),
                                    files.code.u32(record + 4) - figures_address};
        path_at_[std::size_t(n)] = files.code.u16(0x4A710 + std::uint32_t(n) * 2);
        frame_at_[std::size_t(n)] = files.code.u16(0x49C68 + std::uint32_t(n) * 2);
        frame_delay_[std::size_t(n)] = files.code.u16(0x49C6C + std::uint32_t(n) * 2);
    }
    cycle_at_ = files.code.u16(0x49A24);
    bind(files);
}

void Ending::bind(const EndingFiles& files) {
    files_ = &files;
    memory_.assign(files.instruments.begin(), files.instruments.end());
    paula_->set_memory(memory_);
}

void Ending::begin() {
    player_.start(files_->song, *paula_); // $4983C
    phase_ = Phase::starting;
    wait_ = start_blanks;
}

void Ending::tick(bool fire) {
    if (phase_ == Phase::finished) return;
    player_.tick(*paula_); // the level-3 handler ($49E76)
    if (phase_ == Phase::looping && fire) {
        // $499B0: the sound stops; intro.code is loaded and started.
        paula_->write_dmacon(0x000F);
        for (int n = 0; n < 4; ++n) paula_->set_volume(n, 0);
        phase_ = Phase::finished;
        return;
    }
    if (wait_ > 0) {
        --wait_;
        return;
    }
    switch (phase_) {
    case Phase::starting: {
        // $4A028: the trophy's first 270 rows, three planes, to row 200 and
        // 64 pixels in; the blit's 15th word, masked off, clears 16 more.
        for (int p = 0; p < 3; ++p)
            for (int r = 0; r < 270; ++r)
                for (int i = 0; i < 30; ++i)
                    screen_[std::size_t(p * plane_bytes + (200 + r) * row_bytes + 8 + i)] =
                        i < 28 ? files_->trophy[std::size_t(p * 0x2BA4 + r * 28 + i)] : 0;
        phase_ = Phase::scrolling;
        steps_ = 0;
        wait_ = scroll_blanks - 1;
        break;
    }
    case Phase::scrolling:
        // $49858: the bitplane pointers a row down every frame.
        ++top_row_;
        wait_ = scroll_blanks - 1;
        if (++steps_ == scroll_rows) {
            phase_ = Phase::holding;
            wait_ = hold_blanks;
        }
        break;
    case Phase::holding: {
        // $498A4: the first figure drawn twice before the loop (the loop
        // clears it at once).
        Figure f = figures_[0];
        f.x = 0xBE;
        f.y = 0x12C;
        draw(f);
        f.x = 0x27;
        f.y = 0xE0;
        draw(f);
        figures_[0] = f;
        phase_ = Phase::looping;
        loop_frame();
        break;
    }
    case Phase::looping: loop_frame(); break;
    case Phase::finished: break;
    }
}

// $498D4: one frame of the loop: the fourth plane cleared from row 202 for
// 270 rows, both figures at their next places along the way round, and,
// after 800 frames, their pictures turning.
void Ending::loop_frame() {
    std::fill(screen_.begin() + 3 * plane_bytes + 202 * row_bytes,
              screen_.begin() + 3 * plane_bytes + (202 + 270) * row_bytes, std::uint8_t{0});
    for (int n = 0; n < 2; ++n) {
        auto& at = path_at_[std::size_t(n)];
        if (files_->code.u16(path + std::uint32_t(at)) == 0xFFFF) at = 0; // byte offsets, as $4A710
        const std::uint32_t entry = path + std::uint32_t(at);
        auto& f = figures_[std::size_t(n)];
        f.x = std::int16_t(files_->code.u16(entry)) - 0x41;
        f.y = std::int16_t(files_->code.u16(entry + 2)) + 0xBE;
        draw(f);
        at += 4;
    }
    if (++frames_ >= 800) animating_ = true;
    if (animating_) animate();
}

// $49BC8: each figure's next picture (every blank once its first delay of
// ten has run out); the first figure's 85th turns the colour cycle on a set.
void Ending::animate() {
    for (int n = 0; n < 2; ++n) {
        auto& delay = frame_delay_[std::size_t(n)];
        if (--delay != 0) continue;
        delay = 1;
        auto& at = frame_at_[std::size_t(n)];
        if (files_->code.u32(pictures + std::uint32_t(at)) == 0xFFFFFFFF) at = 0;
        figures_[std::size_t(n)].picture = files_->code.u32(pictures + std::uint32_t(at)) - figures_address;
        at += 4;
        if (n == 0 && at == 0x154) {
            // $499E8: colours 8-15 from the next set.
            for (std::uint32_t i = 0; i < 8; ++i)
                colours_[8 + i] = files_->code.u16(cycle + std::uint32_t(cycle_at_) + i * 2);
            cycle_at_ += 0x10;
            if (cycle_at_ >= 0x100) cycle_at_ = 0;
        }
    }
}

// $4A60C: a one-plane picture of 128 x 128 OR-ed into the fourth plane.
void Ending::draw(const Figure& f) {
    for (int r = 0; r < 128; ++r)
        for (int i = 0; i < 128; ++i) {
            const std::size_t src = f.picture + std::size_t(r * 16 + (i >> 3));
            if (src >= files_->figures.size() || !(files_->figures[src] >> (7 - (i & 7)) & 1)) continue;
            const long linear = long(f.y + r) * 320 + f.x + i;
            if (linear < 0 || linear >= long(rows) * 320) continue;
            screen_[std::size_t(3 * plane_bytes + linear / 8)] |= std::uint8_t(0x80 >> (linear & 7));
        }
}

platform::Image Ending::picture() const {
    platform::Image out(width, height);
    for (int y = 0; y < height; ++y) {
        const int row = top_row_ + y;
        for (int x = 0; x < width; ++x) {
            int v = 0;
            if (row < rows)
                for (int p = 0; p < 4; ++p)
                    v |= (screen_[std::size_t(p * plane_bytes + row * row_bytes + x / 8)] >> (7 - (x & 7)) & 1) << p;
            const auto c = colours_[std::size_t(v)];
            out.put(x, y, platform::Color{std::uint8_t((c >> 8 & 15) * 17), std::uint8_t((c >> 4 & 15) * 17),
                                          std::uint8_t((c & 15) * 17), 255});
        }
    }
    return out;
}

} // namespace beach_volley::front
