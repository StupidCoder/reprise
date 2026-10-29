#include "app/launcher.hpp"

#include "game/profile.hpp"
#include "importer/ipa_import.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace doc::app {
namespace {

using shell::Color;

constexpr Color kSkyTop{255, 222, 120}, kSkyBottom{250, 183, 64};
constexpr Color kPanel{255, 250, 236};
constexpr Color kInk{70, 46, 22};
constexpr Color kMuted{122, 92, 58};
constexpr Color kAccent{226, 92, 44};
constexpr Color kButton{255, 255, 255};
constexpr Color kDisabled{214, 204, 188};
constexpr Color kCheese{255, 206, 74}, kCheeseRind{236, 160, 40}, kHole{214, 150, 36};

constexpr const char* kProgressFile = "progress.txt";
constexpr double kResetArmSeconds = 4.0;

double now() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

constexpr const char* kImportHint =
    "Import the IPA of your own copy (version 1.0.5), or drop it on this window. The app copies "
    "the game's files into your user folder; nothing is downloaded or decrypted.";

// A cheese wedge, drawn from simple shapes (the launcher uses no game graphics).
void drawWedge(shell::Image& im, int x, int y, int w, int h) {
    for (int row = 0; row < h; ++row) {
        // Top face slopes up to the right; the rind is the lower band.
        const int width = w * (row + 1) / h;
        shell::rect(im, x + w - width, y + row, width, 1, row > h * 2 / 3 ? kCheeseRind : kCheese);
    }
    const int holes[][3] = {{w * 5 / 8, h / 2, h / 9}, {w * 3 / 4, h * 4 / 5, h / 12}, {w / 2, h * 3 / 4, h / 14}};
    for (const auto& hole : holes) {
        const int cx = x + hole[0], cy = y + hole[1], r = hole[2];
        for (int dy = -r; dy <= r; ++dy) {
            const int dx = int(std::sqrt(double(r * r - dy * dy)));
            shell::rect(im, cx - dx, cy + dy, 2 * dx + 1, 1, kHole);
        }
    }
}

}  // namespace

Launcher::Launcher(platform::Host& host, std::filesystem::path dataDirectory)
    : host_(host), dataDirectory_(std::move(dataDirectory)) {
    ready_ = importer::gameReady(dataDirectory_);
    readProgress();
}

void Launcher::readProgress() {
    game::Profile profile;
    savedLevels_ = profile.load(dataDirectory_ / kProgressFile) ? profile.completedLevels() : -1;
}

// The progress file is moved aside (one backup, replaced by the next reset); the next Play
// starts at level 1-1 on world 1's map.
void Launcher::resetProgress() {
    std::error_code ec;
    const std::filesystem::path file = dataDirectory_ / kProgressFile;
    std::filesystem::path backup = file;
    backup += ".bak";
    std::filesystem::remove(backup, ec);
    std::filesystem::rename(file, backup, ec);
    if (ec) {
        message_ = "Could not reset the progress: " + ec.message();
    } else {
        message_ = "Progress reset. Play starts from the first level.";
    }
    resetArmedUntil_ = 0;
    readProgress();
}

Launcher::~Launcher() {
    if (cancel_) cancel_->store(true);
    if (worker_.joinable()) worker_.join();
}

void Launcher::startImport(const std::filesystem::path& ipa) {
    if (importing_) return;
    if (worker_.joinable()) worker_.join();
    importing_ = true;
    finished_ = false;
    progress_ = 0;
    step_ = "Reading the archive";
    result_.clear();
    message_.clear();
    cancel_ = std::make_shared<std::atomic<bool>>(false);
    auto cancel = cancel_;
    worker_ = std::thread([this, ipa, cancel] {
        std::string error;
        const bool ok = importer::importIpa(
            ipa, dataDirectory_,
            [this](double fraction, const std::string& step) {
                std::lock_guard guard(mutex_);
                progress_ = fraction;
                step_ = step;
            },
            cancel.get(), error);
        std::lock_guard guard(mutex_);
        if (cancel->load()) result_ = "Import cancelled. Nothing was changed.";
        else if (!ok) result_ = "Import failed. " + error;
        else result_ = "Import complete. Press Play to start.";
        finished_ = true;
    });
}

void Launcher::finishImport() {
    if (worker_.joinable()) worker_.join();
    importing_ = false;
    message_ = result_;
    ready_ = importer::gameReady(dataDirectory_);
}

void Launcher::chooseIpa() { host_.chooseFile("iPhone app archive (.ipa)", "ipa"); }

Launcher::Action Launcher::handle(const platform::Event& e) {
    using platform::Event;
    switch (e.kind) {
    case Event::Quit: return Action::Quit;
    case Event::DropFile: startImport(e.file); break;
    case Event::PointerMove:
        hover_ = importButton_.contains(e.x, e.y)  ? 0
                 : playButton_.contains(e.x, e.y) ? 1
                 : resetButton_.contains(e.x, e.y) ? 2
                                                    : -1;
        break;
    case Event::PointerDown:
        if (importButton_.contains(e.x, e.y) && !importing_) {
            chooseIpa();
        } else if (resetButton_.contains(e.x, e.y) && !importing_ && savedLevels_ >= 0) {
            if (now() < resetArmedUntil_) {
                resetProgress();
            } else {
                resetArmedUntil_ = now() + kResetArmSeconds;
                message_.clear();
            }
        } else if (playButton_.contains(e.x, e.y)) {
            if (importing_) cancel_->store(true);
            else if (ready_) return Action::Play;
        }
        break;
    case Event::Key:
        if (e.key == Event::Return && !importing_) {
            if (ready_) return Action::Play;
            chooseIpa();
        } else if (e.key == Event::Escape) {
            if (importing_) cancel_->store(true);
            else return Action::Quit;
        }
        break;
    default: break;
    }
    return Action::None;
}

