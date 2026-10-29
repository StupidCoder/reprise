#pragma once
#include "gamegear/tile_screen.hpp"
#include "sonic1_gg/audio/sound.hpp"
#include "sonic1_gg/data/flow_assets.hpp"

namespace sonic1_gg {
struct StateAccess;

// The frontend screens (logo, title, map) and the transitions between all
// screens: fades and black holds. The Session asks it to show a map, to fade
// out after an act or a death, and reads `scene` to know what runs next.
//
// Every screen is a gg::TileScreen built from extracted name tables, patterns
// and palettes; sprite layouts and animation scripts are read from ROM data.
class FrontEnd {
    friend struct StateAccess;

public:
    // Logo: SEGA logo animation. Voice: the sampled "SEGA" plays (no updates).
    // Fade: palette fade toward a target, then `after_fade_`. Title, Map: as
    // named. Black: a 60-update black hold before gameplay. Gameplay, Results,
    // GameOver: owned by the Session.
    enum class Scene { Logo, Voice, Fade, Title, Map, Black, Gameplay, Results, GameOver };

    explicit FrontEnd(const FlowAssets& assets) : assets_(assets) { reset(); }

    void reset();
    void tick(bool start, bool action = false, bool directions = false);
    void show_title();
    void show_map(unsigned act, unsigned lives, bool demo);
    void finish_demo();
    void finish_act(Bytes palette);
    void finish_life(Bytes palette, bool game_over);
    void enter_special(unsigned index, unsigned count);
    Scene scene{Scene::Logo};
    gg::TileScreen screen;
    unsigned act{}, lives{3}, demo_cycle{}, ticks{}, scene_ticks{};
    bool demo{}, voice_requested{};
    std::vector<SoundEvent> sound_events;
    // Animation state of the title and map screens (script pointer, remaining
    // updates of the current pose, sprite layout, PRESS START blink, map route).
    unsigned animation_pointer{}, animation_remaining{}, mapping{}, blink{}, route_x{}, route_y{};
    int logo_x{44};
    unsigned logo_step{}, step_ticks{};

private:
    const FlowAssets& assets_;
    Scene after_fade_{Scene::Title};
    Bytes fade_target_;
    unsigned voice_ticks_{}, map_ticks_{}, map_intro_ticks_{};
    void load_screen(unsigned id, unsigned background, unsigned sprites);
    void logo_tick(bool start);
    void map_tick(bool start, bool action, bool directions);
    void patch(unsigned address, unsigned attributes = 0);
    void sprite_mapping(unsigned address, int x, int y);
    void map_lives();
    void fade(Scene next, Bytes target);
    void logo_trademark();
};
} // namespace sonic1_gg
