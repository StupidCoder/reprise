#include "sonic/reprise/entry.hpp"
#include "bluestreak_embed.hpp"
#include "platform/files.hpp"
#include "ui/draw.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <optional>
#include <set>
#include <stdexcept>
#include <utility>

namespace sonic_reprise {
namespace {
namespace fs = std::filesystem;
using bluestreak::Enhancement;
using bluestreak::Title;

// What the pause menu calls BlueStreak's enhancements (BlueStreak's own texts). All but the
// expanded view are presentation only: they observe the game and never feed back into it, so
// they keep the player's choice when a save is loaded. The expanded view changes which objects
// are awake (gameplay), so a save keeps the view it was made with.
struct EnhancementText {
    const char* name;
    const char* description;
};

EnhancementText text(Enhancement which) {
    switch (which) {
    case Enhancement::shield_transparency:
        return {"Shield transparency", "A translucent shield replaces the original alternating frames."};
    case Enhancement::widescreen:
        return {"Expanded view",
                "See more of the level, up to 21:9. Off uses the original Game Gear view. Changes which "
                "objects are awake, so saves keep the view they were made with."};
    case Enhancement::water:
        return {"Enhanced water", "Pixelated refraction, translucent water, waves, splashes and rain on the lens."};
    case Enhancement::rings:
        return {"Scattered rings", "Lost rings scatter, bounce and slide with transparency."};
    case Enhancement::lcd_screen:
        break;
    }
    return {"LCD screen",
            "The Game Gear's screen: muted colours, motion smear and RGB stripes (stripes from three "
            "screen pixels per game pixel). Drawn before the VHS look when both are on."};
}

// A game of the collection in Reprise: BlueStreak's session drawn with the host's renderer at
// the window's resolution (so the LCD look's stripes resolve), ticking at the original's frame
// rate, its sound queued for the host's mixer.
class Module final : public app::GameModule {
public:
    Module(Title title, fs::path pack)
        : title_(title), pack_(std::move(pack)), offered_(bluestreak::offered(title)),
          points_(bluestreak::start_points(title)), session_(bluestreak::Session::start(title, pack_)) {}

    ~Module() override {
        for (auto* t : {texture_, mask_, glow_}) {
            if (t) SDL_DestroyTexture(t);
        }
    }

    double step_seconds() const override { return session_->step_seconds(); }

    void tick(const app::GameInput& in) override {
        session_->set_view(widescreen_, canvas_[0], canvas_[1]);
        // Start during play opens the launcher's pause menu, as BlueStreak's shell did (the
        // modules keep Start from the game while an act is played; Sonic Advance never reports
        // play, so its own pause screen keeps Start).
        if (in.start && !start_held_ && session_->gameplay()) {
            menu_request_ = true;
        }
        start_held_ = in.start;
        const bluestreak::Buttons buttons{in.left, in.right, in.up, in.down, in.fire, in.button2, in.start};
        const auto samples = session_->tick(buttons, enhancements());
        sound_.insert(sound_.end(), samples.begin(), samples.end());
        // At most a few frames queued: a stall must not leave the sound behind the picture.
        const std::size_t limit = std::size_t(4 * step_seconds() * rate) * 2;
        if (sound_.size() > limit) {
            sound_.erase(sound_.begin(), sound_.end() - std::ptrdiff_t(limit));
        }
        frame_.reset();
    }

    void audio(std::span<float> stereo, int sample_rate) override {
        const std::size_t frames = stereo.size() / 2;
        // (The host mixes at the modules' own 48 kHz; any other rate takes the nearest sample.)
        const double step = double(rate) / std::max(sample_rate, 1);
        std::size_t used = 0;
        for (std::size_t i = 0; i < frames; ++i) {
            const auto from = std::size_t(double(i) * step) * 2;
            const bool have = from + 1 < sound_.size();
            stereo[2 * i] = have ? sound_[from] : 0.0f;
            stereo[2 * i + 1] = have ? sound_[from + 1] : 0.0f;
            used = std::min(sound_.size(), from + 2);
        }
        if (sample_rate == rate) used = std::min(sound_.size(), frames * 2);
        sound_.erase(sound_.begin(), sound_.begin() + std::ptrdiff_t(used));
    }

    // The frame is drawn by the game for the canvas's aspect: the expanded view is wider or
    // taller than the Game Gear's 160 x 144.
    std::optional<std::array<int, 2>> gpu_frame_size() const override {
        const auto& f = frame();
        return std::array<int, 2>{f.width, f.height};
    }
    void attach_gpu(platform::GpuSurface*) override {}

