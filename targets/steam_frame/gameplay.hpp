#pragma once
#include "app/game_entry.hpp"
#include <memory>

// Portable game/session services presented as a texture by the XR room.
class Gameplay {
public:
    Gameplay();
    ~Gameplay();
    void start(const app::GameEntry&, const std::filesystem::path& content, bool resume);
    bool active() const;
    void update(const platform::Controls&, double now, bool focused);
    platform::Image draw(double now);
    void set_world_surface_supported(bool);
    const std::optional<app::WorldSurface>& world_surface() const;
    void set_scene_surface_supported(bool);
    const std::optional<app::SceneSurface>& scene_surface() const;
    std::string take_message();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
