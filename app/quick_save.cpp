#include "app/quick_save.hpp"
#include "platform/files.hpp"
#include "platform/sha256.hpp"
#include "platform/host.hpp"
#include "state/archive.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace app {
namespace {
namespace fs = std::filesystem;
constexpr int schema = 1;
constexpr std::uintmax_t max_save = 64 * 1024 * 1024;
struct Record {
    std::string magic{"Joyport quick save"}, game, content, screenshot;
    int version{schema};
    std::int64_t time{};
    std::vector<std::pair<std::string, std::string>> options;
    std::vector<std::uint8_t> state;
    template <class A> void persist(A &a) {
        a(magic, version, game, content, time, screenshot, options, state);
    }
};
fs::path folder(const fs::path &content, std::string_view game) {
    return content.parent_path().parent_path() / "saves" / std::string(game);
}
std::vector<fs::path> generations(const fs::path &base) {
    std::vector<fs::path> out;
    if (!fs::exists(base))
        return out;
    for (const auto &e : fs::directory_iterator(base)) {
        const auto n = e.path().filename().string();
        if (e.is_directory() && n.size() == 20 &&
            std::all_of(n.begin(), n.end(), [](char c) { return c >= '0' && c <= '9'; }))
            out.push_back(e.path());
    }
    std::sort(out.begin(), out.end());
    return out;
}
std::string fingerprint(const fs::path &content) {
    const auto bytes = platform::read_file(content / "release.txt");
    std::istringstream in(std::string(bytes.begin(), bytes.end()));
    std::string line, key;
    while (std::getline(in, line))
        if (line.starts_with("file ") || line.starts_with("release "))
            key += line + '\n';
    if (key.empty())
        throw std::runtime_error("Import this game's files before continuing.");
    return platform::sha256({reinterpret_cast<const std::uint8_t *>(key.data()), key.size()});
}
std::vector<std::uint8_t> bounded_read(const fs::path &path, std::uintmax_t limit = max_save) {
    if (fs::file_size(path) > limit)
        throw std::runtime_error("The save file is too large.");
    return platform::read_file(path);
}
Record read_record(const fs::path &dir, const fs::path &content, std::string_view game) {
    const auto bytes = bounded_read(dir / "state.bin");
    if (bytes.size() < 64)
        throw std::runtime_error("The quick save is incomplete.");
    const auto payload = std::span(bytes).subspan(64);
    if (platform::sha256(payload) != std::string(bytes.begin(), bytes.begin() + 64))
        throw std::runtime_error("The quick save is damaged.");
    Record r;
    state::Reader a(payload);
    a(r);
    a.finish();
    if (r.magic != "Joyport quick save" || r.version != schema || r.game != game)
        throw std::runtime_error("This save needs a different game or save-format version.");
    if (r.content != fingerprint(content))
        throw std::runtime_error("This save belongs to a different imported release.");
    if (r.state.empty())
        throw std::runtime_error("The quick save has no game state.");
    return r;
}
platform::Image thumbnail(GameModule &game) {
    const auto source = game.save_picture();
    if (source.width <= 0 || source.height <= 0)
        throw std::runtime_error("Cannot capture the game picture.");
    const double aspect = std::max(.1, game.pixel_aspect());
    const double scale = std::min(480.0 / (source.width * aspect), 300.0 / source.height);
    platform::Image out(std::max(1, int(source.width * aspect * scale)),
                        std::max(1, int(source.height * scale)));
    for (int y = 0; y < out.height; ++y)
        for (int x = 0; x < out.width; ++x)
            out.put(x, y,
                    source.pixels[std::size_t(y * source.height / out.height) * source.width +
                                  x * source.width / out.width]);
    return out;
}
void write_record(const fs::path &content, std::string_view game, GameModule &module) {
    Record r;
    r.game = game;
    r.content = fingerprint(content);
    r.state = module.write_state();
    if (r.state.empty())
        throw std::runtime_error("Persistent saves are not available for this game yet.");
    for (const auto &o : module.options())
        if (o.value >= 0 && std::size_t(o.value) < o.choices.size())
            r.options.emplace_back(o.name, o.choices[o.value]);
    r.time =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();
    const auto base = folder(content, game);
    fs::create_directories(base);
    const auto old = generations(base);
    const auto n = old.empty() ? 1 : std::stoull(old.back().filename().string()) + 1;
    std::ostringstream name;
    name << std::setw(20) << std::setfill('0') << n;
    const auto final = base / name.str();
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto staging = base / ("pending-" + std::to_string(stamp));
    if (!fs::create_directory(staging))
        throw std::runtime_error("Cannot prepare the save folder.");
    try {
        platform::save_png(thumbnail(module), staging / "preview.png");
        r.screenshot = platform::sha256(bounded_read(staging / "preview.png", 4 * 1024 * 1024));
        state::Writer a;
        a(r);
        auto hash = platform::sha256(a.bytes);
        std::vector<std::uint8_t> file(hash.begin(), hash.end());
        file.insert(file.end(), a.bytes.begin(), a.bytes.end());
        platform::write_file(staging / "state.bin", file);
        // Read back before publishing the pair. A directory rename makes the
        // state and picture visible together; the preceding save stays intact.
        read_record(staging, content, game);
        fs::rename(staging, final);
    } catch (...) {
        std::error_code ec;
        fs::remove_all(staging, ec);
        throw;
    }
    // Retain the preceding generation as a recovery copy. Cleanup failures
    // cannot turn a successful save into a reported failure.
    for (std::size_t i = 0; i + 1 < old.size(); ++i) {
        std::error_code ec;
        fs::remove_all(old[i], ec);
    }
}
class SavedGame final : public GameModule {
    GameEntry entry_;
    fs::path content_;
    std::unique_ptr<GameModule> game_;
    std::string error_;
    bool available_{};
    int width_{}, height_{};
    platform::GpuSurface* surface_{};
    bool world_surface_supported_{};
    platform::InputCapabilities input_capabilities_{};

