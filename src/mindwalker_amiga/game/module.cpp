#include "mindwalker_amiga/game/module.hpp"
#include "mindwalker_amiga/data/game_files.hpp"
#include "mindwalker_amiga/screen/canvas.hpp"
#include "mindwalker_amiga/title/title_stage.hpp"
#include "mindwalker_amiga/world/world.hpp"
#include "mindwalker_amiga/landscape/landscape_stage.hpp"
#include "mindwalker_amiga/tunnel/tunnel_stage.hpp"
#include "mindwalker_amiga/arena/arena_stage.hpp"
#include "mindwalker_amiga/psyche/psyche_stage.hpp"
#include "mindwalker_amiga/data/assets.hpp"
#include <optional>
#include "platform/files.hpp"
#include "ui/vhs.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <map>
#include <memory>
#include <sstream>

namespace mindwalker::game {
namespace {

// Intuition's mouse pointer, from Workbench's preferences on the game disk
// (struct Preferences: PointerMatrix at +28, two control words then 16 rows of
// two plane words; XOffset/YOffset at +100/+101; colours 17-19 at +102).
struct MousePointer {
    std::array<std::array<std::uint8_t, 16>, 16> pixels{};
    int x_offset{}, y_offset{};
    std::array<screen::Rgb4, 3> colours{};

    explicit MousePointer(const std::vector<std::uint8_t>& prefs) {
        const auto word = [&](std::size_t at) { return std::uint16_t(prefs.at(at) << 8 | prefs.at(at + 1)); };
        for (int y = 0; y < 16; ++y) {
            const auto a = word(28 + 4 + std::size_t(y) * 4), b = word(28 + 6 + std::size_t(y) * 4);
            for (int x = 0; x < 16; ++x) {
                pixels[std::size_t(y)][std::size_t(x)] = std::uint8_t((a >> (15 - x) & 1) | (b >> (15 - x) & 1) << 1);
            }
        }
        x_offset = std::int8_t(prefs.at(100));
        y_offset = std::int8_t(prefs.at(101));
        for (std::size_t c = 0; c < 3; ++c) colours[c] = word(102 + c * 2) & 0xFFF;
    }

    void draw(platform::Image& image, int x0, int y0) const {
        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 16; ++x) {
                const auto v = pixels[std::size_t(y)][std::size_t(x)];
                if (v) image.put(x0 + x_offset + x, y0 + y_offset + y, screen::to_color(colours[v - 1u]));
            }
        }
    }
};

// What T mixes into its seed from CurrentTime ($600142): seconds since 1
// January 1978 shifted left by 3, XOR the microseconds.
std::uint32_t clock_value() {
    using namespace std::chrono;
    constexpr seconds amiga_epoch{252460800}; // 1978-01-01 in Unix time
    const auto now = duration_cast<microseconds>(system_clock::now().time_since_epoch()) - amiga_epoch;
    const auto secs = std::uint32_t(duration_cast<seconds>(now).count());
    const auto micros = std::uint32_t(now.count() % 1000000);
    return secs << 3 ^ micros;
}

class MindWalker final : public app::GameModule {
    enum class Phase { title, loading, landscape, tunnel, arena, psyche, stopped };

public:
    explicit MindWalker(const std::filesystem::path& content)
        : content_(content), files_(content), picture_(platform::read_file(content / "MindB")),
          pointer_(platform::read_file(content / "system-configuration")), assets_(data::load_assets(content)) {
        title_.emplace(files_, picture_, clock_value(), true, session(0));
    }

    // The stages wait for each vertical blank (PAL, 50 Hz, like the reference
    // machine; hypothesis until the release's video standard is settled).
    double step_seconds() const override { return 1.0 / 50.0; }
    double pixel_aspect() const override { return 1.0; }

