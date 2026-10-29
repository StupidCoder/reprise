#include "spy_mouse_embed.hpp"

#include "app/game_flow.hpp"
#include "data/game_files.hpp"
#include "game/float_mode.hpp"
#include "importer/ipa_import.hpp"
#include "platform/audio.hpp"
#include "platform/host.hpp"
#include "platform/image.hpp"
#include "data/level_table.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string_view>

namespace spy_mouse {
namespace {

using namespace doc;

class EmbeddedGame final : public Game {
public:
    EmbeddedGame(data::GameFiles files, const std::filesystem::path& progressFile)
        : files_(std::move(files)) {
        audio_.openPull();
        flow_ = std::make_unique<app::GameFlow>(files_, progressFile, audio_);
    }

    void touch(Touch kind, float x, float y) override {
        const auto k = kind == Touch::Down ? app::GameFlow::Touch::Down
                       : kind == Touch::Move ? app::GameFlow::Touch::Move
                                             : app::GameFlow::Touch::Up;
        flow_->touch({k, x, y});
    }

    void tick() override {
        // The original's arithmetic (denormals flushed), for the game's code only.
        const game::ScopedFlushToZero mode;
        if (!begun_) {
            begun_ = true;
            std::string error;
            if (!flow_->begin(error)) failure_ = error;
        }
        if (failure_.empty()) flow_->tick();
    }

    void draw(SDL_Renderer* renderer, SDL_Texture* target, int width, int height) override {
        if (!host_ || renderer != renderer_) {
            host_ = platform::Host::attach(renderer);
            renderer_ = renderer;
        }
        if (!host_) return;
        host_->beginFrameInto(target, width, height);
        flow_->draw(*host_);
        host_->endFrame();
    }

    // The mixer runs at 44.1 kHz (the game's files are decoded to it); the embedder's rate is
    // reached by linear interpolation between consecutive mixed frames.
    void audio(float* stereo, int frames, int rate) override {
        const double step = double(platform::Audio::kRate) / double(rate > 0 ? rate : platform::Audio::kRate);
        for (int f = 0; f < frames; ++f) {
            while (phase_ >= 1.0) {
                previous_[0] = next_[0];
                previous_[1] = next_[1];
                if (at_ >= mixed_.size()) {
                    mixed_.resize(512 * 2);
                    audio_.render(mixed_.data(), 512);
                    at_ = 0;
                }
                next_[0] = mixed_[at_];
                next_[1] = mixed_[at_ + 1];
                at_ += 2;
                phase_ -= 1.0;
            }
            const float t = float(phase_);
            stereo[f * 2] = previous_[0] + (next_[0] - previous_[0]) * t;
            stereo[f * 2 + 1] = previous_[1] + (next_[1] - previous_[1]) * t;
            phase_ += step;
        }
    }

    bool left() const override { return flow_->left(); }
    std::string failure() const override { return failure_.empty() ? flow_->failure() : failure_; }

    std::vector<std::uint8_t> saveState() const override {
        if (!begun_) return {};
        return flow_->saveState();
    }

    bool restore(std::span<const std::uint8_t> state, std::string& error) override {
        const game::ScopedFlushToZero mode;
        if (!flow_->restore(state, error)) return false;
        begun_ = true;
        return true;
    }

private:
    data::GameFiles files_;
    platform::Audio audio_;
    std::unique_ptr<app::GameFlow> flow_;
    bool begun_ = false;
    std::string failure_;
    std::unique_ptr<platform::Host> host_;
    SDL_Renderer* renderer_ = nullptr;
    std::vector<float> mixed_;
    std::size_t at_ = 0;
    double phase_ = 1.0;
    float previous_[2] = {0, 0}, next_[2] = {0, 0};
};

}  // namespace

bool looksLikeIpa(std::span<const std::uint8_t> bytes) { return importer::isAppArchive(bytes); }

std::vector<ImportedFile> importIpa(std::span<const std::uint8_t> ipa, std::string& release) {
    importer::ImportedFiles imported;
    std::string error;
    if (!importer::readIpa(ipa, imported, error)) {
        if (error.find("does not contain the game") != std::string::npos) throw NotSpyMouse(error);
        throw std::runtime_error(error);
    }
    std::vector<ImportedFile> files;
    files.reserve(imported.files.size());
    for (auto& [name, bytes] : imported.files) files.push_back({name, std::move(bytes)});
    release = "Spy Mouse 1.1.1 (iOS)";
    return files;
}

std::unique_ptr<Game> Game::start(const std::filesystem::path& content, const std::filesystem::path& progressFile) {
    std::string error;
    auto files = data::GameFiles::open(content, error);
    if (!files) throw std::runtime_error("The imported game cannot be opened: " + error);
    return std::make_unique<EmbeddedGame>(std::move(*files), progressFile);
}

std::vector<Picture> previews(const std::filesystem::path& content) {
    std::string error;
    auto files = data::GameFiles::open(content, error);
    if (!files) throw std::runtime_error(error);
    std::vector<Picture> out;
    auto add = [&](const std::string& title, const std::string& file) {
        auto bytes = files->readResource(file);
        if (!bytes) return;
        if (auto image = platform::decodeImage(*bytes)) out.push_back({title, image->width, image->height, std::move(image->rgba)});
    };
    add("Main menu", "ui/main_menu_bg.png");
    add("Spy Mouse logo", "ui/logo_squeek.png");
    // Level backgrounds (the map's own picture).
    if (auto table = data::LevelTable::load(*files, error))
        for (const char* number : {"1-2", "2-1", "3-1", "4-1"})
            if (const data::LevelInfo* level = table->byNumber(number)) {
                const std::string folder = "map" + std::to_string(level->map) + "/";
                const std::size_t before = out.size();
                add("Level " + std::string(number), folder + "map" + std::to_string(level->map) + ".jpg");
                if (out.size() == before) add("Level " + std::string(number), folder + "map.jpg");
            }
    add("The mouse running", "sprites/player_run.png");
    add("The brown cat", "sprites/browncat01.png");
    return out;
}

std::filesystem::path legacyDataDirectory() { return platform::Host::dataDirectory(); }

std::vector<ImportedFile> readImportedFolder(const std::filesystem::path& folder, std::string& release) {
    std::string error;
    if (!data::GameFiles::open(folder, error)) throw std::runtime_error(error);
    std::vector<ImportedFile> files;
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(folder, ec); !ec && it != std::filesystem::recursive_directory_iterator();
         it.increment(ec)) {
        if (!it->is_regular_file()) continue;
        const std::string name = std::filesystem::relative(it->path(), folder).generic_string();
        if (name != "SpyMouse" && name.rfind("res/", 0) != 0) continue;  // (the import marker stays)
        auto bytes = data::readWholeFile(it->path());
        if (!bytes) throw std::runtime_error("Cannot read " + it->path().string() + ".");
        files.push_back({name, std::move(*bytes)});
    }
    if (ec) throw std::runtime_error("Cannot read " + folder.string() + ": " + ec.message());
    release = "Spy Mouse 1.1.1 (iOS)";
    return files;
}

}  // namespace spy_mouse
