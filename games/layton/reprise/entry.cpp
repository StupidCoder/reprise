#include "layton/reprise/entry.hpp"
#include "layton/reprise/dual_screen.hpp"
#include "layton/reprise/saved_checkpoint.hpp"
#include "layton_embed.hpp"
#include "state/archive.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace layton_reprise {
namespace {
namespace fs = std::filesystem;
namespace embed = picarat::embed;

// What Reprise knows about each adventure.
struct Info {
    const char* id;
    embed::Title title;
    const char* name;
    app::Machine machine;
    const char* media;
    const char* progress;
};

constexpr const char* ds_media = "the cartridge image (NDS)";
constexpr const char* ctr_media = "the unencrypted cartridge image (CCI) of the German edition";

const Info games[] = {
    {"curious_village_ds", embed::Title::CuriousVillage, "Curious Village", app::Machine::nintendo_ds,
     "the cartridge image (NDS) of the European edition",
     "The whole game from the title to the end credits in English, French, German, Spanish and Italian: "
     "the story, all puzzles, the trunk, hotel and painting, the bonus section, music, voices and movies. "
     "The Wi-Fi download ends in the original's connection error. Saves: the game's three slots; the "
     "quick save keeps the last room checkpoint."},
    {"pandoras_box_ds", embed::Title::PandorasBox, "Pandora's Box", app::Machine::nintendo_ds,
     "the cartridge image (NDS) of the German edition",
     "The whole game from the title to the ending and the post-game: the story, all 187 puzzles, the tea, "
     "camera, photo and hamster games, the bag, the bonus section, music, voices and movies. Some windows "
     "and transitions use a native layout; one rarely reached room (place 63) is missing. Saves: the "
     "game's three slots; the quick save keeps the last checkpoint (room, event or puzzle start)."},
    {"lost_future_ds", embed::Title::LostFuture, "Lost Future", app::Machine::nintendo_ds,
     "the cartridge image (NDS) of the German edition",
     "From the logos to the end credits: the story, all 224 puzzles, the parrot, toy car and picture "
     "book, the bag, music, voices and movies. The puzzle index, dictionary, secrets and Wi-Fi menus are "
     "not reconstructed yet. Saves: the game's three slots; the quick save keeps the last room checkpoint."},
    {"spectres_call_ds", embed::Title::SpectresCall, "Spectre's Call", app::Machine::nintendo_ds,
     "the cartridge image (NDS) of the German edition",
     "From the title to the end credits: the story, 228 puzzles, the bag's three minigames, the "
     "whack-a-mouse, the bonus section, films, music and effects. Places and some faces are approximated. "
     "Saves: the game's own three slots and its continue from the title; no Reprise quick save yet."},
    {"miracle_mask_3ds", embed::Title::MiracleMask, "Miracle Mask", app::Machine::nintendo_3ds, ctr_media,
     "From the title to the end credits: the story, all 150 puzzles, the ruins, robot, shop, horse ride, "
     "train and rabbit, the secrets, movies, music and voices. Some side-game stages and the Wi-Fi menus "
     "are not compared or not reconstructed. Saves: the game's own three slots; the quick save keeps the "
     "start of the current place."},
    {"azran_legacy_3ds", embed::Title::AzranLegacy, "Azran Legacy", app::Machine::nintendo_3ds, ctr_media,
     "Import and pictures only: its story is not reconstructed yet, so it cannot be started (its puzzles "
     "and two minigames run in the engine's research tools)."},
};

// One tick of sound at Reprise's output rate.
constexpr int output_rate = platform::Host::audio_sample_rate;


std::string preferred_locale() {
    std::string iso;
    int count = 0;
    if (SDL_Locale** locales = SDL_GetPreferredLocales(&count)) {
        if (count > 0 && locales[0] && locales[0]->language) {
            iso = locales[0]->language;
            if (locales[0]->country) {
                iso += std::string("-") + locales[0]->country;
            }
        }
        SDL_free(locales);
    }
    return iso;
}

// The image the library hands over, as the engine's reader.
class ImageReader final : public embed::ImageSource {
public:
    explicit ImageReader(platform::FileSource& file) : file_(file) {}
    std::uint64_t size() const override { return file_.size(); }
    void read(std::uint64_t offset, std::span<std::uint8_t> out) override { file_.read(offset, out); }

private:
    platform::FileSource& file_;
};

class Module final : public app::GameModule {
public:
    Module(const Info& info, fs::path pack, fs::path progress)
        : info_(info), pack_(std::move(pack)), progress_(std::move(progress)),
          screen_(embed::screens(info.title)), languages_(embed::languages(info.title, pack_)) {}