    void tick(const app::GameInput& input) override {
        ++frames_;
        if (input.pointer) {
            // The launcher gives the pointer in pixels of the picture we
            // returned last, which the VHS look doubles: back to the
            // screen's 320 x 200.
            mouse_x_ = input.pointer_x * screen::Canvas::width / shown_width_;
            mouse_y_ = input.pointer_y * screen::Canvas::height / shown_height_;
        }
        const bool press = input.mouse_button && !button_was_down_;
        button_was_down_ = input.mouse_button;
        switch (phase_) {
        case Phase::title:
            title_->frame({mouse_x_, mouse_y_, input.mouse_button});
            if (title_->phase() == title::TitleStage::Phase::loading) {
                if (title_->restarted()) { // the command screen set the game up
                    load_next(restart_phase_);
                    break;
                }
                const auto& game = *title_->new_game();
                state_ = {game.seed, game.stage, game.lives, game.score, std::nullopt, 0, game.progress, game.bonus};
                load_next(Phase::landscape);
            }
            break;
        case Phase::loading:
            // The loading picture on S's screen, held a fixed 2 seconds
            // (owner's decision) while the loader brings the next program
            // (after the psyche screen its portrait first, 50 frames more).
            if (++loading_frames_ >= 100) open(next_);
            break;
        case Phase::landscape: {
            count_time(landscape_->paused());
            int gadget = press ? landscape_->gadget_at(mouse_x_, mouse_y_) : -1;
            if (gadget == -2) { // Intuition's close gadget: CLOSEWINDOW: back to T (status 1)
                state_.score = landscape_->score();
                state_.lives = landscape_->lives();
                state_.resume = landscape_->resume_state(); // ($20B056 keeps it on every exit)
                to_title(2);
                return;
            }
            landscape::LandscapeInput in{input.up, input.down, input.left, input.right, input.fire,
                                         input.mouse_button, mouse_x_, mouse_y_};
            landscape_->frame([&](landscape::InputRead) { return in; }, gadget);
            if (!landscape_->over()) break;
            state_.score = landscape_->score();
            state_.lives = landscape_->lives();
            if (landscape_->out_of_lives()) {
                state_.resume = landscape_->resume_state();
                to_title(2); // the game over: T, its Hall of Fame
            } else {
                // The way on ($20B056 keeps the landscape, mode 9): the tunnel.
                state_.resume = landscape_->resume_state();
                state_.tunnel_seed = landscape_->next_seed();
                load_next(Phase::tunnel);
            }
            break;
        }
        case Phase::tunnel: {
            count_time(tunnel_->paused());
            int gadget = press ? tunnel_->gadget_at(mouse_x_, mouse_y_) : -1;
            tunnel::TunnelInput in{input.up, input.down, input.left, input.right, input.fire, input.mouse_button};
            tunnel_->frame([&](tunnel::TunnelRead) { return in; }, gadget);
            if (!tunnel_->over()) break;
            state_.score = tunnel_->score();
            switch (tunnel_->outcome()) {
            case tunnel::TunnelStage::Outcome::ran_out: load_next(Phase::landscape); break; // mode 1: the landscape again
            case tunnel::TunnelStage::Outcome::caught: load_next(Phase::arena); break; // mode 2
            default: to_title(3); break;
            }
            break;
        }
        case Phase::arena: {
            count_time(arena_->paused());
            const int gadget = press ? arena_->gadget_at(mouse_x_, mouse_y_) : -1;
            arena::ArenaInput in{input.up, input.down, input.left, input.right, input.fire, input.mouse_button};
            arena_->frame([&](arena::ArenaRead) { return in; }, gadget);
            if (!arena_->over()) break;
            state_.score = arena_->score();
            state_.lives = arena_->lives();
            switch (arena_->outcome()) {
            case arena::ArenaStage::Outcome::won:
                // C's exit: progress +7 (at most 42), +$60 set (the way on
                // closed again), mode 3: the psyche screen.
                state_.stage = std::min(state_.stage + 7, 42);
                if (state_.resume) state_.resume->ramp_closed = true;
                load_next(Phase::psyche);
                break;
            case arena::ArenaStage::Outcome::left: load_next(Phase::landscape); break; // mode 1
            case arena::ArenaStage::Outcome::died:
                if (state_.lives < 1) to_title(4); // status 1: T
                else load_next(Phase::landscape); // mode 1, a life less
                break;
            default: to_title(4); break;
            }
            break;
        }
        case Phase::psyche: {
            const int gadget = press ? psyche_->gadget_at(mouse_x_, mouse_y_) : -1;
            psyche::PsycheInput in{mouse_x_, mouse_y_, input.mouse_button};
            psyche_->frame([&] { return in; }, gadget);
            if (!psyche_->over()) break;
            state_.score = psyche_->score();
            state_.bonus = psyche_->bonus();
            if (psyche_->outcome() == psyche::PsycheStage::Outcome::done) {
                // h32: its portrait on S's screen over the loading picture
                // (50 frames, then the loading), then the landscape (mode 1).
                show_portrait(psyche_->portrait(), psyche_->portrait_colour());
                load_next(Phase::landscape);
                loading_frames_ = -50;
            } else if (psyche_->whole()) {
                // The game finished and the window closed: MindF6, then T.
                show_portrait(psyche_->portrait(), psyche_->portrait_colour());
                to_title(5);
                loading_frames_ = -50;
            } else {
                to_title(5);
            }
            break;
        }
        case Phase::stopped: break;
        }
    }

