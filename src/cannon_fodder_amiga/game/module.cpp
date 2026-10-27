#include "cannon_fodder_amiga/game/module.hpp"
#include "amiga/iff.hpp"
#include "amiga/paula_channels.hpp"
#include "cannon_fodder_amiga/data/assets.hpp"
#include "cannon_fodder_amiga/data/preview.hpp"
#include "cannon_fodder_amiga/game/camera.hpp"
#include "cannon_fodder_amiga/game/combat.hpp"
#include "cannon_fodder_amiga/game/dispatch.hpp"
#include "cannon_fodder_amiga/game/explosives.hpp"
#include "cannon_fodder_amiga/game/map_view.hpp"
#include "cannon_fodder_amiga/game/mission.hpp"
#include "cannon_fodder_amiga/game/object_view.hpp"
#include "cannon_fodder_amiga/game/objects.hpp"
#include "cannon_fodder_amiga/game/panel.hpp"
#include "cannon_fodder_amiga/game/tables.hpp"
#include "cannon_fodder_amiga/game/walking.hpp"
#include "cannon_fodder_amiga/sound/board.hpp"
#include "cannon_fodder_amiga/sound/rjp1.hpp"
#include <algorithm>
#include <map>
#include <optional>
#include <string>

namespace fodder::game {
namespace {

int whole(std::int32_t v) { return std::int16_t(std::uint32_t(v) >> 16); }

// $8278C by the landscape's tile set (hypothesis: the order of the program's
// per-landscape lists).
int landscape_of(const std::string& base_set) {
    const auto land = base_set.substr(0, 3);
    if (land == "des") return 1;
    if (land == "ice") return 2;
    if (land == "mor") return 3;
    if (land == "int") return 4;
    return 0;
}

// One phase on its map.
struct Phase {
    int number{};
    data::MissionMap map;
    std::vector<data::Tile> base_tiles, sub_tiles;
    std::optional<MapPicture> picture;
    std::optional<Terrain> terrain;
    data::Palette palette;
    ObjectSheets sheets;
};

class Module : public app::GameModule {
public:
    explicit Module(const std::filesystem::path& folder)
        : content_(folder), program_(content_.read("fodderc", 1)), tables_(program_), frames_(program_),
          panel_art_(program_, amiga::read_ilbm(content_.read("pstuff.lbm", 1))) {
        start_phase(0);
    }

    std::string kinds() const {
        std::string out;
        for (const auto& [k, n] : unhandled_kinds_) out += " " + k + ":" + std::to_string(n);
        return out;
    }

    std::string describe() const {
        int recruits = 0;
        for (const int s : world_.roster_slots) recruits += s >= 0;
        return "phase " + std::to_string(phase_.number) + ", phases played " + std::to_string(phases_played_) + ", won " +
               std::to_string(won_) + ", lost " + std::to_string(lost_) + ", recruits on the map " + std::to_string(recruits) +
               ", enemies " + std::to_string(world_.objects.enemies) + ", killed " + std::to_string(world_.enemies_killed) +
               ", casualties " + std::to_string(world_.casualties) + ", unhandled updates " + std::to_string(unhandled_) +
               kinds();
    }

    double step_seconds() const override { return 1.0 / 50.0; } // a PAL frame
    double pixel_aspect() const override { return 1.0; }
    std::string stopped() const override { return stopped_; }

    void tick(const app::GameInput& input) override {
        frame(input);
        if (++frames_since_tick_ >= 3) { // the mission loop waits for 3 vertical blanks ($86292)
            frames_since_tick_ = 0;
            game_tick();
        }
    }

    platform::Image picture() const override;

    void audio(std::span<float> stereo, int rate) override {
        std::fill(stereo.begin(), stereo.end(), 0.0f);
        if (music_) paula_.render(stereo, rate);
    }

private:
    void start_phase(int number);
    void frame(const app::GameInput& input);
    void game_tick();
    void set_up_squad();

