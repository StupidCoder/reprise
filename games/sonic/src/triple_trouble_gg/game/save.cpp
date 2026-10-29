// Quick saves of the game (save.hpp): saved while an act's loop waits for
// its next frame, resumed there (Game::resume_play).

#include "triple_trouble_gg/game/save.hpp"
#include "triple_trouble_gg/game/game.hpp"

namespace triple_trouble_gg {

namespace {
// The picture's parts, in the order version 3 keeps them.
template <class D, class P, class F>
void picture_parts(D& display, P& origin, F&& f) {
    f(display.tiles);
    f(display.map);
    f(display.sprites);
    f(display.gap);
    f(display.colors);
    f(display.shown);
    f(display.level_shown);
    f(display.line_interrupt);
    f(display.scroll_x);
    f(display.scroll_y);
    f(origin.x);
    f(origin.y);
    f(origin.pending);
}

constexpr char save_magic[8] = {'B', 'S', 'T', 'T', 'G', 'G', 'Q', 'S'};
// 3: sprite entries keep their full positions (widescreen); 4: the shared
// sound driver's channel records (gg_engine/audio/sound_driver.hpp); 5: the
// module keeps the arena force field (widescreen) after the game's parts.
constexpr std::uint32_t save_version = 5;
} // namespace

void Game::save(save::Writer& w) {
    if (!can_save()) {
        throw std::runtime_error("Quick saves are possible during an act");
    }
    w.bytes(save_magic, sizeof save_magic);
    w.value(save_version);
    // Script positions are kept as cartridge addresses: the step numbers
    // depend on the order in which this session decoded the scripts.
    const auto steps = objects;
    for (auto& o : objects) {
        if (o.script.type) {
            o.script.step = step_address(o);
        }
    }
    for_each_state([&](const auto& part) { save::write_part(w, part); });
    objects = steps;
    picture_parts(display, picture_origin, [&](const auto& part) { save::write_part(w, part); });
    for_each_timing([&](const auto& part) { save::write_part(w, part); });
}

void Game::load(save::Reader& r) {
    char magic[sizeof save_magic];
    r.bytes(magic, sizeof magic);
    std::uint32_t version{};
    r.value(version);
    if (std::memcmp(magic, save_magic, sizeof magic) != 0 || version != save_version) {
        throw std::runtime_error("This quick save is not from this Triple Trouble module");
    }
    for_each_state([&](auto& part) { save::read_part(r, part); });
    for (auto& o : objects) {
        if (o.script.type) {
            o.script.step =
                scripts_.step_at(o.script.type, o.script.state, o.script.step, o.script.bank);
        }
    }
    picture_parts(display, picture_origin, [&](auto& part) { save::read_part(r, part); });
    for_each_timing([&](auto& part) { save::read_part(r, part); });
    resume_play();
}

} // namespace triple_trouble_gg
