#include "beach_volley_amiga/match/match.hpp"
#include <algorithm>

namespace beach_volley::match {
namespace {

constexpr int plane_bytes = 0x1F40; // a sprite sheet plane: 200 rows of 40 bytes
constexpr int sheet_row = 40;
constexpr int buffer_width = 304;   // the screen buffer: 19 words a row
constexpr int buffer_rows = 232;
constexpr int above = 32;           // rows above the buffer's top the program draws into
constexpr int window_left = 16;     // the display window shows buffer columns 16-303

Image picture_at(std::uint32_t address, int width, int height) {
    return {address - sprite_block_address, width, height};
}

// B2's starting height steps through these, one per rally ($26532).
constexpr int b2_heights[] = {130, 100, 50, 90, 40, 60, 100, 120, 50, 80, 40, 130, 70};

} // namespace

Match::Match(const MatchData& data, int players)
    : data_(&data), players_count_(players), objects_(data.objects), b_toss_(data.serve_toss_b) {
    for (auto& w : walk_) w.delay = 3; // as volley.code is loaded
    serve_a_setup();
}

// ---- side A's serve ($26B20) ----------------------------------------------

void Match::serve_a_setup() {
    clear_for_serve_a();
    place_for_rally();
    serving_a_ = true;
    fire_latch_a_ = false;
    a_jumping_ = false;
    a1_acting_ = false;
    a2_setting_ = false;
    b2_setting_ = false;
    b_fire_latch_ = false;
    b1_acting_ = false;
    serving_a_ = true;
    serve_contact_ = false;
    serve_wait_ = 100;
    auto& p = objects_[a1];
    p.x = 50;
    p.y = 70;
    court_x_[0] = 50;
    court_y_[0] = 70;
    objects_[a1_shadow].x = p.x + 3;
    objects_[a1_shadow].y = p.y + 68;
    p.image = picture_at(0x64CA0, 48, 68);
    phase_ = Phase::serve_a_wait;
}

// $262AA: everyone to their places for a new rally.
void Match::place_for_rally() {
    camera_bytes_ = 0;
    camera_shift_ = 0;
    const auto put = [&](Object who, Object shadow, int slot, int x, int y) {
        objects_[who].x = x;
        objects_[who].y = y;
        court_x_[std::size_t(slot)] = x;
        court_y_[std::size_t(slot)] = y;
        objects_[shadow].x = x + 3;
        objects_[shadow].y = y + 57;
    };
    put(a1, a1_shadow, 0, 50, 120);
    objects_[a1].image.at = 0x5BB20 - sprite_block_address;
    put(a2, a2_shadow, 1, 238, 40);
    objects_[a2].image = {data_->stance[1].at, 48, 59};
    put(b1, b1_shadow, 2, 400, 60);
    objects_[b1].image = picture_at(0x5C3E8, 48, 59);
    put(b2, b2_shadow, 3, 300, b2_heights[b2_height_at_]);
    b2_height_at_ = (b2_height_at_ + 1) % int(std::size(b2_heights));
    objects_[b2].image = picture_at(0x5C3E8, 48, 59);
    auto& b = objects_[ball];
    b.x = 60;
    b.y = 120;
    b.image = picture_at(0x53E0C, 32, 16);
    ball_court_x_ = 60;
    objects_[ball_shadow].x = b.x;
    objects_[ball_shadow].y = b.y + 25;
    objects_[net].x = 276;
    objects_[net].y = 10;
    objects_[net].image.at = 0x53E00 - sprite_block_address;
    objects_[clock].x = 18;
    objects_[clock].y = 166;
    objects_[clock].image.width = 48;
    objects_[clock].image.height = 31;
    objects_[landing].x = 120;
    objects_[landing].y = 132;
    objects_[landing].image.at = 0x53E08 - sprite_block_address;
}

// The wait before the toss ($26CB0): A1 may step up or down; the ball, its
// shadow and the landing mark follow him.
void Match::serve_a_wait_step() {
    if (place_pending_) {
        place_pending_ = false;
        serve_a_setup();
    }
    draw_serve_a();
    // $2B2D4: up and down only, read straight from the port.
    auto& p = objects_[a1];
    if (sticks_.port1.down && !sticks_.port1.up && court_y_[0] < 87) {
        p.y += a1_speed_;
        court_y_[0] += a1_speed_;
    }
    if (sticks_.port1.up && !sticks_.port1.down && court_y_[0] >= 52) {
        p.y -= a1_speed_;
        court_y_[0] -= a1_speed_;
    }
    objects_[ball].x = p.x + 22;
    objects_[ball].y = p.y + 30;
    objects_[a1_shadow].y = p.y + 65;
    objects_[ball_shadow].y = p.y + 68;
    objects_[ball_shadow].x = p.x + 20;
    objects_[landing].y = p.y + 64;
    if (--serve_wait_ == 0 || fire_now_) {
        toss_at_ = 0;
        fire_latch_a_ = false;
        swing_delay_ = 8;
        swing_done_ = false;
        swing_frame_delay_ = 1;
        swing_missed_ = false;
        jump_at_ = 0;
        phase_ = Phase::serve_a_toss;
    }
}

// The toss ($26D6C): the ball rises and falls by the toss table while the
// server may swing; the toss running out is a fault.
void Match::serve_a_toss_step() {
    const auto& toss = data_->toss;
    if (toss_at_ >= int(toss.size())) {
        reach_ = false;
        rally_winner_ = 1; // $26E30: the serve's fault, side B's point
        point_over();      // this frame draws nothing: the older buffer shows
        return;
    }
    int dy = toss[std::size_t(toss_at_)];
    if (toss_at_ >= 45 && dy == -2) {
        // $293DA plays the bounce; its delay loop leaves -1 in the register
        // that held the step, so the ball moves by -1 here (original quirk).
        sound_ = Sound::toss;
        dy = -1;
    }
    ++toss_at_;
    objects_[ball].y += dy;
    // When the toss runs out the program finds it at once, before the next
    // blank (the fault's frame follows in the next one).
    if (toss_at_ == int(toss.size())) wait_ = 0;
    auto& p = objects_[a1];
    if (toss_at_ == 1) {
        p.image = picture_at(0x64CA4, 64, 68);
        p.x -= 14;
    }
    if (toss_at_ == 4) p.image = picture_at(0x64CAA, 64, 68);
    spin_ball();
    draw_serve_a();
    serve_a_swing();
    if (serve_contact_) { // $26E40: the serve is away; the rally's first pass follows without a frame between
        reach_ = false;
        sound_ = Sound::toss;
        falling_ = false;
        hit();
        court_x_[0] = objects_[a1].x;
        phase_ = Phase::rally;
        run_on_ = true;
    }
}

// $26E6C: the swing. Up held when it starts makes a jump serve.
void Match::serve_a_swing() {
    if (swing_delay_ > 0) {
        --swing_delay_;
        return;
    }
    if (swing_done_) return;
    auto& p = objects_[a1];
    if (swing_started_ || (sticks_.a & 1)) {
        if (!swing_started_) {
            jump_at_ = 0;
            swing_missed_ = false;
        }
        swing_started_ = true;
        const auto& jump = data_->jump;
        if (jump_at_ >= int(jump.size())) {
            jump_at_ = 0;
            swing_frame_delay_ = 0;
            swing_done_ = true;
            p.image = data_->stance[1];
            p.image.width = 48;
            p.image.height = 57;
            p.y += 10;
        } else {
            const auto& frames = data_->jump_a;
            const auto set = [&](std::size_t n, int dx) {
                p.image = frames[n];
                p.x += dx;
            };
            if (jump_at_ == 0) set(0, 4);
            if (jump_at_ == 3) set(1, 6);
            if (jump_at_ == 6) set(2, 2);
            if (jump_at_ == 10) set(3, 0);
            if (jump_at_ == 12) set(4, 2);
            p.y += jump[std::size_t(jump_at_)];
            ++jump_at_;
            if (jump_at_ >= 21) {
                latch_fire_a();
                if (fire_latch_a_) {
                    p.image = frames[5];
                    const auto& b = objects_[ball];
                    if (b.y >= p.y) swing_missed_ = true;
                    if (b.y + 16 < p.y) swing_missed_ = true;
                    if (!swing_missed_) {
                        serve_contact_ = true;
                        swing_missed_ = true;
                    }
                }
            }
        }
    }
    if (swing_started_) return;
    if (!fire_latch_a_) {
        if (!sticks_.port1.fire) return;
        fire_latch_a_ = true;
        swing_frame_delay_ = 1;
    }
    if (--swing_frame_delay_ != 0) return;
    swing_frame_delay_ = 3;
    const auto& frames = data_->serve_swing_a;
    if (swing_frame_ >= int(frames.size())) {
        swing_frame_ = 0;
        swing_frame_delay_ = 1;
        swing_done_ = true;
        p.image = data_->stance[1];
        p.image.width = 48;
        p.image.height = 57;
        p.y += 11;
        return;
    }
    p.image = frames[std::size_t(swing_frame_)];
    ++swing_frame_;
    if (swing_frame_ == 3) {
        p.x += 13;
        // $27122: the ball within the server's reach.
        const int by = objects_[ball].y, py = p.y;
        if (by >= py && py + 30 >= by) {
            serve_contact_ = true;
            serving_a_ = true;
        }
    }
}

// $2B184: the ball's spin, a frame every third pass.
void Match::spin_ball() {
    if (--ball_spin_delay_ != 0) return;
    ball_spin_delay_ = 3;
    if (ball_spin_frame_ >= int(data_->ball_spin.size())) ball_spin_frame_ = 0;
    auto& b = objects_[ball];
    b.image = data_->ball_spin[std::size_t(ball_spin_frame_++)];
    b.image.width = 32;
    b.image.height = 16;
}

// $2724E.
void Match::draw_serve_a() {
    draw_court();
    draw(ball_shadow);
    draw(a1_shadow);
    draw(a2_shadow);
    draw(a1);
    draw(a2, true);
    draw(ball);
    draw(net);
    draw(clock);
}

// ---- display -----------------------------------------------------------------

void Match::draw_court() {
    buffers_[std::size_t(back_)].clear();
    Blit b;
    b.court = true;
    b.camera_bytes = camera_bytes_;
    b.camera_shift = camera_shift_;
    b.lift = lift_;
    b.board = board_;
    buffers_[std::size_t(back_)].push_back(b);
}

void Match::draw(Object n, bool other_team) {
    // $3030E's mulu of the row leaves its high half in d0 (see hit()).
    const auto& o = objects_[std::size_t(n)];
    const bool shown_rows = o.y < -32 ? o.y + 32 + o.image.height > 0 : o.y < 200;
    const bool shown_cols = o.x < 0 ? ((o.image.width + o.x) >> 4) >= 1
                                    : (0x150 - o.x >= o.image.width || ((0x150 - o.x) >> 4) >= 1);
    if (shown_rows && shown_cols) {
        const int row = o.y < -32 ? -32 : o.y;
        d0_high_ = int((std::uint32_t(std::uint16_t(row)) * 38u) >> 16);
    }
    Blit b;
    b.sprite = objects_[std::size_t(n)];
    b.other_team = other_team;
    b.lift = lift_;
    if (n == clock) b.ring_used = ring_plotted_;
    if (n == clock_dots) b.hand = hand_at_;
    buffers_[std::size_t(back_)].push_back(b);
}

// The screen as the program builds it: the court blitted across the whole
// buffer (the blitter's shift carrying the end of one row into the start
// of the next, outside the window), then each object cookie-cut in with
// its mask (the OR of its four planes) less its last word.
platform::Image Match::picture() const {
    const auto pixels = screen();
    platform::Image out(width, height);
    const auto& palette = data_->court.palette;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const auto c = palette[pixels[std::size_t(y * width + x)]];
            out.put(x, y, platform::Color{std::uint8_t((c >> 8 & 15) * 17), std::uint8_t((c >> 4 & 15) * 17),
                                          std::uint8_t((c & 15) * 17), 255});
        }
    }
    return out;
}

