#include "rock_n_roll_amiga/intro/rainbow_logo.hpp"
#include "rock_n_roll_amiga/data/preview.hpp"
#include <algorithm>

namespace rocknroll::intro {
namespace {

// Where the logo program keeps what is drawn, as it runs from $30000.
constexpr std::uint32_t copper_colours = 0x30DDC;  // colour n's value at +4n
constexpr std::uint32_t copper_plane0 = 0x30DBC;   // BPL1PTH/L's values
constexpr std::uint32_t anchor_at = 0x30D1A;       // the fuse's far end, x and y
constexpr std::uint32_t pen_at = 0x30D1E;          // the pen's start
constexpr std::uint32_t path_pointer = 0x30D24;    // dx, dy, steps per segment
constexpr std::uint32_t path_last = 0x30D2A;       // segments - 1
constexpr std::uint32_t bob_frame_at = 0x30D32;
constexpr std::uint32_t layer_waits = 0x30D38;     // 7 words
constexpr std::uint32_t layer_pieces = 0x30D46;
constexpr std::uint32_t layer_sources = 0x30D54;   // offsets from $32000
constexpr std::uint32_t layer_pictures = 0x32000;
constexpr std::uint32_t piece_list = 0x30B6E;      // 6 bytes a piece
constexpr int piece_count = 0x35;                  // $30582: a layer ends after 53 pieces
constexpr std::uint32_t bob_pictures = 0x30E80;    // 10 frames of 4 planes, 28 rows of 32 pixels
constexpr int bob_frames = 10, bob_rows = 28;
constexpr std::uint32_t bob_plane_bytes = 112, bob_frame_bytes = 448;
constexpr std::uint32_t roll_row_at = 0x30D18, speed_index_at = 0x30D16;
constexpr std::uint32_t speeds = 0x30CEE;          // rows a frame, by speed index
constexpr std::uint32_t logo_pointer = 0x30CEA;    // the logo picture's rows (18 bytes, 4 planes)
constexpr std::uint32_t bar_patterns = 0x30CAC, bar_pointers = 0x30CB4, bar_counts = 0x30CC8,
                        bar_modes = 0x30CD2, bar_steps = 0x30CDC;
constexpr std::uint32_t fade_masks = 0x30D7E;      // R, G, B masks; +6 one step; +$C the rest
constexpr std::uint32_t crackle_sample = 0x37028, chord_sample = 0x376E8;

// The program's own constants.
constexpr int frames_to_fade = 596; // the main loop's count ($30D96, $80000 passes) on an A500
constexpr int fuse_hidden_rows = 5; // $30364: the rows under the pen are left out
constexpr int box_bottom_row = 110, box_bottom_rows = 19; // $30604
constexpr int box_byte = 11, box_bytes = 18;               // bytes 11-28 of a row
constexpr int logo_rows_end = 0x6E, saved_rows_end = 0x80;  // $306DA-$30764
constexpr int roll_window = 61, roll_bottom = 0xD0, roll_end = 0xD9, chord_from = 0xB4;
constexpr std::uint32_t plane_size = std::uint32_t(RainbowLogo::row_bytes) * RainbowLogo::height;

int signed_word(std::uint16_t w) { return std::int16_t(w); }

} // namespace

RainbowLogo::RainbowLogo(const data::Memory& logo) : logo_(&logo) {
    for (auto& p : planes_) p.assign(plane_size, 0);
    for (int i = 0; i < 16; ++i) colours_[std::size_t(i)] = logo.u16(copper_colours + std::uint32_t(i) * 4);

    anchor_ = {signed_word(logo.u16(anchor_at)), signed_word(logo.u16(anchor_at + 2))};
    pen_ = {signed_word(logo.u16(pen_at)), signed_word(logo.u16(pen_at + 2))};
    const std::uint32_t path = logo.u32(path_pointer);
    for (int i = 0; i <= signed_word(logo.u16(path_last)); ++i) {
        const std::uint32_t at = path + std::uint32_t(i) * 6;
        path_.push_back({signed_word(logo.u16(at)), signed_word(logo.u16(at + 2)), signed_word(logo.u16(at + 4))});
    }
    bob_frame_ = logo.u16(bob_frame_at);
    fuse_ = pen_; // $301A0: the set-up draws the fuse once

    for (int i = 0; i < piece_count; ++i) {
        const std::uint32_t at = piece_list + std::uint32_t(i) * 6;
        const std::uint16_t kind = logo.u16(at + 4);
        pieces_.push_back({logo.u16(at), logo.u16(at + 2), (kind & 0xFF00) != 0, kind & 0xFF});
    }
    for (std::size_t l = 0; l < 7; ++l) {
        layer_wait_[l] = signed_word(logo.u16(layer_waits + std::uint32_t(l) * 2));
        layer_piece_[l] = logo.u16(layer_pieces + std::uint32_t(l) * 2);
        layer_source_[l] = layer_pictures + logo.u16(layer_sources + std::uint32_t(l) * 2);
    }

    roll_row_ = logo.u16(roll_row_at);
    speed_index_ = logo.u16(speed_index_at);
    logo_source_ = logo.u32(logo_pointer);
    const std::uint32_t plane0 = (logo.u32(copper_plane0) & 0xFFFF0000u) | logo.u16(copper_plane0 + 4);
    for (std::size_t b = 0; b < bars_.size(); ++b) {
        const auto at = std::uint32_t(b);
        bars_[b] = {logo.u32(bar_pointers + at * 4) - plane0, logo.u16(bar_counts + at * 2),
                    signed_word(logo.u16(bar_modes + at * 2)), signed_word(logo.u16(bar_steps + at * 2))};
    }

    const auto bytes = logo.bytes(logo.base(), logo.end() - logo.base());
    sound_memory_.assign(bytes.begin(), bytes.end());
    paula_.set_memory(sound_memory_);
}

std::uint8_t& RainbowLogo::plane(int p, std::uint32_t offset) {
    const std::uint32_t linear = std::uint32_t(p) * plane_size + offset;
    if (linear >= planes * plane_size) return beyond_;
    return planes_[linear / plane_size][linear % plane_size];
}

bool RainbowLogo::tick(bool skip) {
    if (stage_ == Stage::done) return true;
    switch (stage_) {
    case Stage::pen: pen_frame(); break;
    case Stage::reveal: reveal_frame(); break;
    case Stage::fade: fade(); break;
    case Stage::done: break;
    }
    ++frame_;
    // $3010C, between the frames: the main loop counts out or sees a button
    // or a key, silences the channels ($303D2) and hands the interrupt to
    // the fade.
    if (stage_ == Stage::pen || stage_ == Stage::reveal) {
        if (skip || frame_ >= frames_to_fade) {
            silence();
            sound_frames_ = sweep_frames_ = 0;
            stage_ = Stage::fade;
            fade_wait_ = 1;
        }
    }
    return stage_ == Stage::done;
}

// $3025A: the pen's frame. The fuse is taken off and the background under
// the bob put back first, so the burnt edge is drawn on the plain picture.
void RainbowLogo::pen_frame() {
    bob_.reset();
    fuse_.reset();
    burn();
    if (layer_piece_[6] == piece_count) { // the last layer is through: on to the reveal
        stage_ = Stage::reveal;
        save_box_bottom();
        return;
    }
    if (!pen_done_) { // $3044C: the bob where the pen is, its next frame
        bob_frame_ = (bob_frame_ + 1) % bob_frames;
        bob_ = pen_;
        bob_frame_shown_ = bob_frame_;
    }
    move_pen();
    if (!pen_done_) {
        fuse_ = pen_;
        pen_sound();
    }
}

// $30572: each layer, the last first, waits its counter out, then draws its
// next piece and waits 4 frames. A piece is a column of rows of bytes or
// words, the layer's pictures read on through the four planes.
void RainbowLogo::burn() {
    for (int l = 6; l >= 0; --l) {
        const auto layer = std::size_t(l);
        if (--layer_wait_[layer] >= 0) continue;
        if (layer_piece_[layer] == piece_count) continue;
        layer_wait_[layer] = 3;
        const Piece& piece = pieces_[std::size_t(layer_piece_[layer]++)];
        std::uint32_t from = layer_source_[layer] + piece.from;
        for (int p = 0; p < planes; ++p) {
            std::uint32_t to = piece.to;
            for (int r = 0; r < piece.rows; ++r, to += row_bytes) {
                plane(p, to) = logo_->u8(from++);
                if (piece.words) plane(p, to + 1) = logo_->u8(from++);
            }
        }
    }
}

// $3039C: one step along the path; after the last segment the pen is done
// and the channels fall silent.
void RainbowLogo::move_pen() {
    if (pen_done_) return;
    const Segment& s = path_[segment_];
    if (segment_left_ == 0) segment_left_ = s.steps;
    pen_.x += s.dx;
    pen_.y += s.dy;
    if (--segment_left_ != 0) return;
    if (++segment_ < path_.size()) return;
    pen_done_ = true;
    sound_frames_ = sweep_frames_ = 0;
    silence();
}

void RainbowLogo::silence() {
    for (int c = 0; c < 4; ++c) paula_.set_volume(c, 0);
}

// $3085E: the crackle. Two channels loop the program's bytes from $37028,
// their start sliding 16 bytes a frame forwards for 64 frames, then back
// (16 times); the second wobbles in pitch, the first's volume is switched
// on or off at random each frame.
void RainbowLogo::pen_sound() {
    if (sound_frames_ == 0) {
        sweep_at_ = {crackle_sample, crackle_sample};
        paula_.set_location(0, crackle_sample - logo_->base());
        paula_.set_length(0, 0x80);
        paula_.set_period(0, 0xA0);
        paula_.set_volume(0, 0x40);
        paula_.set_location(1, crackle_sample - logo_->base());
        paula_.set_length(1, 0x100);
        paula_.set_period(1, 0x200);
        wobble_base_ = 0x200;
        paula_.set_volume(1, 0x40);
        paula_.write_dmacon(0x8203);
        wobble_reload_ = wobble_count_ = 6; // $30A4A with $00060040
        wobble_step_ = 0x40;
        wobble_ = 0;
        paula_.set_period(1, std::uint16_t(wobble_base_));
    }
    const int slide = sweep_frames_ < 0x40 ? 16 : sweep_frames_ < 0x80 ? -16 : 0;
    if (slide != 0) {
        for (int c = 0; c < 2; ++c) {
            sweep_at_[std::size_t(c)] += std::uint32_t(slide);
            paula_.set_location(c, sweep_at_[std::size_t(c)] - logo_->base());
        }
    } else if (++sweeps_ < 16) {
        sweep_frames_ = 0;
    }
    // $30A68: the pitch wobble.
    wobble_ += std::int8_t(wobble_step_);
    paula_.set_period(1, std::uint16_t(wobble_ + wobble_base_));
    if (--wobble_count_ == 0) {
        wobble_count_ = wobble_reload_;
        wobble_step_ = -wobble_step_;
    }
    // $30AC2: a random word (the original takes it from the beam position).
    paula_.set_volume(0, std::uint16_t(random_() & 0x40));
    ++sound_frames_;
    ++sweep_frames_;
}

// $30604: the box's bottom edge (rows 110-128, bytes 11-28), kept to be
// laid down again under the logo.
void RainbowLogo::save_box_bottom() {
    for (int p = 0; p < planes; ++p) {
        auto& saved = box_bottom_[std::size_t(p)];
        saved.clear();
        for (int r = 0; r < box_bottom_rows; ++r)
            for (int b = 0; b < box_bytes; ++b)
                saved.push_back(plane(p, std::uint32_t((box_bottom_row + r) * row_bytes + box_byte + b)));
    }
}

// $302B0: the reveal's frame.
void RainbowLogo::reveal_frame() {
    if (roll_row_ >= roll_end) bars();
    else roll();
    if (roll_row_ >= chord_from) end_sound();
}

// $30634: the 61 rows below the roll's row move down by the speed (rows
// below 208 are not taken), then as many rows are laid down from the roll's
// row: the logo picture's rows to row 110, the saved bottom edge to row 128,
// then black. The speed grows from 1 to 11 rows a frame.
void RainbowLogo::roll() {
    const int speed = logo_->u16(speeds + std::uint32_t(speed_index_));
    auto row = [](int, int r) { return std::uint32_t(r * row_bytes + box_byte); };
    for (int r = roll_row_ + roll_window; r > roll_row_; --r) {
        if (r > roll_bottom) continue;
        for (int p = 0; p < planes; ++p)
            for (int b = 0; b < box_bytes; ++b) plane(p, row(p, r + speed) + std::uint32_t(b)) = plane(p, row(p, r) + std::uint32_t(b));
    }

    // $306DA-$307BE. A frame lays `speed` rows; passing row 110 moves on to
    // the saved edge without counting that row, and reaching row 128 clears
    // a whole `speed` rows more.
    int r = roll_row_;
    auto lay = [&](auto&& source_byte) {
        for (int p = 0; p < planes; ++p)
            for (int b = 0; b < box_bytes; ++b) plane(p, row(p, r) + std::uint32_t(b)) = source_byte(p, b);
        ++r;
    };
    auto clear = [&] {
        for (int n = 0; n < speed; ++n) lay([](int, int) { return std::uint8_t(0); });
    };
    if (r > 0x81) {
        clear();
    } else {
        bool logo_rows = r <= logo_rows_end;
        for (int left = speed - 1;;) {
            if (logo_rows) {
                const std::uint32_t at = logo_source_;
                lay([&](int p, int b) { return logo_->u8(at + std::uint32_t(p) * 0x44A + std::uint32_t(b)); });
                logo_source_ += box_bytes;
                if (r >= logo_rows_end) {
                    logo_rows = false;
                    continue;
                }
            } else {
                const std::size_t at = saved_row_++;
                lay([&](int p, int b) { return box_bottom_[std::size_t(p)][at * box_bytes + std::size_t(b)]; });
                if (r >= saved_rows_end) {
                    clear();
                    break;
                }
            }
            if (--left < 0) break;
        }
    }
    roll_row_ += speed;
    if (speed_index_ != 0) speed_index_ -= 2;
}

// $307D0: the five bars, the last first. Pattern bars draw 3 rows a frame
// (a byte in each plane) and move by their step; line bars widen a 4-row
// line in plane 3 by 2 pixels a frame and move a byte on when it is full.
void RainbowLogo::bars() {
    for (int b = 4; b >= 0; --b) {
        Bar& bar = bars_[std::size_t(b)];
        if (bar.left == 0) continue;
        if (bar.pattern < 0) {
            std::uint8_t v = 0;
            for (int r = 0; r < 4; ++r) {
                std::uint8_t& byte = plane(3, bar.at + std::uint32_t(r * row_bytes));
                v = bar.step >= 0 ? std::uint8_t(byte >> 2 | 0xC0) : std::uint8_t(byte << 2 | 0x03);
                byte = v;
            }
            if (--bar.left == 0) continue;
            if (v == 0xFF) bar.at += std::uint32_t(bar.step);
            continue;
        }
        std::uint32_t at = bar.at;
        bool ended = false;
        for (int r = 0; r < 3; ++r) {
            for (int p = 0; p < planes; ++p)
                plane(p, at) = logo_->u8(bar_patterns + std::uint32_t(bar.pattern + p));
            at += std::uint32_t(bar.step);
            if (--bar.left == 0) {
                ended = true;
                break;
            }
        }
        if (!ended) bar.at = at;
    }
}

// $30972: the closing chord. Channel 0 starts on the bytes from $37028
// (their first long cleared), channel 1 five frames later; on the sixth
// frame both are pointed at $376E8 and set to full volume, which then falls
// by 1 (channel 0) and 5/8 (channel 1) a frame.
void RainbowLogo::end_sound() {
    if (sound_frames_ == 0) {
        std::fill_n(sound_memory_.begin() + std::ptrdiff_t(crackle_sample - logo_->base()), 4, std::int8_t(0));
        for (int c = 0; c < 2; ++c) {
            paula_.set_location(c, crackle_sample - logo_->base());
            paula_.set_length(c, 0x500);
            paula_.set_period(c, c == 0 ? 0x400 : 0x600);
        }
        paula_.write_dmacon(0x8201);
    }
    if (sound_frames_ < 6) {
        if (sound_frames_ > 4) paula_.write_dmacon(0x8202);
    } else if (sound_frames_ == 6) {
        for (int c = 0; c < 2; ++c) {
            paula_.set_location(c, chord_sample - logo_->base());
            paula_.set_length(c, 0x1A0);
            fade_volume_[std::size_t(c)] = std::uint16_t((fade_volume_[std::size_t(c)] & 0xFF) | 0x4000);
        }
        fade_step_ = {0xFF00, 0xFF60};
    }
    for (std::size_t c = 0; c < 2; ++c) {
        if ((fade_volume_[c] >> 8) == 0 || fade_step_[c] == 0) continue;
        fade_volume_[c] = std::uint16_t(fade_volume_[c] + fade_step_[c]);
        paula_.set_volume(int(c), fade_volume_[c] >> 8);
    }
    ++sound_frames_; // $30968, shared with the crackle
    ++sweep_frames_;
}

// $302C6: every other frame each colour's red, green and blue fall by one;
// the logo ends at the first pass that finds them all black.
void RainbowLogo::fade() {
    if (--fade_wait_ >= 0) return;
    fade_wait_ = 1;
    int changed = 0;
    for (auto& c : colours_) {
        for (int k = 2; k >= 0; --k) {
            const std::uint32_t at = fade_masks + std::uint32_t(k) * 2;
            const std::uint16_t mask = logo_->u16(at), one = logo_->u16(at + 6), rest = logo_->u16(at + 12);
            if ((c & mask) == 0) continue;
            ++changed;
            c = std::uint16_t((((c & mask) - one) & 0xFFFF) | (c & rest));
        }
    }
    if (changed == 0) stage_ = Stage::done;
}

// The planes as shown: the bob over the picture (colour 0 of its own is
// clear), then the fuse toggling plane 0 from the pen down to the anchor's
// row, one pixel a row, stepping sideways at most a pixel a row ($30312).
void RainbowLogo::compose(std::array<std::vector<std::uint8_t>, planes>& out) const {
    out = planes_;
    auto pixel = [&](int p, int x, int y) -> std::uint8_t* {
        if (x < 0 || x >= width || y < 0 || y >= height) return nullptr;
        return &out[std::size_t(p)][std::size_t(y * row_bytes + x / 8)];
    };
    if (bob_) {
        const std::uint32_t frame = bob_pictures + std::uint32_t(bob_frame_shown_) * bob_frame_bytes;
        for (int r = 0; r < bob_rows; ++r) {
            std::array<std::uint32_t, planes> bits{};
            std::uint32_t mask = 0;
            for (int p = 0; p < planes; ++p) {
                bits[std::size_t(p)] = logo_->u32(frame + std::uint32_t(p) * bob_plane_bytes + std::uint32_t(r) * 4);
                mask |= bits[std::size_t(p)];
            }
            for (int c = 0; c < 32; ++c) {
                const std::uint32_t bit = 0x80000000u >> c;
                if (!(mask & bit)) continue;
                const int x = bob_->x - 15 + c, y = bob_->y - 14 + r;
                for (int p = 0; p < planes; ++p) {
                    if (std::uint8_t* byte = pixel(p, x, y)) {
                        const std::uint8_t b = std::uint8_t(0x80 >> (x & 7));
                        *byte = (bits[std::size_t(p)] & bit) ? std::uint8_t(*byte | b) : std::uint8_t(*byte & ~b);
                    }
                }
            }
        }
    }
    if (fuse_) {
        const int dx = anchor_.x - fuse_->x, dy = std::abs(anchor_.y - fuse_->y);
        const int across = std::abs(dx);
        const int step = dx > 0 ? 1 : -1; // towards the anchor (none when straight)
        int x = fuse_->x, y = fuse_->y, error = dy;
        for (int n = 0; n < dy; ++n, ++y) {
            if (n >= fuse_hidden_rows)
                if (std::uint8_t* byte = pixel(0, x, y)) *byte ^= std::uint8_t(0x80 >> (x & 7));
            error -= across;
            if (error <= 0) {
                if (dx != 0) x += step;
                else x -= 1;
                error += dy;
            }
        }
    }
}

std::vector<std::uint8_t> RainbowLogo::shown_planes() const {
    std::array<std::vector<std::uint8_t>, planes> out;
    compose(out);
    std::vector<std::uint8_t> bytes;
    for (const auto& p : out) bytes.insert(bytes.end(), p.begin(), p.end());
    return bytes;
}

platform::Image RainbowLogo::picture() const {
    std::array<std::vector<std::uint8_t>, planes> out;
    compose(out);
    platform::Image image(width, height);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const std::size_t at = std::size_t(y * row_bytes + x / 8);
            const int bit = 7 - (x & 7);
            int index = 0;
            for (int p = 0; p < planes; ++p) index |= ((out[std::size_t(p)][at] >> bit) & 1) << p;
            image.pixels[std::size_t(y * width + x)] = data::to_color(colours_[std::size_t(index)]);
        }
    return image;
}

} // namespace rocknroll::intro
