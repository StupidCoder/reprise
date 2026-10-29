#include "spindizzy_worlds_amiga/game/play_screen.hpp"
#include <utility>
#include "spindizzy_worlds_amiga/view/panel.hpp"
#include "spindizzy_worlds_amiga/view/playfield.hpp"
#include "spindizzy_worlds_amiga/view/text.hpp"
#include "spindizzy_worlds_amiga/view/sprites.hpp"
#include "spindizzy_worlds_amiga/world/object_kind.hpp"

namespace spindizzy::game {
namespace {

constexpr int playfield_height = 168; // the panel starts at row 168 ($149F4: buffer + $1A40)
constexpr int clue_hold = 26;         // $15FDE: $10A1E with 25
constexpr int clue_wait = 126;        // $15FE6: $109E2 with $7D
constexpr std::uint32_t reserve_text = 0x15162;  // $15192: box, " Reserve fuel collected   "
constexpr std::uint32_t overview_box = 0x1134A;  // $11368
constexpr int overview_rows[] = {0x34, 0x22, 0x10, 0}; // $11380: the views' tops in 8-row steps, bottom first

// $15186: the message, then the reserve this level added in per cent
// ($16EF8: × 100 / 256, "%" first and the digits leftward, $18818).
// Returns the printer as it ends.
view::TextPrinter print_reserve(view::Screen& s, const data::Memory& main, const play::Progress& p) {
    view::TextPrinter text(main);
    text.print(s, reserve_text);
    const int added = std::uint16_t((p.reserve >> 16) - (p.reserve_at_start >> 16));
    text.put(s, 0x25);
    text.column -= 2;
    text.number(s, std::uint16_t(added * 100 / 0x100));
    return text;
}

} // namespace

PlayScreen::PlayScreen(const data::Assets& assets, const play::SurfaceTable& surfaces, play::Progress& progress)
    : assets_(assets), surfaces_(surfaces), progress_(progress) {
    start_level();
}

PlayScreen::PlayScreen(const data::Assets& assets, const play::SurfaceTable& surfaces, play::Progress& progress,
                       play::LevelPlay::Restore restore)
    : assets_(assets), surfaces_(surfaces), progress_(progress),
      play_(std::make_unique<play::LevelPlay>(assets, surfaces, progress, restore)), map_(std::make_unique<view::TileMap>()) {}

void PlayScreen::start_level() {
    shown_.reset();
    play_ = std::make_unique<play::LevelPlay>(assets_, surfaces_, progress_);
    map_ = std::make_unique<view::TileMap>(view::TileMap::build(play_->level().grid, assets_.graphics));
    map_build_ = play_->build_count();
}

void PlayScreen::frame(const PlayInput& input, bool slow, play::StickByte& stick) {
    if (outcome_ != Outcome::playing) return;
    if (input.key) key_waiting_ = true;
    // $109CC / $109E2 poll the key ($109B2 reads and clears $107A8) and
    // take the stick's fire bit.
    auto wait_over = [&] { return std::exchange(key_waiting_, false) | stick.take_fire(); };
    if (message_frames_ >= 0) { // $109E2: until a key, fire or d7 + 1 frames
        if (wait_over() || --message_frames_ <= 0) {
            message_frames_ = -1;
            held_.reset();
            finish_level();
        }
        return;
    }
    if (notice_) { // $109CC: until a key or fire
        if (!wait_over()) return;
        notice_ = false;
        held_.reset();
        return;
    }
    bool resumed = false;
    if (overview_) { // $113FA: until a key or fire, then the level loop goes on
        if (!wait_over()) return;
        overview_.reset();
        resumed = true;
    }
    if (paused_) { // $1133C: until a key or fire, the palette back ($1106E), the ticks
        if (!wait_over()) return;
        paused_ = false;
        resumed = true;
    }
    if (clue_) { // the game waits inside $15FC0: 26 frames, then $109E2
        ++clue_frames_;
        if (clue_frames_ <= clue_hold) return;
        if (!wait_over() && clue_frames_ < clue_hold + clue_wait) return;
        // The rest of the frame ran when the clue came up, so this one ends here.
        clue_.reset();
        held_.reset();
        return;
    }
    if (!resumed) {
        // The level loop's key ($112F6, once a frame).
        key_waiting_ = false;
        if (input.pause || input.hold) {
            paused_ = true;
            dimmed_ = input.pause;
            return;
        }
        if (input.overview && !(play_->level_flags() & 0x4000)) { // header bit 14: "Disable Mapper"
            open_overview(slow);
            return;
        }
        if (input.centre) play_->centre();
    }
    if (input.turn) play_->turn(input.turn);
    if (input.space && !space_held_) play_->brake();
    space_held_ = input.space;
    for (int tick = 0; tick < (slow ? 1 : 2) && !play_->ended() && !notice_; ++tick) {
        play_->tick(stick.byte);
        if (play_->take_stick_clear()) stick.byte = 0;
        if (auto clue = play_->take_clue()) {
            clue_ = clue->number;
            clue_frames_ = 0;
            hold_picture([&](view::Screen& screen) { print_clue(screen); });
        }
    }
    // The rest of the level loop: camera, fuel, frame handlers, rebuilds.
    play_->frame(slow);
    explorer_frame_ = play_->explorer_frame(assets_.main, slow);
    if (play_->take_notice()) { // printed by its frame handler ($13862), then $109CC
        notice_ = true;
        hold_picture([&](view::Screen& screen) {
            view::TextPrinter text(assets_.main);
            text.print(screen, 0x13888);
        });
    }
    if (play_->build_count() != map_build_) {
        map_ = std::make_unique<view::TileMap>(view::TileMap::build(play_->level().grid, assets_.graphics));
        map_build_ = play_->build_count();
    }
    if (!play_->ended() || clue_) return;
    if (play_->take_reserve_message()) {
        // $109E2 counts d7 down, which the printer left as the text's
        // height ($37A6) plus its row.
        view::Screen scratch;
        const auto text = print_reserve(scratch, assets_.main, progress_);
        message_frames_ = text.height() + text.row + 1;
        hold_picture([&](view::Screen& screen) { print_reserve(screen, assets_.main, progress_); });
        return;
    }
    finish_level();
}

void PlayScreen::finish_level() {
    // $18A18: the game over, the world done (level 0), or the next level.
    if (progress_.game_over) outcome_ = Outcome::game_over;
    else if (progress_.level == 0) outcome_ = Outcome::world_done;
    else start_level();
}

void PlayScreen::open_overview(bool slow) {
    // $1135A: the box over the screen as it is ($249E set meanwhile), then
    // for 4 rows × 4 columns of views (bottom right first) the camera
    // placed ($256C … $2572), the frame handlers run ($143B6) and the view
    // drawn, then $18DC6 squeezes it into the box: every fourth pixel from
    // the view's pixel 16 ($3FC4 twice: every other bit) and every fourth
    // row, 64 × 42 at x 32 + 64 × column, y 8 + 2 × row; each view's
    // tile overlaps the one below by 6 rows.
    view::Screen shown;
    draw(shown);
    view::TextPrinter text(assets_.main);
    text.print(shown, overview_box);
    view::Canvas canvas(320, playfield_height);
    for (const int row : overview_rows)
        for (int column = 3; column >= 0; --column) {
            const view::Camera at{column * 0x100, row * 8};
            play_->frame_handlers(at, slow);
            explorer_frame_ = play_->explorer_frame(assets_.main, slow);
            draw_view(canvas, at);
            for (int y = 0; y < 42; ++y)
                for (int x = 0; x < 64; ++x) shown.put(32 + 64 * column + x, 8 + 2 * row + y, canvas.at(16 + 4 * x, 4 * y));
        }
    overview_ = shown;
}

void PlayScreen::draw_playfield(view::Screen& screen) const {
    view::Canvas canvas(320, playfield_height);
    draw_view(canvas, play_->camera());
    screen.paste(canvas, 0, 0);
}

void PlayScreen::draw_view(view::Canvas& canvas, const view::Camera& camera) const {
    // The whole buffer width (the pillars cover its edges): column 0 is
    // the map's $2570 − 16.
    canvas.pixels.assign(canvas.pixels.size(), 0);
    const int left = camera.left(), top = camera.top();
    view::draw_playfield(canvas, *map_, assets_.graphics, left, top);
    view::draw_stars(canvas, play_->stars());
    const auto& objects = play_->objects().objects();
    auto sprites = view::start_sprites(play_->level(), play_->switches(), assets_.main, [&](std::size_t index) {
        view::ObjectState state;
        state.gone = true;
        for (const auto& o : objects) {
            if (o.source != index) continue;
            state.gone = !o.alive;
            state.step = o.step;
            state.z = o.z;
            state.shown = o.byte20 != 0;
            if (o.kind == world::ObjectKind::boink || o.kind == world::ObjectKind::pac || o.kind == world::ObjectKind::pyramid) { state.x = o.x; state.y = o.y; state.heading = o.heading & 3; state.popping = o.popping; }
            if (o.kind == world::ObjectKind::pac && o.eaten) {
                state.gone = !o.eaten_drawn;
                state.eaten_picture = o.eaten_picture;
            }
            break;
        }
        return state;
    });
    const auto& e = play_->explorer();
    const view::WorldPosition at{e.x, e.y, e.z};
    const view::ObjectSprite explorer{0, data::read_sprite_frame(assets_.main, explorer_frame_, 1, 16),
                                      view::explorer_place(at), view::sort_value(at) + 0xC};
    view::draw_with_explorer(canvas, std::move(sprites), explorer, *map_, assets_.graphics, left, top);
}

void PlayScreen::draw(view::Screen& screen) const {
    if (overview_) {
        screen = *overview_;
        return;
    }
    if (held_) {
        screen = *held_;
        return;
    }
    draw_live(screen);
    shown_ = screen;
}

// The texts are printed once into the shown buffer ($379C: the picture
// on screen, the frame before) and stay while the game waits.
void PlayScreen::hold_picture(const std::function<void(view::Screen&)>& print) {
    view::Screen screen;
    if (shown_) screen = *shown_;
    else draw_live(screen);
    print(screen);
    held_ = std::move(screen);
}

void PlayScreen::print_clue(view::Screen& screen) const {
    // $15FC0: the clue's text ($15F94 picks it from the list at $15CDA by
    // its number).
    view::TextPrinter text(assets_.main);
    text.to_hidden = false;
    std::uint32_t at = 0x15CDA;
    while (assets_.main.u8(at) != 0 && assets_.main.u8(at) != std::uint8_t(*clue_)) {
        ++at;
        while (assets_.main.u8(at) != 0) ++at;
        ++at;
    }
    if (assets_.main.u8(at) == std::uint8_t(*clue_)) text.print(screen, at + 1);
}

void PlayScreen::draw_live(view::Screen& screen) const {
    screen.fill(0);
    view::set_level_palette(screen, assets_.graphics, play_->palette());
    if (paused_ && dimmed_) // $11074: each ST colour word less its ror by 1 & $777, then doubled
        for (std::size_t i = 0; i < 16; ++i) {
            const int st = screen.palette[i] >> 1;
            screen.palette[i] = data::Colour((st - ((st >> 1) & 0x777)) * 2);
        }
    draw_playfield(screen);
    view::draw_panel(screen, assets_, progress_.world);
    view::draw_compass(screen, assets_.main, progress_.rotation);
    view::draw_clue(screen, assets_.main, progress_.clue_word);
    view::draw_pillars(screen, assets_.main, progress_.fuel_shown);
    if (play_->dying()) { // $12D22: not over the overview map ($249E)
        view::TextPrinter text(assets_.main);
        text.print(screen, 0x12D68);
    }
    if (play_->time_out()) { // $153EE, after the countdown
        view::TextPrinter text(assets_.main);
        text.print(screen, 0x153F6);
    }
    // $153C4: a timer's seconds, right-aligned at column 20, row 6, in the
    // printer's state the panel left (bank 2, normal size).
    for (const auto& o : play_->objects().objects()) {
        if (!o.alive || o.kind != world::ObjectKind::timer) continue;
        view::TextPrinter text(assets_.main);
        text.put(screen, 0x10);
        text.put(screen, 0x02);
        text.column = 20;
        text.row = 6;
        text.number(screen, progress_.timer >> 8);
    }
}

} // namespace spindizzy::game
