#include "spindizzy_worlds_amiga/game/module.hpp"
#include "spindizzy_worlds_amiga/data/assets.hpp"
#include "spindizzy_worlds_amiga/game/front_end.hpp"
#include "spindizzy_worlds_amiga/game/play_screen.hpp"
#include "spindizzy_worlds_amiga/play/level_play.hpp"
#include "spindizzy_worlds_amiga/play/surfaces.hpp"
#include "spindizzy_worlds_amiga/view/screen.hpp"
#include <memory>
#include <optional>

namespace spindizzy::game {
namespace {

constexpr std::uint32_t main_menu = 0x18B44, main_menu_box = 0x18B3A;   // $18A84
constexpr std::uint32_t next_menu = 0x18CDC, next_menu_box = 0x18CD2;   // $18CB0: "Next world", "Save game"
constexpr int main_menu_timeout = 0x64 << 3;                             // $18A90: d7 × 8 passes
constexpr std::uint32_t easy_worlds = 0x9F800000, hard_worlds = 0x007FFFFF; // $11E10: $24AE per cluster

// The game's screens in the original's main loop ($18950).
enum class Stage { attract, menu, clusters, map, play, next_menu };

class Module final : public app::GameModule {
public:
    explicit Module(const std::filesystem::path& content)
        : assets_(data::load_assets(content)), surfaces_(play::read_surfaces(assets_.main)) {
        attract_.emplace(assets_);
    }

    double step_seconds() const override { return 1.0 / 50; } // PAL frames
    double pixel_aspect() const override { return 1.0; }

    void tick(const app::GameInput& input) override {
        std::uint8_t joystick = 0;
        if (input.up) joystick |= play::stick::up;
        if (input.down) joystick |= play::stick::down;
        if (input.left) joystick |= play::stick::left;
        if (input.right) joystick |= play::stick::right;
        if (input.fire) joystick |= play::stick::fire;
        switch (stage_) {
        case Stage::attract:
            if (attract_->frame(joystick)) open_menu();
            break;
        case Stage::menu: {
            const int item = menu_->frame(joystick);
            slow_ = menu_->slow_mode;
            if (item == -1) { // timed out: the attract again
                attract_.emplace(assets_);
                stage_ = Stage::attract;
            } else if (item == 1) { // Start new game ($18AB4)
                cluster_ = 0;
                worlds_done_ = 0;
                progress_.clues_left = 0xFFFFFFFF;
                open_map();
            } else if (item == 0 || item == 6) { // Continue / Slow Mode
                open_map();
            }
            break; // Load, Save, Check performance, Wind back world, Create Data disk: not rebuilt
        }
        case Stage::clusters:
            if (const int chosen = clusters_->frame(joystick)) {
                cluster_ = chosen;
                worlds_left_ = chosen == 1 ? easy_worlds : hard_worlds;
                open_map();
            }
            break;
        case Stage::map: {
            const int world = map_->frame(joystick);
            if (world < 0) break;
            // $1177A: world $16 needs the Hard worlds 0-21 done, $1F the Easy 23-28.
            if (world == 0x1F && (worlds_left_ & 0x1F800000)) break;
            if (world == 0x16 && (worlds_left_ & 0x003FFFFF)) break;
            progress_.start_world(assets_.main, world, worlds_done_ * 2);
            if (worlds_done_ == 0) { progress_.fuel = 0x80 << 16; progress_.reserve = 0; } // $16D48
            play_ = std::make_unique<PlayScreen>(assets_, surfaces_, progress_);
            stage_ = Stage::play;
            break;
        }
        case Stage::play: {
            PlayInput in;
            in.joystick = joystick;
            in.space = input.space;
            play_->frame(in, slow_);
            if (play_->outcome() == PlayScreen::Outcome::world_done) {
                // $16B8C: the world is recorded done; fuel and reserve go on.
                worlds_left_ &= ~(1u << (progress_.world & 31));
                ++worlds_done_;
                view::Screen behind;
                play_->draw(behind);
                menu_.emplace(assets_, behind, next_menu, next_menu_box, 0);
                stage_ = Stage::next_menu;
            } else if (play_->outcome() == PlayScreen::Outcome::game_over) {
                // $18C14 offers the reserve fuel (not rebuilt): back to the title.
                attract_.emplace(assets_);
                stage_ = Stage::attract;
            }
            break;
        }
        case Stage::next_menu:
            if (menu_->frame(joystick) >= 0) open_map(); // "Next world" (saving is not rebuilt)
            break;
        }
    }

    platform::Image picture() const override {
        view::Screen screen;
        switch (stage_) {
        case Stage::attract: attract_->draw(screen); break;
        case Stage::menu: case Stage::next_menu: menu_->draw(screen); break;
        case Stage::clusters: clusters_->draw(screen); break;
        case Stage::map: map_->draw(screen); break;
        case Stage::play: play_->draw(screen); break;
        }
        return screen.image();
    }

private:
    void open_menu() {
        view::Screen behind;
        attract_->draw(behind);
        menu_.emplace(assets_, behind, main_menu, main_menu_box, main_menu_timeout);
        menu_->slow_mode = slow_;
        stage_ = Stage::menu;
    }
    void open_map() {
        if (cluster_ == 0) { // $11710: no cluster chosen yet
            clusters_.emplace(assets_);
            stage_ = Stage::clusters;
            return;
        }
        map_.emplace(assets_, cluster_ == 1, worlds_left_);
        stage_ = Stage::map;
    }

    data::Assets assets_;
    play::SurfaceTable surfaces_;
    play::Progress progress_;
    Stage stage_{Stage::attract};
    std::optional<Attract> attract_;
    std::optional<Menu> menu_;
    std::optional<ClusterChoice> clusters_;
    std::optional<WorldMap> map_;
    std::unique_ptr<PlayScreen> play_;
    int cluster_{};                 // $36B4: 0 none yet, 1 Easy, 2 Hard
    std::uint32_t worlds_left_{};   // $24AE
    int worlds_done_{};             // $36B0 / 2
    bool slow_{true};               // $24A6: the menu's "Slow Mode? Yes"
};

} // namespace

std::unique_ptr<app::GameModule> start(const std::filesystem::path& content) {
    return std::make_unique<Module>(content);
}

} // namespace spindizzy::game