    ~Module() override {
        SDL_DestroyTexture(top_);
        SDL_DestroyTexture(bottom_);
    }

    double step_seconds() const override { return 1.0 / embed::kFramesPerSecond; }
    // The 3DS games read the console's motion sensor.
    bool wants_motion() const override {
        return info_.machine == app::Machine::nintendo_3ds;
    }

    void tick(const app::GameInput& in) override {
        if (!session_ && stopped_.empty()) {
            start();
        }
        if (!session_) {
            return;
        }
        auto routed = router_.route(in.pointer_events, in.fingers, screen_, status_.movie, blow_button());
        embed::Input input;
        input.touches = std::move(routed.touches);
        // The arrows are the pad; with Shift they tilt the console (the 3DS games' motion sensor).
        if (in.shift) {
            input.tiltX = (in.right ? 1.0f : 0.0f) - (in.left ? 1.0f : 0.0f);
            input.tiltY = (in.down ? 1.0f : 0.0f) - (in.up ? 1.0f : 0.0f);
        } else {
            input.left = in.left;
            input.right = in.right;
            input.up = in.up;
            input.down = in.down;
        }
        input.shake = in.button2; // held: the console shaken from side to side
        // A phone's accelerometer stands in for the 3DS's (the way it was held at the start
        // counts as flat; game_session's consoleAcceleration).
        input.motion = in.motion;
        // Blowing into the microphone: the button on the screen, or the fire button in a puzzle.
        input.blow = routed.blowing || (in.fire && status_.puzzle);
        // Skipping (a convenience): Start or Return ends what plays, if it can be skipped.
        input.skip = in.start && !start_held_;
        start_held_ = in.start;
        sound_.assign(std::size_t(output_rate / embed::kFramesPerSecond) * 2, 0.0f);
        if (session_->step(input, sound_, output_rate)) {
            queue_.insert(queue_.end(), sound_.begin(), sound_.end());
            // (Never more than a quarter second behind.)
            const std::size_t limit = std::size_t(output_rate / 4) * 2;
            if (queue_.size() > limit) {
                queue_.erase(queue_.begin(), queue_.begin() + std::ptrdiff_t(queue_.size() - limit));
            }
            if (session_->frame(pixels_)) {
                ++frames_;
            }
            status_ = session_->status();
            // A checkpoint was written with this frame: it is the picture of a save made now. A
            // checkpoint written during a fade (Pandora's Box writes them at an event's start, still
            // black) takes the frames after it while they grow brighter, until the picture has
            // been steady for 10 frames (at most three seconds).
            if (session_->checkpointGeneration() != checkpoint_generation_ && !pixels_.empty()) {
                checkpoint_generation_ = session_->checkpointGeneration();
                checkpoint_pixels_ = pixels_;
                checkpoint_brightness_ = brightness(pixels_);
                picture_wait_ = 180;
                picture_steady_ = 0;
            } else if (picture_wait_ > 0 && !pixels_.empty()) {
                --picture_wait_;
                const double now = brightness(pixels_);
                if (now > checkpoint_brightness_ + 0.5) {
                    checkpoint_pixels_ = pixels_;
                    checkpoint_brightness_ = now;
                    picture_steady_ = 0;
                } else if (++picture_steady_ >= 10) {
                    picture_wait_ = 0;
                }
            }
        }
        if (session_->ended()) {
            const auto error = session_->error();
            if (!error.empty()) {
                stopped_ = error;
            } else {
                left_ = true; // the story is over (or the game returned to the library)
            }
        }
    }

    // The two screens at their own size, arranged as shown now.
    platform::Image picture() const override { return compose(pixels_); }

    // A quick save keeps the checkpoint: its picture is the frame the checkpoint was written with.
    platform::Image save_picture() const override {
        return compose(checkpoint_pixels_.empty() ? pixels_ : checkpoint_pixels_);
    }

