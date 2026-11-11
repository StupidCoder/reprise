#pragma once
#include "app/launcher.hpp"
#include <filesystem>
#include <future>
#include <mutex>
#include "gameplay.hpp"

// The XR frontend adapts coordinates and presentation, not launcher behavior.
class LauncherPanel {
public:
    explicit LauncherPanel(const std::filesystem::path& data);
    void select(const std::string& id);
    app::Launcher::Request update(const platform::Controls&, double now, bool focused = true);
    platform::Image draw(double now, bool omit_cover) const;
    bool playing() const { return gameplay_.active(); }
    bool separate_cover() const { return !playing() && !choosing_ && !import_work_.valid(); }
    bool open() const { return launcher_.details_open(); }
    bool reduced_motion() const { return launcher_.reduced_motion(); }
    const app::GameEntry& selected() const { return library_.games().at(launcher_.selected()); }
    std::filesystem::path content() const { return library_.content_directory(launcher_.selected()); }
    app::Library& library() { return library_; }
    app::Launcher& launcher() { return launcher_; }
private:
    app::Library library_;
    app::Launcher launcher_;
    mutable Gameplay gameplay_;
    bool choosing_{};
    std::vector<std::filesystem::path> choices_;
    int first_{};
    mutable std::mutex progress_mutex_;
    std::string phase_;
    double fraction_=-1;
    // Declared last: joins before fields used by the worker are destroyed.
    std::future<app::Library::Outcome> import_work_;
    void choose_import(double now);
    void begin_import(const std::filesystem::path& path);

};
