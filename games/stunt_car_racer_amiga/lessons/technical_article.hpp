#pragma once
#include "app/lesson.hpp"
#include "stunt_car_racer_amiga/data/track.hpp"
#include <algorithm>
#include <optional>
#include <string>

namespace stuntcar::lessons {
// Read-only technical articles. Imported geometry supplies the illustrations;
// the explanatory charts neither advance gameplay nor alter a save.
class TechnicalArticle final : public app::Lesson {
public:
    TechnicalArticle(const std::filesystem::path& content, int chapter);
    void set_canvas(int width, int height) override;
    void update(const platform::Controls&, double) override;
    platform::Image draw() const override;
    int scroll() const { return int(scroll_); }
    int scroll_limit() const { return std::max(0, page_.height - (height_ - 128)); }
private:
    void compose();
    platform::Image figure(const std::string&, int width) const;
    data::Circuits circuits_;
    std::vector<data::CrossSection> road_;
    platform::Image race_{1, 1}, page_{1, 1};
    std::string article_;
    int chapter_{}, width_{960}, height_{640};
    double scroll_{};
    std::optional<int> drag_y_;
};
} // namespace stuntcar::lessons
