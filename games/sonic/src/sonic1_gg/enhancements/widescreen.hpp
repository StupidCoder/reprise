#pragma once
#include "gamegear/force_field.hpp"
#include "gamegear/video.hpp"
#include "gamegear/wide_view.hpp"
#include "sonic1_gg/game/game.hpp"
#include <array>
#include <vector>

// Widescreen enhancement. None of this is cartridge behaviour: it adapts the
// reconstructed rules to a presented view larger than the 160x144 LCD. With
// the original view size every function here reproduces the LCD crop.
//
// The game side is in Game (update_wide_view, wide_activation, stage_object and
// the wide_* margins used by off-screen tests); this header adds the view size
// and two purely cosmetic helpers.
namespace sonic1_gg {
// World rectangle shown by a view of the given pixel size.
struct ViewRect {
    int x{}, y{}, width{160}, height{144};
};

// Scene size for a drawable canvas and its aspect limits (10:9 to 21:9) are
// shared with the other Game Gear games (gamegear/wide_view.hpp).
using gg::expanded_view_size;
using gg::max_view_aspect;
using gg::min_view_aspect;
// The view is centred on the LCD crop, then clamped so it never reveals more
// than the LCD could show anywhere in the act: the loader's camera limits
// (header words, before arenas or autoscroll narrow them) plus the crop.
// When that area is smaller than the view, the area is centred instead. The
// LCD crop itself always remains inside the view.
ViewRect view_rect(const Game&, int width, int height);

// Fades objects that are activated or deactivated inside the larger view
// under their original bounds (bosses, doors, scripted objects), instead of
// letting them pop. Cosmetic: no object slots, RNG or saves are touched.
class ObjectFades {
public:
    struct Ghost {
        Object object;
        int ticks{};
    };

    void reset();
    void tick(const Game*, bool enabled);

    float alpha(int slot) const { return 1.f - float(fade_[slot]) / fade_ticks; }

    const std::vector<Ghost>& ghosts() const { return ghosts_; }

    static constexpr int fade_ticks = 10;

private:
    std::array<Object, 32> last_{};
    std::array<int, 32> type_{}, fade_{};
    std::array<bool, 32> drawn_{}, running_{};
    std::vector<Ghost> ghosts_;
    bool primed_{};
    unsigned act_{~0u};
};

// Force field drawn where a locked arena's invisible walls fall inside the
// larger view (gamegear/force_field.hpp). Walls are the 4F33-4F8C player
// limits; nothing here changes them. Cosmetic: independent of object slots,
// RNG and quick saves.
class ArenaBarrier {
public:
    void reset();
    void tick(const Game*, bool enabled);
    void draw(gg::Screen&, const Game&, int view_x, int view_y) const;

private:
    gg::ForceField field_;
    unsigned act_{~0u};
};
} // namespace sonic1_gg
