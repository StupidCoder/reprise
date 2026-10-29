#include "spindizzy_worlds_amiga/game/module.hpp"
#include "spindizzy_worlds_amiga/data/assets.hpp"
#include "spindizzy_worlds_amiga/data/preview.hpp"
#include "spindizzy_worlds_amiga/play/level_play.hpp"
#include "spindizzy_worlds_amiga/view/camera.hpp"
#include "spindizzy_worlds_amiga/view/objects.hpp"
#include "spindizzy_worlds_amiga/view/playfield.hpp"
#include "spindizzy_worlds_amiga/view/starfield.hpp"
#include "spindizzy_worlds_amiga/view/tile_map.hpp"
#include "spindizzy_worlds_amiga/world/object_kind.hpp"
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace spindizzy::game {
namespace {

constexpr int tutorial = 23;        // the world the Easy map starts on
constexpr int shown_width = 288;    // the playfield between the pillars (buffer columns 16-303)
constexpr int shown_height = 150;   // above the panel

class Module final : public app::GameModule {
public:
    explicit Module(const std::filesystem::path& content)
        : assets_(data::load_assets(content)), surfaces_(play::read_surfaces(assets_.main)) {
        for (const auto& world : assets_.worlds)
            for (const auto& [id, record] : world.levels) levels_.push_back({world.number, id});
        for (std::size_t i = 0; i < levels_.size(); ++i)
            if (levels_[i] == std::pair{tutorial, 1}) choice_ = int(i);
        start_level();
    }

    double step_seconds() const override { return 1.0 / 50; } // one rules tick per PAL frame (slow mode)
    double pixel_aspect() const override { return 1.0; }

    void tick(const app::GameInput& input) override {
        std::uint8_t joystick = 0;
        if (input.up) joystick |= play::stick::up;
        if (input.down) joystick |= play::stick::down;
        if (input.left) joystick |= play::stick::left;
        if (input.right) joystick |= play::stick::right;
        if (input.fire) joystick |= play::stick::fire;
        if (input.space && !space_held_) play_->brake();
        space_held_ = input.space;
        play_->tick(joystick);
        // The frame's drawing steps the explorer's animation ($1490A and
        // the lost/arriving kinds' handlers), then the camera follows ($14138).
        frame_ = play_->explorer_frame(assets_.main);
        view::follow(camera_, explorer_place(), play_->explorer().z, play_->level_flags());
    }

    platform::Image picture() const override {
        view::Canvas canvas(shown_width, shown_height);
        view::draw_playfield(canvas, *map_, assets_.graphics, camera_.scroll_x, camera_.top());
        // Stars are fixed to the screen; buffer column 0 is 16 pixels left.
        view::draw_stars(canvas, stars_, -16);
        const auto& e = play_->explorer();
        const view::WorldPosition at{e.x, e.y, e.z};
        const view::ObjectSprite explorer{0, data::read_sprite_frame(assets_.main, frame_, 1, 16), explorer_place(),
                                          view::sort_value(at) + 0xC};
        view::draw_with_explorer(canvas, sprites_, explorer, *map_, assets_.graphics, camera_.scroll_x, camera_.top());
        platform::Image image(canvas.width, canvas.height);
        const auto& palette = assets_.graphics.palettes[0];
        for (int y = 0; y < canvas.height; ++y)
            for (int x = 0; x < canvas.width; ++x) image.put(x, y, data::to_color(palette[canvas.at(x, y)]));
        return image;
    }

    std::vector<app::GameOption> options() const override {
        app::GameOption level{"Level", {}, choice_, "Test drive: the level to drive in (objects do not act yet)."};
        for (const auto& [world, id] : levels_)
            level.choices.push_back(assets_.worlds.at(std::size_t(world)).name + " " + std::to_string(id));
        return {level};
    }

    void set_option(std::size_t index, int value) override {
        if (index != 0 || value < 0 || value >= int(levels_.size())) return;
        choice_ = value;
        start_level();
    }

private:
    view::SpritePlace explorer_place() const {
        const auto& e = play_->explorer();
        return view::explorer_place({e.x, e.y, e.z});
    }

    void start_level() {
        const auto [world, id] = levels_.at(std::size_t(choice_));
        play_ = std::make_unique<play::LevelPlay>(assets_, surfaces_, world, id);
        map_ = std::make_unique<view::TileMap>(view::TileMap::build(play_->level().grid, assets_.graphics));
        sprites_ = view::start_sprites(play_->level(), play_->switches(), assets_.main);
        stars_.clear();
        view::StarRandom running(0); // used by seed 0 only; its state at a level start is not traced yet
        for (const auto& object : play_->level().objects)
            if (object.kind == int(world::ObjectKind::starfield)) stars_ = view::make_stars(assets_.main, object, running);
        frame_ = play_->explorer_frame(assets_.main);
        camera_ = view::snap_camera(explorer_place(), play_->level_flags()); // $14202
    }

    data::Assets assets_;
    play::SurfaceTable surfaces_;
    std::vector<std::pair<int, int>> levels_; // (world, level)
    int choice_{};
    std::unique_ptr<play::LevelPlay> play_;
    std::unique_ptr<view::TileMap> map_;
    std::vector<view::ObjectSprite> sprites_;
    std::vector<view::Star> stars_;
    view::Camera camera_;
    std::uint32_t frame_{view::explorer_frames};
    bool space_held_{};
};

} // namespace

std::unique_ptr<app::GameModule> start(const std::filesystem::path& content) {
    return std::make_unique<Module>(content);
}

} // namespace spindizzy::game
