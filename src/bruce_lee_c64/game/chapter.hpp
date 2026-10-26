#pragma once
// An isolated, resettable field guide to the reconstructed enemy decisions.
// Uses imported assets and the same World as play; never reads or writes saves.
#include "bruce_lee_c64/game/world.hpp"
#include "platform/host.hpp"
#include <memory>
#include "app/lesson.hpp"
namespace bruce_lee::game {
class EnemyChapter : public app::Lesson {
  public:
    explicit EnemyChapter(const std::filesystem::path &content);
    void update(const platform::Controls &, double now);
    platform::Image draw() const;
    unsigned passes() const { return passes_; }

  private:
    void reset();
    void step(const platform::Controls &);
    data::Assets assets_;
    std::unique_ptr<World> world_;
    int enemy_{2}, scenario_{}, topic_{};
    bool paused_{true}, slow_{true}, boundary_{};
    double last_{-1}, credit_{};
    unsigned passes_{};
    platform::Image picture_{320, 200};
};
} // namespace bruce_lee::game
