// The first screen: import the game from the user's IPA, then Play.
//
// DoubleOCheese ships without game data. The launcher imports the files from an IPA of the
// user's own copy (importer/), shows the progress and enables Play once a complete import is
// present. It is drawn with the shell's pixel font into a canvas; no operating-system controls.
#pragma once

#include "app/shell_style.hpp"
#include "platform/host.hpp"

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace doc::app {

class Launcher {
public:
    static constexpr int kWidth = 480;  // design size: the play area
    static constexpr int kHeight = 320;

    Launcher(platform::Host& host, std::filesystem::path dataDirectory);
    ~Launcher();

    enum class Action { None, Play, Quit };
    Action handle(const platform::Event& event);
    // Polls the file picker and the import thread.
    Action update();
    void draw();

    // Starts an import (the picker's choice, a dropped file or --import).
    void startImport(const std::filesystem::path& ipa);
    bool ready() const { return ready_; }
    // Reads the saved progress again (after playing).
    void readProgress();

private:
    struct Button {
        int x = 0, y = 0, w = 0, h = 0;
        bool contains(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
    };

    platform::Host& host_;
    std::filesystem::path dataDirectory_;
    bool ready_ = false;
    std::string message_;
    shell::Image canvas_{kWidth, kHeight};
    platform::Texture texture_;

    // The import runs on a worker thread; progress and the result come back under the mutex.
    std::thread worker_;
    std::shared_ptr<std::atomic<bool>> cancel_;
    std::mutex mutex_;
    bool importing_ = false;
    bool finished_ = false;
    double progress_ = 0;
    std::string step_;
    std::string result_;

    Button importButton_, playButton_, resetButton_;
    // Reset progress: the first press arms it (for a few seconds), the second deletes the
    // progress file (kept as progress.txt.bak). `savedLevels_` < 0: no progress saved.
    int savedLevels_ = -1;
    double resetArmedUntil_ = 0;
    void resetProgress();
    int hover_ = -1;

    void finishImport();
    void chooseIpa();
    void paint();
};

}  // namespace doc::app
