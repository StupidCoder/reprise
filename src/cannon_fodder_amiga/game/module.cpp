#include "cannon_fodder_amiga/game/module.hpp"
#include "amiga/iff.hpp"
#include "amiga/paula_channels.hpp"
#include "cannon_fodder_amiga/data/assets.hpp"
#include "cannon_fodder_amiga/data/preview.hpp"
#include "cannon_fodder_amiga/game/camera.hpp"
#include "cannon_fodder_amiga/game/campaign.hpp"
#include "cannon_fodder_amiga/game/combat.hpp"
#include "cannon_fodder_amiga/game/dispatch.hpp"
#include "cannon_fodder_amiga/game/explosives.hpp"
#include "cannon_fodder_amiga/game/front_end.hpp"
#include "cannon_fodder_amiga/game/map_view.hpp"
#include "cannon_fodder_amiga/game/mission.hpp"
#include "cannon_fodder_amiga/game/object_view.hpp"
#include "cannon_fodder_amiga/game/objects.hpp"
#include "cannon_fodder_amiga/game/panel.hpp"
#include "cannon_fodder_amiga/game/tables.hpp"
#include "cannon_fodder_amiga/game/walking.hpp"
#include "cannon_fodder_amiga/game/scenery.hpp"
#include "cannon_fodder_amiga/game/vehicles.hpp"
#include "cannon_fodder_amiga/sound/board.hpp"
#include "cannon_fodder_amiga/sound/music_director.hpp"
#include "cannon_fodder_amiga/sound/rjp1.hpp"
#include <algorithm>
#include <fstream>
#include <iterator>
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
    Module(const std::filesystem::path& folder, int first_phase)
        : content_(folder), program_(content_.read("fodderc", 1)), tables_(program_), frames_(program_),
          panel_art_(program_, amiga::read_ilbm(content_.read("pstuff.lbm", 1))) {
        campaign_.start(tables_.mission);
        if (first_phase < 0) {
            // A new game: the presentation, then the hill and the briefing.
            front_.emplace(content_, program_, panel_art_);
            front_->start_intro();
            after_front_ = AfterFront::hill;
            return;
        }
        while (campaign_.phase < first_phase && campaign_.next_phase(tables_.mission)) {}
        start_phase(campaign_.phase);
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
               kinds() + ", music points " + std::to_string(director_.points().size()) + ", song " +
               std::to_string(jon_playing_ ? -1 : director_.song()) + ", volumes " +
               (music_ ? std::to_string(music_->channel_volume(0)) + "/" + std::to_string(music_->channel_volume(1)) : "-") +
               ", pointer " + std::to_string(pointer_image_);
    }

    double step_seconds() const override { return 1.0 / 50.0; } // a PAL frame
    double pixel_aspect() const override { return 1.0; }
    std::string stopped() const override { return stopped_; }

    void tick(const app::GameInput& input) override {
        if (front_ && front_->active()) {
            front_frame(input);
            return;
        }
        frame(input);
        if (++frames_since_tick_ >= 3) { // the mission loop waits for 3 vertical blanks ($86292)
            frames_since_tick_ = 0;
            game_tick();
        }
    }

    platform::Image picture() const override;
    bool draws_pointer() const override { return true; }

    void audio(std::span<float> stereo, int rate) override {
        std::fill(stereo.begin(), stereo.end(), 0.0f);
        if (music_) paula_.render(stereo, rate);
    }

