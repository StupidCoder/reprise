#pragma once
// Room 2's opening floor: reconstructed room logic, imported tile patterns,
// and a resettable laboratory that never changes the user's progress.
#include "app/lesson.hpp"
#include "bruce_lee_c64/game/world.hpp"
#include <memory>
namespace bruce_lee::game {
class RoomChapter : public app::Lesson {
  public:
    explicit RoomChapter(const std::filesystem::path &);
    void update(const platform::Controls &, double) override;
    platform::Image draw() const override;
    int stage() const;
    int changed_cells() const;

  private:
    void reset();
    void step();
    data::Assets assets_;
    std::unique_ptr<World> world_;
    Playfield before_;
    platform::Image before_picture_{320, 200};
    std::array<bool, 4> cleared_{true, true, false, false};
    bool running_{}, article_{};
    int article_scroll_{};
    double last_{-1}, credit_{};
};
} // namespace bruce_lee::game
