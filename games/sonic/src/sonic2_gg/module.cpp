// Sonic 2 (Game Gear) as a BlueStreak game module: loads the asset pack, runs
// the Game one frame at a time, plays its sound through the PSG, renders the
// VDP picture and provides quick saves.

#include "sonic2_gg/module.hpp"
#include "gamegear/psg.hpp"
#include "platform/host.hpp"
#include "sonic2_gg/audio/voice.hpp"
#include "sonic2_gg/catalog.hpp"
#include "sonic2_gg/enhancements/water.hpp"
#include "sonic2_gg/enhancements/widescreen.hpp"
#include "sonic2_gg/game/game.hpp"
#include "sonic2_gg/render/wide_view.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <type_traits>
#include <vector>
static_assert(platform::Host::audio_sample_rate == int(gg::Psg::sample_rate),
              "The host must play PSG output at its native sample rate");

namespace sonic2_gg {

namespace {

// One Game Gear video frame: 262 scanlines of 228 CPU clocks (59.92 Hz).
constexpr unsigned frame_clocks = 262 * 228;

// Quick saves: magic, version, RAM, VRAM, CRAM, VDP registers, then the
// PSG and the stand-in for the refresh register.
constexpr char save_magic[] = "BLUESTREAK S2GG";
constexpr std::uint32_t save_version = 10;

// The picture's parts, in the order quick saves keep them.
template <class D, class F>
void display_parts(D& display, F&& f) {
    f(display.tiles);
    f(display.map);
    f(display.sprites);
    f(display.colors);
    f(display.shown);
    f(display.level_shown);
    f(display.line_interrupt);
    f(display.scroll_x);
    f(display.scroll_y);
}

struct Writer {
    std::vector<std::uint8_t> out;

    void bytes(const void* p, std::size_t n) {
        const auto* b = static_cast<const std::uint8_t*>(p);
        out.insert(out.end(), b, b + n);
    }

    template <class T>
    void value(const T& v) {
        bytes(&v, sizeof v); // host order: saves stay on this machine
    }

    template <class... T>
    void operator()(const T&... v) {
        (field(v), ...);
    }

    template <class T>
    void field(const T& v) {
        if constexpr (requires {
                          v.size();
                          v.data();
                      }) {
            for (const auto& e : v) {
                field(e);
            }
        } else {
            value(v);
        }
    }
};

// A part of the state: plain data as bytes, vectors with their length.
template <class T>
struct is_vector : std::false_type {};

template <class T>
struct is_vector<std::vector<T>> : std::true_type {};

template <class T>
void write_part(Writer& w, const T& part) {
    if constexpr (is_vector<T>::value) {
        const auto n = std::uint32_t(part.size());
        w.value(n);
        w.bytes(part.data(), n * sizeof(typename T::value_type));
    } else {
        static_assert(std::is_trivially_copyable_v<T>);
        w.bytes(&part, sizeof part);
    }
}

struct Reader {
    std::span<const std::uint8_t> in;
    std::size_t pos{};

    void bytes(void* p, std::size_t n) {
        if (pos + n > in.size()) {
            throw std::runtime_error("The quick save is incomplete");
        }
        std::memcpy(p, in.data() + pos, n);
        pos += n;
    }

    template <class T>
    void value(T& v) {
        bytes(&v, sizeof v);
    }

    template <class... T>
    void operator()(T&... v) {
        (field(v), ...);
    }

    template <class T>
    void field(T& v) {
        if constexpr (requires {
                          v.size();
                          v.data();
                      }) {
            for (auto& e : v) {
                field(e);
            }
        } else {
            value(v);
        }
    }
};

template <class T>
void read_part(Reader& r, T& part) {
    if constexpr (is_vector<T>::value) {
        std::uint32_t n{};
        r.value(n);
        if (n > 4096) {
            throw std::runtime_error("The quick save is damaged");
        }
        part.resize(n);
        r.bytes(part.data(), n * sizeof(typename T::value_type));
    } else {
        r.bytes(&part, sizeof part);
    }
}

class Sonic2Module final : public shell::GameModule {
public:
    Sonic2Module(const std::filesystem::path& pack, std::span<const std::uint8_t> state, int zone,
                 int act)
        : assets_(load_assets(pack)), voice_(assets_) {
        game_ = std::make_unique<Game>(assets_, zone, act);
        game_->direct_start = zone >= 0;
        game_->ending_start = zone == ending_zone;
        start_zone_ = zone;
        start_act_ = act;
        if (!state.empty()) {
            load(state);
        }
    }