    platform::Image compose(const std::vector<std::uint8_t>& pixels) const {
        const auto s = embed::screens(info_.title);
        const bool side = screen_.shown().side_by_side;
        const int width = side ? s.top.width + s.bottom.width : std::max(s.top.width, s.bottom.width);
        const int height = side ? std::max(s.top.height, s.bottom.height) : s.top.height + s.bottom.height;
        platform::Image out(width, height);
        std::fill(out.pixels.begin(), out.pixels.end(), platform::Color{0, 0, 0, 255});
        const int row = std::max(s.top.width, s.bottom.width);
        if (pixels.size() < std::size_t(row) * std::size_t(s.top.height + s.bottom.height) * 4) {
            return out;
        }
        auto put = [&](const embed::Size& screen, int y0, int x, int y) {
            for (int sy = 0; sy < screen.height; ++sy) {
                for (int sx = 0; sx < screen.width; ++sx) {
                    const std::uint8_t* p = pixels.data() + (std::size_t(y0 + sy) * std::size_t(row) + std::size_t(sx)) * 4;
                    out.put(x + sx, y + sy, {p[0], p[1], p[2], 255});
                }
            }
        };
        if (side) {
            put(s.top, 0, 0, (height - s.top.height) / 2);
            put(s.bottom, s.top.height, s.top.width, (height - s.bottom.height) / 2);
        } else {
            put(s.top, 0, (width - s.top.width) / 2, 0);
            put(s.bottom, s.top.height, (width - s.bottom.width) / 2, s.top.height);
        }
        return out;
    }
    double pixel_aspect() const override { return 1.0; }

    // Drawn with Reprise's renderer at the window's own size: the frame is the drawing area, the
    // screens are placed in it (DualScreen).
    std::optional<std::array<int, 2>> gpu_frame_size() const override {
        if (canvas_[0] > 0 && canvas_[1] > 0) {
            return canvas_;
        }
        const auto p = picture_size();
        return std::array<int, 2>{p[0], p[1]};
    }
    void set_canvas(int width, int height) override { canvas_ = {std::max(1, width), std::max(1, height)}; }

    void draw_gpu(const platform::GpuFrame& frame) override {
        screen_.show(frame.width, frame.height);
        const auto& p = screen_.shown();
        SDL_Renderer* r = frame.renderer;
        SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
        SDL_RenderClear(r);
        if (pixels_.empty()) {
            return;
        }
        upload(r);
        const SDL_FRect top{float(p.top.x), float(p.top.y), float(p.top.width), float(p.top.height)};
        const SDL_FRect bottom{float(p.bottom.x), float(p.bottom.y), float(p.bottom.width), float(p.bottom.height)};
        SDL_RenderTexture(r, top_, nullptr, &top);
        SDL_RenderTexture(r, bottom_, nullptr, &bottom);
        if (touch_areas_ && !status_.areas.empty()) {
            draw_touch_areas(r, p);
        }
        if (const auto button = blow_button()) {
            draw_blow_button(r, *button);
        }
    }

    // Each screen is a picture of its own for the screen filter (VHS).
    std::vector<std::array<int, 4>> filter_regions() const override {
        const auto size = gpu_frame_size();
        const auto p = screen_.place((*size)[0], (*size)[1]);
        std::vector<std::array<int, 4>> out;
        for (const auto& rect : {p.top, p.bottom}) {
            const int x0 = int(std::lround(rect.x)), y0 = int(std::lround(rect.y));
            const int x1 = int(std::lround(rect.x + rect.width)), y1 = int(std::lround(rect.y + rect.height));
            out.push_back({x0, y0, x1 - x0, y1 - y0});
        }
        return out;
    }

    void audio(std::span<float> stereo, int) override {
        const std::size_t n = std::min(stereo.size(), queue_.size());
        std::copy_n(queue_.begin(), n, stereo.begin());
        std::fill(stereo.begin() + std::ptrdiff_t(n), stereo.end(), 0.0f);
        queue_.erase(queue_.begin(), queue_.begin() + std::ptrdiff_t(n));
    }

    std::vector<std::uint8_t> write_state() override {
        if (!embed::hasCheckpoints(info_.title)) {
            throw std::runtime_error(std::string(info_.name) +
                                     " keeps its progress in its own save slots (save from the game's menu).");
        }
        const auto checkpoint = session_ ? session_->checkpoint() : std::nullopt;
        if (!checkpoint) {
            throw std::runtime_error("Quick save becomes available once the story reaches its first room.");
        }
        SavedCheckpoint record;
        record.game = info_.id;
        record.language = language_code();
        record.checkpoint = *checkpoint;
        state::Writer a;
        record.persist(a);
        return a.bytes;
    }

