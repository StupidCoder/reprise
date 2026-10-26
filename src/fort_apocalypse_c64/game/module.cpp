#include "fort_apocalypse_c64/game/module.hpp"
#include "c64/program.hpp"
#include "platform/files.hpp"
#include "ui/vhs.hpp"
#include <algorithm>
#include <cmath>

namespace fort_apocalypse::game {

namespace {
// The border as a PAL screen shows it around the display window.
constexpr int margin_x = 32, margin_y = 36;
} // namespace

Module::Module(data::Assets assets, data::GameTables tables)
    : assets_(std::make_unique<const data::Assets>(std::move(assets))),
      tables_(std::make_unique<const data::GameTables>(std::move(tables))),
      game_(std::make_unique<Game>(*assets_, *tables_)), enhanced_(*assets_, *tables_) {
    shown_ = game_->picture();
}

void Module::tick(const app::GameInput& input) {
    ++frames_;
    const bool button = input.fire || input.space;
    const bool pressed = button && !hold_button_;
    hold_button_ = button;
    if (world_) {
        crash_tick(input, pressed);
        return;
    }
    if (!game_->craft_crashing()) {
        held_this_crash_ = false;
    }
    shake_ *= 0.82;
    if (hit_stop_ > 0) {
        --hit_stop_; // the moment of a big kill: the game holds still
        return;
    }
    game_->tick(read_controls(input), options_);
    if (const int impact = game_->carnage_state().impact; impact > 0 && particles()) {
        constexpr int stops[] = {0, 0, 2, 4};
        constexpr double shakes[] = {0, 1.2, 2.5, 5.5};
        hit_stop_ = stops[impact];
        shake_ = std::min(7.0, shake_ + shakes[impact]);
    }
    if (after_hold_ && game_->craft_crashing()) {
        return; // the crash's last frame, as the copy left it on screen
    }
    after_hold_ = false;
    draw();
    if (particles() && game_->craft_crashing() && !held_this_crash_) {
        // The crash's first frame: from here a copy of the game is shown.
        held_this_crash_ = true;
        world_ = std::make_unique<Game>(*game_);
        holding_ = skip_ = false;
        hold_frames_ = 0;
    }
}

void Module::crash_tick(const app::GameInput& input, bool pressed) {
    // The copy plays on with the crash kept open, its camera following the
    // wreck until it bursts.
    skip_ = skip_ || pressed;
    world_->prolong_crash(enhanced_.craft_wreck_sprite());
    world_->tick(read_controls(app::GameInput{}), options_);
    enhanced_.update(*world_, {particles(), smooth_, wide_.columns});
    shown_ = enhanced_.picture();
    // The HUD (score, fuel, radar) is the real game's: the copy's would
    // count on and jump back when the real game takes over.
    const auto& real = game_->picture();
    const int left = (shown_.width - real.width) / 2;
    if (left >= 0) {
        for (int y = 0; y < Game::hud_lines; ++y) {
            std::copy_n(real.pixels.begin() + y * real.width, real.width,
                        shown_.pixels.begin() + y * shown_.width + left);
        }
    }
    if (!holding_) {
        if (particles() && game_->crash_frames_left() > 1) {
            game_->tick(read_controls(input), options_); // the crash runs its course
            return;
        }
        // Its last frame comes next: the game waits here until the wreck
        // has come to rest (or fire or space goes on). Its sound fades
        // meanwhile (the game sets the voices again after).
        holding_ = true;
        auto& sid = game_->sid();
        for (int control : {0x04, 0x0B, 0x12}) {
            sid.write(control, std::uint8_t(sid.read_register(control) & 0xFE));
        }
    }
    ++hold_frames_;
    const bool settled = hold_frames_ > 25 && enhanced_.craft_wreck_rest() > 50;
    if (skip_ || settled || hold_frames_ > 6 * 50 || !particles()) {
        holding_ = false;
        after_hold_ = true;
        world_.reset();
    }
}

void Module::set_canvas(int width, int height) {
    wide_ = widescreen_ ? enhanced::wide_size(width, height, pixel_aspect()) : enhanced::WideSize{};
    options_.wide_view = wide_.columns;
}

void Module::draw() {
    if (particles() || smooth_) {
        enhanced_.update(*game_, {particles(), smooth_, wide_.columns});
        shown_ = enhanced_.picture();
    } else if (wide_.columns > 0) {
        shown_ = enhanced::wide_frame(*game_, wide_.columns).rasterise();
    } else {
        shown_ = game_->picture();
    }
}

platform::Image Module::picture() const {
    auto picture = [&] {
        if (!widescreen_ || wide_.width == 0) {
            return shown_.to_image(margin_x, margin_y);
        }
        // Widescreen: the picture to the window's edges, cut to its shape
        // about the middle (no side border; some border above and below only
        // in windows narrower than the display window's own shape).
        const int width = std::min(shown_.width, wide_.width);
        return shown_.to_image(0, wide_.margin_y, (shown_.width - width) / 2, width);
    }();
    if (shake_ > 0.4) {
        // The playfield shakes (the HUD above it holds).
        const int top = (!widescreen_ || wide_.width == 0 ? margin_y : wide_.margin_y) + Game::hud_lines;
        const int dx = int(std::lround(std::sin(frames_ * 1.9) * shake_));
        const int dy = int(std::lround(std::cos(frames_ * 2.7) * shake_ * 0.6));
        const auto still = picture;
        for (int y = top; y < picture.height; ++y) {
            const int sy = std::clamp(y - dy, top, picture.height - 1);
            for (int x = 0; x < picture.width; ++x) {
                const int sx = std::clamp(x - dx, 0, picture.width - 1);
                picture.pixels[std::size_t(y * picture.width + x)] =
                    still.pixels[std::size_t(sy * picture.width + sx)];
            }
        }
    }
    return vhs_ ? ui::vhs(picture, frames_, vhs_) : picture;
}

void Module::audio(std::span<float> stereo, int sample_rate) {
    mono_.resize(stereo.size() / 2);
    game_->sid().render(mono_, sample_rate);
    for (std::size_t i = 0; i < mono_.size(); ++i) {
        stereo[2 * i] = stereo[2 * i + 1] = mono_[i];
    }
}

bool Module::save() {
    saved_ = std::make_unique<Game>(*game_);
    return true;
}

bool Module::load() {
    if (!saved_) {
        return false;
    }
    game_ = std::make_unique<Game>(*saved_);
    enhanced_.reset();
    holding_ = held_this_crash_ = after_hold_ = false;
    world_.reset();
    shown_ = game_->picture();
    return true;
}

std::vector<app::GameOption> Module::options() const {
    return {{"Enemy pilot",
             {"Original", "Fixed"},
             options_.fixed_enemy_climb ? 1 : 0,
             "Original: a corrupt byte makes the enemy climb into rock, and brown rock cannot "
             "hurt it (the C64's collisions ignore that colour). Fixed: neither."},
            {"Particle effects",
             {"Off", "On"},
             particles_ ? 1 : 0,
             "Debris and fire from hits, tossed tank turrets, and shot-down mines and craft "
             "tumbling to the ground. The game plays exactly the same."},
            {"Smooth motion",
             {"Off", "On"},
             smooth_ ? 1 : 0,
             "Smooth banking; tanks, mines and shells glide instead of jumping a character at "
             "a time, and tanks rock over rough ground. The game plays the same."},
            {"Widescreen",
             {"Off", "On"},
             widescreen_ ? 1 : 0,
             "Fills the window to its edges and shows more of the map left and right, up to "
             "21:9. The game plays exactly the same."},
            {"VHS tape",
             {"Off", "Light", "Medium", "Heavy", "Broken"},
             vhs_,
             "The picture as from a worn video tape: bleeding colour, wobbling lines, a "
             "tracking band, dropouts and noise; Heavy and Broken add tears, fading colour and "
             "a slipping vertical hold."},
            {"Infinite lives",
             {"Off", "On"},
             options_.infinite_lives ? 1 : 0,
             "A crash costs no pilot.",
             "Cheats"},
            {"Infinite fuel",
             {"Off", "On"},
             options_.infinite_fuel ? 1 : 0,
             "The tank stays full.",
             "Cheats"},
            {"No laser barriers",
             {"Off", "On"},
             options_.no_lasers ? 1 : 0,
             "The flashing barriers and the laser grids stay dark and can be flown through. "
             "Level 1's cavern barriers stay: they are its teleport.",
             "Cheats"},
            {"Enemies don't fire",
             {"Off", "On"},
             options_.enemies_hold_fire ? 1 : 0,
             "The tanks launch no missiles and the enemy helicopter does not shoot. Ramming "
             "them still costs a pilot.",
             "Cheats"},
            {"Slower moving walls",
             {"Off", "On"},
             options_.slow_walls ? 1 : 0,
             "The sweeping walls move half as fast.",
             "Cheats"},
            {"Carnage mode",
             {"Off", "On"},
             options_.carnage ? 1 : 0,
             "Not the original game: a rapid-fire gun that carves the rock; loose rock, "
             "tanks, towers and laser emitters fall, depots blow up; combos, overdrive and "
             "momentum flight.",
             "Carnage"}};
}

void Module::set_option(std::size_t index, int value) {
    if (index == 0) {
        options_.fixed_enemy_climb = value == 1;
    } else if (index == 1) {
        particles_ = value == 1;
    } else if (index == 2) {
        smooth_ = value == 1;
    } else if (index == 3) {
        widescreen_ = value == 1;
        options_.widescreen = widescreen_;
        if (!widescreen_) {
            wide_ = {};
            options_.wide_view = 0;
        }
    } else if (index == 4) {
        vhs_ = value;
    } else if (index == 5) {
        options_.infinite_lives = value == 1;
    } else if (index == 6) {
        options_.infinite_fuel = value == 1;
    } else if (index == 7) {
        options_.no_lasers = value == 1;
    } else if (index == 8) {
        options_.enemies_hold_fire = value == 1;
    } else if (index == 9) {
        options_.slow_walls = value == 1;
    } else if (index == 10) {
        options_.carnage = value == 1;
    }
    if (!particles() && !smooth_) {
        draw(); // the effects' picture follows with the next frame
    }
}

std::unique_ptr<app::GameModule> start(const std::filesystem::path& content) {
    const auto program = c64::Program::from_prg(platform::read_file(content / "game.prg"));
    return std::make_unique<Module>(data::decode_assets(program), data::decode_tables(program));
}

} // namespace fort_apocalypse::game