private:
    void start_phase(int number);
    void frame(const app::GameInput& input);
    void game_tick();
    void set_up_squad();
    void panel_click(int x, int y);
    void select_squad(int squad);
    void split_squad();
    void front_frame(const app::GameInput& input);
    void between_phases();
    void call_up_preview();
    void finish_phase();
    void end_music(EndText started);
    void start_jon(int song, bool warx = false);
    void draw_pointer(platform::Image& image) const;
    void set_pointer(int image, int offset) {
        pointer_image_ = image;
        pointer_offset_ = offset;
    }

    data::Content content_;
    std::vector<std::uint8_t> program_;
    ProgramTables tables_;
    FrameTables frames_;
    PanelArt panel_art_;
    Phase phase_;
    World world_;
    Campaign campaign_;
    bool called_up_{};
    std::vector<std::string> save_names_;
    std::optional<FrontEnd> front_;
    enum class AfterFront { hill, parallax, briefing, mission, loaded, next_phase, new_game, stop } after_front_{AfterFront::mission};
    CameraMotion motion_;
    Point target_{};
    std::optional<sound::Rjp1> music_;
    sound::MusicDirector director_;
    bool jon_playing_{};   // $A3C5C: the end songs (jon.sng) replaced the landscape's
    int end_song_delay_{}; // $A3C5A: ticks until the lost phase's song
    int end_song_{15};     // $A3C58
    struct EngineSound {
        int volume{}, effect{}, param{}; // $829F6, $829F4, $829F8
        std::uint32_t phase{};            // $829FA
        int last{};                       // $829FE
    } engine_;
    mutable amiga::PaulaChannels paula_;
    sound::SoundBoard board_;
    int frames_since_tick_{};
    bool snapshot_tick_{}; // $80612
    bool left_was_down_{}, right_was_down_{};
    int pointer_image_{0x23}; // $81E16: the pstuff piece the pointer shows
    int pointer_offset_{};    // $81E12/$81E14: its offset (both the same)
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
    count_rescue_markers(world_);                       // $98488
    world_.time_left = 100;                              // $884CC
    world_.time_step = 0x28;
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

    // The landscape's music ($A39A4): song 2 with all channel volumes 0;
    // the director fades the first two channels in from there ($87774).
    music_.emplace(content_.read(base + ".sng", disk), content_.read(base + ".ins", disk), program_);
    paula_ = amiga::PaulaChannels{};
    paula_.set_memory(music_->samples());
    music_->start_song(2);
    for (int c = 0; c < 4; ++c) music_->set_channel_volume_raw(c, 0);
    director_.start_phase(phase_.map);
    engine_ = EngineSound{};
    jon_playing_ = false;
    end_song_delay_ = 0;
    board_ = sound::SoundBoard{};
    frames_since_tick_ = 0;
}

void Module::end_music(EndText started) {
    // $8CC72 with the end texts: a won phase plays jon's song 6 (mission
    // complete, $9480A) or 12 (phase complete, $9483A) at once; a lost one
    // song 15 ten ticks later, or 8 when no recruit is left (game over).
    switch (started) {
    case EndText::mission_complete: start_jon(6); break;
    case EndText::phase_complete: start_jon(12); break;
    case EndText::try_again: end_song_delay_ = 10; end_song_ = 15; break;
    case EndText::game_over: end_song_delay_ = 10; end_song_ = 8; break;
    case EndText::none: break;
    }
    // $863F0, once a tick.
    if (end_song_delay_ > 0 && --end_song_delay_ == 0) start_jon(end_song_);
}

void Module::start_jon(int song, bool warx) {
    // $A3C60 + $A3CEC: jon.sng/jon.ins from disk 1, the song, all channels
    // at volume 64.
    music_.emplace(content_.read(warx ? "warx4.sng" : "jon.sng", 1), content_.read(warx ? "warx1.ins" : "jon.ins", 1),
                   program_);
    paula_ = amiga::PaulaChannels{};
    paula_.set_memory(music_->samples());
    music_->start_song(song);
    for (int c = 0; c < 4; ++c) music_->set_channel_volume_raw(c, 0x40);
    jon_playing_ = true;
}