    double step_seconds() const override { return frame_clocks / 3579545.0; }

    std::vector<float> tick(shell::GameInput in, const shell::Enhancements& enhancements) override {
        ++ticks_;
        std::uint8_t buttons = 0;
        buttons |= in.up ? button::up : 0;
        buttons |= in.down ? button::down : 0;
        buttons |= in.left ? button::left : 0;
        buttons |= in.right ? button::right : 0;
        buttons |= (in.jump || in.action) ? button::one : 0;
        // Start during play belongs to the shell's pause menu.
        buttons |= (in.start && !gameplay()) ? button::start : 0;
        // Widescreen rules apply to played acts only; attract demos replay
        // original controller bytes and keep the LCD rules.
        const bool wide = wide_play(*game_);
        set_game_view(*game_, wide ? view_[0] : 160, wide ? view_[1] : 144);
        game_->frame(buttons);
        // Enhancements only observe the game; they never feed back into it.
        barrier_.tick(arena_view(*game_, expanded_));
        water_.tick(*game_, enhancements.water, view_[0], view_[1]);
        for (const auto& w : game_->take_sound_writes()) {
            if (w.port == 0x06) {
                psg_.stereo(w.value);
            } else {
                psg_.write(w.value);
            }
        }
        if (game_->voice_requested) {
            voice_.start();
        }
        return voice_.playing() ? voice_.render(psg_, frame_clocks) : psg_.render(frame_clocks);
    }

    platform::Image render(const shell::Enhancements& enhancements, int, int) const override {
        if (!enhancements.widescreen && !enhancements.water) {
            return game_->picture();
        }
        return render_frame(*game_, view_[0], view_[1],
                            enhancements.widescreen ? &barrier_ : nullptr,
                            enhancements.water ? &water_ : nullptr);
    }

    void set_view(bool widescreen, int canvas_width, int canvas_height) override {
        expanded_ = widescreen;
        view_ =
            widescreen ? gg::expanded_view_size(canvas_width, canvas_height) : std::array{160, 144};
    }

    bool gameplay() const override {
        const auto& r = *game_;
        // An act is being played (not starting, ending or paused). Boss
        // fights that stop the map scrolling count too.
        return !r.flow.attract_flags &&
               (r.flow.game_flags & u8(~game_flag::paused)) == game_flag::playing;
    }

    std::string status() const override {
        const auto& r = *game_;
        if (!r.flow.attract_flags && r.flow.zone == ending_zone) {
            return "Ending";
        }
        if (r.flow.attract_flags || r.flow.zone >= zone_names.size()) {
            return "Sonic 2 / Game Gear";
        }
        return std::string(zone_names[r.flow.zone]) + " / Act " + std::to_string(r.flow.act + 1);
    }

    void restart() override {
        const auto& r = *game_;
        int zone = start_zone_, act = start_act_;
        if (!r.flow.attract_flags && r.flow.zone < zone_names.size()) {
            zone = r.flow.zone;
            act = r.flow.act;
        }
        game_ = std::make_unique<Game>(assets_, zone, act);
        game_->direct_start = zone >= 0;
        game_->ending_start = zone == ending_zone;
        barrier_.reset();
        water_.reset();
        psg_ = gg::Psg{};
        voice_ = LogoVoice(assets_);
    }

    std::vector<std::uint8_t> save_state() override {
        if (!game_->at_main_loop()) {
            throw std::runtime_error("Quick saves are possible during play");
        }
        Writer w;
        w.bytes(save_magic, sizeof save_magic);
        w.value(save_version);
        game_->for_each_state([&](const auto& part) { write_part(w, part); });
        display_parts(game_->display, [&](const auto& part) { write_part(w, part); });
        barrier_.for_each_state([&](const auto& part) { write_part(w, part); });
        water_.for_each_state([&](const auto& part) { write_part(w, part); });
        psg_.serialize(w);
        w.value(game_->random_state());
        w.value(ticks_);
        return std::move(w.out);
    }

    std::uint64_t ticks() const override { return ticks_; }

