#pragma once
#include "gamegear/video.hpp"
#include "sonic1_gg/game/game.hpp"

namespace sonic1_gg {
// Native presentation mask recovered from the imported basin tile families.
// Does not modify layout, tile priority, collision, water flags or player physics.
class BasinWater {
public:
    struct Surface {
        int x, y, width;
    };

    struct Outlet {
        int x, y, width;
    };

    // One vertical span of falling water in a world pixel column.
    struct FallEdge {
        int x, top, bottom, side;
    };

    explicit BasinWater(const Level&);

    bool empty() const { return surfaces_.empty() && falls_.empty(); }

    bool has_basins() const { return !surfaces_.empty(); }

    bool has_falls() const { return !falls_.empty(); }

    bool fall_contains(int world_x, int world_y) const;
    // Water region (detection, splashes). replaces() excludes original art
    // kept inside it, such as Jungle's palette-animated surface waves.
    bool contains(int world_x, int world_y) const;
    bool replaces(int world_x, int world_y) const;
    int surface_at(int world_x, int world_y) const;
    unsigned foreground_index(const Level&, int world_x, int world_y) const;
    unsigned background_index(const Level&, int world_x, int world_y) const;

    void draw(gg::Screen&, const Game&, int camera_x, int camera_y) const;
    // Waterfall replacement. Called after the background (all fall pixels) and
    // again after priority foreground tiles with priority_only, so Sonic stays
    // in front of or behind exactly the waterfall pixels the original uses.
    void draw_falls(gg::Screen&, const Game&, int camera_x, int camera_y, bool priority_only) const;

private:
    struct FallTile {
        std::uint64_t mask{};
        std::uint64_t fed{};     // pixel's run spills out of a basin directly above
        std::uint64_t curtain{}; // translucent Jungle stripe curtain pixel
        bool priority{};
        // World Y of the top/bottom pixel of the vertical run containing
        // each pixel, so flow speed and foam follow the authored fall height.
        std::array<std::int32_t, 64> top{}, bottom{};
    };

    const FallTile* fall_tile(int world_x, int world_y) const;
    void draw_fall_particles(gg::Screen&, const Game&, int camera_x, int camera_y) const;
    int width_{}, height_{};
    unsigned scene_{};
    // One 64-bit coverage mask per 8x8 tile; curved rims remain uncovered.
    std::vector<std::uint64_t> masks_;
    std::vector<std::uint64_t> keep_; // original pixels retained inside masks_
    std::vector<Surface> surfaces_;
    std::vector<Outlet> outlets_;
    std::vector<std::int32_t> fall_index_; // per 8x8 tile, -1 when dry
    std::vector<FallTile> falls_;
    std::vector<FallEdge> fall_edges_;
};
} // namespace sonic1_gg