// The phase is over: the recruits' kills and the fallen go to the
// campaign, then the next phase (after the mission's debrief), the phase
// again, or a new game when nobody is left.
void Module::finish_phase() {
    const bool won = world_.outcome == World::Outcome::won && !world_.failed_objective;
    for (std::size_t k = 0; k < 8; ++k) {
        auto& r = campaign_.roster[k];
        if (!r.on_map) continue;
        r.kills = world_.kills[k];
        if (world_.roster_slots[k] < 0) campaign_.fallen(int(k));
    }
    campaign_.away += world_.enemies_killed;
    ++(won ? won_ : lost_);
    ++phases_played_;
    if (won) {
        campaign_.phase_won();
        const bool mission_over = tables_.mission.last_of_mission(campaign_.phase);
        if (campaign_.phase >= MissionTables::phases - 1) { // $9CAB2
            if (front_) {
                front_->start_ending();
                after_front_ = AfterFront::stop;
            } else {
                stopped_ = "The last phase is won.";
            }
            return;
        }
        if (mission_over && front_) {
            // $A8AF6: the mission's debrief (the promotions it shows are
            // written back after it), then the next mission's hill.
            front_->start_debrief(campaign_);
            after_front_ = AfterFront::next_phase;
            return;
        }
        if (mission_over) campaign_.promote();
        campaign_.next_phase(tables_.mission);
    } else if (campaign_.pool == 0 && campaign_.alive() == 0) {
        // $A8AF6 with nobody left: the fallen, then a new game.
        if (front_) {
            front_->start_debrief(campaign_);
            after_front_ = AfterFront::new_game;
            return;
        }
        campaign_.start(tables_.mission);
    }
    between_phases();
}

// The hill and the briefing before the next phase (the tools go straight on).
void Module::between_phases() {
    if (!front_) {
        start_phase(campaign_.phase);
        return;
    }
    call_up_preview();
    front_->start_hill(campaign_);
    after_front_ = AfterFront::parallax;
}

// $9E19A … as the original runs it before the hill of a new mission and
// again after the map loads: who goes into the phase (the hill and the
// briefing show it; a second run changes nothing).
void Module::call_up_preview() {
    const int map_number = campaign_.phase + 1;
    const auto placements =
        data::read_placements(content_.read("mapm" + std::to_string(map_number) + ".spt", data::map_disk(map_number)));
    int placed = 0;
    for (std::size_t i = 0; i < placements.size() && i < 30; ++i) placed += placements[i].words[4] == 0;
    campaign_.call_up(placed);
    called_up_ = true;
}