    // Status 1: the game left (closed, or the last life lost): the loading
    // picture, then T again with the game in memory (its score for the Hall
    // of Fame, saving it from the command screen).
    void to_title(int where) {
        ended_where_ = where;
        ended_ = state_;
        restore_ = stage_start_;
        load_next(Phase::title);
    }

    // What T is handed: the Hall of Fame (our own file once changed, else
    // the disk's MindHighs) and the saved worlds.
    title::Session session(int where) {
        title::Session s;
        s.score = where ? ended_.score : 0;
        s.where = where;
        const auto own = content_ / "Hall of Fame";
        s.fame = title::HallOfFame::from_file(platform::read_file(std::filesystem::exists(own) ? own : content_ / "MindHighs"));
        s.fame_changed = [this](const title::HallOfFame& fame) { platform::write_file(content_ / "Hall of Fame", fame.to_file()); };
        s.world_saved = [this](int world) { return std::filesystem::exists(world_file(world)); };
        s.save_world = [this](int world) { return save_world(world); };
        s.restart = [this](int world, bool same_world) { return restart(world, same_world); };
        return s;
    }

    std::filesystem::path world_file(int world) const {
        return content_ / ("World " + std::string(1, char('A' + world)) + ".save");
    }

    // $6026AE: the game in memory, with where it was and its stage's restore
    // point (B, C and P copy the record on entry, +$2BC on), as text.
    bool save_world(int world) {
        std::string text = "Mind Walker saved world 1\n";
        const auto put = [&](const char* name, long long value) { text += std::string(name) + " " + std::to_string(value) + "\n"; };
        put("where", ended_where_);
        const auto record = [&](const char* prefix, const GameState& g) {
            const auto key = [&](const char* name) { return std::string(prefix) + name; };
            put(key("seed").c_str(), g.seed);
            put(key("stage").c_str(), g.stage);
            put(key("lives").c_str(), g.lives);
            put(key("score").c_str(), g.score);
            put(key("tunnel_seed").c_str(), g.tunnel_seed);
            put(key("time").c_str(), g.time);
            put(key("bonus").c_str(), g.bonus);
        };
        record("", ended_);
        record("restore_", restore_);
        if (ended_.resume) {
            const auto& r = *ended_.resume;
            const long long values[] = {r.home_x, r.home_y, r.marked_u, r.marked_v, r.direction, r.cell_x, r.cell_y, r.x, r.level,
                                        r.z, r.slow, r.found[0], r.found[1], r.form, r.view_x, r.view_y, r.damage, r.ramp_closed};
            text += "resume";
            for (const auto v : values) text += " " + std::to_string(v);
            text += "\n";
        }
        try {
            platform::write_text(world_file(world), text);
        } catch (const std::exception&) {
            return false;
        }
        return true;
    }