    void draw_gpu(const platform::GpuFrame& target) override {
        const auto& image = frame();
        const double now = double(SDL_GetTicksNS()) / 1e9;
        const platform::Image* shown = &image;
        if (lcd_) {
            if (!lcd_shown_) lcd_look_.reset();
            shown = &lcd_look_.apply(image, last_draw_ < 0 ? 0.0 : now - last_draw_);
        }
        lcd_shown_ = lcd_;
        last_draw_ = now;
        SDL_Renderer* renderer = target.renderer;
        if (renderer != renderer_) {
            for (auto* t : {&texture_, &mask_, &glow_}) {
                if (*t) SDL_DestroyTexture(*t);
                *t = nullptr;
            }
            renderer_ = renderer;
            mask_key_ = {};
        }
        upload(texture_, *shown, SDL_BLENDMODE_NONE, SDL_SCALEMODE_NEAREST);
        SDL_Texture* previous = SDL_GetRenderTarget(renderer);
        SDL_SetRenderTarget(renderer, target.target);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        const float w = float(image.width), h = float(image.height);
        SDL_FRect destination{0, 0, float(target.width), float(target.height)};
        if (lcd_) {
            // BlueStreak's rule: whole device pixels per dot, else the stripes beat against the
            // device grid; below three per dot they cannot resolve and fade out (LcdLook::mask).
            const int k = int(std::floor(std::min(target.width / w, target.height / h) + 1e-6));
            if (k >= 3) {
                destination = {float((target.width - int(w) * k) / 2), float((target.height - int(h) * k) / 2),
                               w * k, h * k};
            }
        }
        SDL_RenderTexture(renderer, texture_, nullptr, &destination);
        if (lcd_) {
            const std::array<double, 4> key{destination.w, destination.h, destination.w / w, destination.h / h};
            if (!mask_ || key != mask_key_) {
                mask_key_ = key;
                upload(mask_,
                       bluestreak::LcdLook::mask(int(std::ceil(key[0])), int(std::ceil(key[1])), key[2], key[3], 0, 0),
                       SDL_BLENDMODE_MOD, SDL_SCALEMODE_NEAREST);
            }
            SDL_RenderTexture(renderer, mask_, nullptr, &destination);
            upload(glow_, *shown, SDL_BLENDMODE_ADD, SDL_SCALEMODE_LINEAR);
            const auto glow = Uint8(bluestreak::LcdLook::glow());
            SDL_SetTextureColorMod(glow_, glow, glow, glow);
            SDL_RenderTexture(renderer, glow_, nullptr, &destination);
        }
        SDL_SetRenderTarget(renderer, previous);
    }

    // The raw game frame (save pictures): no LCD look and no VHS filter.
    platform::Image picture() const override { return frame(); }
    double pixel_aspect() const override { return 1.0; }

    void set_canvas(int width, int height) override {
        if (width == canvas_[0] && height == canvas_[1]) return;
        canvas_ = {width, height};
        session_->set_view(widescreen_, width, height);
        frame_.reset();
    }

    bool take_menu_request() override { return std::exchange(menu_request_, false); }

    // Without Reprise's persistent saves (Sonic Advance), the reason quick saves are missing.
    bool save() override {
        try {
            session_->save();
            error_ = "Quick saves are not available for this game yet.";
        } catch (const std::exception& e) {
            error_ = e.what();
        }
        return false;
    }
    std::string save_error() const override { return error_; }
    std::string stopped() const override { return session_->stopped(); }
    int vhs_level() const override { return vhs_; }

    // The VHS look, BlueStreak's enhancements this game offers, and the level selector's start
    // points (none for Sonic Advance).
    std::vector<app::GameOption> options() const override {
        std::vector<app::GameOption> out{app::screen_filter_option(vhs_)};
        for (const auto which : offered_) {
            const auto [name, description] = text(which);
            out.push_back({name, {"Off", "On"}, setting(which) ? 1 : 0, description, {},
                           which != Enhancement::widescreen});
        }
        if (!points_.empty()) {
            app::GameOption start{"Start at", {"Power on"}, start_point_,
                                  "Restarts the game at once: from power-on (the cartridge's own start), or "
                                  "directly at an act, special stage or ending. Kept for the next launch.",
                                  "Level select"};
            for (const auto& p : points_) start.choices.push_back(p.world + " " + p.label);
            out.push_back(std::move(start));
        }
        return out;
    }

