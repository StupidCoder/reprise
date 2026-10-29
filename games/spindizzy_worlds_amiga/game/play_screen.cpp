#include "spindizzy_worlds_amiga/game/play_screen.hpp"
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

} // namespace

PlayScreen::PlayScreen(const data::Assets& assets, const play::SurfaceTable& surfaces, play::Progress& progress)
    : assets_(assets), surfaces_(surfaces), progress_(progress) {
    start_level();
}

void PlayScreen::start_level() {
    play_ = std::make_unique<play::LevelPlay>(assets_, surfaces_, progress_);
    map_ = std::make_unique<view::TileMap>(view::TileMap::build(play_->level().grid, assets_.graphics));
    map_build_ = play_->build_count();
    stars_.clear();
    view::StarRandom running(0); // used by seed 0 only; its state at a level start is not traced yet
    for (const auto& object : play_->level().objects)
        if (object.kind == int(world::ObjectKind::starfield)) stars_ = view::make_stars(assets_.main, object, running);
}

void PlayScreen::frame(const PlayInput& input, bool slow) {
    if (outcome_ != Outcome::playing) return;
    const bool fire_now = input.joystick & play::stick::fire;
    const bool fire_pressed = fire_now && !fire_held_;
    fire_held_ = fire_now;
    if (notice_) { // $109CC: until fire
        if (fire_now) notice_ = false;
        return;
    }
    (void)fire_pressed;
    if (clue_) { // the game waits inside $15FC0
        ++clue_frames_;
        const bool fire = input.joystick & play::stick::fire;
        if (clue_frames_ >= clue_hold + clue_wait || (clue_frames_ > clue_hold && fire)) clue_.reset();
        return;
    }
    if (input.space && !space_held_) play_->brake();
    space_held_ = input.space;
    for (int tick = 0; tick < (slow ? 1 : 2) && !play_->ended() && !notice_; ++tick) {
        play_->tick(input.joystick);
        if (auto clue = play_->take_clue()) {
            clue_ = clue->number;
            clue_frames_ = 0;
        }
    }
    // The rest of the level loop: camera, fuel, frame handlers, rebuilds.
    play_->frame(slow);
    explorer_frame_ = play_->explorer_frame(assets_.main, slow);
    if (play_->take_notice()) notice_ = true; // drawn by its frame handler
    if (play_->build_count() != map_build_) {
        map_ = std::make_unique<view::TileMap>(view::TileMap::build(play_->level().grid, assets_.graphics));
        map_build_ = play_->build_count();
    }
    if (!play_->ended() || clue_) return;
    // $18A18: the game over, the world done (level 0), or the next level.
    if (progress_.game_over) outcome_ = Outcome::game_over;
    else if (progress_.level == 0) outcome_ = Outcome::world_done;
    else start_level();
}

void PlayScreen::draw_playfield(view::Screen& screen) const {
    // The whole buffer width (the pillars cover its edges): column 0 is
    // the map's $2570 − 16.
    view::Canvas canvas(320, playfield_height);
    const int left = play_->camera().left(), top = play_->camera().top();
    view::draw_playfield(canvas, *map_, assets_.graphics, left, top);
    view::draw_stars(canvas, stars_);
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
            break;
        }
        return state;
    });
    const auto& e = play_->explorer();
    const view::WorldPosition at{e.x, e.y, e.z};
    const view::ObjectSprite explorer{0, data::read_sprite_frame(assets_.main, explorer_frame_, 1, 16),
                                      view::explorer_place(at), view::sort_value(at) + 0xC};
    view::draw_with_explorer(canvas, std::move(sprites), explorer, *map_, assets_.graphics, left, top);
    screen.paste(canvas, 0, 0);
}

void PlayScreen::draw(view::Screen& screen) const {
    screen.fill(0);
    view::set_level_palette(screen, assets_.graphics);
    draw_playfield(screen);
    view::draw_panel(screen, assets_, progress_.world);
    view::draw_compass(screen, assets_.main, progress_.rotation);
    view::draw_pillars(screen, assets_.main, progress_.fuel_shown);
    if (clue_) {
        // $15FC0: the clue's text over the playfield ($15F94 picks it from
        // the list at $15CDA by its number).
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
    if (notice_) {
        view::TextPrinter text(assets_.main);
        text.print(screen, 0x13888);
    }
}

} // namespace spindizzy::game