    void assist(const Assist& a) {
        auto& g = *game_;
        if (a.lives >= 0) {
            g.score.lives = u8(a.lives);
        }
        if (a.rings >= 0) {
            g.score.rings = u8(((a.rings / 10) % 10) << 4 | (a.rings % 10)); // BCD
        }
        if (a.boss_hits >= 0) {
            for (auto& o : g.objects) {
                const bool boss =
                    o.type == object_type::underground_boss ||
                    o.type == object_type::sky_high_boss || o.type == object_type::aqua_lake_boss ||
                    o.type == object_type::green_hills_boss ||
                    o.type == object_type::gimmick_boss ||
                    o.type == object_type::scrambled_egg_boss || o.type == object_type::final_boss;
                if (boss && o.hits > a.boss_hits) {
                    o.hits = u8(a.boss_hits); // hits left
                }
            }
        }
        if (a.teleport_x >= 0 && gameplay()) {
            auto& sonic = g.objects[0];
            sonic.x = unsigned(a.teleport_x);
            sonic.y = unsigned(a.teleport_y);
            sonic.vx = 0;
            sonic.vy = 0;
            if (a.move_camera) {
                const unsigned cx = unsigned(std::max(0, a.teleport_x - 0x50));
                const unsigned cy = unsigned(std::max(0, a.teleport_y - 0x60));
                g.camera.x = std::uint16_t(cx);
                g.camera.target_x = std::uint16_t(cx);
                g.camera.y = std::uint16_t(cy);
                g.camera.target_y = std::uint16_t(cy);
            }
        }
    }

    Observation observe() const {
        Observation o;
        const auto& r = *game_;
        o.playing = gameplay();
        o.sonic_x = int(unsigned(r.objects[0].x));
        o.sonic_y = int(unsigned(r.objects[0].y));
        for (std::size_t i = 1; i < std::size(r.objects); ++i) {
            const auto& t = r.objects[i];
            if (t.type && t.type < 0xFE) {
                o.objects.push_back({t.type, int(unsigned(t.x)), int(unsigned(t.y))});
            }
        }
        return o;
    }

private:
    Assets assets_;
    std::unique_ptr<Game> game_;
    gg::Psg psg_;
    LogoVoice voice_;
    std::uint64_t ticks_{};
    int start_zone_{-1}, start_act_{};
    // Widescreen enhancement: the presented view and the arena force field.
    bool expanded_{};
    std::array<int, 2> view_{160, 144};
    ArenaBarrier barrier_;
    // Water enhancement: splash droplets and rain on the lens.
    WaterEffects water_;

    void load(std::span<const std::uint8_t> state) {
        Reader r{state};
        char magic[sizeof save_magic];
        r.bytes(magic, sizeof magic);
        std::uint32_t version{};
        r.value(version);
        if (std::memcmp(magic, save_magic, sizeof magic) != 0 || version != save_version) {
            throw std::runtime_error("This quick save is not from this Sonic 2 module");
        }
        game_->for_each_state([&](auto& part) { read_part(r, part); });
        display_parts(game_->display, [&](auto& part) { read_part(r, part); });
        barrier_.for_each_state([&](auto& part) { read_part(r, part); });
        water_.for_each_state([&](auto& part) { read_part(r, part); });
        psg_.serialize(r);
        std::uint32_t random{};
        r.value(random);
        r.value(ticks_);
        game_->resume_main_loop();
        game_->set_random_state(random);
    }
};

} // namespace

void assist(shell::GameModule& module, const Assist& changes) {
    dynamic_cast<Sonic2Module&>(module).assist(changes);
}

Observation observe(const shell::GameModule& module) {
    return dynamic_cast<const Sonic2Module&>(module).observe();
}

std::unique_ptr<shell::GameModule> make_module(const std::filesystem::path& p,
                                               std::span<const std::uint8_t> state) {
    return std::make_unique<Sonic2Module>(p, state, -1, 0);
}

std::unique_ptr<shell::GameModule> make_level_module(const std::filesystem::path& p,
                                                     unsigned level) {
    if (level == ending_start || level == good_ending_start) {
        return std::make_unique<Sonic2Module>(p, std::span<const std::uint8_t>{}, ending_zone,
                                              level == good_ending_start ? 1 : 0);
    }
    if (level >= levels.size()) {
        throw std::runtime_error("This level is not a start point");
    }
    return std::make_unique<Sonic2Module>(p, std::span<const std::uint8_t>{},
                                          int(levels[level].zone), int(levels[level].act));
}

} // namespace sonic2_gg