    data::Content content_;
    std::vector<std::uint8_t> program_;
    ProgramTables tables_;
    FrameTables frames_;
    PanelArt panel_art_;
    Phase phase_;
    World world_;
    CameraMotion motion_;
    Point target_{};
    std::optional<sound::Rjp1> music_;
    mutable amiga::PaulaChannels paula_;
    sound::SoundBoard board_;
    int frames_since_tick_{};
    bool snapshot_tick_{}; // $80612
    bool left_was_down_{}, right_was_down_{};
    int outcome_ticks_{}; // ticks since the mission was won or lost
    int phases_played_{}, won_{}, lost_{}, unhandled_{};
    std::map<std::string, int> unhandled_kinds_; // kind/state: count (tools)
    std::string stopped_;
};

void Module::start_phase(int number) {
    const int map_number = number + 1;
    const int disk = data::map_disk(map_number);
    Phase p;
    p.number = number;
    p.map = data::read_map(content_.read("mapm" + std::to_string(map_number) + ".map", disk));
    p.base_tiles = data::read_tile_set(content_.read(p.map.base_set, disk));
    p.sub_tiles = data::read_tile_set(content_.read(p.map.sub_set, disk));
    p.picture.emplace(p.map, p.base_tiles, p.sub_tiles);
    const auto base = p.map.base_set.substr(0, p.map.base_set.size() - 4);
    const auto sub = p.map.sub_set.substr(0, p.map.sub_set.size() - 4);
    p.terrain.emplace(p.map, content_.read(base + ".hit", disk), content_.read(sub + ".hit", disk),
                      content_.read(base + ".bht", disk), content_.read(sub + ".bht", disk), program_);
    p.terrain->set_swaps(content_.read(base + ".swp", disk), content_.read(sub + ".swp", disk));
    const auto land = p.map.base_set.substr(0, 3);
    p.palette = data::read_palette(content_.read(land + "base.pal", disk));
    p.sheets = {amiga::read_ilbm(content_.read(land + "army.lbm", disk)),
                amiga::read_ilbm(content_.read(land + "copt.lbm", disk))};

    world_ = World{};
    world_.objects = spawn_objects(data::read_placements(content_.read("mapm" + std::to_string(map_number) + ".spt", disk)));
    for (int i = 0; i < object_slots; ++i) {
        const auto& o = world_.objects.slots[std::size_t(i)];
        auto& w = world_.walkers[std::size_t(i)];
        w.waypoint_x = o.home_x; // (+$26/+$28 hold the placement)
        w.waypoint_y = o.home_y;
        w.fire_delay = o.start_delay;
    }
    world_.landscape = landscape_of(p.map.base_set);
    start_mission(world_, tables_.mission, number);
    phase_ = std::move(p);
    set_up_squad();

    // The camera starts on the leader.
    const auto& leader = world_.objects.slots[std::size_t(std::max(world_.leader_slot, 0))];
    const auto start = clamp_camera({whole(leader.x) - 144, whole(leader.y) - 112}, phase_.picture->width(),
                                    phase_.picture->height());
    motion_ = CameraMotion{};
    motion_.x = start.x << 16;
    motion_.y = start.y << 16;
    world_.camera = motion_.camera();
    world_.snapshot = world_.camera;
    target_ = {start.x + 144, start.y + 112};

    // The landscape's song ($A39A4 starts song 2; the recorded missions
    // played it with all channel volumes at 64).
    music_.emplace(content_.read(base + ".sng", disk), content_.read(base + ".ins", disk), program_);
    paula_ = amiga::PaulaChannels{};
    paula_.set_memory(music_->samples());
    music_->start_song(2);
    board_ = sound::SoundBoard{};
    outcome_ticks_ = 0;
    frames_since_tick_ = 0;
}

void Module::set_up_squad() {
    // A new game's recruits 0, 1, ... take the placed soldiers in order,
    // all in squad 0, rank 0, weapon 3 (as at the start of mission 1).
    auto& squad = world_.squads[0];
    int recruit = 0;
    for (int i = 0; i < object_slots && recruit < 8; ++i) {
        auto& o = world_.objects.slots[std::size_t(i)];
        if (!o.used || o.kind != 0) continue;
        auto& w = world_.walkers[std::size_t(i)];
        o.squad = 0;
        w.roster_index = recruit;
        w.recruit = recruit;
        w.rank = 0;
        w.roster_weapon = 3;
        w.facing = 8;
        w.idle = 0xff;
        world_.roster_slots[std::size_t(recruit)] = i;
        squad.members.push_back(i);
        ++recruit;
    }
    world_.selected_squad = 0;
    world_.leader_slot = squad.members.empty() ? -1 : squad.members.front();
    // $9E0B4: two grenades a soldier from phase 5 on, a rocket a soldier from
    // phase 10 on; the leader's both-button weapon starts as the grenade
    // (seen at a mission start; the panel's weapon choice is not rebuilt).
    const int soldiers = int(squad.members.size());
    squad.grenades = world_.phase >= 5 ? 2 * soldiers : 0;
    squad.rockets = world_.phase >= 10 ? soldiers : 0;
    squad.weapon = 1;
    // The rank marker over the leader ($954BE), in slot 42.
    auto& marker = world_.objects.slots[42];
    if (!marker.used && world_.leader_slot >= 0) {
        marker = GameObject{};
        marker.used = true;
        marker.kind = 0x15;
        marker.graphic = 0x7c;
        marker.side = -1;
        marker.x = world_.objects.slots[std::size_t(world_.leader_slot)].x;
        marker.y = world_.objects.slots[std::size_t(world_.leader_slot)].y;
    }
    rebuild_squads(world_);
}

void Module::frame(const app::GameInput& input) {
    // The pointer in the game's units ($8CABA -16…301, $8CABC 4…227) from
    // the picture's pixels (hypothesis: column − 16, line + 4).
    if (input.pointer) {
        world_.pointer_x = std::clamp(input.pointer_x - 16, -16, 0x12d);
        world_.pointer_y = std::clamp(input.pointer_y + 4, 4, 0xe3);
    }
    const bool over_map = input.pointer && input.pointer_x >= panel_width;
    const bool left_press = input.mouse_button && !left_was_down_;
    const bool right_press = input.mouse_right && !right_was_down_;
    left_was_down_ = input.mouse_button;
    right_was_down_ = input.mouse_right;
    if (world_.outcome == World::Outcome::playing) {
        // Both buttons: the leader's grenade or rocket ($8CBA0 → $8147E).
        if (input.mouse_button && input.mouse_right && (left_press || right_press)) world_.both_buttons = true;
        if (!input.mouse_right) world_.both_buttons = false;
        if (input.mouse_right && world_.leader_slot >= 0) {
            hold_fire(world_, tables_.fire, right_press); // $9F454, every frame while held
        } else if (left_press && over_map && !input.mouse_right) {
            // $9EB36: the walk order at the pointer.
            const int x = std::max(world_.camera.x + world_.pointer_x - 22, 0);
            const int y = std::max(world_.camera.y + world_.pointer_y - 3, 3);
            target_ = {x, y};
            order_walk(world_, x, y);
        }
    }
    // The camera: the look-ahead from the leader towards the pointer
    // ($8727C), the hard follow and the chase (camera.hpp).
    if (world_.leader_slot >= 0) {
        const auto& l = world_.objects.slots[std::size_t(world_.leader_slot)];
        const Point leader{whole(l.x), whole(l.y)};
        if (over_map) {
            if (const auto t = look_ahead_target(tables_.geometry, leader,
                                                 {world_.camera.x + world_.pointer_x, world_.camera.y + world_.pointer_y},
                                                 false)) {
                target_ = *t;
            }
        }
    }
    CameraFrame cf;
    cf.map_width = phase_.picture->width();
    cf.map_height = phase_.picture->height();
    cf.frozen = map_fits_screen(phase_.map);
    cf.tick_overdue = frames_since_tick_ == 2;
    cf.target = target_;
    step_camera(motion_, tables_.geometry, cf);
    world_.camera = motion_.camera();
    if (music_) music_->frame(paula_);
    ++world_.frame_counter;
}

void Module::game_tick() {
    auto& w = world_;
    update_fire_control(w, tables_.fire);
    check_objectives(w);
    tick_route_windows(w);
    rebuild_squads(w);
    // $A38D6: the walk-frame clock, the objects, the draw list, the
    // snapshot every second tick, the map damage.
    w.clock.tick();
    for (int i = 0; i < object_slots; ++i) {
        const auto& o = w.objects.slots[std::size_t(i)];
        if (!o.used || o.removed || whole(o.x) < 0) continue;
        const int kind = o.kind, state = w.walkers[std::size_t(i)].state, squeeze = w.walkers[std::size_t(i)].squeeze,
                  entering = w.walkers[std::size_t(i)].entering;
        if (!update_object(w, *phase_.terrain, tables_, i)) {
            ++unhandled_;
            ++unhandled_kinds_[std::to_string(kind) + "/" + std::to_string(state) + (squeeze ? "/squeeze" : "") +
                               (entering == 1 ? "/entering" : "")];
        }
    }
    const auto order = draw_list(w.objects, w.snapshot, motion_.drawer_direction_x, motion_.drawer_direction_y);
    w.drawn_objects = int(order.size());
    snapshot_tick_ = !snapshot_tick_;
    if (snapshot_tick_) {
        tile_drawer_tick(motion_);
        w.snapshot = w.camera;
    }
    const int changed = apply_map_damage(w, *phase_.terrain, tables_.blasts);
    if (changed >= 0) {
        const int width = phase_.terrain->width_tiles();
        phase_.picture->set_tile(changed % width, changed / width, phase_.terrain->cell(changed), phase_.base_tiles,
                                 phase_.sub_tiles);
    }
    if (music_) board_.play_all(w, tables_.geometry, *music_);
    w.sounds.clear();
    board_.tick();
    // The end: after a while the next phase, or the phase again.
    if (w.outcome != World::Outcome::playing && ++outcome_ticks_ > 100) {
        const int next = w.outcome == World::Outcome::won ? phase_.number + 1 : phase_.number;
        ++(w.outcome == World::Outcome::won ? won_ : lost_);
        ++phases_played_;
        if (next >= MissionTables::phases) {
            stopped_ = "The last phase is won.";
            return;
        }
        start_phase(next);
    }
}

platform::Image Module::picture() const {
    auto view = view_indices(*phase_.picture, world_.camera);
    std::vector<int> order;
    for (int i = 0; i < object_slots; ++i) {
        const auto& o = world_.objects.slots[std::size_t(i)];
        if (o.used && !o.removed && o.on_screen) order.push_back(i);
    }
    // The draw list's layers: below, sorted by y, above.
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        const auto& oa = world_.objects.slots[std::size_t(a)];
        const auto& ob = world_.objects.slots[std::size_t(b)];
        const int la = oa.layer < 0 ? 0 : oa.layer == 0 ? 1 : 2, lb = ob.layer < 0 ? 0 : ob.layer == 0 ? 1 : 2;
        if (la != lb) return la < lb;
        return la == 1 && whole(oa.y) < whole(ob.y);
    });
    draw_objects(view, world_.camera, world_.objects, order, frames_, phase_.sheets);
    platform::Image image(view_width, view_height);
    for (std::size_t i = 0; i < view.size(); ++i) image.pixels[i] = data::to_color(phase_.palette[view[i] & 15]);
    // The panel (sprites, colours 16-31 from pstuff).
    PanelState state;
    state.selected_squad = std::max(world_.selected_squad, 0);
    for (int n = 0; n < 3; ++n) {
        const auto& s = world_.squads[std::size_t(n)];
        state.soldiers[std::size_t(n)] = int(s.members.size());
        state.icon[std::size_t(n)] = s.members.size() > 1 ? 1 : 0;
        state.grenades[std::size_t(n)] = s.grenades;
        state.rockets[std::size_t(n)] = s.rockets;
    }
    for (const int slot : world_.roster_slots) {
        if (slot < 0) continue;
        const auto& w = world_.walkers[std::size_t(slot)];
        state.roster.push_back({w.recruit, w.rank, std::max(world_.objects.slots[std::size_t(slot)].squad, 0), 0, true});
    }
    const auto panel = compose_panel(panel_art_, state);
    const auto& pstuff = panel_art_.pstuff();
    for (int y = 0; y < panel_height; ++y) {
        for (int x = 0; x < panel_width; ++x) {
            const auto c = panel[std::size_t(y * panel_width + x)];
            if (c != 0) image.put(x - 1, y, data::to_color(pstuff.palette[c])); // sprites start one pixel left
        }
    }
    return image;
}

} // namespace

std::unique_ptr<app::GameModule> start_game(const std::filesystem::path& content) {
    return std::make_unique<Module>(content);
}

std::string describe(const app::GameModule& game) {
    const auto* m = dynamic_cast<const Module*>(&game);
    return m ? m->describe() : std::string();
}

} // namespace fodder::game