    // $602248: Continue Game: the saved game where it was (the landscape
    // resumed; the tunnel, arena or psyche screen from their restore point;
    // without lives a new game in its world); New Game, Same World: the
    // world from the start.
    bool restart(int world, bool same_world) {
        std::map<std::string, std::vector<long long>> v;
        try {
            const auto bytes = platform::read_file(world_file(world));
            std::istringstream in(std::string(bytes.begin(), bytes.end()));
            std::string line;
            std::getline(in, line);
            if (line != "Mind Walker saved world 1") return false;
            while (std::getline(in, line)) {
                std::istringstream f(line);
                std::string key;
                f >> key;
                for (long long x; f >> x;) v[key].push_back(x);
            }
        } catch (const std::exception&) {
            return false;
        }
        const auto get = [&](const std::string& key) { return v.count(key) && !v[key].empty() ? v[key][0] : 0; };
        const auto record = [&](const std::string& prefix) {
            GameState g;
            g.seed = std::uint32_t(get(prefix + "seed"));
            g.stage = int(get(prefix + "stage"));
            g.lives = int(get(prefix + "lives"));
            g.score = std::uint32_t(get(prefix + "score"));
            g.tunnel_seed = std::uint32_t(get(prefix + "tunnel_seed"));
            g.time = std::uint32_t(get(prefix + "time"));
            g.bonus = std::uint32_t(get(prefix + "bonus"));
            return g;
        };
        const int where = int(get("where"));
        GameState g = where >= 3 ? record("restore_") : record("");
        g.seed = std::uint32_t(get("seed"));
        g.tunnel_seed = std::uint32_t(get("tunnel_seed"));
        if (v.count("resume") && v["resume"].size() == 18) {
            const auto& r = v["resume"];
            landscape::LandscapeStage::Resume s;
            s.home_x = std::uint16_t(r[0]); s.home_y = std::uint16_t(r[1]); s.marked_u = std::uint16_t(r[2]);
            s.marked_v = std::uint16_t(r[3]); s.direction = int(r[4]); s.cell_x = std::uint16_t(r[5]);
            s.cell_y = std::uint16_t(r[6]); s.x = int(r[7]); s.level = int(r[8]); s.z = int(r[9]); s.slow = int(r[10]);
            s.found = {r[11] != 0, r[12] != 0}; s.form = int(r[13]); s.view_x = std::uint16_t(r[14]);
            s.view_y = std::uint16_t(r[15]); s.damage = int(r[16]); s.ramp_closed = r[17] != 0;
            g.resume = s;
        }
        restart_phase_ = where == 2 ? Phase::landscape : where == 3 ? Phase::tunnel : where == 4 ? Phase::arena
                       : where == 5 ? Phase::psyche : Phase::landscape;
        if (where < 2) g.resume.reset(); // mode 0: the landscape from its start
        if (same_world || g.lives <= 0) { // a new game in the saved world
            GameState fresh;
            fresh.seed = g.seed;
            g = fresh;
            restart_phase_ = Phase::landscape;
        }
        state_ = g;
        return true;
    }

    // A, B and C add the seconds they ran (not paused) to +$62 (CurrentTime
    // at start and end, $206718 / $303298 / $4048A2).
    void count_time(bool paused) {
        if (!paused && ++state_.time_frames >= 50) {
            state_.time_frames = 0;
            ++state_.time;
        }
    }

    // P's h32: MindF1-6 (rows 0-187) from row 12 on, colour 1 its own.
    void show_portrait(int index, screen::Rgb4 colour) {
        loading_picture_ = title_->loading_picture(state_.stage);
        const auto& portrait = assets_.portraits[std::size_t(index)];
        for (int y = 0; y < 188 && y < portrait.height; ++y)
            for (int x = 0; x < 320; ++x) loading_picture_.plot(x, y + 12, portrait.at(x, y));
        loading_colour_ = colour;
        keep_picture_ = true;
    }

    void load_next(Phase next) {
        next_ = next;
        loading_frames_ = 0;
        phase_ = Phase::loading;
        landscape_.reset();
        tunnel_.reset();
        arena_.reset();
        psyche_.reset();
        if (!keep_picture_) {
            loading_picture_ = title_->loading_picture(state_.stage);
            loading_colour_ = 0x0F0;
        }
        keep_picture_ = false;
    }