    void set_option(std::size_t index, int value) override {
        if (index == 0) {
            vhs_ = std::clamp(value, 0, ui::vhs_levels - 1);
            return;
        }
        if (index - 1 < offered_.size()) {
            setting(offered_[index - 1]) = value != 0;
            session_->set_view(widescreen_, canvas_[0], canvas_[1]);
            frame_.reset();
            return;
        }
        if (index == offered_.size() + 1 && value >= 0 && value <= int(points_.size()) && value != start_point_) {
            auto next = bluestreak::Session::start(title_, pack_, value == 0 ? -1 : points_[std::size_t(value - 1)].id);
            session_ = std::move(next);
            start_point_ = value;
            restarted();
        }
    }

    // Quick saves: BlueStreak's own state (sound driver, PSG and game included) after a short
    // header naming the game. Sonic Advance has none yet; its session says why.
    std::vector<std::uint8_t> write_state() override {
        auto state = session_->save();
        std::vector<std::uint8_t> out(magic.begin(), magic.end());
        out.push_back(std::uint8_t(title_));
        out.insert(out.end(), state.begin(), state.end());
        return out;
    }

    void read_state(std::span<const std::uint8_t> bytes) override {
        if (bytes.size() <= magic.size() || !std::equal(magic.begin(), magic.end(), bytes.begin()) ||
            bytes[magic.size()] != std::uint8_t(title_)) {
            throw std::runtime_error("This save is not from this game.");
        }
        session_ = bluestreak::Session::restore(title_, pack_, bytes.subspan(magic.size() + 1));
        restarted();
        start_held_ = true; // (Start held while loading does not open the menu)
    }

private:
    static constexpr int rate = bluestreak::Session::sample_rate();
    static constexpr std::array<std::uint8_t, 8> magic{'B', 'S', 'R', 'E', 'P', 'R', '1', 0};

    bool& setting(Enhancement which) {
        switch (which) {
        case Enhancement::shield_transparency: return enhancements_.shield_transparency;
        case Enhancement::widescreen: return widescreen_;
        case Enhancement::water: return enhancements_.water;
        case Enhancement::rings: return enhancements_.rings;
        case Enhancement::lcd_screen: break;
        }
        return lcd_;
    }
    bool setting(Enhancement which) const { return const_cast<Module*>(this)->setting(which); }

    bluestreak::Enhancements enhancements() const {
        auto e = enhancements_;
        e.widescreen = widescreen_;
        return e;
    }

    const platform::Image& frame() const {
        if (!frame_) frame_ = session_->render(enhancements());
        return *frame_;
    }

    void restarted() {
        session_->set_view(widescreen_, canvas_[0], canvas_[1]);
        sound_.clear();
        frame_.reset();
        lcd_shown_ = false;
        menu_request_ = false;
    }

    // Replaces a texture's pixels, (re)creating it at the picture's size.
    void upload(SDL_Texture*& texture, const platform::Image& image, SDL_BlendMode blend, SDL_ScaleMode scale) {
        if (texture && (texture->w != image.width || texture->h != image.height)) {
            SDL_DestroyTexture(texture);
            texture = nullptr;
        }
        if (!texture) {
            texture = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
                                        std::max(image.width, 1), std::max(image.height, 1));
            if (!texture) throw std::runtime_error(SDL_GetError());
        }
        SDL_SetTextureBlendMode(texture, blend);
        SDL_SetTextureScaleMode(texture, scale);
        if (!SDL_UpdateTexture(texture, nullptr, image.pixels.data(), image.width * int(sizeof(platform::Color)))) {
            throw std::runtime_error(SDL_GetError());
        }
    }