std::vector<std::uint8_t> Match::screen() const {
    const int rows = above + buffer_rows;
    std::vector<std::uint8_t> buffer(std::size_t(buffer_width * rows));
    const auto pixel = [&](int x, int y) -> std::uint8_t& {
        return buffer[std::size_t((y + above) * buffer_width + x)];
    };
    const auto& court = data_->court.picture;
    const auto& sheet = data_->sprites;
    const auto sheet_word = [&](std::uint32_t at) -> std::uint16_t {
        if (at + 1 >= sheet.size()) return 0;
        return std::uint16_t(sheet[at] << 8 | sheet[at + 1]);
    };
    // The match clock is plotted into the sprite sheet itself ($31E5C,
    // $31EAA on the sheet at $6B500): the ring's eleven 3 x 3 dots, used
    // ones colour 9, the rest 15; the hand's region (rows 152-182, bytes
    // 34-37) cleared each pass and two dots of colour 8 put in it.
    const auto clock_word = [&](const Blit& b, std::uint32_t at, const auto& base) -> std::uint16_t {
        std::uint16_t w = base(at);
        if (b.ring_used < 0 && b.hand[0] < 0) return w;
        const std::uint32_t sheet3 = 0x6B500 - sprite_block_address;
        if (at < sheet3) return w;
        std::uint32_t off = at - sheet3;
        const int plane = int(off / plane_bytes);
        if (plane > 3) return w;
        off %= plane_bytes;
        const int row = int(off / sheet_row), left = int(off % sheet_row) * 8;
        const auto plot = [&](int x, int y, int colour) {
            if (y != row || x < left || x >= left + 16) return;
            const std::uint16_t bit = std::uint16_t(1 << (15 - (x - left)));
            if (colour >> plane & 1) w |= bit;
            else w = std::uint16_t(w & ~bit);
        };
        const auto& pos = data_->clock_positions;
        if (b.hand[0] >= 0) {
            if (row >= 152 && row < 183) {
                for (int i = 0; i < 16; ++i)
                    if ((left + i) >= 34 * 8 && (left + i) < 38 * 8) w = std::uint16_t(w & ~(1 << (15 - i)));
            }
            for (const int h : b.hand) {
                if (h < 0 || h >= int(pos.size())) continue;
                for (int dy = 0; dy < 3; ++dy)
                    for (int dx = 0; dx < 3; ++dx) plot(pos[std::size_t(h)].first + dx, pos[std::size_t(h)].second + 31 + dy, 8);
            }
        }
        if (b.ring_used >= 0) {
            for (int k = 0; k < int(pos.size()); ++k)
                for (int dy = 0; dy < 3; ++dy)
                    for (int dx = 0; dx < 3; ++dx)
                        plot(pos[std::size_t(k)].first + dx, pos[std::size_t(k)].second + dy, k < b.ring_used ? 9 : 15);
        }
        return w;
    };

    for (const auto& b : shown()) {
        if (b.court) {
            // $28F90: 232 rows of 19 words from the court picture at the
            // camera's byte, shifted right by camera_shift, to the buffer
            // 32 rows above its top (the scene drawn `lift` rows lower).
            for (int r = 0; r < buffer_rows; ++r) {
                const int y = r - above + b.lift;
                for (int x = 0; x < buffer_width; ++x) {
                    int cx = b.camera_bytes * 8 + x - b.camera_shift;
                    int cy = r;
                    if (cx < b.camera_bytes * 8) { // carried from the previous row's end
                        cx += buffer_width;
                        --cy;
                    }
                    std::uint8_t v = 0;
                    if (cy >= 0 && cx < court.width) v = court.pixels[std::size_t(cy * court.width + cx)];
                    // The scoreboard: digits drawn into the court at rows
                    // 68-77, x 256 and 288 ($26210).
                    if (cy >= 68 && cy < 78 && cx >= 256 && cx < 304 && (cx < 272 || cx >= 288)) {
                        const int which = cx < 272 ? 0 : 1, value = b.board[std::size_t(which)];
                        if (value >= 0 && value < 8) {
                            const auto& d = data_->board_digits[std::size_t(value)];
                            const int dx = cx - (which ? 288 : 256), dy = cy - 68;
                            int idx = 0;
                            for (int p = 0; p < 4; ++p) {
                                const std::uint32_t at = d.at + std::uint32_t(p * plane_bytes + dy * sheet_row + (dx >> 3));
                                if (at < sheet.size()) idx |= (sheet[at] >> (7 - (dx & 7)) & 1) << p;
                            }
                            v = std::uint8_t(idx);
                        }
                    }
                    if (y >= -above && y < buffer_rows) pixel(x, y) = v;
                }
            }
            continue;
        }
        if (b.scores) {
            // $305AE: each score in 8 x 8 blocks of the dot picture,
            // straight into the shown buffer at bytes $ABA and $AC8.
            const std::uint32_t dot = 0x53E04 - sprite_block_address;
            for (int side = 0; side < 2; ++side) {
                const int value = side ? b.score_b : b.score_a;
                if (value < 0 || value > 7) continue;
                const int base = side ? 0xAC8 : 0xABA;
                for (const auto& [cx, cy] : data_->big_digits[std::size_t(value)]) {
                    const int at = base + cx + cy * 0x130;
                    const int x0 = (at % 38) * 8, y0 = at / 38;
                    for (int r = 0; r < 8; ++r)
                        for (int i = 0; i < 8; ++i) {
                            int v = 0;
                            for (int p = 0; p < 4; ++p) {
                                const std::uint32_t src = dot + std::uint32_t(p * plane_bytes + r * sheet_row);
                                if (src < sheet.size()) v |= (sheet[src] >> (7 - i) & 1) << p;
                            }
                            if (x0 + i < buffer_width && y0 + r < buffer_rows) pixel(x0 + i, y0 + r) = std::uint8_t(v);
                        }
                }
            }
            continue;
        }
        const auto& s = b.sprite;
        // $3030E: clipping as the program does it.
        int height = s.image.height, y = s.y, skip_rows = 0;
        int words = s.image.width >> 4;
        int x = s.x, skip_bytes = 0;
        if (y < -32) {
            const int over = -(y + 32);
            if (over >= s.image.height) continue;
            height = s.image.height - over;
            skip_rows = over;
            y = -32;
        } else {
            if (y >= 200) continue;
            if (s.image.height + y >= 200) height = 200 - y;
        }
        if (s.x < 0) {
            words = (s.image.width + s.x) >> 4;
            if (words < 1) continue;
            const int cut = (s.x >> 4) * 2; // negative: words cut off, as bytes
            skip_bytes = -cut;
            x = s.x - cut * 8;
        } else {
            const int room = 0x150 - s.x;
            if (room < s.image.width) {
                words = room >> 4;
                if (words < 1) continue;
            }
        }
        const int shift = x & 15;
        const int dest_word = x >> 4;
        const std::uint32_t source = s.image.at + std::uint32_t(skip_rows * sheet_row + skip_bytes);
        // Planes 0 and 1 trade places for the other colours ($3030E with
        // $25B60 set).
        const int order[4] = {b.other_team ? 1 : 0, b.other_team ? 0 : 1, 2, 3};
        std::uint16_t carry_mask = 0;
        std::array<std::uint16_t, 4> carry{};
        for (int r = 0; r < height; ++r) {
            const int dy = y + r + b.lift;
            for (int w = 0; w < words; ++w) {
                const std::uint32_t at = source + std::uint32_t(r * sheet_row + w * 2);
                std::array<std::uint16_t, 4> planes{};
                for (int p = 0; p < 4; ++p)
                    planes[std::size_t(p)] = clock_word(b, at + std::uint32_t(order[p] * plane_bytes), sheet_word);
                // $3030E clears the last-word mask (BLTALWM): each row's last
                // word is masked off before the shift, so it is never drawn
                // and carries nothing into the next row.
                const std::uint16_t mask = w == words - 1 ? 0 : planes[0] | planes[1] | planes[2] | planes[3];
                const auto shifted = [&](std::uint16_t value, std::uint16_t& previous) {
                    const std::uint16_t out =
                        shift ? std::uint16_t(previous << (16 - shift) | value >> shift) : value;
                    previous = value;
                    return out;
                };
                const std::uint16_t m = shifted(mask, carry_mask);
                std::array<std::uint16_t, 4> d{};
                for (std::size_t p = 0; p < 4; ++p) d[p] = shifted(planes[p], carry[p]);
                const int bx = (dest_word + w) * 16;
                if (bx >= buffer_width || dy < -above || dy >= buffer_rows) continue;
                for (int i = 0; i < 16; ++i) {
                    if (!(m >> (15 - i) & 1)) continue;
                    int v = 0;
                    for (std::size_t p = 0; p < 4; ++p) v |= (d[p] >> (15 - i) & 1) << p;
                    if (bx + i < buffer_width) pixel(bx + i, dy) = std::uint8_t(v);
                }
            }
        }
    }

    std::vector<std::uint8_t> out(std::size_t(width * height));
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) out[std::size_t(y * width + x)] = pixel(x + window_left, y);
    return out;
}

} // namespace beach_volley::match