    void open(Phase next) {
        stage_start_ = state_; // (the restore point B, C and P keep, +$2BC on)
        phase_ = next;
        if (next == Phase::landscape) {
            landscape_.emplace(files_, assets_,
                               landscape::LandscapeStage::Start{state_.seed, state_.stage, state_.lives, state_.score,
                                                                state_.resume});
        } else if (next == Phase::tunnel) {
            tunnel_.emplace(files_, assets_,
                            tunnel::TunnelStage::Start{state_.tunnel_seed, state_.stage, state_.lives, state_.score});
        } else if (next == Phase::title) {
            title_.emplace(files_, picture_, clock_value(), false, session(ended_where_));
        } else if (next == Phase::psyche) {
            psyche_.emplace(files_, assets_,
                            psyche::PsycheStage::Start{state_.seed, state_.stage, state_.lives, state_.score, state_.bonus,
                                                       state_.time});
        } else {
            arena_.emplace(files_, assets_,
                           arena::ArenaStage::Start{state_.tunnel_seed, state_.stage, state_.lives, state_.score,
                                                    state_.resume ? state_.resume->form : 0});
        }
        apply_cheats();
    }

    void stop(const char* format, std::uint32_t score) {
        char text[200];
        std::snprintf(text, sizeof text, format, score);
        message_ = text;
        phase_ = Phase::stopped;
    }

    void audio(std::span<float> stereo, int sample_rate) override {
        std::fill(stereo.begin(), stereo.end(), 0.0f);
        if (phase_ == Phase::landscape) landscape_->render_audio(stereo, sample_rate);
        if (phase_ == Phase::tunnel) tunnel_->render_audio(stereo, sample_rate);
        if (phase_ == Phase::arena) arena_->render_audio(stereo, sample_rate);
        if (phase_ == Phase::psyche) psyche_->render_audio(stereo, sample_rate);
    }

    platform::Image picture() const override {
        platform::Image image = phase_ == Phase::landscape ? landscape_->picture()
                                : phase_ == Phase::tunnel  ? tunnel_->picture()
                                : phase_ == Phase::arena   ? arena_->picture()
                                : phase_ == Phase::psyche  ? psyche_->picture()
                                : phase_ == Phase::loading ? loading_picture_.picture({0x000, loading_colour_})
                                                           : title_->picture();
        pointer_.draw(image, mouse_x_, mouse_y_);
        auto shown = vhs_ ? ui::vhs(image, frames_, vhs_) : image;
        shown_width_ = std::max(shown.width, 1);
        shown_height_ = std::max(shown.height, 1);
        return shown;
    }

    // Quick save and load, in the landscape, the tunnel, the arena and the
    // psyche screen: a copy of the running stage and of the game's record
    // (the stages hold no pointers into themselves, so a copy is the whole
    // state, sound and random generators included).
    bool save() override {
        if (!in_game()) return false;
        auto snapshot = std::make_unique<Snapshot>();
        snapshot->phase = phase_;
        snapshot->state = state_;
        snapshot->stage_start = stage_start_;
        snapshot->mouse_x = mouse_x_;
        snapshot->mouse_y = mouse_y_;
        snapshot->button_was_down = button_was_down_;
        if (landscape_) snapshot->landscape.emplace(*landscape_);
        if (tunnel_) snapshot->tunnel.emplace(*tunnel_);
        if (arena_) snapshot->arena.emplace(*arena_);
        if (psyche_) snapshot->psyche.emplace(*psyche_);
        saved_ = std::move(snapshot);
        return true;
    }
    bool load() override {
        if (!saved_ || stopped_by_end()) return false;
        const Snapshot& s = *saved_;
        phase_ = s.phase;
        state_ = s.state;
        stage_start_ = s.stage_start;
        mouse_x_ = s.mouse_x;
        mouse_y_ = s.mouse_y;
        button_was_down_ = s.button_was_down;
        landscape_.reset();
        tunnel_.reset();
        arena_.reset();
        psyche_.reset();
        if (s.landscape) landscape_.emplace(*s.landscape);
        if (s.tunnel) tunnel_.emplace(*s.tunnel);
        if (s.arena) arena_.emplace(*s.arena);
        if (s.psyche) psyche_.emplace(*s.psyche);
        apply_cheats(); // (the options as they are now)
        return true;
    }
    bool has_save() const override { return saved_ != nullptr; }