    Title title_;
    fs::path pack_;
    std::vector<Enhancement> offered_;
    std::vector<bluestreak::StartPoint> points_;
    std::unique_ptr<bluestreak::Session> session_;
    bluestreak::Enhancements enhancements_{};
    bool widescreen_{}, lcd_{};
    int vhs_{}, start_point_{};
    std::array<int, 2> canvas_{};
    bool start_held_{}, menu_request_{};
    std::string error_;
    std::vector<float> sound_;
    mutable std::optional<platform::Image> frame_;
    // Presentation: the LCD look's state and the renderer's textures.
    bluestreak::LcdLook lcd_look_;
    bool lcd_shown_{};
    double last_draw_{-1};
    SDL_Renderer* renderer_{};
    SDL_Texture *texture_{}, *mask_{}, *glow_{};
    std::array<double, 4> mask_key_{};
};

// --- Import and previews ---------------------------------------------------------------------

app::ImportResult import(Title title, std::span<const std::uint8_t> image) {
    app::ImportResult result;
    try {
        for (auto& file : bluestreak::extract(title, image, result.release)) {
            result.files.push_back({std::move(file.name), std::move(file.bytes)});
        }
    } catch (const bluestreak::NotThisTitle& e) {
        throw app::NotThisGame(e.what());
    }
    return result;
}

platform::Image waveform(const std::vector<float>& mono) {
    constexpr int width = 640, height = 160;
    platform::Image image(width, height);
    ui::fill(image, {16, 20, 28});
    ui::rect(image, 0, height / 2, width, 1, {60, 70, 90});
    for (int x = 0; x < width && !mono.empty(); ++x) {
        const auto from = mono.size() * std::size_t(x) / width;
        const auto to = std::max(from + 1, mono.size() * std::size_t(x + 1) / width);
        float low = 1, high = -1;
        for (auto i = from; i < to && i < mono.size(); ++i) {
            low = std::min(low, mono[i]);
            high = std::max(high, mono[i]);
        }
        const int top = height / 2 - int(std::clamp(high, -1.0f, 1.0f) * (height / 2 - 4));
        const int bottom = height / 2 - int(std::clamp(low, -1.0f, 1.0f) * (height / 2 - 4));
        ui::rect(image, x, top, 1, std::max(1, bottom - top + 1), {110, 200, 255});
    }
    return image;
}

// Pictures the engine draws from the imported pack (each act's first seconds, played without
// input) and the sound test's tracks through the game's own sound driver.
std::vector<app::Preview> previews(Title title, const fs::path& content) {
    bluestreak::validate(title, content);
    std::vector<app::Preview> out;
    auto run = [&](int start_point, int frames, const std::string& name) {
        auto session = bluestreak::Session::start(title, content, start_point);
        session->set_view(false, 0, 0);
        for (int f = 0; f < frames && session->stopped().empty(); ++f) session->tick({}, {});
        out.push_back({name, session->render({})});
    };
    if (title == Title::sonic_advance_gba) {
        for (const int seconds : {4, 12, 30}) {
            run(-1, seconds * 60, "From power-on, after " + std::to_string(seconds) + " seconds");
        }
    } else {
        std::set<std::string> worlds;
        for (const auto& p : bluestreak::start_points(title)) {
            if (p.world == "Ending" || !worlds.insert(p.world).second) continue;
            run(p.id, 240, p.world + " " + p.label + ", after four seconds");
        }
    }
    constexpr int preview_rate = 24000; // (half the PSG model's rate: mono previews stay small)
    for (auto& track : bluestreak::sound_test(title, content, 30.0)) {
        std::vector<float> mono;
        const int step = bluestreak::Session::sample_rate() / preview_rate;
        for (std::size_t i = 0; i + 1 < track.stereo.size(); i += 2 * std::size_t(step)) {
            mono.push_back((track.stereo[i] + track.stereo[i + 1]) * 0.5f);
        }
        out.push_back({"Sound test: " + track.title + (track.description.empty() ? "" : " (" + track.description + ")"),
                       waveform(mono), app::PreviewSound{std::move(mono), double(preview_rate)}});
    }
    return out;
}

// The standalone BlueStreak app's import, taken over once (copied; BlueStreak keeps its own).
// Its data folder is Reprise's sibling (both are SDL preference folders of the same
// organisation). Its quick saves use BlueStreak's own format without pictures, so they stay
// there.
std::optional<app::LegacyCopy> legacy(Title title, const std::string& name, const fs::path& data) {
    fs::path base = data;
    if (base.filename().empty()) base = base.parent_path();
    const fs::path old = base.parent_path() / "BlueStreak";
    std::error_code error;
    if (!fs::is_directory(old / "games", error)) {
        return std::nullopt;
    }
    app::LegacyCopy copy;
    const auto pack = bluestreak::standalone_pack(title, old);
    if (fs::exists(pack / "pack.manifest", error) && !bluestreak::standalone_pack_current(title, old)) {
        copy.notes.push_back(name + ": BlueStreak's import was made by an older version and lacks data the game "
                                    "now reads; import the ROM again. BlueStreak's files were left as they are.");
    } else if (fs::exists(pack / "pack.manifest", error)) {
        try {
            bluestreak::validate(title, pack);
            app::ImportResult result;
            for (auto it = fs::recursive_directory_iterator(pack, error); !error && it != fs::recursive_directory_iterator();
                 it.increment(error)) {
                if (!it->is_regular_file()) continue;
                result.files.push_back({fs::relative(it->path(), pack).generic_string(), platform::read_file(it->path())});
            }
            if (error) throw std::runtime_error(error.message());
            result.release = bluestreak::release(title);
            copy.import = std::move(result);
        } catch (const std::exception& e) {
            copy.notes.push_back(name + ": BlueStreak's import cannot be used here (" + e.what() +
                                 "). Import the ROM again; BlueStreak's files were left as they are.");
        }
    }
    if (fs::exists(bluestreak::standalone_quick_save(title, old), error)) {
        copy.notes.push_back(name + ": BlueStreak's quick save stays in BlueStreak (its format has no picture or "
                                    "release check); make a new save in Reprise.");
    }
    if (!copy.import && copy.notes.empty()) {
        return std::nullopt;
    }
    return copy;
}

struct Info {
    const char* id;
    const char* title;
    Title which;
    app::Machine machine;
    const char* media;
    std::uintmax_t largest;
    const char* progress;
};

app::GameEntry make_entry(const Info& info) {
    const Title which = info.which;
    const std::string name = info.title;
    app::GameEntry game{
        .id = info.id,
        .title = info.title,
        .machine = info.machine,
        .media = info.media,
        .import = [which](std::span<const std::uint8_t> image) { return import(which, image); },
        .largest_image = info.largest,
        .previews = [which](const fs::path& content) { return previews(which, content); },
        .start = [which](const fs::path& content) -> std::unique_ptr<app::GameModule> {
            return std::make_unique<Module>(which, content);
        },
        .legacy = [which, name](const fs::path& data) { return legacy(which, name, data); },
        .progress = info.progress,
    };
    game.collection = app::Collection::sonic;
    return game;
}

} // namespace

