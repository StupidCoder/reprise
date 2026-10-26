#pragma once
#include "app/library.hpp"
#include "platform/host.hpp"
#include "platform/image.hpp"
#include "ui/scroll_list.hpp"
#include <filesystem>
#include <string>
#include <vector>

// The collector's shelf: filtered, scrollable covers and imported-content details.
// Cover illustrations are original UI assets, separate from imported game data.
namespace app {

class Launcher {
public:
    explicit Launcher(Library& library);

    enum class Request { none, choose_image, play };

    // One frame of input. Returns what the main loop should do for it.
    Request update(const platform::Controls& controls, double now);

    // Imports an image (picked or dropped) and reports the outcome on screen.
    void import(const std::filesystem::path& image, double now);
    void show_error(std::string message, double now);
    void show_message(std::string message, bool ok, double now);
    // The selected game (the one to play after Request::play).
    std::size_t selected() const { return selected_; }

    platform::Image draw(double now) const;

    // For headless previews.
    void select(std::size_t game);
    void open_details(std::size_t page = 0);

private:
    void move(int columns, int rows);
    std::vector<std::size_t> visible_games() const;
    void set_filter(int filter);
    void reveal_selection();
    std::string status(std::size_t game) const;
    void activate();
    void close_details();
    void draw_card(platform::Image&, std::size_t game, int x, int y, double now) const;
    void draw_details(platform::Image&) const;

    Library& library_;
    std::size_t selected_{};
    bool details_{};
    // Details pages: 0 lists the imported files, the rest show decoded data.
    std::vector<Preview> previews_;
    std::size_t page_{};
    ui::ScrollList files_; // page 0's list of imported files
    bool pending_choice_{}, pending_play_{};
    std::string message_;
    bool message_ok_{};
    double message_time_{-10};
    int filter_{}, first_row_{};
    bool sidebar_focus_{};
    std::string details_error_;
};

} // namespace app