void Module::front_frame(const app::GameInput& input) {
    front_->tick(input);
    if (const auto song = front_->take_song(); song.number >= 0) start_jon(song.number, song.warx);
    if (music_ && front_->music_fading()) { // $A3D8A: a step down each frame
        for (int c = 0; c < 4; ++c) music_->set_channel_volume_raw(c, std::max(music_->channel_volume(c) - 1, 0));
    }
    if (music_) music_->frame(paula_);
    if (front_->active()) return;
    switch (after_front_) {
    case AfterFront::hill:
        call_up_preview();
        front_->start_hill(campaign_);
        after_front_ = AfterFront::parallax;
        break;
    case AfterFront::parallax: {
        if (front_->hill_choice() != FrontEnd::HillChoice::go_on) {
            after_front_ = AfterFront::briefing; // (load/save handled there)
            front_frame(input);
            return;
        }
        const int map_number = campaign_.phase + 1;
        const auto map = data::read_map(content_.read("mapm" + std::to_string(map_number) + ".map", data::map_disk(map_number)));
        front_->start_parallax(campaign_, map.base_set.substr(0, 3));
        after_front_ = AfterFront::briefing;
        break;
    }
    case AfterFront::briefing: {
        // $A9E56: the hill's LOAD and SAVE (one saved game in the game's
        // folder); after a load the game goes on as after a won phase
        // ($8794C).
        // $A9E56: the hill's SAVE (a saved game named by its mission and
        // phase, in the game's folder) and LOAD (the list of them); after a
        // load the game goes on as after a won phase ($8794C).
        if (front_->hill_choice() == FrontEnd::HillChoice::save) {
            const auto folder = content_.folder() / "saves";
            std::filesystem::create_directories(folder);
            const std::string name = "MISSION " + std::to_string(campaign_.missions_started) + " PHASE " +
                                     std::to_string(campaign_.phase_in_mission + 1);
            std::ofstream(folder / (name + ".txt")) << campaign_.write();
            front_->start_hill(campaign_);
            after_front_ = AfterFront::parallax;
            return;
        }
        if (front_->hill_choice() == FrontEnd::HillChoice::load) {
            save_names_.clear();
            std::error_code error;
            for (const auto& e : std::filesystem::directory_iterator(content_.folder() / "saves", error)) {
                if (e.path().extension() == ".txt") save_names_.push_back(e.path().stem().string());
            }
            std::sort(save_names_.begin(), save_names_.end());
            front_->start_save_list(save_names_);
            after_front_ = AfterFront::loaded;
            return;
        }
        if (!called_up_) call_up_preview();
        front_->start_briefing(campaign_, tables_.mission);
        after_front_ = AfterFront::mission;
        break;
    }
    case AfterFront::mission:
        start_phase(campaign_.phase);
        break;
    case AfterFront::loaded: {
        const int k = front_->picked_save();
        if (k >= 0 && k < int(save_names_.size())) {
            std::ifstream in(content_.folder() / "saves" / (save_names_[std::size_t(k)] + ".txt"));
            const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            // (The original's saved block resumes through its "phase won"
            // path; here the saved campaign is the hill's own, so it goes on
            // with that phase.)
            campaign_.read(text);
        }
        call_up_preview();
        front_->start_hill(campaign_);
        after_front_ = AfterFront::parallax;
        break;
    }
    case AfterFront::next_phase:
        campaign_.promote();
        campaign_.next_phase(tables_.mission);
        between_phases();
        break;
    case AfterFront::new_game:
        campaign_.start(tables_.mission);
        between_phases();
        break;
    case AfterFront::stop:
        stopped_ = "The last phase is won.";
        break;
    }
}

// The panel's regions ($821E0 list, built by $88566): per squad with
// soldiers, its bar (select it, or split off the picked recruits when it is
// the selected one), its weapon icons, its names (pick a recruit, while a
// squad is free for the split).
void Module::panel_click(int x, int y) {
    int end = 0;
    for (int n = 0; n < 3; ++n) {
        const auto& squad = world_.squads[std::size_t(n)];
        const int count = int(squad.members.size());
        if (count == 0) continue;
        const int top = n == 0 || end == 0 ? 0 : end + 8;
        end = top + 0x22 + 12 * count;
        if (y >= top && y < top + 0xe) { // $88934 … / $889A4
            if (n != world_.selected_squad) select_squad(n);
            else split_squad();
            return;
        }
        const bool picking = std::any_of(world_.marked.begin(), world_.marked.end(), [](bool m) { return m; });
        if (n == world_.selected_squad && y >= top + 0xe && y <= top + 0xe + 0x14) { // $886AE / $8879A
            auto& s = world_.squads[std::size_t(n)];
            if (x <= 0xc) {
                if (picking) world_.grenade_split = (world_.grenade_split + 1) % 3;
                else s.weapon = 1;
            } else if (x >= 0x24 && x <= 0x24 + 0xc) {
                if (picking) world_.rocket_split = (world_.rocket_split + 1) % 3;
                else s.weapon = 3;
            }
            return;
        }
        const bool free_squad = std::any_of(world_.squads.begin(), world_.squads.end(),
                                            [](const Squad& q) { return q.members.empty(); });
        if (n == world_.selected_squad && free_squad && y >= top + 0x22 && y < end) { // $88B6E
            const int k = (y - top - 0x22) / 12;
            if (k < count) {
                const int place = world_.walkers[std::size_t(squad.members[std::size_t(k)])].roster_index;
                if (place >= 0 && place < 8) world_.marked[std::size_t(place)] = !world_.marked[std::size_t(place)];
            }
            return;
        }
    }
}

