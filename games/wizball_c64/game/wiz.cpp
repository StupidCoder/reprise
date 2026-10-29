#include "wizball_c64/game/wiz.hpp"

namespace wizball::game {
namespace {
// What the landscape probes answer with ($B3BE: two handlers per probe; the
// table holds code addresses, mapped here to what each routine does).
enum class Response { none, floor, ceiling, wall_right, wall_left };
Response response_of(std::uint16_t address) {
    switch (address) {
    case 0x841A: return Response::floor;
    case 0x8405: return Response::ceiling;
    case 0x842F: return Response::wall_right;
    case 0x843E: return Response::wall_left;
    default: return Response::none;
    }
}
constexpr std::uint8_t solid_code = 0x98;     // $83B0: codes from $98 on stop the wiz
constexpr std::uint8_t ground_y = 0xC8, top_y = 0x49;
} // namespace

Wiz::Wiz(const data::GameImage& image) {
    for (int i = 0; i < 8; ++i) {
        probe_offsets_[std::size_t(i)] = image.byte(std::uint16_t(0xB3B6 + i));
        probe_handlers_[std::size_t(i)] = {image.word(std::uint16_t(0xB3BE + 4 * i)), image.word(std::uint16_t(0xB3C0 + 4 * i))};
    }
    gravity_ = image.byte(0xB27C);
}

void Wiz::reset_for_life() {
    // $7CDA clears $B20D-$B26C; then $B20F = $49.
    x_ = 0;
    y_ = top_y;
    y_fraction_ = vy_ = vy_fraction_ = 0;
    falling_ = false;
    vy_direct_ = 0;
    spin_ = spin_fraction_ = 0;
    vx_ = last_vx_ = 0;
    appear_ = appear_frame_ = appear_clock_ = 0;
    tunnel_ = 0;
    direct_clock_ = 0;
    speed_index_ = 0;
    trail_index_ = 0;
    trail_y_ = {};
    trail_x_ = {};
}

void Wiz::start_level() {
    // $8040: appear at the bottom, bouncing up.
    appear_ = 1;
    appear_frame_ = 0x17;
    y_ = ground_y;
    y_fraction_ = 0;
    vy_ = 3;
    vy_fraction_ = 0xF0;
}

void Wiz::fall_step() {
    const unsigned sum = vy_fraction_ + gravity_;
    vy_fraction_ = std::uint8_t(sum);
    if (sum > 0xFF) ++vy_;
}

void Wiz::rise_step() {
    const int diff = vy_fraction_ - gravity_;
    vy_fraction_ = std::uint8_t(diff);
    if (diff < 0) {
        if (std::int8_t(--vy_) < 0) { // $7FDC: DEC; BPL
            vy_ = 0;
            vy_fraction_ = 0;
        }
    }
}

void Wiz::direct_vertical(const Stick& stick) {
    // $8BEA: every third pass the stick changes the speed (up to 4 either
    // way), or it eases back to 0.
    if (++direct_clock_ == 3) {
        direct_clock_ = 0;
        if (!stick.up() && !stick.down()) {
            if (vy_direct_ != 0) {
                const auto old = vy_direct_;
                --vy_direct_;
                if (old < 0) vy_direct_ = std::int8_t(vy_direct_ + 2);
            }
        } else {
            if (stick.down() && vy_direct_ != 4) ++vy_direct_;
            if (stick.up() && vy_direct_ != -4) --vy_direct_;
        }
    }
    y_ = std::uint8_t(y_ + vy_direct_);
}

void Wiz::vertical(const Powers& powers, bool bonus) {
    if (powers.antigravity || bonus) {
        direct_vertical(stick_);
        return;
    }
    if (falling_) { // $7F6D
        const unsigned f = y_fraction_ + vy_fraction_;
        y_fraction_ = std::uint8_t(f);
        if (f > 0xFF) ++y_;
        y_ = std::uint8_t(y_ + vy_);
        if (y_ >= ground_y) {
            falling_ = false; // the bounce
            update_speed_index();
            return;
        }
        fall_step();
        return;
    }
    const int f = y_fraction_ - vy_fraction_; // $7FA9: rising
    y_fraction_ = std::uint8_t(f);
    if (f < 0) --y_;
    y_ = std::uint8_t(y_ - vy_);
    if (vy_ == 0 && vy_fraction_ == 0) {
        falling_ = true;
        return;
    }
    rise_step();
}

void Wiz::horizontal_input(const Stick& stick, const Powers& powers, bool bonus) {
    // $80F8: the stick changes the horizontal speed by 6 a pass up to ±$30.
    if (stick.left() || stick.right()) {
        if (stick.left() && vx_ != std::int8_t(0xD0)) {
            const auto v = std::int8_t(vx_ - 6);
            vx_ = std::int8_t(std::uint8_t(v - 0xD0)) >= 0 ? v : std::int8_t(0xD0);
        }
        if (stick.right() && vx_ != 0x30) {
            const auto v = std::int8_t(vx_ + 6);
            vx_ = std::int8_t(std::uint8_t(v - 0x30)) < 0 ? v : std::int8_t(0x30);
        }
    } else if (((powers.thrust && powers.antigravity) || bonus) && vx_ != 0) {
        vx_ = vx_ > 0 ? std::int8_t(vx_ - 1) : std::int8_t(vx_ + 1); // $8138: eases to 0
    }
    if (vx_ != 0) last_vx_ = vx_;
    // Spin: twice the speed added to the 8.8 spin, 16 frames round.
    for (int k = 0; k < 2; ++k) {
        const unsigned sum = spin_fraction_ + std::uint8_t(vx_);
        spin_fraction_ = std::uint8_t(sum);
        if (vx_ >= 0) {
            if (sum > 0xFF) ++spin_;
        } else if (sum <= 0xFF) {
            --spin_;
        }
    }
    spin_ &= 0x0F;
}

void Wiz::appear_step() {
    // $7FEA: rings close in (1) or open out (2) every third pass.
    if (appear_ == 0) {
        behind_ = false;
        return;
    }
    ++appear_clock_;
    if (appear_ == 2) {
        if (appear_clock_ == 3) {
            appear_clock_ = 0;
            if (++appear_frame_ == 0x17) appear_ = 0;
        }
    } else if (appear_clock_ == 3) {
        appear_clock_ = 0;
        if (std::int8_t(--appear_frame_) < 0) {
            appear_ = 0;
            spin_ = 0;
            return;
        }
    }
    spin_ = std::uint8_t(appear_frame_ + 0x10);
    behind_ = true;
}

void Wiz::clamp() {
    // $8097
    if (y_ < top_y) {
        y_ = top_y;
        vy_direct_ = 1;
    } else if (y_ >= ground_y) {
        y_ = ground_y;
        vy_direct_ = -1;
    }
    frame_ = std::uint8_t(spin_ + 0x10);
}

bool Wiz::sink() {
    // $822C
    if (--tunnel_ == 0) return true;
    spin_ = 0xA4; // the sprite $B4 the game draws the sinking wiz into
    clamp();
    return false;
}

void Wiz::update_speed_index() {
    // $818E
    if (tunnel_ != 0) return;
    if (vx_ >= 0) {
        const int a = vx_ - 4;
        speed_index_ = a < 0 ? 0 : std::int8_t((a >> 3) + 1);
    } else {
        const int a = -vx_ - 4;
        speed_index_ = a < 0 ? 0 : std::int8_t(-((a >> 3) + 1));
    }
}

void Wiz::landscape(Playfield& playfield, bool bonus, const Powers& powers) {
    // $836E through $7C1D: the screen cell under the wiz's box.
    const int half_x = (x_ >> 1) & 0xFF;
    const int column = std::uint8_t(half_x - 0x0C + (playfield.fine_scroll() >> 1)) >> 2;
    const int row = std::uint8_t(y_ - 0x33) >> 3;
    const int origin = row * 40 + column;
    auto at = [&](int offset) -> std::uint8_t {
        const int i = origin + offset;
        const int r = i / 40, c = i % 40;
        if (r < 3 || r >= 3 + Playfield::rows) return 0;
        return playfield.code(c, r - 3);
    };
    const auto centre = at(0x51);
    if ((centre >= 0xC2 && centre < 0xC4) || centre == 0xC6 || centre == 0xC7) {
        std::uint8_t last = 0; // a tunnel: $BE to the right means the next place
        for (int k = 1; k <= 3; ++k) {
            last = at(0x51 + k);
            if (last == 0xBE) break;
        }
        place_change_ = last == 0xBE ? 1 : -1;
        return;
    }
    for (int i = 0; i < 8; ++i) {
        if (at(probe_offsets_[std::size_t(i)]) < solid_code) continue;
        bumped_ = true;
        for (auto handler : probe_handlers_[std::size_t(i)]) {
            switch (response_of(handler)) {
            case Response::floor:
                if (powers.antigravity || bonus) {
                    if (vy_direct_ >= 0) {
                        vy_direct_ = std::int8_t(-vy_direct_);
                        y_ = std::uint8_t(y_ + vy_direct_);
                    }
                } else if (falling_) {
                    falling_ = false;
                    rise_step();
                }
                break;
            case Response::ceiling:
                if (powers.antigravity || bonus) {
                    if (vy_direct_ < 1) {
                        vy_direct_ = std::int8_t(-vy_direct_);
                        y_ = std::uint8_t(y_ + vy_direct_);
                    }
                } else if (!falling_) {
                    falling_ = true;
                    fall_step();
                }
                break;
            case Response::wall_right:
                if (vx_ >= 0) vx_ = std::int8_t(-vx_);
                break;
            case Response::wall_left:
                if (vx_ < 0) vx_ = std::int8_t(-vx_);
                break;
            case Response::none: break;
            }
        }
        update_speed_index();
        return;
    }
}

void Wiz::move(const Stick& stick, const Powers& powers, Playfield& playfield, bool bonus, bool no_landscape) {
    stick_ = stick;
    place_change_ = 0;
    bumped_ = false;
    // $7F3D: x follows the view's end offset.
    const int offset = playfield.wiz_offset();
    x_ = offset >= 0 ? 0xAC + offset : (0xAC + offset) & 0xFF;
    vertical(powers, bonus);
    horizontal_input(stick, powers, bonus);
    appear_step();
    clamp();
    if (appear_ == 0 && !no_landscape) landscape(playfield, bonus, powers); // $836E: not once the place is cleared, nor in the lab
}

void Wiz::record_trail() {
    trail_index_ = std::uint8_t(trail_index_ + 1);
    trail_y_[trail_index_ & 7] = y_;
    trail_x_[trail_index_ & 7] = std::uint8_t(x_ >> 1); // $858D: LSR of the low byte only
}

Wiz::Probe Wiz::probe() const {
    return {y_fraction_, vy_, vy_fraction_, std::uint8_t(falling_), std::uint8_t(vy_direct_), spin_, spin_fraction_,
            std::uint8_t(vx_), std::uint8_t(last_vx_), appear_, appear_frame_, appear_clock_, trail_index_};
}

} // namespace wizball::game
