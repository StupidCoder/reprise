#pragma once
#include "app/lesson.hpp"
#include "fa_18_interceptor_amiga/data/assets.hpp"
#include "fa_18_interceptor_amiga/data/shapes.hpp"
#include "fa_18_interceptor_amiga/game/aircraft.hpp"
#include <optional>
namespace interceptor::lessons {
// Isolated contact probe using the production carrier and movement routines.
// No engine, control integration, mission progress or player save is advanced.
struct LandingProbe {
    int rate{}, clearance{};
    bool deck{}, hull{}, grounded{}, crashed{}, fast_contact{};
};
LandingProbe landing_probe(const data::Assets&, const game::FlightTables&, bool inverted, int setting);
class TechnicalLesson final : public app::Lesson {
  public:
    TechnicalLesson(const std::filesystem::path &, int chapter);
    void set_canvas(int, int) override;
    void update(const platform::Controls &, double) override;
    platform::Image draw() const override;
    int scroll() const { return int(scroll_); }
    int scroll_limit() const { return std::max(0, page_.height - (height_ - 184)); }
    int selection() const { return selection_; }
    int stage() const { return stage_; }

  private:
    void compose();
    platform::Image figure(const std::string &, int) const;
    void simulate();
    data::Assets assets_;
    game::FlightTables tables_;
    data::Shape plane_;
    struct Road {
        std::array<int, 2> a, b;
        std::array<std::array<int, 2>, 3> before, after;
    };
    std::vector<Road> roads_;
    struct Sample {
        double speed, pitch, bank, height;
    };
    std::vector<Sample> flight_, neutral_;
    std::string article_;
    platform::Image page_{1, 1};
    int chapter_{}, width_{960}, height_{640}, selection_{}, stage_{};
    double scroll_{};
    bool fire_was_{};
    std::optional<int> drag_y_;
};
} // namespace interceptor::lessons