// $88942: another squad selected: the picks cleared, its leader chosen, the
// view goes to him.
void Module::select_squad(int squad) {
    world_.selected_squad = squad;
    world_.marked.fill(false);
    rebuild_squads(world_);
    choose_leader(world_);
    if (world_.leader_slot >= 0) {
        const auto& l = world_.objects.slots[std::size_t(world_.leader_slot)];
        target_ = {whole(l.x), whole(l.y)};
    }
    world_.grenade_split = world_.rocket_split = 1;
}

// $889FC: the picked recruits (some, not all) become a squad of their own
// (the first free one), stopped where they are; the grenades and rockets
// are shared by the split choices ($885CE); the new squad is selected.
void Module::split_squad() {
    const int old = world_.selected_squad;
    if (old < 0) return;
    auto& members = world_.squads[std::size_t(old)].members;
    int picked = 0;
    for (const int m : members) {
        const int place = world_.walkers[std::size_t(m)].roster_index;
        picked += place >= 0 && place < 8 && world_.marked[std::size_t(place)];
    }
    if (picked == 0 || picked == int(members.size())) return; // $88A92
    int fresh = -1;
    for (int n = 0; n < 3; ++n) {
        if (world_.squads[std::size_t(n)].members.empty()) {
            fresh = n;
            break;
        }
    }
    if (fresh < 0) return;
    for (const int m : members) {
        auto& w = world_.walkers[std::size_t(m)];
        const int place = w.roster_index;
        if (place < 0 || place >= 8 || !world_.marked[std::size_t(place)]) continue;
        world_.objects.slots[std::size_t(m)].squad = fresh;
        w.route_index = 0;
        w.took_waypoint = false;
        w.wait = 0;
        w.waypoint_y += 4;
    }
    world_.marked.fill(false);
    auto& from = world_.squads[std::size_t(old)];
    auto& to = world_.squads[std::size_t(fresh)];
    const auto share = [](int mode, int& from_count, int& to_count) {
        if (mode == 0) {
            to_count = from_count;
            from_count = 0;
        } else if (mode == 1) {
            const int keep = from_count >> 1;
            to_count = from_count - keep;
            from_count = keep;
        }
    };
    share(world_.grenade_split, from.grenades, to.grenades);
    share(world_.rocket_split, from.rockets, to.rockets);
    to.weapon = from.weapon;
    to.route.clear(); // $ABFB2: the new squad stops
    world_.selected_squad = fresh;
    world_.grenade_split = world_.rocket_split = 1;
    rebuild_squads(world_);
    for (const int m : world_.squads[std::size_t(fresh)].members) {
        auto& w = world_.walkers[std::size_t(m)];
        const auto& o = world_.objects.slots[std::size_t(m)];
        w.route_index = 0;
        w.waypoint_x = whole(o.x);
        w.waypoint_y = whole(o.y);
    }
    choose_leader(world_);
}

