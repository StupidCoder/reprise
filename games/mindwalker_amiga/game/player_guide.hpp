#pragma once
#include "app/lesson.hpp"

namespace mindwalker::game {
// A read-only player guide. No imported data or running session is needed.
class PlayerGuide final : public app::Lesson {
public:
    explicit PlayerGuide(int page = 0);
    void update(const platform::Controls&, double) override;
    platform::Image draw() const override;
    int page() const { return page_; }
private:
    int page_{};
};
}