    // On a fresh module (before its first tick): continue from the saved checkpoint. A save that
    // cannot be continued throws here, before anything replaces the running game.
    void read_state(std::span<const std::uint8_t> bytes) override {
        SavedCheckpoint record;
        state::Reader a(bytes);
        record.persist(a);
        a.finish();
        if (record.magic != SavedCheckpoint{}.magic || record.version != 1 || record.game != info_.id) {
            throw std::runtime_error("This save belongs to another game or save version.");
        }
        if (session_) {
            throw std::runtime_error("The game is already running.");
        }
        const auto problem = embed::Session::checkCheckpoint(info_.title, pack_, progress_, record.checkpoint);
        if (!problem.empty()) {
            throw std::runtime_error(problem);
        }
        resume_ = std::move(record.checkpoint);
    }

    std::string saved_message() const override {
        return "Saved: Continue returns to the start of this room";
    }

    std::vector<app::GameOption> options() const override {
        std::vector<app::GameOption> out{
            app::screen_filter_option(vhs_),
            {"Touch areas",
             {"Off", "On"},
             touch_areas_ ? 1 : 0,
             "Outlines what a room offers to touch on the lower screen: text (blue), people to talk to "
             "(green), puzzles not solved yet (red) and hint coins not found yet (yellow). A help, not part "
             "of the original.",
             {},
             true},
        };
        if (!languages_.empty()) {
            app::GameOption language{"Language", {"As the system"}, language_, "", {}, false};
            for (const auto& [code, name] : languages_) {
                language.choices.push_back(name);
            }
            language.description =
                "The cartridge's language. As the system: the one the console would pick for this "
                "computer's language. A change applies when the game starts again (Restart).";
            out.push_back(std::move(language));
        }
        return out;
    }
    void set_option(std::size_t index, int value) override {
        if (index == 0) {
            vhs_ = std::clamp(value, 0, ui::vhs_levels - 1);
        } else if (index == 1) {
            touch_areas_ = value != 0;
        } else if (index == 2 && !languages_.empty()) {
            language_ = std::clamp(value, 0, int(languages_.size()));
        }
    }
    int vhs_level() const override { return vhs_; }

    std::string stopped() const override { return stopped_; }
    bool finished() const override { return left_; }

private:
    // The mean brightness of a frame (0..255).
    static double brightness(const std::vector<std::uint8_t>& rgba) {
        std::uint64_t sum = 0;
        for (std::size_t i = 0; i + 3 < rgba.size(); i += 4) {
            sum += rgba[i] + rgba[i + 1] + rgba[i + 2];
        }
        return rgba.empty() ? 0.0 : double(sum) / double(rgba.size() / 4 * 3);
    }

    std::array<int, 2> picture_size() const {
        const auto s = embed::screens(info_.title);
        return {std::max(s.top.width, s.bottom.width), s.top.height + s.bottom.height};
    }

    std::string language_code() const {
        if (languages_.empty()) {
            return {};
        }
        if (language_ > 0 && std::size_t(language_) <= languages_.size()) {
            return languages_[std::size_t(language_ - 1)].first;
        }
        return embed::systemLanguage(info_.title, pack_, preferred_locale());
    }

    void start() {
        try {
            embed::StartOptions options;
            options.language = language_code();
            options.checkpoint = std::move(resume_);
            resume_.reset();
            session_ = embed::Session::start(info_.title, pack_, progress_, options);
        } catch (const std::exception& e) {
            stopped_ = std::string("The game could not start: ") + e.what();
        }
    }

    // The "hold to blow" button (frame units), while the scene listens to the microphone.
    std::optional<embed::Rect> blow_button() const {
        if (!status_.listening) {
            return std::nullopt;
        }
        const auto& p = screen_.shown();
        const double scale = std::max(1.0, double(std::min(p.width, p.height)) / 400.0);
        const double w = 150 * scale, h = 36 * scale, margin = 16 * scale;
        return embed::Rect{p.width - margin - w, p.height - margin - h, w, h};
    }