std::vector<app::GameEntry> entries() {
    const Info games[] = {
        {"sonic_gg", "Sonic the Hedgehog", Title::sonic1_gg, app::Machine::game_gear,
         "the cartridge dump (.gg) of Japan/USA, 256 KiB", 1u << 20,
         "The whole game from the SEGA logo to both endings, compared with the original. Start opens the "
         "pause menu during play; button 1 Space/Z, button 2 X, Start Return. Options: expanded view (up to 21:9), "
         "shield transparency, enhanced water, scattered rings, Game Gear LCD screen, VHS look, level select. "
         "Quick save. No touch controls yet."},
        {"sonic2_gg", "Sonic the Hedgehog 2", Title::sonic2_gg, app::Machine::game_gear,
         "the cartridge dump (.gg) of the World release, 512 KiB", 1u << 20,
         "The whole game from the SEGA logo to both endings; a few paths (continue screen, parts of some boss "
         "fights, rare gimmicks) were not compared with the original. Options: expanded view, enhanced water, "
         "Game Gear LCD screen, VHS look, level select. Quick save during play. No touch controls yet."},
        {"triple_trouble_gg", "Sonic Triple Trouble", Title::triple_trouble_gg, app::Machine::game_gear,
         "the cartridge dump (.gg) of USA/Europe/Brazil, 512 KiB", 1u << 20,
         "Playable from the SEGA logo to the ending. A few paths were not compared with the original, and a "
         "routine not rebuilt yet would stop the game with a message. Options: expanded view (not in special "
         "stages, flights or the train ride), Game Gear LCD screen, VHS look, level select. Quick save. No touch "
         "controls yet."},
        {"sonic_advance_gba", "Sonic Advance", Title::sonic_advance_gba, app::Machine::game_boy_advance,
         "the cartridge dump (.gba) of the USA release, 8 MiB", 16u << 20,
         "Playable as Sonic from power-on to the ending. Not rebuilt yet, and stopping the game with a message: "
         "Tails, Knuckles and Amy, the act select (a new game after clearing an act), the continue and time-over "
         "screens, time attack, options, multiplayer and the Tiny Chao Garden. The cartridge's save is not kept, "
         "so every start is a first start. No quick saves yet. Options: VHS look. No touch controls yet."},
    };
    std::vector<app::GameEntry> out;
    for (const auto& info : games) out.push_back(make_entry(info));
    return out;
}

} // namespace sonic_reprise