void Module::set_up_squad() {
    // $9E19A … $9E21C: the roster's recruits take the placed soldiers in
    // order (all in squad 0); soldiers beyond the recruits there are leave.
    int placed = 0;
    for (int i = 0; i < 30; ++i) {
        const auto& o = world_.objects.slots[std::size_t(i)];
        placed += o.used && whole(o.x) >= 0 && o.kind == 0;
    }
    if (!called_up_) campaign_.call_up(placed);
    called_up_ = false;
    auto& squad = world_.squads[0];
    int place = 0;
    for (int i = 0; i < 30; ++i) {
        auto& o = world_.objects.slots[std::size_t(i)];
        if (!o.used || whole(o.x) < 0 || o.kind != 0) continue;
        const int k = place++;
        if (k >= 8 || !campaign_.roster[std::size_t(k)].on_map) {
            o = GameObject{}; // removed ($9E21C: x $8000, kind 4)
            continue;
        }
        const auto& r = campaign_.roster[std::size_t(k)];
        auto& w = world_.walkers[std::size_t(i)];
        o.squad = 0;
        w.roster_index = k;
        w.recruit = r.name;
        w.rank = r.rank;
        w.roster_weapon = r.weapon;
        w.heading = 0x40;
        w.facing = 8;
        w.idle = 0xff;
        world_.roster_slots[std::size_t(k)] = i;
        world_.kills[std::size_t(k)] = r.kills;
        squad.members.push_back(i);
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
    if (world_.pointer_x < 0x20) set_pointer(0x23, 0); // $9EC80: over the panel, the arrow
    const bool left_press = input.mouse_button && !left_was_down_;
    const bool right_press = input.mouse_right && !right_was_down_;
    left_was_down_ = input.mouse_button;
    right_was_down_ = input.mouse_right;
    world_.left_held = input.mouse_button;
    world_.right_held = input.mouse_right;
    if (world_.outcome == World::Outcome::playing && world_.driven_vehicle >= 0) {
        // In a vehicle ($9EDDC, $9F4F4): the left button drives (or a click on
        // the vehicle gets out), the right button fires.
        if (over_map) {
            drive(world_, *phase_.terrain, left_press, world_.camera.x + world_.pointer_x,
                  world_.camera.y + world_.pointer_y, tables_.hits);
            if (right_press) fire_vehicle(world_);
        }
    } else if (world_.outcome == World::Outcome::playing) {
        // Both buttons: the leader's grenade or rocket ($8CBA0 → $8147E).
        if (input.mouse_button && input.mouse_right && (left_press || right_press)) world_.both_buttons = true;
        if (!input.mouse_right) world_.both_buttons = false;
        if (input.mouse_right && world_.leader_slot >= 0) {
            if (over_map) set_pointer(0x24, -8); // $9EAD2: the cross-hair, centred
            hold_fire(world_, tables_.fire, right_press); // $9F454, every frame while held
        } else if (left_press && !over_map && input.pointer && !input.mouse_right) {
            panel_click(input.pointer_x, input.pointer_y);
        } else if (left_press && over_map && !input.mouse_right) {
            // $9EB36: the walk order at the pointer.
            const int x = std::max(world_.camera.x + world_.pointer_x - 22, 0);
            const int y = std::max(world_.camera.y + world_.pointer_y - 3, 3);
            target_ = {x, y};
            choose_vehicle(world_, world_.camera.x + world_.pointer_x - 0xf, world_.camera.y + world_.pointer_y - 3,
                           tables_.hits); // $9EEB0
            click_recruit(world_, world_.camera.x + world_.pointer_x - 0xf, world_.camera.y + world_.pointer_y - 3);
            order_walk(world_, x, y);
            set_pointer(0x23, 0); // $9EB22: back to the arrow
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
    if (music_) {
        music_->frame(paula_);
        // $878A2 every fourth frame while the mission runs.
        if (!jon_playing_ && world_.outcome == World::Outcome::playing) {
            if ((world_.frame_counter & 3) == 0) director_.fade(*music_, engine_.volume != 0);
            // $A3B14: an engine restarts its sound on channel 1 at a rate set
            // by its speed (or a helicopter's height).
            if (engine_.volume != 0) {
                engine_.phase += std::uint32_t(std::uint16_t(engine_.param)) << 16 >> 7;
                const int now = int(engine_.phase >> 16) & 0xffff;
                if (now != engine_.last) {
                    music_->set_channel_volume_raw(1, engine_.volume);
                    music_->start_effect(1, engine_.effect);
                    engine_.last = now;
                }
            }
        }
    }
    ++world_.frame_counter;
}

void Module::game_tick() {
    auto& w = world_;
    ++w.tick_counter; // $8061A
    w.engine = World::Engine{}; // $A3AD0
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
    update_boarding(w); // $9A8FC, $9A730
    update_last_mission(w); // $984B6, $9837A
    if (w.phase == 0x47 && ++w.time_step >= 0x28) { // $884DE: the panel's count
        w.time_step = 0;
        if (--w.time_left < 0) w.time_left = 0;
    }
    if (music_ && !jon_playing_ && w.outcome == World::Outcome::playing && w.leader_slot >= 0) {
        // $87774: the song for the leader's tile.
        const auto& l = w.objects.slots[std::size_t(w.leader_slot)];
        director_.choose(whole(l.x) >> 4, whole(l.y) >> 4, tables_.geometry, *music_, engine_.volume != 0);
    }
    // $A3AE4: the loudest engine of the tick plays.
    if (w.engine.volume != 0) {
        engine_.volume = w.engine.volume;
        engine_.effect = w.engine.effect;
        engine_.param = w.engine.param;
    } else {
        engine_.volume = 0;
    }
    // $8CC5C: the end texts and count.
    EndText started = EndText::none;
    const bool ended = run_mission_end(w, tables_.mission.last_of_mission(phase_.number), true, &started);
    end_music(started);
    if (music_) board_.play_all(w, tables_.geometry, *music_);
    w.sounds.clear();
    board_.tick();
    // The end: the next phase, or the phase again.
    if (ended) finish_phase();
}

platform::Image Module::picture() const {
    if (front_ && front_->active()) return front_->picture();
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
        state.weapon[std::size_t(n)] = s.weapon;
        state.marking = std::any_of(world_.marked.begin(), world_.marked.end(), [](bool m) { return m; });
    }
    for (const int slot : world_.roster_slots) {
        if (slot < 0) continue;
        const auto& w = world_.walkers[std::size_t(slot)];
        const bool marked = w.roster_index >= 0 && w.roster_index < 8 && world_.marked[std::size_t(w.roster_index)];
        state.roster.push_back({w.recruit, w.rank, std::max(world_.objects.slots[std::size_t(slot)].squad, 0), marked ? 1 : 0, true});
    }
    const auto panel = compose_panel(panel_art_, state);
    const auto& pstuff = panel_art_.pstuff();
    for (int y = 0; y < panel_height; ++y) {
        for (int x = 0; x < panel_width; ++x) {
            const auto c = panel[std::size_t(y * panel_width + x)];
            if (c != 0) image.put(x - 1, y, data::to_color(pstuff.palette[c])); // sprites start one pixel left
        }
    }
    draw_pointer(image);
    return image;
}

void Module::draw_pointer(platform::Image& image) const {
    // $895BC: the piece's first 16 columns on the attached sprites 0/1 in
    // pstuff's colours (0 transparent), at column pointer x + offset + 15,
    // line pointer y + offset - 4: with offset 0 its top-left corner sits
    // at the mouse position.
    const auto piece = panel_art_.piece(pointer_image_);
    const auto& pstuff = panel_art_.pstuff();
    const int left = world_.pointer_x + 16 + pointer_offset_, top = world_.pointer_y - 4 + pointer_offset_;
    for (int y = 0; y < piece.lines; ++y) {
        for (int x = 0; x < std::min(piece.width, 16); ++x) {
            const int px = piece.x + x, py = piece.y + y;
            if (px >= pstuff.width || py >= pstuff.height) continue;
            const auto c = pstuff.pixels[std::size_t(py * pstuff.width + px)];
            if (c != 0) image.put(left + x, top + y, data::to_color(pstuff.palette[c]));
        }
    }
}

} // namespace

std::unique_ptr<app::GameModule> start_game(const std::filesystem::path& content, int first_phase) {
    return std::make_unique<Module>(content, first_phase);
}

std::string describe(const app::GameModule& game) {
    const auto* m = dynamic_cast<const Module*>(&game);
    return m ? m->describe() : std::string();
}

} // namespace fodder::game