    std::vector<app::GameOption> options() const override {
        return {{"VHS look", {"Off", "Light", "Medium", "Heavy", "Broken"}, vhs_,
                 "A worn video tape's colour bleed and noise."},
                {"God mode", {"Off", "On"}, cheats_.god ? 1 : 0,
                 "Landscape: no creature tethers the walker. Arena: creatures cost no strength and the walls' "
                 "pulse does not kill.",
                 "Cheats"},
                {"Minimap", {"Off", "On"}, cheats_.minimap ? 1 : 0,
                 "Landscape: a small map in the corner, zoomed out to the nearest unlit beacon (once lit, the "
                 "way on's ramps), flashing. Arena: the whole maze with the exit flashing.",
                 "Cheats"},
                {"Auto transform", {"Off", "On"}, cheats_.auto_transform ? 1 : 0,
                 "Landscape: the walker takes the form of each cell's surface on entering it, so every cell "
                 "joins the trail.",
                 "Cheats"}};
    }
    void set_option(std::size_t index, int value) override {
        if (index == 0) vhs_ = value;
        if (index == 1) cheats_.god = value != 0;
        if (index == 2) cheats_.minimap = value != 0;
        if (index == 3) cheats_.auto_transform = value != 0;
        apply_cheats();
    }

    void apply_cheats() {
        if (landscape_) landscape_->set_cheats(cheats_);
        if (arena_) arena_->set_cheats(cheats_);
    }

    std::string stopped() const override {
        if (title_->phase() == title::TitleStage::Phase::quit) return "Mind Walker: QUIT.";
        return phase_ == Phase::stopped ? message_ : std::string();
    }

private:
    bool in_game() const {
        return phase_ == Phase::landscape || phase_ == Phase::tunnel || phase_ == Phase::arena || phase_ == Phase::psyche;
    }
    bool stopped_by_end() const { return title_->phase() == title::TitleStage::Phase::quit; }

    std::filesystem::path content_;
    data::GameFiles files_;
    std::vector<std::uint8_t> picture_;
    MousePointer pointer_;
    data::Assets assets_;
    std::optional<title::TitleStage> title_;
    int mouse_x_{160}, mouse_y_{100};
    int loading_frames_{};
    bool button_was_down_{};
    Phase phase_{Phase::title}, next_{Phase::landscape};
    // What crosses between the programs (the loader's shared block).
    struct GameState {
        std::uint32_t seed{};          // the world's
        int stage{}, lives{3};
        std::uint32_t score{};
        std::optional<landscape::LandscapeStage::Resume> resume; // +$18-$60
        std::uint32_t tunnel_seed{};   // +$4C
        std::uint32_t time{};          // +$62: seconds played
        std::uint32_t bonus{42000};    // +$308: the self-development bonus
        int time_frames{};
    } state_;
    screen::Canvas loading_picture_;
    screen::Rgb4 loading_colour_{0x0F0};
    bool keep_picture_{};
    std::string message_;
    std::optional<landscape::LandscapeStage> landscape_;
    std::optional<tunnel::TunnelStage> tunnel_;
    std::optional<arena::ArenaStage> arena_;
    std::optional<psyche::PsycheStage> psyche_;
    struct Snapshot {
        Phase phase{};
        GameState state, stage_start;
        int mouse_x{}, mouse_y{};
        bool button_was_down{};
        std::optional<landscape::LandscapeStage> landscape;
        std::optional<tunnel::TunnelStage> tunnel;
        std::optional<arena::ArenaStage> arena;
        std::optional<psyche::PsycheStage> psyche;
    };
    GameState stage_start_;
    GameState ended_, restore_;    // the game T was handed, and its stage's start
    int ended_where_{};            // +$2BA
    Phase restart_phase_{Phase::landscape};
    std::unique_ptr<Snapshot> saved_;
    long frames_{};
    int vhs_{};
    mutable int shown_width_{screen::Canvas::width}, shown_height_{screen::Canvas::height}; // the last picture's size
    Cheats cheats_;
};

} // namespace

std::unique_ptr<app::GameModule> start(const std::filesystem::path& content) {
    return std::make_unique<MindWalker>(content);
}

} // namespace mindwalker::game