    void upload(SDL_Renderer* r) {
        const auto s = embed::screens(info_.title);
        if (renderer_ != r) {
            SDL_DestroyTexture(top_);
            SDL_DestroyTexture(bottom_);
            top_ = bottom_ = nullptr;
            renderer_ = r;
        }
        auto make = [&](SDL_Texture*& t, const embed::Size& size) {
            if (!t) {
                t = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, size.width, size.height);
                if (!t) throw std::runtime_error(SDL_GetError());
                SDL_SetTextureScaleMode(t, SDL_SCALEMODE_NEAREST);
                uploaded_ = 0;
            }
        };
        make(top_, s.top);
        make(bottom_, s.bottom);
        if (uploaded_ == frames_ && frames_ != 0) {
            return;
        }
        const int row = std::max(s.top.width, s.bottom.width) * 4;
        SDL_UpdateTexture(top_, nullptr, pixels_.data(), row);
        SDL_UpdateTexture(bottom_, nullptr, pixels_.data() + std::size_t(row) * std::size_t(s.top.height), row);
        uploaded_ = frames_;
    }

    void draw_touch_areas(SDL_Renderer* r, const DualScreen::Placement& p) {
        // A light wash and a solid edge per area; puzzles and coins last, so they stay visible
        // where areas overlap (the standalone app's colours).
        const auto s = embed::screens(info_.title);
        const float scale = float(p.bottom.width) / float(s.bottom.width);
        const float edge = std::max(1.0f, std::round(scale / 2));
        const SDL_Color colours[] = {{70, 140, 235, 255}, {60, 190, 90, 255}, {230, 60, 50, 255}, {245, 200, 40, 255}};
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        for (const int kind : {0, 1, 3, 2}) {
            for (const auto& a : status_.areas) {
                if (a.kind != kind) continue;
                const auto c = colours[std::clamp(kind, 0, 3)];
                const float x = float(p.bottom.x) + float(a.x) * scale, y = float(p.bottom.y) + float(a.y) * scale;
                const float w = float(a.width) * scale, h = float(a.height) * scale;
                SDL_SetRenderDrawColor(r, c.r, c.g, c.b, 70);
                const SDL_FRect wash{x, y, w, h};
                SDL_RenderFillRect(r, &wash);
                SDL_SetRenderDrawColor(r, c.r, c.g, c.b, 255);
                const SDL_FRect edges[] = {{x, y, w, edge}, {x, y + h - edge, w, edge}, {x, y, edge, h}, {x + w - edge, y, edge, h}};
                SDL_RenderFillRects(r, edges, 4);
            }
        }
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    }

    void draw_blow_button(SDL_Renderer* r, const embed::Rect& b) {
        const SDL_FRect box{float(b.x), float(b.y), float(b.width), float(b.height)};
        SDL_SetRenderDrawColor(r, 40, 120, 160, 255);
        SDL_RenderFillRect(r, &box);
        const char* label = "Hold to blow";
        const float text = std::max(1.0f, std::floor(float(b.height) / 20.0f));
        float sx = 1, sy = 1;
        SDL_GetRenderScale(r, &sx, &sy);
        SDL_SetRenderScale(r, text, text);
        SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
        const float width = float(std::strlen(label)) * SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE * text;
        SDL_RenderDebugText(r, (box.x + (box.w - width) / 2) / text,
                            (box.y + (box.h - SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE * text) / 2) / text, label);
        SDL_SetRenderScale(r, sx, sy);
    }

    const Info& info_;
    fs::path pack_, progress_;
    DualScreen screen_;
    StylusRouter router_;
    std::vector<std::pair<std::string, std::string>> languages_;
    std::unique_ptr<embed::Session> session_;
    std::optional<std::vector<std::uint8_t>> resume_;
    std::vector<std::uint8_t> pixels_, checkpoint_pixels_;
    std::uint64_t frames_ = 0, uploaded_ = 0, checkpoint_generation_ = 0;
    int picture_wait_ = 0, picture_steady_ = 0; // finding the checkpoint's picture after a fade
    double checkpoint_brightness_ = 0;
    embed::Status status_;
    std::vector<float> sound_, queue_;
    std::array<int, 2> canvas_{};
    SDL_Renderer* renderer_{};
    SDL_Texture* top_{};
    SDL_Texture* bottom_{};
    std::string stopped_;
    bool left_ = false, start_held_ = false, touch_areas_ = false;
    int vhs_ = 0, language_ = 0;
};

