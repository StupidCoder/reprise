#pragma once
#include "app/game_module.hpp"
#include "platform/host.hpp"
#include "platform/image.hpp"
#include <string>
#include <vector>

// The in-game pause menu: resume, restart, quick save and load, sound, the
// game's options (fixes and enhancements, and pages of grouped options such
// as cheats) and the way back to the launcher.
namespace app {

class PauseMenu {
public:
    enum class Action { none, resume, restart, save, load, quit };

    bool open() const { return open_; }
    void show(double now);
    void hide() { open_ = false; }
    void show_options() { options_page_ = true; }
    void show_group(std::string group) {
        options_page_ = true;
        group_ = std::move(group);
    }

    // Handles one frame of input; options are changed on the module directly.
    Action update(const platform::Controls&, GameModule&, double now);

    void set_message(std::string message, double now) {
        message_ = std::move(message);
        message_time_ = now;
    }
    bool muted() const { return muted_; }
    void toggle_muted() { muted_ = !muted_; }

    platform::Image draw(const GameModule&, const std::string& title, double now, const std::string& cover_id) const;

private:
    enum Item { resume_item, restart_item, save_item, load_item, sound_item, options_item, quit_item, items };

    // A row of an options page: an option (its index), a group's page, or
    // Back.
    struct OptionRow {
        int option{-1};
        std::string group;
    };
    std::vector<OptionRow> option_rows(const GameModule&) const;

    bool open_{}, options_page_{}, muted_{}, help_page_{};
    std::string group_; // the group whose page is shown ("": the options page)
    int selection_{}, option_selection_{};
    std::string message_;
    double message_time_{-10};
};

} // namespace app
