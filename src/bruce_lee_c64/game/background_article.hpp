#pragma once
// Read-only illustrated room-storage lesson, composed from imported maps and
// glyphs. Only scrolling and returning to the lesson list are interactive.
#include "app/lesson.hpp"
namespace bruce_lee::game {
class BackgroundArticle : public app::Lesson {
  public:
    explicit BackgroundArticle(const std::filesystem::path &);
    void update(const platform::Controls &, double) override;
    platform::Image draw() const override;

  private:
    platform::Image page_{880, 3040};
    int scroll_{};
};
} // namespace bruce_lee::game