Launcher::Action Launcher::update() {
    if (auto choice = host_.takeFileChoice()) {
        if (choice->file) startImport(*choice->file);
        else if (!choice->error.empty()) message_ = choice->error;
    }
    bool done;
    {
        std::lock_guard guard(mutex_);
        done = importing_ && finished_;
    }
    if (done) finishImport();
    if (resetArmedUntil_ > 0 && now() >= resetArmedUntil_) resetArmedUntil_ = 0;
    return Action::None;
}

void Launcher::paint() {
    shell::Image& im = canvas_;
    // A warm gradient with a soft dot pattern.
    for (int y = 0; y < kHeight; ++y)
        shell::rect(im, 0, y, kWidth, 1, shell::mix(kSkyTop, kSkyBottom, y * 255 / (kHeight - 1)));
    for (int y = 6; y < kHeight; y += 16)
        for (int x = (y / 16) % 2 * 8 + 4; x < kWidth; x += 16) shell::blend_rect(im, x, y, 3, 3, Color{255, 255, 255}, 70);
    drawWedge(im, 300, 70, 150, 120);

    // The panel on the left.
    const int px = 18, py = 18, pw = 262, ph = 262;
    shell::blend_rect(im, px + 3, py + 4, pw, ph, Color{90, 50, 10}, 60);
    shell::rounded(im, px, py, pw, ph, 8, kPanel);
    int y = py + 14;
    shell::text(im, "DoubleOCheese", px + 14, y, 3, kInk, shell::Ink::shadow, Color{255, 214, 120});
    y += 34;
    y += shell::wrapped(im, "A cheese-stealing mouse game for iPhone, rebuilt natively from your own copy.", px + 14, y,
                        pw - 28, 3, 1, kMuted) * shell::line_rows + 10;

    std::string status;
    double progress = 0;
    {
        std::lock_guard guard(mutex_);
        progress = progress_;
        if (importing_) status = "Importing: " + step_ + "...";
    }
    const bool armed = resetArmedUntil_ > 0;
    if (status.empty() && armed) status = "Press Reset again to erase all saved progress: every level's best score, goals and unlocks.";
    if (status.empty() && !message_.empty()) status = message_;
    if (status.empty() && ready_) {
        status = "Ready. Press Play to start.";
        if (savedLevels_ > 0)
            status += " Saved progress: " + std::to_string(savedLevels_) + (savedLevels_ == 1 ? " level" : " levels") +
                      " completed.";
    }
    if (status.empty()) status = kImportHint;
    shell::wrapped(im, status, px + 14, y, pw - 28, 7, 1, kInk);
    if (importing_) {
        shell::rounded(im, px + 14, py + ph - 52, pw - 28, 6, 3, kDisabled);
        shell::rounded(im, px + 14, py + ph - 52, std::max(6, int((pw - 28) * std::clamp(progress, 0.0, 1.0))), 6, 3,
                       kAccent);
    }

    // Import (left), Play or Cancel (right).
    auto button = [&](const Button& b, const std::string& label, Color fill, Color ink, bool hot) {
        shell::blend_rect(im, b.x + 1, b.y + 2, b.w, b.h, Color{90, 50, 10}, 70);
        shell::rounded(im, b.x, b.y, b.w, b.h, 5, hot ? shell::mix(fill, Color{255, 255, 255}, 50) : fill);
        shell::text(im, label, b.x + (b.w - shell::text_width(label, 1)) / 2, b.y + (b.h - 7) / 2, 1, ink);
    };
    const int bw = (pw - 28 - 8) / 2;
    importButton_ = {px + 14, py + ph - 36, bw, 24};
    playButton_ = {px + 14 + bw + 8, py + ph - 36, bw, 24};
    button(importButton_, ready_ ? "Import again..." : "Import IPA...", importing_ ? kDisabled : kButton, kInk,
           hover_ == 0 && !importing_);
    // Reset progress: a full-width button above the others, only with saved progress.
    resetButton_ = {};
    if (!importing_ && savedLevels_ >= 0) {
        resetButton_ = {px + 14, py + ph - 64, pw - 28, 20};
        button(resetButton_, armed ? "Reset: press again to confirm" : "Reset progress",
               armed ? kAccent : kButton, armed ? Color{255, 255, 255} : kInk, hover_ == 2);
    }
    if (importing_) button(playButton_, "Cancel", kButton, kInk, hover_ == 1);
    else button(playButton_, "Play", ready_ ? kAccent : kDisabled, ready_ ? Color{255, 255, 255} : kMuted, hover_ == 1 && ready_);

    // The legal line along the bottom edge.
    shell::blend_rect(im, 0, kHeight - 16, kWidth, 16, Color{70, 40, 10}, 140);
    shell::text(im, "No game data is included. Imported files stay in your user folder.", 8, kHeight - 12, 1,
                Color{255, 255, 255});
}

void Launcher::draw() {
    paint();
    platform::Image image;
    image.width = kWidth;
    image.height = kHeight;
    image.rgba.resize(std::size_t(kWidth) * kHeight * 4);
    std::memcpy(image.rgba.data(), canvas_.pixels.data(), image.rgba.size());
    texture_ = host_.createTexture(image, true);
    host_.draw(texture_, 0, 0, kWidth, kHeight, 0, 0, kWidth, kHeight);
}

}  // namespace doc::app