  public:
    SavedGame(const GameEntry &entry, fs::path content)
        : entry_(entry), content_(std::move(content)), game_(entry_.start(content_)) {
        available_ = !generations(folder(content_, entry_.id)).empty();
    }
    double step_seconds() const override { return game_->step_seconds(); }
    void tick(const GameInput &in) override { game_->tick(in); }
    platform::Image picture() const override { return game_->picture(); }
    void set_world_surface_supported(bool supported) override {
        world_surface_supported_ = supported; game_->set_world_surface_supported(supported);
    }
    std::optional<WorldSurface> world_surface() const override { return game_->world_surface(); }
    platform::Image save_picture() const override { return game_->save_picture(); }
    void set_frame_fraction(double fraction) override { game_->set_frame_fraction(fraction); }
    void set_display_pointer(std::optional<std::array<int, 2>> at) override { game_->set_display_pointer(at); }
    double pixel_aspect() const override { return game_->pixel_aspect(); }
    std::optional<std::array<int, 2>> gpu_frame_size() const override { return game_->gpu_frame_size(); }
    void draw_gpu(const platform::GpuFrame &frame) override { game_->draw_gpu(frame); }
    void attach_gpu(platform::GpuSurface *surface) override {
        surface_ = surface;
        game_->attach_gpu(surface);
    }
    bool finished() const override { return game_->finished(); }
    std::vector<std::array<int, 4>> filter_regions() const override { return game_->filter_regions(); }
    std::string saved_message() const override { return game_->saved_message(); }
    bool take_menu_request() override { return game_->take_menu_request(); }
    int vhs_level() const override { return game_->vhs_level(); }
    bool draws_pointer() const override { return game_->draws_pointer(); }
    bool captures_mouse() const override { return game_->captures_mouse(); }
    bool wants_motion() const override { return game_->wants_motion(); }
    void set_input_capabilities(platform::InputCapabilities value) override {
        input_capabilities_=value; game_->set_input_capabilities(value);
    }
    platform::Orientation orientation() const override { return game_->orientation(); }
    std::vector<platform::TouchControl> touch_controls() const override { return game_->touch_controls(); }
    std::optional<platform::TiltConfig> tilt_controls() const override { return game_->tilt_controls(); }
    bool take_tilt_calibration_request() override { return game_->take_tilt_calibration_request(); }
    void set_canvas(int w, int h) override {
        width_ = w;
        height_ = h;
        game_->set_canvas(w, h);
    }
    void audio(std::span<float> s, int rate) override { game_->audio(s, rate); }
    std::vector<GameOption> options() const override { return game_->options(); }
    void set_option(std::size_t i, int v) override { game_->set_option(i, v); }
    std::string stopped() const override { return game_->stopped(); }
    std::string save_error() const override { return error_; }
    bool has_save() const override { return available_; }
    bool save() override {
        error_.clear();
        try {
            write_record(content_, entry_.id, *game_);
            available_ = true;
            return true;
        } catch (const std::exception &e) {
            error_ = e.what();
            return false;
        }
    }
    bool load() override {
        error_.clear();
        try {
            const auto dirs = generations(folder(content_, entry_.id));
            if (dirs.empty())
                throw std::runtime_error("No quick save yet.");
            auto record = read_record(dirs.back(), content_, entry_.id);
            auto fresh = entry_.start(content_);
            fresh->set_canvas(width_, height_);
            fresh->attach_gpu(surface_);
            fresh->set_input_capabilities(input_capabilities_);
            fresh->set_world_surface_supported(world_surface_supported_);
            // The save's own game options; presentation options (the screen filter) keep the
            // player's current choice.
            const auto opts = fresh->options();
            const auto current = game_->options();
            for (std::size_t i = 0; i < opts.size(); ++i) {
                if (opts[i].presentation) {
                    for (const auto &now : current)
                        if (now.name == opts[i].name && now.value >= 0 && std::size_t(now.value) < opts[i].choices.size())
                            fresh->set_option(i, now.value);
                    continue;
                }
                for (const auto &[name, choice] : record.options)
                    if (opts[i].name == name) {
                        auto at = std::find(opts[i].choices.begin(), opts[i].choices.end(), choice);
                        if (at != opts[i].choices.end())
                            fresh->set_option(i, int(at - opts[i].choices.begin()));
                    }
            }
            fresh->read_state(record.state);
            game_ = std::move(fresh);
            return true;
        } catch (const std::exception &e) {
            error_ = e.what();
            return false;
        }
    }
};
} // namespace
bool persistent_saves(std::string_view game) {
    return game == "captain_toad_3ds" || game == "fort_apocalypse_c64" || game == "bruce_lee_c64" || game == "wizball_c64" ||
           game == "marble_madness_amiga" || game == "mindwalker_amiga" || game == "wings_of_fury_amiga" ||
           game == "cannon_fodder_amiga" || game == "spy_mouse_ios" || game == "sonic_gg" || game == "sonic2_gg" ||
           game == "triple_trouble_gg" || game == "tower_of_babel_amiga" || game == "rock_n_roll_amiga" ||
           game == "spindizzy_worlds_amiga" || game == "fa_18_interceptor_amiga" ||
           // The puzzle adventures with checkpoints: the quick save keeps the last room checkpoint
           // (games/layton/reprise/saved_checkpoint.hpp), not the exact moment.
           game == "curious_village_ds" || game == "pandoras_box_ds" || game == "lost_future_ds" ||
           game == "spectres_call_ds" || game == "miracle_mask_3ds";
}
std::unique_ptr<GameModule> with_quick_save(const GameEntry &entry, const fs::path &content) {
    if (persistent_saves(entry.id))
        return std::make_unique<SavedGame>(entry, content);
    return entry.start(content);
}
SavePreview save_preview(const fs::path &content, std::string_view game) {
    SavePreview out;
    if (!persistent_saves(game))
        return out;
    try {
        const auto dirs = generations(folder(content, game));
        if (dirs.empty())
            return out;
        const auto r = read_record(dirs.back(), content, game);
        out.available = true;
        out.saved_at = r.time;
        const std::time_t t = r.time;
        std::tm tm{};
        if (const auto *local = std::localtime(&t))
            tm = *local;
        std::ostringstream date;
        date << std::put_time(&tm, "%d %b %Y, %H:%M");
        out.date = date.str();
        const auto png = bounded_read(dirs.back() / "preview.png", 4 * 1024 * 1024);
        if (platform::sha256(png) != r.screenshot)
            throw std::runtime_error("The saved picture is damaged; game progress is still available.");
        SDL_Surface *loaded = SDL_LoadPNG_IO(SDL_IOFromConstMem(png.data(), png.size()), true);
        if (!loaded)
            throw std::runtime_error("The saved picture could not be opened.");
        SDL_Surface *rgba = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(loaded);
        if (!rgba)
            throw std::runtime_error("The saved picture could not be converted.");
        if (rgba->w > 480 || rgba->h > 300) {
            SDL_DestroySurface(rgba);
            throw std::runtime_error("The saved picture has invalid dimensions.");
        }
        out.image.emplace(rgba->w, rgba->h);
        for (int y = 0; y < rgba->h; ++y)
            std::memcpy(out.image->pixels.data() + y * rgba->w,
                        static_cast<const std::uint8_t *>(rgba->pixels) + y * rgba->pitch,
                        std::size_t(rgba->w) * 4);
        SDL_DestroySurface(rgba);
    } catch (const std::exception &e) {
        out.error = e.what();
    }
    return out;
}
} // namespace app
