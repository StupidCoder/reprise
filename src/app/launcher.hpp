#pragma once
#include "app/library.hpp"
#include "app/quick_save.hpp"
#include "bruce_lee_c64/game/chapter.hpp"
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

    enum class Request { none, choose_image, play, continue_game };

    // One frame of input. Returns what the main loop should do for it.
    Request update(const platform::Controls& controls, double now);

    // Imports an image (picked or dropped) and reports the outcome on screen.
    // Imports dropped or chosen images in order (several for a multi-disk game).
    void import(const std::vector<std::filesystem::path>& images, double now);
    void show_error(std::string message, double now);
    void show_message(std::string message, bool ok, double now);
    // The selected game (the one to play after Request::play).
    std::size_t selected() const { return selected_; }

    platform::Image draw(double now) const;

    // For headless previews.
    void select(std::size_t game);
    void open_details(std::size_t page = 0);
    void refresh_save();
    bool reduced_motion() const { return reduced_motion_; }
    void open_documentation() { open_assets(); doc_navigation_ = true; }
    void open_lessons() {
        documentation_ = true;
        about_ = false;
        lessons_ = true;
    }
    void open_assets(std::size_t page = 0) {
        documentation_ = true;
        about_ = false;
        lessons_ = false;
        page_ = std::min(page, previews_.size());
    }
    void open_history() {
        documentation_ = true;
        about_ = true;
        lessons_ = false;
    }

private:
    Request update_input(const platform::Controls&, double now);
    platform::Image draw_scene(double now) const;
    void button(platform::Image&, std::string_view, int x, int y, int w, int h,
                bool primary = false, bool danger = false) const;
    double scroll_position(double now) const;
    std::optional<std::size_t> card_at(int x, int y, double now) const;
    std::array<int, 4> cover_bounds(double now) const;
    double scroll_from_{}, scroll_start_{-10};
    bool reduced_motion_{};
    int sidebar_row_{};
    std::optional<std::size_t> hovered_;
    std::optional<std::array<int, 2>> last_pointer_;
    std::vector<double> card_emphasis_;
    double motion_tick_{-1};
    double press_time_{-10};
    int press_x_{-1}, press_y_{-1};
    bool press_keyboard_{};
    mutable double draw_time_{};
    std::optional<platform::Image> transition_from_;
    double transition_start_{-10};
    enum class Transition { page, cover, document, asset };
    Transition transition_{Transition::page};
    std::array<int, 4> cover_from_{}, cover_to_{};
    mutable bool omit_cover_{};
    std::size_t import_game_{};
    double import_time_{-10};
    std::optional<platform::Image> previous_save_;
    mutable bool save_fade_pending_{};
    mutable double save_fade_start_{-10};
    void move(int columns, int rows);
    std::vector<std::size_t> visible_games() const;
    void set_filter(int filter);
    void reveal_selection();
    std::string status(std::size_t game) const;
    void activate();
    void close_details();
    void draw_card(platform::Image&, std::size_t game, int x, int y, double now) const;
    void draw_details(platform::Image&) const;
    // Removing a game's imported files asks first (focus 0 Cancel, 1 Remove).
    bool confirm_remove_{};
    int confirm_focus_{};
    Request update_confirm(const platform::Controls&, double now);
    void draw_confirm(platform::Image&) const;

    std::unique_ptr<Lesson> chapter_;
    bool lessons_{};
    bool about_{};
    int lesson_selected_{};
    ui::ScrollList lesson_list_;
    void open_lesson(double now);
    void draw_lessons(platform::Image&) const;
    Library& library_;
    std::size_t selected_{};
    bool details_{};
    bool documentation_{}, doc_navigation_{};
    int detail_focus_{};
    SavePreview save_preview_;
    std::string preview_game_;
    void draw_overview(platform::Image &) const;
    void draw_identity(platform::Image &) const;
    // Details pages: 0 lists the imported files, the rest show decoded data.
    std::vector<Preview> previews_;
    std::size_t page_{};
    ui::ScrollList files_; // page 0's list of imported files
    bool pending_choice_{};
    std::string message_;
    bool message_ok_{};
    double message_time_{-10};
    int filter_{}, first_row_{};
    bool sidebar_focus_{};
    std::string details_error_;
};

} // namespace app
