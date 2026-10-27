#pragma once
#include "app/lesson.hpp"
#include "marble_madness_amiga/game/view.hpp"
#include <memory>

namespace marble_madness::game {
// A separate, disposable race. Imported geometry drives every illustration;
// experiments call the shipping rules and never save player progress.
class MechanicsChapter final : public app::Lesson {
public:
    MechanicsChapter(const std::filesystem::path&, int topic);
    void update(const platform::Controls&, double) override;
    platform::Image draw() const override;
    int steps() const { return steps_; }
    const Race& race() const { return *race_; }
private:
    void reset(int experiment = 0);
    void place(Marble&, int x, int y);
    void step(const platform::Controls&);
    void terrain_figure(platform::Image&, int x, int y, bool triangle) const;
    void route_figure(platform::Image&, int x, int y) const;
    void covers_figure(platform::Image&, int x, int y) const;
    void decision_figure(platform::Image&, int x, int y) const;
    void silly_figure(platform::Image&, int x, int y) const;
    void lab(platform::Image&) const;
    data::GameFiles files_;
    data::Assets assets_;
    std::unique_ptr<Race> race_;
    std::unique_ptr<View> view_;
    bool enemies_{}, silly_{}, running_{};
    int page_{}, steps_{}, experiment_{};
    double last_{-1}, credit_{};
};
}