// The import: the game's cartridge image, streamed into its pack (the importer's own folder,
// whose contents then become the installed files).
std::string import_into(const Info& info, platform::FileSource& file, const fs::path& folder,
                        const app::ImportProgress& progress) {
    ImageReader image(file);
    const auto title = embed::identify(image);
    if (!title || *title != info.title) {
        throw app::NotThisGame(title ? "a cartridge of another game of the collection"
                                     : "not a cartridge image of this collection");
    }
    const fs::path pack = folder / "pack";
    const std::string release = embed::importImage(info.title, image, pack, [&](double fraction) {
        if (progress) progress("Importing game assets", fraction);
    });
    for (const auto& entry : fs::directory_iterator(pack)) {
        fs::rename(entry.path(), folder / entry.path().filename());
    }
    fs::remove(pack);
    return release;
}

fs::path progress_folder(const fs::path& data, const Info& info) { return data / "progress" / info.id; }

// The standalone Picarat app's import and progress, taken over once (copied; Picarat's files
// are never changed). Its pack is taken only in the layout this version reads; progress only
// when Reprise has none for the game yet.
std::optional<app::LegacyCopy> legacy(const Info& info, const fs::path& data) {
    const fs::path root = embed::legacyDataDirectory();
    std::error_code error;
    if (root.empty() || !fs::is_directory(root, error)) {
        return std::nullopt;
    }
    app::LegacyCopy copy;
    const fs::path pack = root / embed::legacyPackFolder(info.title);
    if (fs::is_directory(pack, error)) {
        const auto problem = embed::checkPack(info.title, pack);
        if (problem.empty()) {
            copy.folder = pack;
            copy.folder_release = embed::packRelease(info.title, pack);
        } else {
            copy.notes.push_back(std::string("Picarat's import of ") + info.name + " was not taken over: " + problem);
        }
    }
    const fs::path progress = progress_folder(data, info);
    const auto files = embed::legacyProgress(info.title);
    if (!files.empty()) {
        if (fs::exists(progress, error)) {
            copy.notes.push_back(std::string(info.name) +
                                 ": Reprise's own saves were kept; Picarat's were not copied.");
        } else {
            for (const auto& [from, name] : files) {
                copy.copies.emplace_back(from, progress / name);
            }
        }
    }
    if (!copy.folder && copy.copies.empty() && copy.notes.empty()) {
        return std::nullopt;
    }
    return copy;
}

std::vector<app::Preview> previews(const Info& info, const fs::path& content) {
    std::vector<app::Preview> out;
    for (auto& picture : embed::previews(info.title, content)) {
        platform::Image image(picture.width, picture.height);
        std::memcpy(image.pixels.data(), picture.rgba.data(), std::min(picture.rgba.size(), image.pixels.size() * 4));
        out.push_back({std::move(picture.title), std::move(image)});
    }
    return out;
}
} // namespace

std::vector<app::GameEntry> entries() {
    std::vector<app::GameEntry> out;
    for (const auto& info : games) {
        app::GameEntry entry;
        entry.id = info.id;
        entry.title = info.name;
        entry.machine = info.machine;
        entry.media = info.media;
        entry.import_into = [&info](platform::FileSource& file, const fs::path& folder,
                                  const app::ImportProgress& progress) {
            return import_into(info, file, folder, progress);
        };
        entry.largest_image = std::uintmax_t(8) << 30;
        if (info.title == embed::Title::CuriousVillage || info.title == embed::Title::PandorasBox) {
            entry.previews = [&info](const fs::path& content) { return previews(info, content); };
        }
        if (embed::storyPlayable(info.title)) {
            entry.start = [&info](const fs::path& content) -> std::unique_ptr<app::GameModule> {
                return std::make_unique<Module>(info, content, progress_folder(content.parent_path().parent_path(), info));
            };
        }
        entry.legacy = [&info](const fs::path& data) { return legacy(info, data); };
        entry.progress = info.progress;
        if (embed::hasCheckpoints(info.title)) {
            entry.continue_note = "Continue returns to the start of the room you were in when you saved (the "
                                  "game's own checkpoint, not the exact moment); the picture shows that "
                                  "moment. The game's save slots are kept separately; Start opens the title.";
        } else if (embed::storyPlayable(info.title)) {
            entry.saves_note = "This game keeps its progress in its own three save slots: save from the "
                               "game's menu, then choose Start and continue from its title. Reprise quick "
                               "saves are not available for it yet.";
        }
        out.push_back(std::move(entry));
    }
    return out;
}

} // namespace layton_reprise
