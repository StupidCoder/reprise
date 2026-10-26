#include "wizball_c64/game/play.hpp"

// Tunnels between places ($81C9-$8369). Running into a tunnel's mouth
// stops the wiz; for 21 passes it sinks out of sight (a copy of its sprite
// drawn one row lower each pass). Then the next place is set up and, in a
// sequence of its own frames inside part A, a tunnel exit rises out of the
// ground on the right (two stretched sprites drawn into $ED40/$EDC0 from
// $CE7F/$CEBF a row at a time), the wiz slides out of it to its start
// column, and the exit sinks again. Then the wiz routine runs once more
// and part A goes on.
namespace wizball::game {
namespace {
constexpr int sprite_b5 = 0x40, sprite_b7 = 0xC0; // offsets in the work sprites ($ED40, $EDC0)
constexpr std::uint16_t exit_left = 0xCE7F, exit_right = 0xCEBF;
} // namespace

void Play::enter_tunnel(int direction) {
    // $81C9: a tunnel to the right leads on only from a place that is not
    // the furthest one reached; it then brings the wiz back to the same
    // place. To the left it always goes back one.
    if (direction > 0) {
        if (place_ != furthest_) ++place_;
    } else {
        --place_;
    }
    timer_ticks_ = timer_seconds_ = 0;
    // $81E6: the wiz's current sprite into sprite $B4.
    const auto& sprite = assets_.sprites[std::size_t(wiz_.frame())];
    std::copy(sprite.rows.begin(), sprite.rows.end(), work_sprites_.begin());
    work_sprites_[63] = sprite.extra;
    wiz_.enter_tunnel();
    playfield_.halt();
    sound_->start_effect(3);
}

void Play::sink_step() {
    // $8216: the copy one row lower.
    for (int y = 0x3E; y >= 1; --y) work_sprites_[std::size_t(y)] = work_sprites_[std::size_t(y - 3)];
    work_sprites_[0] = work_sprites_[1] = work_sprites_[2] = 0;
    if (wiz_.sink()) start_arrival();
}

void Play::remove_objects_for_place() {
    // $A1C9: everything goes; what came from the list may come again.
    cursor_ = 0;
    for (auto& o : objects_) {
        if (o.y == 0) continue;
        o.y = 0;
        o.box_top = o.box_bottom = 0;
        if (o.entry >= 0) spawn_lists_[std::size_t(o.entry_slot)][std::size_t(o.entry)].kind &= 0x7F;
    }
    sprite_mask_ &= 0xFC;
}

void Play::start_arrival() {
    // $8239
    wiz_.arrive();
    playfield_.set_wiz_offset(0);
    sprite_mask_ = 0;
    catellite_.place_at(0, 0x3C);
    playfield_.align_fine_scroll();
    remove_objects_for_place();
    enter_place();
    count_aliens();
    // $8C50 turns the screen back on; $9CCB clears the enemy shots.
    for (auto& e : enemy_shots_) e = EnemyShot{};
    beeping_ = 0;
    cursor_ = 0;
    // $8290: the four work sprites cleared, sprites 1 and 2 the exit.
    std::fill(work_sprites_.begin(), work_sprites_.begin() + 0x100, std::uint8_t(0)); // $8F15: sprites $B4-$B7
    catellite_.show_icon(0xD0, 0x46, 0xB5, 3); // sprite 1's registers serve the exit ($827E-$82A5)
    sprite_mask_ = 6;
    arrival_ = Arrival{Arrival::Stage::rise_open, 0x15, 0, 0};
}

void Play::arrival_frame() {
    // $6E92: the frame's wait, which also moves the stars.
    marker(0x6E92);
    playfield_.animate_stars(wiz_.speed_index(), random_);
    auto& a = *arrival_;
    auto next = [&](Arrival::Stage stage, int count) {
        a.stage = stage;
        a.count = count;
    };
    auto rise = [&] { // $830C: the exit's rows come up from below
        a.offset = std::uint8_t(a.offset + 3);
        const auto left = image_.bytes(exit_left, 0x40), right = image_.bytes(exit_right, 0x40);
        for (int x = 0; x < 0x40; ++x) {
            work_sprites_[std::size_t(a.offset + x)] = left[std::size_t(x)];
            work_sprites_[std::size_t(0x80 + a.offset + x)] = right[std::size_t(x)];
        }
    };
    auto settle = [&] { // $8336: both shown sprites up a row
        a.offset = std::uint8_t(a.offset - 3);
        for (int y = 0; y < 0x40; ++y) {
            work_sprites_[std::size_t(sprite_b5 + y)] = work_sprites_[std::size_t(sprite_b5 + 3 + y)];
            work_sprites_[std::size_t(sprite_b7 + y)] = work_sprites_[std::size_t(sprite_b7 + 3 + y)];
        }
        for (int y = 0x3C; y < 0x3F; ++y) work_sprites_[std::size_t(sprite_b5 + y)] = work_sprites_[std::size_t(sprite_b7 + y)] = 0;
    };
    switch (a.stage) {
    case Arrival::Stage::rise_open:
        rise();
        if (--a.count == 0) next(Arrival::Stage::settle_open, 5);
        break;
    case Arrival::Stage::settle_open:
        settle();
        if (--a.count == 0) { // $82C7: sprite 3 is the wiz, at the exit
            next(Arrival::Stage::slide, 0);
            a.slide_x = 0xD0;
            sprite_mask_ = 0x0E;
        }
        break;
    case Arrival::Stage::slide: // $82E1: one pixel a frame to its column
        if (--a.slide_x == wiz_.x()) next(Arrival::Stage::rise_close, 5);
        break;
    case Arrival::Stage::rise_close:
        rise();
        if (--a.count == 0) next(Arrival::Stage::settle_close, 0x15);
        break;
    case Arrival::Stage::settle_close:
        settle();
        if (--a.count == 0) {
            sprite_mask_ = 3;
            wiz_.end_arrival();
            finished_arrival_back_to_play_ = a.back_to_play;
            arrival_.reset();
        }
        break;
    }
}

void Play::resume(const Ports& ports) {
    if (pending_ == Pending::round_end) {
        round_end_step();
        return;
    }
    if (pending_ == Pending::flight_intro || pending_ == Pending::flight_outro) {
        flight_wait_step();
        return;
    }
    if (pending_ != Pending::none) {
        bonus_setup_step();
        return;
    }
    arrival_frame();
    if (waiting()) return;
    const bool back_to_play = finished_arrival_back_to_play_;
    wiz_routine(); // $8306: JMP $7DF4
    if (waiting() || back_to_play) return;
    pass_a_rest(ports);
    pass_b();
}

} // namespace wizball::game
