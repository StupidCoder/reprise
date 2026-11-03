#include "app/launcher.hpp"
#include "app/catalog.hpp"
#include "app/feature_badges.hpp"
#include "fa_18_interceptor_amiga/lessons/technical_lessons.hpp"
#include "stunt_car_racer_amiga/lessons/technical_article.hpp"
#include "mindwalker_amiga/game/player_guide.hpp"
#include "bruce_lee_c64/game/room_chapter.hpp"
#include "bruce_lee_c64/game/background_article.hpp"
#include "ui/shelf.hpp"
#include <algorithm>
#include <iterator>
#include <span>
#include "marble_madness_amiga/game/mechanics_chapter.hpp"
#include <exception>
#include <cmath>
#include <fstream>
#include <numbers>
#include <tuple>
#include "platform/files.hpp"

namespace app {
using platform::Image;
namespace s = ui::shelf;
namespace {
// Three columns and two fully visible rows in the 960 x 640 logical canvas. All input and drawing
// use these same bounds; selection brings its row into view, including on pads.
constexpr int card_w = 234, card_h = 198, gap = 14;
constexpr int file_list_y = 182, file_row = 25, file_rows_shown = 11;
// The details page's left column below Play: Lessons (for games that have
// them), then Remove imported files at the bottom for every game.
constexpr int lessons_y = 462, side_button_h = 40;
// The remove confirmation.
constexpr int dialog_x = 270, dialog_y = 220, dialog_w = 420, dialog_h = 196;
constexpr int cancel_x = 450, confirm_x = 566, dialog_button_y = 362, dialog_button_w = 104, dialog_button_h = 36;
struct LessonEntry { const char* title; const char* summary; const char* format; };
constexpr LessonEntry lessons[] = {
    {"Behind the enemy decisions", "Inspect the Ninja and Yamo: facing, distance, movement and attack choices.", "Live experiment"},
    {"A floor opens", "Discover the progress conditions, step through the opening, and read how room logic meets data.", "Experiment + illustrated article"},
    {"Building a background", "Peel apart a room: shared tiles, compressed runs, color and measured storage savings.", "Illustrated article"},
};
constexpr LessonEntry marble_lessons[] = {
    {"A marble in a painted world", "Explore the height field, rolling, collisions and the masks that hide a sprite behind a wall.", "Illustrated explanation + live laboratory"},
    {"Inside the black marble", "Follow imported routes, inspect patrol and chase decisions, and experiment with momentum.", "Decision diagram + live laboratory"},
    {"How Silly Race bends the rules", "Reverse the slope response, chase the critters and inspect the course-specific surprises.", "Illustrated explanation + live laboratory"},
};
constexpr LessonEntry interceptor_lessons[] = {
    {"Painting the Hornet", "Walk the imported draw tree and build the aircraft one polygon list at a time.", "Illustrated article + experiment"},
    {"The roads that start as zeros", "Follow stored endpoints into the road corners generated at startup.", "Illustrated article + experiment"},
    {"What makes the Hornet fly?", "Explore input ramps, flight tables and motion with isolated physics runs.", "Illustrated article + experiment"},
    {"Landing the wrong way up", "Compare upright and inverted contact: height, sink rate, gear and carrier rules.", "Illustrated article + experiment"},
};
constexpr LessonEntry stunt_lessons[] = {
    {"A world measured in angles", "How bearings, elevation and roll turn the imported road into a cockpit view.", "Illustrated article"},
    {"A car balanced on three springs", "Road sampling, suspension, grip and the forces behind a convincing landing.", "Illustrated article"},
    {"A rival built for the road", "Track speed targets, passing decisions and the suspension behind the computer car.", "Illustrated article"},
};
constexpr LessonEntry mindwalker_guide[] = {
    {"The whole journey", "How the landscape, tunnel, arena and picture puzzle fit together.",
     "Player guide"},
    {"1 / The landscape",
     "Connect the trail, activate a beacon, and escape a view held by kite-like objects.", "Player guide"},
    {"When the trail stops", "Surface, connection and the 64 x 64 region: why walking may stop making marks.",
     "Player guide"},
    {"2 / The tunnel", "Catch a growing opening to reach the arena.", "Player guide"},
    {"3 / The arena", "Retrieve the target, watch the wall pulse, and find your way back.", "Player guide"},
    {"4 / The psyche", "Assemble the moving picture, seven tiles at a time, and use timed hints.",
     "Player guide"},
};
std::span<const LessonEntry> lessons_for(const GameEntry& game) {
    if (game.id == "mindwalker_amiga")
        return mindwalker_guide;
    if(game.id=="bruce_lee_c64") return lessons;
    if(game.id=="marble_madness_amiga") return marble_lessons;
    if(game.id=="stunt_car_racer_amiga") return stunt_lessons;
    if(game.id=="fa_18_interceptor_amiga") return interceptor_lessons;
    return {};
}
constexpr const char* filters[] = {"All games", "Continue playing", "Commodore classics", "Sonic collection", "Layton collection"};
constexpr int filter_y(int i) { return i < 2 ? 90 + i * 44 : 240 + (i - 2) * 48; }
constexpr Collection filter_collection(int i) {
    return i == 2 ? Collection::commodore : i == 3 ? Collection::sonic : i == 4 ? Collection::layton : Collection::none;
}
constexpr int filter_count = int(std::size(filters));
bool hit(const platform::Controls& in, int x, int y, int w, int h) {
    return in.click && in.mouse_x >= x && in.mouse_x < x + w && in.mouse_y >= y && in.mouse_y < y + h;
}
std::string size_text(std::uintmax_t bytes) {
    std::string digits = std::to_string(bytes);
    for (int at = int(digits.size()) - 3; at > 0; at -= 3) digits.insert(std::size_t(at), ",");
    return digits + " bytes";
}
}

Launcher::Launcher(Library& library) : library_(library) {
    for (std::size_t i = 0; i < library_.games().size(); ++i) {
        if (library_.installed(i)) { selected_ = i; break; }
    }
    reveal_selection();
    card_emphasis_.resize(library_.games().size());
    continue_available_.resize(library_.games().size());
    continue_saved_at_.resize(library_.games().size());
    std::ifstream settings(library_.data_directory() / "options" / "launcher.txt");
    std::string setting;
    while (std::getline(settings, setting))
        if (setting == "Reduced motion = On") reduced_motion_ = true;
}

void Launcher::set_canvas(int width, int height) {
    const bool portrait = width > 0 && height > width;
    const int logical_height = portrait ? std::clamp(int(640.0 * height / width), 800, 1600) : 640;
    const int logical_width = portrait ? 640 : std::max(640, int(640.0 * std::max(width, 1) / std::max(height, 1)));
    if (portrait_ == portrait && shelf_height_ == logical_height && shelf_width_ == logical_width) return;
    dragging_ = false; asset_contacts_.clear();
    asset_zoom_ = 1; asset_pan_x_ = asset_pan_y_ = 0;
    portrait_ = portrait;
    shelf_height_ = logical_height;
    shelf_width_ = logical_width;
    if (chapter_) chapter_->set_canvas(shelf_width_, shelf_height_);
    columns_ = portrait ? 2 : std::max(1, (shelf_width_ - 198 - 24 + gap) / (card_w + gap));
    manual_scroll_.reset();
    grid_x = portrait ? 79 : shelf_width_ == 960 ? 198 :
        std::max(198, 184 + (shelf_width_ - 184 - (columns_ * (card_w + gap) - gap)) / 2);
    grid_y = portrait ? 234 : 126;
    footer_y = portrait ? shelf_height_ - 82 : 558;
    rows_shown_ = std::max(1, (footer_y - grid_y) / (card_h + gap));
    reveal_selection();
    scroll_from_ = first_row_; scroll_start_ = -10;
    transition_from_.reset(); hovered_.reset(); last_pointer_.reset(); shelf_press_.reset();
    document_press_.reset(); document_scroll_ = 0;
}

Theme Launcher::theme() const { return theme_for(filter_collection(filter_)); }

std::vector<std::size_t> Launcher::visible_games() const {
    std::vector<std::size_t> result;
    const auto& games = library_.games();
    for (std::size_t i = 0; i < games.size(); ++i) {
        if (filter_ == 1 && (!continue_available_[i] || !library_.installed(i))) continue;
        if (filter_ >= 2 && games[i].collection != filter_collection(filter_)) continue;
        result.push_back(i);
    }
    std::stable_sort(result.begin(), result.end(), [&](auto a, auto b) {
        if (filter_ == 1 && continue_saved_at_[a] != continue_saved_at_[b])
            return continue_saved_at_[a] > continue_saved_at_[b];
        if (filter_ == 3) {
            const auto rank = [](std::string_view id) {
                constexpr std::string_view order[] = {"sonic_gg", "sonic2_gg", "triple_trouble_gg", "sonic_advance_gba"};
                const auto found = std::find(std::begin(order), std::end(order), id);
                return int(found - std::begin(order));
            };
            if (rank(games[a].id) != rank(games[b].id)) return rank(games[a].id) < rank(games[b].id);
        }
        if (games[a].release_year != games[b].release_year)
            return (games[a].release_year ? games[a].release_year : 9999) < (games[b].release_year ? games[b].release_year : 9999);
        return games[a].title < games[b].title;
    });
    return result;
}
void Launcher::reveal_selection() {
    const auto games = visible_games();
    const auto at = std::find(games.begin(), games.end(), selected_);
    if (at == games.end()) return;
    const int row = int(at - games.begin()) / columns_;
    first_row_ = std::clamp(first_row_, std::max(0, row - rows_shown_ + 1), row);
    first_row_ = std::min(first_row_, std::max(0, (int(games.size()) + columns_ - 1) / columns_ - rows_shown_));
}
void Launcher::select(std::size_t game) {
    if (library_.games().empty()) return;
    transition_from_.reset();
    manual_scroll_.reset();
    hovered_.reset();
    if (selected_ != game) { save_preview_ = {}; previous_save_.reset(); }
    selected_ = std::min(game, library_.games().size() - 1);
    filter_ = 0;
    sidebar_focus_ = false;
    reveal_selection();
    scroll_from_ = first_row_; scroll_start_ = -10;
}
void Launcher::set_filter(int filter) {
    filter_ = std::clamp(filter, 0, filter_count - 1);
    first_row_ = 0;
    manual_scroll_.reset();
    if (filter_ == 1) {
        for (std::size_t i = 0; i < library_.games().size(); ++i) {
            const auto save = library_.installed(i) && library_.games()[i].start
                ? save_preview(library_.content_directory(i), library_.games()[i].id) : SavePreview{};
            continue_available_[i] = save.available;
            continue_saved_at_[i] = save.saved_at;
        }
    }
    const auto games = visible_games();
    if (!games.empty() && (filter_ == 1 || std::find(games.begin(), games.end(), selected_) == games.end())) selected_ = games.front();
    reveal_selection();
}
void Launcher::move(int columns, int rows) {
    const auto games = visible_games();
    if (games.empty()) {
        if (columns < 0) { sidebar_focus_ = true; sidebar_row_ = filter_; }
        return;
    }
    int at = int(std::find(games.begin(), games.end(), selected_) - games.begin());
    if (at >= int(games.size())) at = 0;
    if (columns < 0 && at % columns_ == 0) { sidebar_focus_ = true; sidebar_row_ = filter_; return; }
    if (rows) {
        const int target_row = std::clamp(at / columns_ + rows, 0, (int(games.size()) - 1) / columns_);
        at = std::min(target_row * columns_ + at % columns_, int(games.size()) - 1);
    }
    else if (columns && (columns < 0 || at % columns_ < columns_ - 1)) at = std::clamp(at + columns, 0, int(games.size()) - 1);
    selected_ = games[std::size_t(at)];
    reveal_selection();
}
std::string Launcher::status(std::size_t index) const {
    const auto& game = library_.games()[index];
    if (game.integration_pending) return "Coming to Reprise";
    if (!importable(game)) return "Planned";
    if (!library_.installed(index)) {
        const auto parts = library_.imported_parts(index);
        return parts.empty() ? "Import your copy" : "Imported " + parts;
    }
    return game.start ? "Ready to play" : "Data viewer";
}
void Launcher::open_details(std::size_t page) {
    transition_from_.reset();
    details_ = true;
    documentation_ = page > 0;
    doc_navigation_ = false;
    refresh_save();
    detail_focus_ = save_preview_.available ? 1 : 0;
    details_error_.clear();
    stop_sound();
    model_preview_ = nullptr;
    animated_model_.reset();
    previews_.clear();
    const auto& game = library_.games()[selected_];
    if (library_.installed(selected_) && game.previews) {
        try { previews_ = game.previews(library_.content_directory(selected_)); }
        catch (const std::exception& e) { details_error_ = std::string("Could not decode the imported data: ") + e.what(); }
    }
    lessons_ = false;
    about_ = page == 0;
    lesson_list_.resize(int(lessons_for(game).size()),3);
    lesson_list_.reset(); lesson_selected_=0;
    page_ = std::min(page, previews_.size());
    files_.resize(int(library_.installed_files(selected_).size()), file_rows_shown);
    files_.reset();
}
void Launcher::refresh_save() {
    const auto& id = library_.games()[selected_].id;
    if (preview_game_ != id) { save_preview_ = {}; previous_save_.reset(); }
    preview_game_ = id;
    auto next = app::save_preview(library_.content_directory(selected_), library_.games()[selected_].id);
    // Compare pixels too: two saves can have the same displayed date.
    const bool changed = next.image && (!save_preview_.image ||
        next.image->width != save_preview_.image->width || next.image->height != save_preview_.image->height ||
        next.image->pixels.size() != save_preview_.image->pixels.size() ||
        !std::equal(next.image->pixels.begin(), next.image->pixels.end(), save_preview_.image->pixels.begin(),
            [](auto a, auto b) { return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a; }));
    previous_save_ = changed ? std::move(save_preview_.image) : std::nullopt;
    save_fade_pending_ = changed;
    save_preview_ = std::move(next);
    continue_available_[selected_] = save_preview_.available && library_.installed(selected_) && bool(library_.games()[selected_].start);
    continue_saved_at_[selected_] = save_preview_.saved_at;
    if (filter_ == 1) { reveal_selection(); scroll_from_ = first_row_; scroll_start_ = -10; }
}
void Launcher::close_details() { details_ = false; confirm_remove_ = false; stop_sound(); previews_.clear(); animated_model_.reset(); model_preview_=nullptr; }
void Launcher::activate() {
    open_details();
}
Launcher::Request Launcher::update_input(const platform::Controls& in, double now) {
    if (chapter_) {
        if (in.back || hit(in,24,20,104,34)) chapter_.reset();
        else chapter_->update(in,now);
        return Request::none;
    }
    if (details_ && confirm_remove_) return update_confirm(in, now);
    if (details_) {
        const auto &entry = library_.games()[selected_];
        const bool back = in.back || hit(in, 24, 22, 104, 36);
        if (!documentation_) {
            if (back) {
                close_details();
                return Request::none;
            }
            detail_focus_ = std::clamp(detail_focus_ + int(in.nav_down) - int(in.nav_up), 0, 3);
            if (in.nav_left)
                detail_focus_ = 0;
            if (in.nav_right)
                detail_focus_ = 1;
            int action = -1;
            if (hit(in, 24, 374, 114, 40))
                action = 0;
            if (hit(in, 150, 374, 114, 40))
                action = 1;
            if (hit(in, 24, 434, 240, 40))
                action = 2;
            if (hit(in, 24, 494, 240, 34) || in.remove)
                action = 3;
            if (in.accept)
                action = detail_focus_;
            if (action == 0 && importable(entry)) {
                if (!library_.installed(selected_))
                    return Request::choose_image;
                if (entry.start)
                    return Request::play;
                if (entry.previews) { open_assets(previews_.empty() ? 0 : 1); doc_navigation_=false; }
            }
            if (action == 1 && save_preview_.available && library_.installed(selected_) && entry.start)
                return Request::continue_game;
            if (action == 2)
                open_documentation();
            if (action == 3 && library_.installed(selected_)) {
                confirm_remove_ = true;
                confirm_focus_ = 0;
            }
            return Request::none;
        }
        if (back) {
            documentation_ = false;
            detail_focus_ = 2;
            return Request::none;
        }
        if (!doc_navigation_ && in.nav_left && (about_ || lessons_ || page_ == 0)) {
            doc_navigation_ = true;
            return Request::none;
        }
        if (in.click && in.mouse_x < 288) doc_navigation_ = true;
        if (doc_navigation_) {
            int selected = lessons_ ? 2 : about_ ? 1 : 0;
            selected = std::clamp(selected + int(in.nav_down) - int(in.nav_up), 0,
                                  lessons_for(entry).empty() ? 1 : 2);
            if (selected == 0) open_assets(page_);
            else if (selected == 1) open_history();
            else open_lessons();
            if (in.accept || in.nav_right || (in.click && in.mouse_x >= 288))
                doc_navigation_ = false;
            else if (!in.click) return Request::none;
            if (in.accept || in.nav_right) return Request::none;
        }
        if (hit(in, 24, 418, 240, 40))
            open_history();
        if (hit(in, 24, 374, 240, 40))
            open_assets();
        if (!lessons_for(entry).empty() && hit(in, 24, lessons_y, 240, side_button_h))
            open_lessons();
        if (lessons_) {
            const int delta =
                int(in.nav_down) - int(in.nav_up) + in.wheel + (int(in.page_down) - int(in.page_up)) * 3;
            lesson_selected_ = std::clamp(lesson_selected_ + delta, 0, int(lessons_for(entry).size()) - 1);
            if(lesson_selected_<lesson_list_.first())lesson_list_.scroll(lesson_selected_-lesson_list_.first());
            if(lesson_selected_>=lesson_list_.first()+3)lesson_list_.scroll(lesson_selected_-lesson_list_.first()-2);
            for (int row = 0; row < lesson_list_.shown(); ++row)
                if (hit(in, 308, 178 + row * 106, 600 + (portrait_ ? 0 : detail_width() - 960), 96)) {
                    lesson_selected_ = lesson_list_.first() + row;
                    open_lesson(now);
                    return Request::none;
                }
            if (in.accept)
                open_lesson(now);
        } else if (!about_) {
            const auto pages = previews_.size() + 1;
            if (in.nav_right || hit(in, 858 + (portrait_ ? 0 : detail_width() - 960), 112, 54, 34))
                page_ = (page_ + 1) % pages;
            else if (in.nav_left || hit(in, 794 + (portrait_ ? 0 : detail_width() - 960), 112, 54, 34))
                page_ = (page_ + pages - 1) % pages;
            else if (page_ == 0 && files_.scrollable()) {
                files_.scroll(int(in.nav_down) - int(in.nav_up) + in.wheel);
                files_.page(int(in.page_down) - int(in.page_up));
            } else if (in.nav_down)
                page_ = (page_ + 1) % pages;
            else if (in.nav_up)
                page_ = (page_ + pages - 1) % pages;
        }
    } else {
        if (sidebar_focus_) {
            if (in.nav_up || in.nav_down) {
                sidebar_row_ = (sidebar_row_ + filter_count + int(in.nav_down) - int(in.nav_up)) % filter_count;
                set_filter(sidebar_row_);
            }
            if (in.nav_right || in.accept || in.back) sidebar_focus_ = false;
        } else {
            if (in.nav_left) move(-1, 0);
            if (in.nav_right) move(1, 0);
            if (in.nav_up) move(0, -1);
            if (in.nav_down) move(0, 1);
            if (in.page_down) move(0, 2);
            if (in.page_up) move(0, -2);
            if (in.accept && !visible_games().empty()) activate();
        }
        for (int i = 0; i < filter_count; ++i) if (hit(in, portrait_ ? 22 + (i % 3) * 200 : 18, portrait_ ? 82 + (i / 3) * 44 : filter_y(i), portrait_ ? 192 : 160, 38)) {
            set_filter(i); sidebar_row_ = i; sidebar_focus_ = true;
        }
        auto games = visible_games();
        if (in.wheel && !games.empty()) {
            const int rows = (int(games.size()) + columns_ - 1) / columns_;
            first_row_ = std::clamp(first_row_ + in.wheel, 0, std::max(0, rows - rows_shown_));
            const int at = int(std::find(games.begin(), games.end(), selected_) - games.begin());
            selected_ = games[std::size_t(std::clamp(at, first_row_ * columns_, std::min(int(games.size()) - 1, first_row_ * columns_ + columns_ * rows_shown_ - 1)))];
            sidebar_focus_ = false;
        }
        if (in.click) {
            if (const auto game = card_at(in.mouse_x, in.mouse_y, now)) {
                selected_ = *game; sidebar_focus_ = false;
                activate();
            }
        }
        if (hit(in, portrait_ ? 480 : shelf_width_ - 162, 20, 138, 36)) pending_choice_ = true;
        if (hit(in, portrait_ ? 506 : shelf_width_ - 136, portrait_ ? footer_y + 20 : 578, 112, 38) && !visible_games().empty()) open_details();
    }
    if (pending_choice_) { pending_choice_ = false; return Request::choose_image; }
    return Request::none;
}
Launcher::Request Launcher::update_confirm(const platform::Controls& in, double now) {
    const int cancel_x = (detail_width() - dialog_w) / 2 + 180;
    const int confirm_x = cancel_x + 116;
    const int dialog_button_y = 220 + (detail_height() - 640) / 2 + 142;
    if (in.nav_left || in.nav_right) confirm_focus_ = 1 - confirm_focus_;
    const bool cancel = in.back || hit(in, cancel_x, dialog_button_y, dialog_button_w, dialog_button_h) ||
                        (in.accept && confirm_focus_ == 0);
    const bool confirm = hit(in, confirm_x, dialog_button_y, dialog_button_w, dialog_button_h) ||
                         (in.accept && confirm_focus_ == 1);
    if (cancel) {
        confirm_remove_ = false;
    } else if (confirm) {
        confirm_remove_ = false;
        library_.remove(selected_);
        close_details();
        show_message("Imported files removed. You can import your copy again.", true, now);
    }
    return Request::none;
}
void Launcher::draw_confirm(Image& im) const {
    const int dialog_x = (im.width - dialog_w) / 2, dialog_y = 220 + (im.height - 640) / 2;
    const int cancel_x = dialog_x + 180, confirm_x = dialog_x + 296, dialog_button_y = dialog_y + 142;
    const auto& game = library_.games()[selected_];
    ui::blend_rect(im, 0, 0, im.width, im.height, theme().ink, 140);
    ui::rounded(im, dialog_x, dialog_y, dialog_w, dialog_h, 10, theme().card);
    s::text(im, "Remove imported files?", dialog_x + 24, dialog_y + 20, 21, theme().ink, true);
    s::wrap(im,
            "Reprise deletes the files it extracted from your copy of " + game.title +
                ". Your original image and persistent quick save are kept.",
            dialog_x + 24, dialog_y + 58, dialog_w - 48, 3, 15, theme().muted);
    for (int i = 0; i < 2; ++i) {
        if (confirm_focus_ == i) {
            ui::rounded(im, (i == 0 ? cancel_x : confirm_x) - 3, dialog_button_y - 3, dialog_button_w + 6,
                        dialog_button_h + 6, 9, theme().accent);
        }
    }
    button(im, "Cancel", cancel_x, dialog_button_y, dialog_button_w, dialog_button_h);
    button(im, "Remove", confirm_x, dialog_button_y, dialog_button_w, dialog_button_h, true, true);
}
void Launcher::show_message(std::string message, bool ok, double now) {
    message_ = std::move(message); message_ok_ = ok; message_time_ = now;
}
void Launcher::import(const std::vector<std::filesystem::path>& images, double now) {
    const auto& games = library_.games();
    for (const auto& image : images) {
        const auto outcome = library_.import(image, importable(games[selected_]) ? std::optional(selected_) : std::nullopt);
        imported(outcome, now);
        if (!outcome.ok) break;
    }
}
void Launcher::imported(const Library::Outcome& outcome, double now) {
    if (outcome.game) {
        close_details(); select(*outcome.game);
        if (outcome.ok && !outcome.waiting) { import_game_ = *outcome.game; import_time_ = now; }
    }
    show_message(outcome.message, outcome.ok, now);
}
void Launcher::show_error(std::string message, double now) { show_message(std::move(message), false, now); }

void Launcher::draw_card(Image& im, std::size_t index, int x, int y, double now) const {
    const auto& game = library_.games()[index];
    const bool imported = library_.installed(index);
    const bool focused = index == selected_ && !sidebar_focus_;
    const double emphasis = reduced_motion_ ? double(focused || hovered_ == index) : card_emphasis_[index];
    y -= int(std::lround(4 * emphasis));
    const double arrival = std::clamp((now - import_time_) / .32, 0.0, 1.0);
    if (!reduced_motion_ && index == import_game_) y -= int(std::lround(12 * (1 - arrival) * (1 - arrival)));
    for (int spread = 3; spread >= 1; --spread)
        ui::rounded(im, x - spread, y + 3 + spread, card_w + spread * 2, card_h, 9,
                    ui::mix(theme().paper, theme().muted, int(emphasis * (14 + (3 - spread) * 9))));
    ui::rounded(im, x - 3, y - 3, card_w + 6, card_h + 6, 9,
                ui::mix(theme().line, theme().accent, int(emphasis * 255)));
    ui::rounded(im, x, y, card_w, card_h, 7, theme().card);
    if (!(omit_cover_ && index == selected_)) {
        s::cover(im, game.id, x + 6, y + 6, card_w - 12, 132);
        if (!imported) {
            // An unimported cover reads like a printed catalog sample. Keep the
            // title at full contrast and state availability in words as well.
            for (int yy = std::max(0, y + 6); yy < std::min(im.height, y + 138); ++yy)
                for (int xx = std::max(0, x + 6); xx < std::min(im.width, x + card_w - 6); ++xx) {
                    auto& c = im.pixels[std::size_t(yy * im.width + xx)];
                    const auto gray = std::uint8_t((int(c.r) * 54 + int(c.g) * 183 + int(c.b) * 19) / 256);
                    c = ui::mix(c, {gray, gray, gray}, 185);
                    c = ui::mix(c, theme().paper, 45);
                }
            const char* label = game.integration_pending ? "Coming to Reprise" : importable(game) ? "Not imported" : "Planned";
            const int w = s::width(label, 12, true) + 18;
            ui::rounded(im, x + 12, y + 108, w, 24, 4, theme().card);
            s::text(im, label, x + 21, y + 111, 12, theme().ink, true);
        }
    }
    s::clipped(im, game.title, x + 10, y + 143, card_w - 20, 16, theme().ink, true);
    s::text(im, (game.release_year ? std::to_string(game.release_year) + "  /  " : "") + std::string(machine_name(game.machine)), x + 10, y + 165, 12, theme().muted);
    if (!imported && importable(game)) s::text(im, "Import your copy to play", x + 10, y + 181, 10, theme().muted);
    if (index == import_game_ && now >= import_time_ && now - import_time_ < .8) {
        const int glow = reduced_motion_ ? 3 : int(3 + 5 * (1 - std::clamp((now - import_time_) / .8, 0.0, 1.0)));
        ui::rounded(im, x + card_w - 24 - glow, y + 173 - glow, 9 + 2 * glow, 9 + 2 * glow, 7,
                    ui::mix(theme().card, s::green, 60));
    }
    ui::rounded(im, x + card_w - 24, y + 173, 9, 9, 4,
                library_.installed(index) && game.start ? s::green : theme().muted);
}

void Launcher::open_lesson(double now) {
    if (library_.games()[selected_].id == "mindwalker_amiga") {
        chapter_ = std::make_unique<mindwalker::game::PlayerGuide>(lesson_selected_);
        return;
    }
    if(!library_.installed(selected_)) {show_error("Import your copy to open this lesson.",now);return;}
    try {
        const auto path=library_.content_directory(selected_);
        if(library_.games()[selected_].id=="fa_18_interceptor_amiga")
            chapter_=std::make_unique<interceptor::lessons::TechnicalLesson>(path,lesson_selected_);
        else if(library_.games()[selected_].id=="stunt_car_racer_amiga")
            chapter_=std::make_unique<stuntcar::lessons::TechnicalArticle>(path,lesson_selected_);
        else if(library_.games()[selected_].id=="marble_madness_amiga")
            chapter_=std::make_unique<marble_madness::game::MechanicsChapter>(path,lesson_selected_);
        else if(lesson_selected_==0) chapter_=std::make_unique<bruce_lee::game::EnemyChapter>(path);
        else if(lesson_selected_==1) chapter_=std::make_unique<bruce_lee::game::RoomChapter>(path);
        else chapter_=std::make_unique<bruce_lee::game::BackgroundArticle>(path);
        chapter_->set_canvas(shelf_width_, shelf_height_);
    } catch(const std::exception& e){show_error(std::string("Could not open lesson: ")+e.what(),now);}
}
void Launcher::draw_lessons(Image& im) const {
    const int dx = im.width - 960, dy = im.height - 640;
    const bool guide = library_.games()[selected_].id == "mindwalker_amiga";
    ui::rounded(im,288,102,(648 + dx),(446 + dy),10,theme().card);
    s::text(im, guide ? "How to play Mind Walker" : "Behind the games", 308, 119, 23, theme().ink, true);
    s::text(im,
            guide ? "Choose a sequence, or start with the whole journey."
                  : "Choose a lesson. Explore at your own pace.",
            308, 153, 14, theme().muted);
    for(int row=0;row<lesson_list_.shown();++row){
        const int index=lesson_list_.first()+row,y=178+row*106;
        const auto& lesson=lessons_for(library_.games()[selected_])[std::size_t(index)];
        ui::rounded(im,308,y,(600 + dx),96,6,index==lesson_selected_?theme().pale:theme().paper);
        s::text(im,std::to_string(index+1)+"  "+lesson.title,322,y+12,19,theme().ink,true);
        s::wrap(im,lesson.summary,322,y+39,(564 + dx),2,14);
        s::text(im, (guide || library_.installed(selected_)) ? lesson.format : "Import your copy to open",
                322, y + 77, 12, theme().muted);
    }
    lesson_list_.draw_bar(im,(922 + dx),178,4,318,theme().paper,theme().muted);
    s::text(im,
            guide ? "Available before import. Read the rules without starting a game."
                  : "Illustrated explanations and live experiments share this library.",
            308, (516 + dy), 13, theme().muted);
    s::text(im, "Up / Down or scroll: choose     Enter or click: open     Esc: details", 24, (598 + dy), 14,
            theme().muted);
}

void Launcher::draw_overview(Image &im) const {
    const int dx = im.width - 960, dy = im.height - 640, extra_picture = dx * 3 / 5;
    const auto &game = library_.games()[selected_];
    const bool installed = library_.installed(selected_);
    ui::fill(im, theme().paper);
    button(im, "<  Library", 24, 22, 104, 36);
    s::text(im, "Game details", 150, 25, 22, theme().ink, true);
    s::right(im, std::string(machine_name(game.machine)) + (game.release_year ? " / " + std::to_string(game.release_year) : ""), (934 + dx), 30,
             15, theme().muted);
    draw_identity(im);
    if (importable(game))
        button(im, installed ? (game.start ? "Start" : "View assets") : "Import", 24, 374, 114, 40, detail_focus_ == 0);
    else {
        button(im, "Start", 24, 374, 114, 40);
        ui::blend_rect(im, 24, 374, 114, 40, theme().paper, 150);
    }
    button(im, "Continue", 150, 374, 114, 40, detail_focus_ == 1 && save_preview_.available);
    if (!save_preview_.available)
        ui::blend_rect(im, 150, 374, 114, 40, theme().paper, 150);
    button(im, "Documentation", 24, 434, 240, 40, detail_focus_ == 2);
    if (installed)
        button(im, "Delete game files", 24, 494, 240, 34, detail_focus_ == 3, true);
    ui::rounded(im, 316, 106, (620 + dx), 180, 9, theme().card);
    s::wrap(im, game.description, 338, 126, (576 + dx), 6, 19, theme().ink);
    if (game.integration_pending) {
        s::text(im, "Coming to Reprise", 338, 324, 24, theme().ink, true);
        s::wrap(im, "This game is not available in Reprise yet. You can explore its cover and description while support is being added.", 338, 365, (550 + dx), 5, 18, theme().muted);
        return;
    }
    ui::rounded(im, 316, 306, (620 + dx), 242 + dy, 9, theme().card);
    if (save_preview_.image) {
        ui::rounded(im, 330, 320, 326 + extra_picture, 214 + dy, 5, {20, 30, 34});
        ui::picture(im, *save_preview_.image, 336, 326, 314 + extra_picture, 202 + dy);
    } else {
        ui::rounded(im, 330, 320, 326 + extra_picture, 214 + dy, 5, theme().paper);

    }
    s::wrap(im, save_preview_.available ? "Pick up where you left off" : "No saved game yet", (678 + extra_picture), 326, (234 + dx - extra_picture),
            2, 18, theme().ink);
    s::wrap(im, save_preview_.date, (678 + extra_picture), 366, (234 + dx - extra_picture), 2, 14, theme().muted);
    const std::string help =
        !save_preview_.error.empty() ? save_preview_.error
        : save_preview_.available ? (!game.continue_note.empty() ? game.continue_note
                                     : "Continue restores this moment. Start begins a new game and keeps this "
                                       "save until you save again.")
        : !game.saves_note.empty() ? game.saves_note
        : persistent_saves(game.id)
            ? "Use Quick save in the pause menu to keep your progress after closing the app."
            : "Persistent saves are not available for this game yet.";
    s::wrap(im, help, (678 + extra_picture), 402, (234 + dx - extra_picture), 7, 16, save_preview_.error.empty() ? theme().muted : s::red);
    draw_feature_badges(im, game, 24, 556 + dy, 912 + dx, theme().ink, theme().pale);
    s::text(im, "Up / Down: choose action     Enter: open     Esc: library", 24, (598 + dy), 14, theme().muted);
}
void Launcher::draw_identity(Image &im) const {
    const auto &game = library_.games()[selected_];
    if (!omit_cover_) s::cover(im, game.id, 24, 106, 240, 158);
    s::wrap(im, game.title, 24, 280, 240, 2, 25, theme().ink);
    s::text(im, status(selected_), 24, 340, 14, theme().muted);
}

void Launcher::draw_details(Image& im) const {
    const int dx = im.width - 960, dy = im.height - 640;
    const auto& game = library_.games()[selected_];
    const bool installed = library_.installed(selected_);
    if (!documentation_) {
        draw_overview(im);
        return;
    }
    ui::fill(im, theme().paper);
    button(im, "<  Details", 24, 22, 104, 36);
    s::text(im, "Documentation", 150, 25, 22, theme().ink, true);
    s::right(im, std::string(machine_name(game.machine)) + (game.release_year ? " / " + std::to_string(game.release_year) : ""), (934 + dx), 30, 15, theme().muted);
    ui::rect(im, 24, 78, (912 + dx), 1, theme().line);
    draw_identity(im);
    button(im, "Extracted assets", 24, 374, 240, 40, !about_ && !lessons_);
    button(im, "Development notes", 24, 418, 240, 40, about_);
    if (!lessons_for(game).empty()) {
        button(im, game.id == "mindwalker_amiga" ? "How to play" : "Lessons", 24, lessons_y, 240,
                  side_button_h, lessons_);
        if(lessons_){draw_lessons(im);return;}
    }
    ui::rounded(im, 288, 102, (648 + dx), (446 + dy), 10, theme().card);
    if (about_) {
        Image body(624 + dx, 398 + dy);
        ui::fill(body, theme().card);
        int y = 0 - document_scroll_;
        y += s::prose(body, game.history_title.empty() ? "Development notes" : game.history_title,
                      12, y, body.width - 40, 24, theme().ink);
        y += s::prose(body, game.history.empty() ? "Development notes will arrive with this game." : game.history,
                      12, y, body.width - 40, 19, theme().ink);
        y += s::prose(body, "Sources: " + game.history_sources, 12, y, body.width - 40, 13, theme().muted);
        document_height_ = y + document_scroll_;
        for (int row = 0; row < body.height; ++row)
            std::copy_n(body.pixels.begin() + row * body.width, body.width,
                        im.pixels.begin() + (126 + row) * im.width + 300);
        if (document_height_ > body.height) {
            const int thumb = std::max(24, body.height * body.height / document_height_);
            ui::rounded(im, im.width - 34, 126, 4, body.height, 2, theme().line);
            ui::rounded(im, im.width - 34, 126 + document_scroll_ * (body.height - thumb) /
                        (document_height_ - body.height), 4, thumb, 2, theme().muted);
        }
        s::text(im, "Scroll / Up / Down: read    Esc: details", 24, 582 + dy, 14, theme().muted);
        return;
    }

    const auto pages = previews_.size() + 1;
    s::clipped(im, page_ == 0 ? (installed ? "Imported files" : "About this game") : previews_[page_ - 1].title, 308, 116, (460 + dx), 20, theme().ink, true);
    if (pages > 1) { button(im, "<", (794 + dx), 112, 54, 34); button(im, ">", (858 + dx), 112, 54, 34); }
    if (page_ == 0) {
        if (!installed) {
            const auto parts = library_.imported_parts(selected_);
            s::wrap(im, importable(game) ? "Drop your own " + game.media + " onto this window, or choose Import your copy." +
                    (parts.empty() ? std::string() : " Imported so far: " + parts + ".") :
                "This title is on the roadmap. Import and play will become available in a future release.",
                312, 190, (592 + dx), 5, 18);

        } else {
            s::clipped(im, library_.release(selected_), 308, 151, (600 + dx), 13, theme().muted);
            const auto files = library_.installed_files(selected_);
            for (int row = 0; row < files_.shown(); ++row) {
                const auto& [name, size] = files[std::size_t(files_.first() + row)];
                const int y = file_list_y + row * file_row;
                if (row % 2 == 0) ui::rect(im, 304, y, (612 + dx), file_row, theme().paper);
                s::clipped(im, name, 314, y + 2, (408 + dx), 14);
                s::right(im, size_text(size), (900 + dx), y + 2, 13, theme().muted);
            }
            files_.draw_bar(im, (924 + dx), file_list_y, 4, file_rows_shown * file_row, theme().paper, theme().muted);
            s::text(im, std::to_string(files.size()) + " files", 308, 470, 13, theme().muted);
            s::wrap(im, details_error_.empty() ? (game.start ? "Original game data from your imported copy. Use the arrows to explore decoded graphics and levels." :
                "Native gameplay is still being rebuilt. Use the arrows to explore the data decoded so far.") : details_error_,
                308, 496, (600 + dx), 2, 14, details_error_.empty() ? theme().muted : s::red);
        }
    } else {
        const auto& preview = previews_[page_ - 1];
        const auto bounds = asset_bounds();
        draw_asset(im, preview, bounds[0],bounds[1],bounds[2],bounds[3]);
        if (preview.animated_model) draw_animation_controls(im,bounds[0],bounds[1]+bounds[3]+8,bounds[2]);
        if (!preview.sound && !preview.load_sound) {
            s::text(im, (preview.model || preview.animated_model) ? "Drag to turn / Shift-drag to pan" : "Drag to pan / Wheel to zoom", 310,540+dy,13,theme().muted);
            button(im,"-",768+dx,538+dy,36,34); button(im,"+",812+dx,538+dy,36,34); button(im,"Reset",856+dx,538+dy,64,34);
        }
        if (preview.sound || preview.load_sound) {
            const bool playing = playback_ && playback_->preview == &preview;
            s::text(im, playing ? "Playing - click to stop" : "Click to play", 316, (508 + dy), 13, theme().muted);
        }
    }
    ui::rect(im, 24, (578 + dy), (912 + dx), 1, theme().line);
    s::text(im, "Left / Right: pages     Up / Down: scroll files     Esc: details", 24, (598 + dy), 14, theme().muted);
    s::right(im, std::to_string(page_ + 1) + " / " + std::to_string(pages), (934 + dx), (598 + dy), 14, theme().muted);
}

namespace {
double ease(double value) {
    value = std::clamp(value, 0.0, 1.0);
    return 1 - (1 - value) * (1 - value) * (1 - value);
}
// Crossfade only the chosen panel, with integer translation so type stays crisp.
void dissolve(Image& to, const Image& from, int x, int y, int w, int h, double progress, int slide = 0, bool through_paper = false) {
    const Image target = to;
    const int alpha = int(std::clamp(progress, 0.0, 1.0) * 255);
    for (int py = y; py < y + h; ++py)
        for (int px = x; px < x + w; ++px) {
            const auto at = std::size_t(py * to.width + px);
            const int source_x = px - slide;
            const auto incoming = source_x >= x && source_x < x + w
                ? target.pixels[std::size_t(py * to.width + source_x)] : s::paper;
            to.pixels[at] = through_paper
                ? (progress < .5 ? ui::mix(from.pixels[at], s::paper, std::min(255, alpha * 2))
                                 : ui::mix(s::paper, incoming, std::clamp((alpha - 127) * 2, 0, 255)))
                : ui::mix(from.pixels[at], incoming, alpha);
        }
}
}

double Launcher::scroll_position(double now) const {
    const double target = manual_scroll_.value_or(first_row_);
    if (reduced_motion_) return target;
    return scroll_from_ + (target - scroll_from_) * ease((now - scroll_start_) / .24);
}

std::optional<std::size_t> Launcher::card_at(int x, int y, double now) const {
    // Hit-test the rendered positions, including partially visible entering rows.
    if (y < grid_y - 8 || y >= footer_y - 8) return std::nullopt;
    const auto games = visible_games();
    const double offset = scroll_position(now);
    for (int slot = 0; slot < int(games.size()); ++slot) {
        const auto index = games[std::size_t(slot)];
        const int cx = grid_x + slot % columns_ * (card_w + gap);
        const double emphasis = reduced_motion_ ? double(index == selected_ || hovered_ == index) : card_emphasis_[index];
        const int cy = grid_y + int(std::lround((slot / columns_ - offset) * (card_h + gap))) - int(std::lround(4 * emphasis));
        if (x >= cx && x < cx + card_w && y >= cy && y < cy + card_h) return index;
    }
    return std::nullopt;
}

std::array<int, 4> Launcher::cover_bounds(double now) const {
    const auto games = visible_games();
    const int slot = int(std::find(games.begin(), games.end(), selected_) - games.begin());
    const double emphasis = reduced_motion_ ? 1 : card_emphasis_[selected_];
    return {grid_x + slot % columns_ * (card_w + gap) + 6,
            grid_y + int(std::lround((slot / columns_ - scroll_position(now)) * (card_h + gap))) + 6 - int(std::lround(4 * emphasis)), card_w - 12, 132};
}

Launcher::Request Launcher::update(const platform::Controls& raw, double now) {
    auto in = raw;
    // Shelf taps open on release, leaving room for a continuous vertical drag.
    // SDL supplies the same pointer coordinates for mouse and touch.
    if (!details_) {
        if (in.click && in.mouse_x >= grid_x - 8 && in.mouse_y >= grid_y - 8 && in.mouse_y < footer_y - 8) {
            shelf_press_ = {in.mouse_x, in.mouse_y};
            swipe_y_ = in.mouse_y; shelf_dragged_ = false;
            in.click = false;
        }
        if (shelf_press_) {
            if (in.ui_pointer) {
                const int y = (*in.ui_pointer)[1];
                if (std::abs(y - (*shelf_press_)[1]) > 12) shelf_dragged_ = true;
                if (shelf_dragged_) {
                    const int rows = (int(visible_games().size()) + columns_ - 1) / columns_;
                    const double limit = std::max(0.0, (rows * (card_h + gap) - gap - (footer_y - grid_y - 8)) / double(card_h + gap));
                    manual_scroll_ = std::clamp(scroll_position(now) + (swipe_y_ - y) / double(card_h + gap), 0.0, limit);
                    scroll_from_ = *manual_scroll_; scroll_start_ = -10;
                    swipe_y_ = y;
                }
            }
            if (!in.mouse_held) {
                if (!shelf_dragged_) {
                    in.click = true;
                    in.mouse_x = (*shelf_press_)[0]; in.mouse_y = (*shelf_press_)[1];
                }
                shelf_press_.reset();
            }
        }
    } else shelf_press_.reset();
    if (!details_) {
        const double wheel = in.wheel_precise != 0 ? in.wheel_precise : double(in.wheel);
        if (wheel != 0) {
            const int rows = (int(visible_games().size()) + columns_ - 1) / columns_;
            const double limit = std::max(0.0, (rows * (card_h + gap) - gap - (footer_y - grid_y - 8)) / double(card_h + gap));
            const double position = scroll_position(now);
            manual_scroll_ = std::clamp(manual_scroll_.value_or(first_row_) + wheel * 54.0 / (card_h + gap), 0.0, limit);
            scroll_from_ = position; scroll_start_ = now;
            in.wheel = 0;
        }
        if (manual_scroll_ && (in.nav_left || in.nav_right || in.nav_up || in.nav_down || in.page_up || in.page_down || in.accept)) {
            const double position = scroll_position(now);
            first_row_ = int(*manual_scroll_);
            manual_scroll_.reset(); reveal_selection();
            scroll_from_ = position; scroll_start_ = now;
        }
    }
    // Hover never steals keyboard/controller selection; moving the pointer resumes it.
    if (in.nav_left || in.nav_right || in.nav_up || in.nav_down || in.accept) hovered_.reset();
    if (in.ui_pointer != last_pointer_) {
        hovered_.reset();
        if (in.ui_pointer && !details_) {
            hovered_ = card_at((*in.ui_pointer)[0], (*in.ui_pointer)[1], now);
        }
        last_pointer_ = in.ui_pointer;
    }
    const double dt = motion_tick_ < 0 ? 0 : std::clamp(now - motion_tick_, 0.0, .15);
    motion_tick_ = now;
    for (std::size_t i = 0; i < card_emphasis_.size(); ++i) {
        const double target = (i == selected_ && !sidebar_focus_) || hovered_ == i ? 1 : 0;
        const double step = dt / .15;
        card_emphasis_[i] += std::clamp(target - card_emphasis_[i], -step, step);
    }
    if (in.click || in.accept) {
        press_time_ = now; press_x_ = in.mouse_x; press_y_ = in.mouse_y;
        press_keyboard_ = in.accept;
    }
    // State changes take effect immediately. Animations only decorate the result
    // and can be replaced by another action at any point.
    const auto key = [&] { return std::tuple{details_, documentation_, about_, lessons_, page_, chapter_.get()}; };
    const auto before = key();
    const bool was_details = details_, was_documentation = documentation_;
    const bool actionable = in.click || in.accept || in.back || in.nav_left || in.nav_right || in.nav_up || in.nav_down;
    const bool interrupted = transition_from_ && now - transition_start_ < .24;
    std::optional<Image> old;
    const double old_scroll = scroll_position(now);
    const int old_row = first_row_, old_filter = filter_;
    const bool was_assets = details_ && documentation_ && !about_ && !lessons_ && page_ > 0 && !chapter_;
    auto from = details_ ? std::array<int, 4>{24, 106, 240, 158} : cover_bounds(now);
    if (actionable && !reduced_motion_) old = draw(now);
    // Desktop/landscape article input; portrait uses the shared document gesture handler.
    if (!portrait_ && details_ && documentation_ && about_ && !chapter_) {
        const int height = detail_height() - 242;
        document_scroll_ += in.wheel * 64 + (int(in.page_down) - int(in.page_up)) * (height - 32);
        if (!doc_navigation_) document_scroll_ += (int(in.nav_down) - int(in.nav_up)) * 40;
        if (in.click && in.mouse_x >= 300 && in.mouse_y >= 126 && in.mouse_y < 126 + height) {
            document_press_ = {in.mouse_x, in.mouse_y};
            document_drag_y_ = in.mouse_y;
        }
        if (document_press_ && in.ui_pointer) {
            const int y = (*in.ui_pointer)[1];
            document_scroll_ += document_drag_y_ - y;
            document_drag_y_ = y;
        }
        if (!in.mouse_held) document_press_.reset();
        document_scroll_ = std::clamp(document_scroll_, 0, std::max(0, document_height_ - height));
    }
    const auto request = portrait_ && details_ && !chapter_ ? update_portrait_details(in, now) : update_input(in, now);
    if (key() != before) { document_scroll_ = 0; document_press_.reset(); }
    update_preview(in, now, request);
    if (filter_ != old_filter) {
        scroll_from_ = first_row_; scroll_start_ = -10; hovered_.reset();
    } else if (first_row_ != old_row) {
        scroll_from_ = old_scroll; scroll_start_ = now; hovered_.reset();
    }
    if (old && !portrait_ && (shelf_width_ == 960 || (was_details && details_)) && key() != before && !reduced_motion_ && request == Request::none) {
        transition_ = was_details != details_ && !interrupted ? Transition::cover
                      : was_assets && documentation_ && !about_ && !lessons_ && page_ > 0 && !chapter_
                          ? Transition::asset
                      : was_details && details_ && was_documentation && documentation_ && !chapter_
                          ? Transition::document : Transition::page;
        cover_from_ = from;
        cover_to_ = details_ ? std::array<int, 4>{24, 106, 240, 158} : cover_bounds(now);
        if (transition_ == Transition::cover) {
            // For a mouse-opened card, the selection may just have changed.
            if (!was_details) {
                cover_from_ = cover_bounds(now);
            }
            const auto& r = cover_from_;
            ui::rect(*old, r[0], r[1], r[2], r[3], was_details ? theme().paper : theme().card);
        }
        transition_from_ = std::move(old);
        transition_start_ = now;
    }
    return request;
}

void Launcher::button(Image& im, std::string_view text, int x, int y, int w, int h, bool primary, bool danger) const {
    const bool pressed = press_keyboard_ ? primary : press_x_ >= x && press_x_ < x + w && press_y_ >= y && press_y_ < y + h;
    const double strength = reduced_motion_ || !pressed ? 0 : 1 - ease((draw_time_ - press_time_) / .14);
    ui::rounded(im, x, y + 3, w, h, 7, theme().line);
    y += int(std::lround(2 * strength));
    if (danger) s::danger_button(im, text, x, y, w, h, primary);
    else {
        ui::rounded(im, x, y, w, h, 7, primary ? theme().accent : theme().line);
        if (!primary) ui::rounded(im, x + 1, y + 1, w - 2, h - 2, 6, theme().card);
        s::text(im, text, x + (w - s::width(text, 15, true)) / 2, y + (h - 22) / 2,
                15, primary ? s::white : theme().ink, true);
    }
}

const Preview* Launcher::shown_preview() const {
    const bool assets = details_ && documentation_ && !about_ && !lessons_ && !chapter_ && !confirm_remove_;
    return assets && page_ > 0 && page_ <= previews_.size() ? &previews_[page_ - 1] : nullptr;
}

void Launcher::stop_sound() {
    if (playback_) audio_cut_ = true;
    playback_.reset();
}

std::array<int, 4> Launcher::asset_bounds() const {
    const int controls = shown_preview() && shown_preview()->animated_model ? 130 : 0;
    return portrait_ ? std::array<int, 4>{24, 396, 592, std::max(100, shelf_height_ - 536 - controls)}
                     : std::array<int, 4>{304, 160, 616 + detail_width() - 960, 370 + detail_height() - 640 - controls};
}

void Launcher::draw_asset(Image &im, const Preview &preview, int x, int y, int w, int h) const {
    ui::rounded(im, x, y, w, h, 7, {20, 30, 34});
    x += 6;
    y += 6;
    w -= 12;
    h -= 12;
    if (preview.animated_model && animated_model_) {
        ui::draw_model(im,x,y,w,h,animated_model_->model(),model_yaw_,model_pitch_,asset_zoom_,asset_pan_x_,asset_pan_y_);
    } else if (preview.animated_model && !animation_error_.empty()) {
        s::wrap(im,animation_error_,x+12,y+12,w-24,7,16,s::red);
    } else if (preview.model) {
        ui::draw_model(im, x, y, w, h, *preview.model, model_yaw_, model_pitch_, asset_zoom_, asset_pan_x_,
                       asset_pan_y_);
    } else if (preview.load_sound && !preview.sound) {
        s::wrap(im,animation_error_,x+12,y+12,w-24,7,16,s::red);
    } else if (preview.sound) {
        ui::picture(im, preview.image, x, y, w, h);
        if(preview.load_sound) {
            const auto& sound=*preview.sound;
            auto clock=[](double seconds) {const int n=int(seconds);return std::to_string(n/60)+":"+(n%60<10?"0":"")+std::to_string(n%60);};
            const double position=playback_ && playback_->preview==&preview ? playback_->position : 0;
            s::text(im,clock(position/sound.rate)+" / "+clock(sound.frames()/sound.rate)+(sound.channels==2 ? "  Stereo" : "  Mono"),x+10,y+10,16,{220,232,234});
        }
        if(playback_ && playback_->preview==&preview && preview.sound->frames()) {
            const double done=playback_->position/preview.sound->frames();
            ui::rect(im,x+int(done*(w-1)),y,2,h,{255,214,120});
        }
    }
    else if (preview.image.width > 0 && preview.image.height > 0) {
        const auto &source = preview.image;
        const double scale = std::min(double(w) / source.width, double(h) / source.height) * asset_zoom_;
        const double left = (w - source.width * scale) / 2 + asset_pan_x_,
                     top = (h - source.height * scale) / 2 + asset_pan_y_;
        // Sample only inside the viewport, even for very large zoomed bitmaps.
        for (int yy = 0; yy < h; ++yy)
            for (int xx = 0; xx < w; ++xx) {
                const int sx = int(std::floor((xx - left) / scale)), sy = int(std::floor((yy - top) / scale));
                if (sx >= 0 && sy >= 0 && sx < source.width && sy < source.height)
                    im.put(x + xx, y + yy, source.pixels[std::size_t(sy) * source.width + sx]);
            }
    }
}

void Launcher::update_preview(const platform::Controls &in, double now, Request request) {
    const Preview *shown = request == Request::none ? shown_preview() : nullptr;
    const auto [x, y, w, h] = asset_bounds();
    auto inside = [&](double px, double py) { return px >= x && px < x + w && py >= y && py < y + h; };
    if (playback_ && playback_->preview != shown)
        stop_sound();
    const double dt = model_time_ < 0 ? 0 : std::clamp(now - model_time_, 0.0, 0.1);
    model_time_ = now;
    if (model_preview_ != shown) {
        for(auto& preview:previews_) if(preview.load_sound && &preview!=shown) {
            preview.sound.reset(); preview.image=Image(0,0);
        }
        model_preview_ = shown;
        animated_model_.reset(); animation_error_.clear(); animation_clip_=0; animation_time_=0;
        animation_speed_=2; animation_loop_=true; animation_playing_=false;
        if (shown && shown->animated_model) {
            try {
                animated_model_=shown->animated_model();
                const auto& clips=animated_model_->clips();
                std::size_t initial=0;
                for (std::size_t i=1;i<clips.size();++i) if (clips[i].name=="idle") {initial=i;break;}
                select_animation(initial);
                animation_playing_=initial>0 && !reduced_motion_;
            } catch (const std::exception& e) { animation_error_=e.what(); }
        }
        if(shown && shown->load_sound) {
            auto& preview=previews_[page_-1];
            try {
                preview.sound=preview.load_sound();
                const auto& sound=*preview.sound;
                if((sound.channels!=1 && sound.channels!=2) || !std::isfinite(sound.rate) || sound.rate<=0 || !sound.frames())
                    throw std::runtime_error("Invalid preview audio");
                preview.image=Image(600,240);
                for(auto& pixel:preview.image.pixels) pixel={20,30,34};
                for(int column=0;column<600;++column) {
                    float peak=0;
                    for(std::size_t frame=column*sound.frames()/600;frame<(column+1)*sound.frames()/600;++frame)
                        for(unsigned ch=0;ch<sound.channels;++ch) peak=std::max(peak,std::abs(sound.samples[frame*sound.channels+ch]));
                    const int height=std::max(1,int(std::min(peak,1.f)*110));
                    ui::rect(preview.image,column,120-height,1,height*2,{111,195,206});
                }
            } catch(const std::exception& e) {preview.sound.reset();animation_error_=e.what();}
        }
        asset_zoom_ = 1;
        asset_pan_x_ = asset_pan_y_ = 0;
        model_yaw_ = -.6;
        model_pitch_ = .35;
        spin_yaw_ = spin_pitch_ = 0;
        dragging_ = false;
        asset_contacts_.clear();
    }
    if (!shown || in.input_cancelled) {
        dragging_ = false;
        asset_contacts_.clear();
        return;
    }
    if (shown->sound) {
        if ((in.click && inside(in.mouse_x, in.mouse_y)) || in.accept) {
            if (playback_)
                stop_sound();
            else
                playback_ = Playback{shown, 0};
        }
        return;
    }
    auto zoom = [&](double factor, double px, double py) {
        const double next = std::clamp(asset_zoom_ * factor, .25, 12.0), ratio = next / asset_zoom_;
        asset_pan_x_ = (asset_pan_x_ - (px - x - w / 2.0)) * ratio + (px - x - w / 2.0);
        asset_pan_y_ = (asset_pan_y_ - (py - y - h / 2.0)) * ratio + (py - y - h / 2.0);
        asset_zoom_ = next;
    };
    const int animation_bar = y+h+8;
    if (animated_model_) {
        const auto& clips=animated_model_->clips();
        if (!clips.empty()) {
            if (hit(in,x,animation_bar,44,36) || in.page_up) select_animation((animation_clip_+clips.size()-1)%clips.size());
            if (hit(in,x+w-44,animation_bar,44,36) || in.page_down) select_animation((animation_clip_+1)%clips.size());
            const auto& clip=clips[animation_clip_];
            const int row=animation_bar+40;
            if((hit(in,x,row,96,36) || in.accept) && animation_error_.empty()) animation_playing_=!animation_playing_;
            if(hit(in,x+104,row,44,36) || hit(in,x+156,row,44,36)) {
                animation_playing_=false;
                animation_time_=std::clamp(animation_time_+(hit(in,x+104,row,44,36)?-1:1)/clip.sample_rate,0.0,clip.duration);
            }
            if(hit(in,x+208,row,90,36)) animation_speed_=(animation_speed_+1)%4;
            if(hit(in,x+306,row,100,36)) animation_loop_=!animation_loop_;
            const int timeline=animation_bar+83;
            const bool scrub=hit(in,x,timeline,w,24) ||
                (in.mouse_held && in.ui_pointer && (*in.ui_pointer)[0]>=x && (*in.ui_pointer)[0]<x+w && (*in.ui_pointer)[1]>=timeline && (*in.ui_pointer)[1]<timeline+24);
            if(scrub) {
                const double pointer=in.ui_pointer ? (*in.ui_pointer)[0] : in.mouse_x;
                animation_time_=std::clamp((pointer-x)/w,0.0,1.0)*clip.duration; animation_playing_=false;
            }
            constexpr double speeds[]{.25,.5,1,2};
            if(animation_playing_ && animation_error_.empty() && clip.duration>0) {
                animation_time_+=dt*speeds[animation_speed_];
                if(animation_time_>=clip.duration) {
                    if(animation_loop_) animation_time_=std::fmod(animation_time_,clip.duration);
                    else {animation_time_=clip.duration;animation_playing_=false;}
                }
            }
            try {animated_model_->pose(animation_time_);} catch(const std::exception& e) {animation_error_=e.what();animation_playing_=false;}
        }
    }
    const int bar = animation_bar + (shown->animated_model ? 130 : 0);
    if (hit(in, x + w - 64, bar, 64, 34)) {
        asset_zoom_ = 1;
        asset_pan_x_ = asset_pan_y_ = 0;
        model_yaw_ = -.6;
        model_pitch_ = .35;
        spin_yaw_ = spin_pitch_ = 0;
    }
    if (hit(in, x + w - 108, bar, 36, 34))
        zoom(1.25, x + w / 2.0, y + h / 2.0);
    if (hit(in, x + w - 152, bar, 36, 34))
        zoom(.8, x + w / 2.0, y + h / 2.0);
    if (in.ui_pointer && inside((*in.ui_pointer)[0], (*in.ui_pointer)[1])) {
        const double wheel = in.wheel_precise ? in.wheel_precise : in.wheel;
        if (wheel)
            zoom(std::exp(-wheel * .12), (*in.ui_pointer)[0], (*in.ui_pointer)[1]);
    }
    const auto &fingers = in.ui_fingers;
    if (!fingers.empty()) {
        if (asset_contacts_.empty() && !inside(fingers[0].x, fingers[0].y))
            return;
        if (fingers.size() == asset_contacts_.size() && fingers.size() <= 2 &&
            fingers[0].id == asset_contacts_[0].id &&
            (fingers.size() == 1 || fingers[1].id == asset_contacts_[1].id)) {
            double dx = fingers[0].x - asset_contacts_[0].x, dy = fingers[0].y - asset_contacts_[0].y;
            if (fingers.size() == 2) {
                const double cx = (fingers[0].x + fingers[1].x) / 2, cy = (fingers[0].y + fingers[1].y) / 2;
                const double ox = (asset_contacts_[0].x + asset_contacts_[1].x) / 2,
                             oy = (asset_contacts_[0].y + asset_contacts_[1].y) / 2;
                const double before = std::hypot(asset_contacts_[0].x - asset_contacts_[1].x,
                                                 asset_contacts_[0].y - asset_contacts_[1].y);
                const double after = std::hypot(fingers[0].x - fingers[1].x, fingers[0].y - fingers[1].y);
                if (before > 4 && after > 4)
                    zoom(after / before, ox, oy);
                dx = cx - ox;
                dy = cy - oy;
            }
            if (fingers.size() == 2 || !(shown->model || shown->animated_model)) {
                asset_pan_x_ += dx;
                asset_pan_y_ += dy;
            } else {
                model_yaw_ += dx * .012;
                model_pitch_ = std::clamp(model_pitch_ + dy * .012, -1.45, 1.45);
                if (dt > 0) {
                    const double keep = std::exp(-dt / .05);
                    spin_yaw_ = spin_yaw_ * keep + dx * .012 / dt * (1 - keep);
                    spin_pitch_ = spin_pitch_ * keep + dy * .012 / dt * (1 - keep);
                }
            }
        }
        asset_contacts_ = fingers;
        dragging_ = false;
        if (fingers.size() > 1)
            spin_yaw_ = spin_pitch_ = 0;
        return;
    }
    if (!asset_contacts_.empty()) {
        asset_contacts_.clear();
        dragging_ = false;
        return;
    }
    if (in.click && !in.mouse_is_touch && inside(in.mouse_x, in.mouse_y)) {
        dragging_ = true;
        drag_last_ = {in.mouse_x, in.mouse_y};
    }
    if (dragging_ && in.mouse_held && in.ui_pointer) {
        const double dx = (*in.ui_pointer)[0] - drag_last_[0], dy = (*in.ui_pointer)[1] - drag_last_[1];
        drag_last_ = *in.ui_pointer;
        if (!(shown->model || shown->animated_model) || in.shift_held) {
            asset_pan_x_ += dx;
            asset_pan_y_ += dy;
        } else {
            model_yaw_ += dx * .012;
            model_pitch_ = std::clamp(model_pitch_ + dy * .012, -1.45, 1.45);
            if (dt > 0) {
                const double keep = std::exp(-dt / .05);
                spin_yaw_ = spin_yaw_ * keep + dx * .012 / dt * (1 - keep);
                spin_pitch_ = spin_pitch_ * keep + dy * .012 / dt * (1 - keep);
            }
        }
        return;
    }
    dragging_ = false;
    const double idle = reduced_motion_ ? 0 : .5;
    const double ease = std::exp(-dt / .9);
    spin_yaw_ = idle + (spin_yaw_ - idle) * ease;
    spin_pitch_ *= ease;
    model_yaw_ = std::remainder(model_yaw_ + dt * spin_yaw_, 2 * std::numbers::pi);
    model_pitch_ = std::clamp(model_pitch_ + dt * spin_pitch_, -1.45, 1.45);
}

void Launcher::select_animation(std::size_t index) {
    if (!animated_model_ || index>=animated_model_->clips().size()) return;
    animation_clip_=index; animation_time_=0; animation_error_.clear();
    try { animated_model_->select(index); animated_model_->pose(0); }
    catch(const std::exception& e) {
        animation_error_=e.what(); animation_playing_=false;
        animated_model_->select(0); animated_model_->pose(0);
    }
}

void Launcher::draw_animation_controls(Image& im,int x,int y,int w) const {
    if(!animated_model_ || animated_model_->clips().empty()) {
        s::clipped(im,animation_error_.empty() ? "No animation available" : animation_error_,x,y,w,15,s::red); return;
    }
    const auto& clip=animated_model_->clips()[animation_clip_];
    button(im,"<",x,y,44,36); button(im,">",x+w-44,y,44,36);
    s::clipped(im,clip.name+"  ("+std::to_string(animation_clip_+1)+"/"+std::to_string(animated_model_->clips().size())+")",x+54,y+8,w-108,16,theme().ink);
    button(im,animation_playing_ ? "Pause" : "Play",x,y+40,96,36);
    button(im,"-1f",x+104,y+40,44,36); button(im,"+1f",x+156,y+40,44,36);
    constexpr const char* rates[]{"0.25x","0.5x","1x","2x"};
    button(im,rates[animation_speed_],x+208,y+40,90,36);
    button(im,animation_loop_ ? "Loop: on" : "Loop: off",x+306,y+40,100,36);
    const auto frame=std::llround(animation_time_*clip.sample_rate), last=std::llround(clip.duration*clip.sample_rate);
    s::clipped(im,std::to_string(frame)+" / "+std::to_string(last)+" frames",x+416,y+50,w-416,13,theme().muted);
    ui::rounded(im,x,y+90,w,8,4,theme().line);
    const int progress=clip.duration>0 ? int(std::clamp(animation_time_/clip.duration,0.0,1.0)*(w-8)) : 0;
    ui::rounded(im,x+progress,y+83,8,22,3,theme().ink);
    s::clipped(im,animation_error_.empty() ? "Clip: Page Up/Down or shoulders · Enter/A: play/pause" : animation_error_,x,y+110,w,12,animation_error_.empty()?theme().muted:s::red);
}

void Launcher::preview_audio(std::span<float> stereo, int rate) {
    std::fill(stereo.begin(), stereo.end(), 0.0f);
    if (!playback_ || !playback_->preview->sound) return;
    const auto& sound = *playback_->preview->sound;
    const double step = sound.rate / rate;
    for (std::size_t i = 0; i + 1 < stereo.size(); i += 2) {
        const auto at = std::size_t(playback_->position);
        if (at >= sound.frames()) {
            playback_.reset(); // played to its end
            return;
        }
        // Nearest sample, as Paula plays them (no smoothing between samples).
        stereo[i] = sound.samples[at*sound.channels] * 0.6f;
        stereo[i+1] = sound.samples[at*sound.channels+(sound.channels==2 ? 1 : 0)] * 0.6f;
        playback_->position += step;
    }
}

Image Launcher::draw(double now) const {
    draw_time_ = now;
    const double duration = transition_ == Transition::cover ? .24 : .18;
    const bool transitioning = !reduced_motion_ && transition_from_ && now - transition_start_ < duration;
    omit_cover_ = transitioning && transition_ == Transition::cover;
    Image result = draw_scene(now);
    omit_cover_ = false;
    if (save_fade_pending_) {
        save_fade_start_ = now;
        save_fade_pending_ = false;
    }
    if (!portrait_ && !reduced_motion_ && details_ && !documentation_ && save_preview_.image && now - save_fade_start_ < .22) {
        Image previous = result;
        ui::rect(previous, 336, 326, 314 + (result.width - 960) * 3 / 5, 202 + result.height - 640, {20, 30, 34});
        if (previous_save_) ui::picture(previous, *previous_save_, 336, 326, 314 + (result.width - 960) * 3 / 5, 202 + result.height - 640);
        dissolve(result, previous, 336, 326, 314 + (result.width - 960) * 3 / 5, 202 + result.height - 640, ease((now - save_fade_start_) / .22));
    }
    if (transitioning) {
        const double t = ease((now - transition_start_) / duration);
        if (transition_ == Transition::asset)
            dissolve(result, *transition_from_, 304, 160, result.width - 344, result.height - 270, t);
        else if (transition_ == Transition::document)
            dissolve(result, *transition_from_, 288, 102, result.width - 312, result.height - 194, t, int(std::lround(8 * (1 - t))));
        else dissolve(result, *transition_from_, 0, 0, result.width, result.height, t, 0, false);
        if (transition_ == Transition::cover) {
            std::array<int, 4> r{};
            for (int n = 0; n < 4; ++n) r[n] = int(std::lround(cover_from_[n] + (cover_to_[n] - cover_from_[n]) * t));
            ui::rounded(result, r[0] - 2, r[1] + 4, r[2] + 4, r[3] + 2, 6, theme().line);
            s::cover(result, library_.games()[selected_].id, r[0], r[1], r[2], r[3]);
        }
    }
    return result;
}

Launcher::Request Launcher::update_portrait_details(const platform::Controls& raw, double now) {
    auto in = raw;
    if (confirm_remove_) {
        const int y = (shelf_height_ - 220) / 2;
        in.click = false;
        if (hit(raw, 300, y + 156, 136, 42)) { in.click = true; in.mouse_x = cancel_x; in.mouse_y = dialog_button_y; }
        if (hit(raw, 452, y + 156, 136, 42)) { in.click = true; in.mouse_x = confirm_x; in.mouse_y = dialog_button_y; }
        return update_confirm(in, now);
    }
    const bool interactive_asset = documentation_ && !about_ && !lessons_ && page_ > 0 && shown_preview();
    if (!interactive_asset && raw.click && raw.mouse_y >= 320) {
        document_press_ = {raw.mouse_x, raw.mouse_y};
        document_drag_y_ = raw.mouse_y; document_dragged_ = false;
        in.click = false;
    }
    if (document_press_) {
        in.click = false;
        if (raw.ui_pointer) {
            const int y = (*raw.ui_pointer)[1];
            if (std::abs(y - (*document_press_)[1]) > 12) document_dragged_ = true;
            if (document_dragged_) document_scroll_ += document_drag_y_ - y;
            document_drag_y_ = y;
        }
        if (!raw.mouse_held) {
            if (!document_dragged_) { in.click = true; in.mouse_x = (*document_press_)[0]; in.mouse_y = (*document_press_)[1]; }
            document_press_.reset();
        }
    }
    if (!interactive_asset) document_scroll_ += raw.wheel * 64 + (int(raw.page_down) - int(raw.page_up)) * (shelf_height_ - 360);
    else { document_scroll_ = 0; document_press_.reset(); }
    if ((!documentation_ || about_) && !doc_navigation_)
        document_scroll_ += (int(raw.nav_down) - int(raw.nav_up)) * 40;
    document_scroll_ = std::clamp(document_scroll_, 0, std::max(0, document_height_ - (shelf_height_ - 344)));
    in.wheel = 0; in.page_down = false; in.page_up = false;
    // Reuse the existing action semantics, with portrait hit regions translated
    // at the boundary. Text and image content are laid out separately below.
    const auto tap = in;
    in.click = false;
    auto action = [&](int x, int y) { in.click = true; in.mouse_x = x; in.mouse_y = y; };
    if (hit(tap, 24, 20, 112, 42)) in.back = true;
    if (!documentation_) {
        if (hit(tap, 24, 258, 184, 44)) action(24, 374);
        if (hit(tap, 224, 258, 184, 44)) action(150, 374);
        if (hit(tap, 424, 258, 192, 44)) action(24, 434);
        if (tap.mouse_y >= 320 && hit(tap, 32, 320 + remove_button_y_ - document_scroll_, 224, 42)) action(24, 494);
    } else {
        if (hit(tap, 24, 258, 184, 44)) action(24, 374);
        if (hit(tap, 224, 258, 184, 44)) action(24, 418);
        if (hit(tap, 424, 258, 192, 44)) action(24, lessons_y);
        if (lessons_) {
            for (int i = 0; i < int(lessons_for(library_.games()[selected_]).size()); ++i)
                if (tap.mouse_y >= 320 && hit(tap, 24, 320 + 20 + i * 124 - document_scroll_, 592, 112)) {
                    lesson_selected_ = i; open_lesson(now); return Request::none;
                }
        } else if (!about_) {
            if (tap.mouse_y >= 320 && hit(tap, 496, 336 - document_scroll_, 48, 42)) action(794, 112);
            if (tap.mouse_y >= 320 && hit(tap, 560, 336 - document_scroll_, 48, 42)) action(858, 112);
        }
    }
    const auto result = update_input(in, now);
    if (documentation_ && lessons_ && (raw.nav_up || raw.nav_down) && !doc_navigation_) {
        const int y = 20 + lesson_selected_ * 124;
        document_scroll_ = std::clamp(document_scroll_, std::max(0, y + 112 - (shelf_height_ - 344)), y);
    }
    return result;
}

Image Launcher::draw_portrait_details() const {
    const auto& game = library_.games()[selected_];
    const bool installed = library_.installed(selected_);
    Image im(640, shelf_height_);
    ui::fill(im, theme().paper);
    // Identical identity block on details and every documentation category.
    button(im, documentation_ ? "<  Details" : "<  Library", 24, 20, 112, 42);
    s::text(im, documentation_ ? "Documentation" : "Game details", 156, 28, 23, theme().ink, true);
    ui::rounded(im, 21, 87, 226, 152, 8, theme().line);
    s::cover(im, game.id, 24, 90, 220, 146);
    s::wrap(im, game.title, 268, 92, 348, 3, 28, theme().ink);
    s::text(im, (game.release_year ? std::to_string(game.release_year) + "  /  " : "") + std::string(machine_name(game.machine)), 268, 199, 14, theme().muted);
    s::clipped(im, status(selected_), 268, 222, 348, 14, theme().muted);
    if (documentation_) {
        button(im, "Extracted assets", 24, 258, 184, 44, !about_ && !lessons_);
        button(im, "Development", 224, 258, 184, 44, about_);
        if (!lessons_for(game).empty()) button(im, game.id == "mindwalker_amiga" ? "How to play" : "Lessons", 424, 258, 192, 44, lessons_);
    } else {
        button(im, installed ? (game.start ? "Start" : "View assets") : "Import", 24, 258, 184, 44, detail_focus_ == 0 && importable(game));
        if (!importable(game)) ui::blend_rect(im, 24, 258, 184, 44, theme().paper, 170);
        button(im, "Continue", 224, 258, 184, 44, detail_focus_ == 1 && save_preview_.available);
        if (!save_preview_.available) ui::blend_rect(im, 224, 258, 184, 44, theme().paper, 170);
        button(im, "Documentation", 424, 258, 192, 44, detail_focus_ == 2);
    }
    Image body(640, shelf_height_ - 344);
    ui::fill(body, theme().card);
    int y = 22 - document_scroll_;
    auto paragraph = [&](std::string_view text, int size, platform::Color color) {
        y += s::wrap(body, text, 32, y, 568, 1000, size, color) * (size + 5) + 18;
    };
    if (!documentation_) {
        const int badges_height = draw_feature_badges(body, game, 32, y, 568, theme().ink, theme().pale);
        if (badges_height) y += badges_height + 22;
        paragraph(game.description, 21, theme().ink);
        ui::rect(body, 32, y, 568, 1, theme().line); y += 24;
        if (game.integration_pending) {
            paragraph("Coming to Reprise", 24, theme().ink);
            paragraph("This game is not available yet. Explore its description and documentation while support is being added.", 18, theme().muted);
        } else {
            if (save_preview_.image) {
                s::text(body, "Your saved game", 32, y, 23, theme().ink, true); y += 38;
                ui::rounded(body, 32, y, 568, 300, 7, {20,30,34});
                ui::picture(body, *save_preview_.image, 40, y + 8, 552, 284); y += 316;
            }
            paragraph(save_preview_.available ? "Continue from your saved state" : "No saved game yet", 22, theme().ink);
            if (!save_preview_.date.empty()) paragraph(save_preview_.date, 15, theme().muted);
            paragraph(!save_preview_.error.empty() ? save_preview_.error : save_preview_.available
                ? (!game.continue_note.empty() ? game.continue_note
                   : "Continue restores this moment. Start begins a new game and keeps this save until you save again.")
                : !game.saves_note.empty() ? game.saves_note
                : persistent_saves(game.id) ? "Use Quick save in the pause menu to keep your progress after closing the app."
                : "Persistent saves are not available for this game yet.", 18, theme().muted);
        }
        remove_button_y_ = y + document_scroll_;
        if (installed) { button(body, "Delete game files", 32, y, 224, 42, detail_focus_ == 3, true); y += 62; }
    } else if (about_) {
        paragraph(game.history_title.empty() ? "Development notes" : game.history_title, 25, theme().ink);
        y += s::prose(body, game.history.empty() ? "Development notes will arrive with this game." : game.history, 32, y, 568, 20, theme().ink);
        if (!game.history_sources.empty()) { ui::rect(body, 32, y, 568, 1, theme().line); y += 24; paragraph("Sources: " + game.history_sources, 15, theme().muted); }
    } else if (lessons_) {
        for (int i = 0; i < int(lessons_for(game).size()); ++i) {
            const auto& lesson = lessons_for(game)[std::size_t(i)];
            y = 20 + i * 124 - document_scroll_;
            ui::rounded(body, 24, y, 592, 112, 7, i == lesson_selected_ ? theme().pale : theme().paper);
            s::clipped(body, std::to_string(i + 1) + "  " + lesson.title, 40, y + 10, 560, 21, theme().ink, true);
            s::wrap(body, lesson.summary, 40, y + 40, 552, 2, 17, theme().muted);
            s::text(body, installed || game.id == "mindwalker_amiga" ? lesson.format : "Import your copy to open", 40, y + 88, 13, theme().muted);
            y += 124;
        }
    } else {
        s::clipped(body, page_ ? previews_[page_ - 1].title : "Imported files", 32, y, 448, 22, theme().ink, true);
        if (!previews_.empty()) { button(body, "<", 496, y - 6, 48, 42); button(body, ">", 560, y - 6, 48, 42); }
        y += 54;
        if (!page_) {
            if (!installed) paragraph(importable(game) ? "Choose Import on the details page to select your own " + game.media + "." : "Import and play will become available in a future release.", 20, theme().muted);
            else {
                paragraph(library_.release(selected_), 16, theme().muted);
                for (const auto& [name, size] : library_.installed_files(selected_)) {
                    s::clipped(body, name, 32, y, 408, 16, theme().ink);
                    s::right(body, size_text(size), 600, y + 2, 13, theme().muted); y += 29;
                }
                y += 18;
                paragraph(details_error_.empty() ? "Original data from your imported copy. Use the arrows to explore decoded graphics, sound and levels." : details_error_, 17, theme().muted);
            }
        } else {
            const auto& preview = previews_[page_ - 1];
            const int height=asset_bounds()[3];
            draw_asset(body,preview,24,y,592,height);
            y+=height+8;
            if(preview.animated_model) {draw_animation_controls(body,24,y,592); y+=130;}
            if (!preview.sound && !preview.load_sound) {
                s::text(body,(preview.model || preview.animated_model) ? "Drag to turn / Pinch to zoom" : "Drag to pan / Pinch to zoom",32,y+8,15,theme().muted);
                button(body,"-",464,y,36,34);button(body,"+",508,y,36,34);button(body,"Reset",552,y,64,34);
                y+=42;
            }
            if (preview.sound || preview.load_sound) paragraph(playback_ ? "Playing — tap to stop" : "Tap the waveform to play", 17, theme().muted);
            paragraph(std::to_string(page_ + 1) + " / " + std::to_string(previews_.size() + 1), 15, theme().muted);
        }
    }
    document_height_ = y + document_scroll_ + 20;
    for (int row = 0; row < body.height; ++row)
        std::copy_n(body.pixels.begin() + row * 640, 640, im.pixels.begin() + (row + 320) * 640);
    if (document_height_ > body.height) {
        const int thumb = std::max(24, body.height * body.height / document_height_);
        ui::rounded(im, 630, 320, 4, body.height, 2, theme().line);
        ui::rounded(im, 630, 320 + document_scroll_ * (body.height - thumb) / (document_height_ - body.height), 4, thumb, 2, theme().muted);
    }
    if (!message_.empty() && draw_time_ - message_time_ < 8) {
        ui::rounded(im, 24, shelf_height_ - 104, 592, 80, 8, theme().pale);
        s::wrap(im, message_, 40, shelf_height_ - 94, 560, 3, 17, message_ok_ ? s::green : s::red);
    }
    if (confirm_remove_) {
        const int top = (shelf_height_ - 220) / 2;
        ui::blend_rect(im, 0, 0, 640, shelf_height_, theme().ink, 160);
        ui::rounded(im, 24, top, 592, 220, 10, theme().card);
        s::text(im, "Delete imported game files?", 44, top + 20, 25, theme().ink, true);
        s::wrap(im, "Your original image and persistent quick save are kept. You can import your copy again later.", 44, top + 65, 548, 3, 19, theme().muted);
        button(im, "Cancel", 300, top + 156, 136, 42, confirm_focus_ == 0);
        button(im, "Delete", 452, top + 156, 136, 42, confirm_focus_ == 1, true);
    }
    return im;
}

Image Launcher::draw_portrait(double now) const {
    Image im(640, shelf_height_);
    ui::fill(im, theme().paper);
    if (filter_ >= 2) {
        const char* backgrounds[] = {"commodore", "sonic", "layton"};
        s::backdrop(im, backgrounds[filter_ - 2]);
    }
    ui::blend_rect(im, 0, 0, 640, 174, theme().paper, 210);
    s::reprise(im, 22, 17);
    s::text(im, "Reprise", 69, 13, 31, theme().ink, true);
    button(im, "+  Import game", 480, 20, 138, 36, true);
    for (int i = 0; i < filter_count; ++i) {
        const int x = 22 + i % 3 * 200, y = 82 + i / 3 * 44;
        button(im, filters[i], x, y, 192, 34, filter_ == i);
        if (sidebar_focus_ && sidebar_row_ == i)
            ui::rect(im, x + 12, y + 29, 168, 2, theme().detail);
    }
    s::text(im, filters[filter_], 79, 184, 26, theme().ink, true);
    const auto games = visible_games();
    s::right(im, std::to_string(games.size()) + " games", 560, 194, 13, theme().muted);
    Image strip = im;
    const double offset = scroll_position(now);
    for (int slot = 0; slot < int(games.size()); ++slot) {
        const int y = grid_y + int(std::lround((slot / columns_ - offset) * (card_h + gap)));
        if (y + card_h + 8 < grid_y - 8 || y - 8 >= footer_y - 8) continue;
        draw_card(strip, games[std::size_t(slot)], grid_x + slot % columns_ * (card_w + gap), y, now);
    }
    for (int y = grid_y - 8; y < footer_y - 8; ++y)
        for (int x = 64; x < 576; ++x)
            im.put(x, y, strip.pixels[std::size_t(y * im.width + x)]);
    const int rows = (int(games.size()) + columns_ - 1) / columns_;
    if (rows > rows_shown_) {
        const int track = footer_y - grid_y - 16, thumb = track * rows_shown_ / rows;
        ui::rounded(im, 594, grid_y, 4, track, 2, theme().line);
        ui::rounded(im, 594, grid_y + int((track - thumb) * offset / (rows - rows_shown_)), 4, thumb, 2, theme().muted);
    }
    if (games.empty())
        s::wrap(im, "Save a game while playing and it will appear here, ready to continue.", 79, 270, 480, 4, 21, theme().muted);
    ui::rect(im, 0, footer_y, 640, 82, theme().card);
    ui::rect(im, 0, footer_y, 640, 1, theme().line);
    if (!games.empty()) {
        const auto& game = library_.games()[selected_];
        s::clipped(im, game.title, 22, footer_y + 14, 460, 20, theme().ink, true);
        s::clipped(im, status(selected_), 22, footer_y + 45, 460, 13, theme().muted);
        button(im, "Details", 506, footer_y + 20, 112, 38);
    }
    if (!message_.empty() && now - message_time_ < 8) {
        ui::rounded(im, 32, footer_y - 80, 576, 66, 7, theme().card);
        s::wrap(im, message_, 44, footer_y - 74, 552, 3, 15, message_ok_ ? s::green : s::red);
    }
    return im;
}

Image Launcher::draw_scene(double now) const {
    if (chapter_) return chapter_->draw();
    if (portrait_) return details_ ? draw_portrait_details() : draw_portrait(now);
    Image im(details_ ? detail_width() : shelf_width_, details_ ? detail_height() : platform::ui_height);
    if (details_) {
        draw_details(im);
        if (!message_.empty() && now - message_time_ < 8) {
            ui::rounded(im, 300, im.height - 160, im.width - 336, 60, 6, theme().card);
            s::wrap(im, message_, 312, im.height - 154, im.width - 360, 2, 15, message_ok_ ? s::green : s::red);
        }
        if (confirm_remove_) draw_confirm(im);
        return im;
    }
    ui::fill(im, theme().paper);
    if (filter_ >= 2) {
        const char* backgrounds[] = {"commodore", "sonic", "layton"};
        s::backdrop(im, backgrounds[filter_ - 2]);
        // Feather a paper wash beneath navigation and the brand. The bitmap
        // stays visible, but its fine lines never compete with small labels.
        for (int y = 0; y < 420; ++y) for (int x = 0; x < 560; ++x) {
            int alpha = 0;
            if (y < 68) alpha = int(110 * std::clamp((560.0 - x) / 110, 0.0, 1.0));
            else if (x < 184) alpha = int(180 * std::clamp((420.0 - y) / 70, 0.0, 1.0));
            if (alpha) {
                auto& pixel = im.pixels[std::size_t(y * im.width + x)];
                pixel = ui::mix(pixel, theme().paper, alpha);
            }
        }
    } else ui::rect(im, 0, 0, im.width, 68, theme().card);
    s::reprise(im, 22, 17);
    s::text(im, "Reprise", 69, 13, 31, theme().ink, true);
    ui::rect(im, 195, 21, 1, 28, theme().line);
    s::text(im, "Classic games, rebuilt and revealed.", 212, 27, 16, theme().muted);
    button(im, "+  Import game", im.width - 162, 20, 138, 36, true);
    ui::rect(im, 184, 68, 1, 490, theme().line);
    const char* headings[] = {"Your library", "Continue playing", "Commodore classics", "Sonic collection", "Layton collection"};
    const char* subtitles[] = {"Your games. A closer look.", "Pick up where you left off.", "Small machines. Remarkable worlds.", "Speed, colour and a little momentum.", "Every town has a mystery."};
    // The artwork carries the collection identity. It stays anchored while
    // the cards scroll; text and controls remain real SDL-rendered elements.
    s::text(im, headings[filter_], grid_x, 73, 25, theme().ink, true);
    s::text(im, subtitles[filter_], grid_x, 104, 12, theme().muted);
    for (int i = 0; i < filter_count; ++i) {
        const bool active = i == filter_;
        if (active) {
            ui::rounded(im, 12, filter_y(i), 164, 38, 6, theme().accent);
            if (i >= 3) ui::rect(im, 12, filter_y(i) + 7, 3, 24, theme().detail);
        }
        s::text(im, filters[i], 22, filter_y(i) + 11, 13, active ? s::white : theme().ink, active);
        if (sidebar_focus_ && sidebar_row_ == i) ui::rect(im, 22, filter_y(i) + 31, 132, 1, active ? s::white : theme().accent);
    }
    ui::rect(im, 22, 194, 140, 1, theme().line);
    s::text(im, "Collections", 22, 211, 13, theme().muted, true);
    if (filter_ < 2) {
        s::text(im, "Your games.", 22, 486, 13, theme().muted);
        s::text(im, "A closer look.", 22, 505, 13, theme().muted);
    }
    const auto games = visible_games();
    // Render the continuous strip separately so incoming/outgoing rows are
    // clipped beneath the stationary header and footer, including their shadows.
    Image strip = im; // Preserve paper texture between the moving cards.
    const double offset = scroll_position(now);
    for (int slot = 0; slot < int(games.size()); ++slot) {
        const int y = grid_y + int(std::lround((slot / columns_ - offset) * (card_h + gap)));
        if (y + card_h + 8 < grid_y - 8 || y - 8 >= footer_y - 8) continue;
        draw_card(strip, games[std::size_t(slot)], grid_x + slot % columns_ * (card_w + gap), y, now);
    }
    for (int y = grid_y - 8; y < footer_y - 8; ++y)
        for (int x = grid_x - 8; x < im.width - 20; ++x)
            im.put(x, y, strip.pixels[std::size_t(y * strip.width + x)]);
    if (int(games.size()) > columns_ * rows_shown_) {
        const int rows = (int(games.size()) + columns_ - 1) / columns_;
        const int track = card_h * 2 + gap, thumb = track * 2 / rows;
        ui::rounded(im, im.width - 17, grid_y, 4, track, 2, theme().line);
        ui::rounded(im, im.width - 17, grid_y + int(std::lround((track - thumb) * offset / (rows - rows_shown_))), 4, thumb, 2, theme().muted);
    }
    if (games.empty()) {
        s::text(im, "Your next adventure is waiting", 252, 246, 24, theme().ink, true);
        s::wrap(im, "Save a game while playing and it will appear here, ready to continue.", 252, 290, 570, 3, 18, theme().muted);
        return im;
    }
    ui::rect(im, 0, footer_y, im.width, im.height - footer_y, theme().card);
    ui::rect(im, 0, footer_y, im.width, 1, theme().line);
    const auto& game = library_.games()[selected_];
    s::cover(im, game.id, 24, 576, 100, 46);
    s::clipped(im, game.title, 140, 573, im.width - 320, 20, theme().ink, true);
    s::text(im, (game.release_year ? std::to_string(game.release_year) + "   /   " : "") + std::string(machine_name(game.machine)) + "   /   " + status(selected_), 140, 602, 13, theme().muted);
    s::right(im, std::to_string(games.size()) + " games", im.width - 164, 586, 13, theme().muted);
    button(im, "Details", im.width - 136, 578, 112, 38);
    if (!message_.empty() && now - message_time_ < 8) {
        ui::rounded(im, 198, 480, 718, 58, 7, theme().card);
        ui::rect(im, 198, 488, 3, 42, message_ok_ ? s::green : s::red);
        s::wrap(im, message_, 212, 487, 690, 2, 15, message_ok_ ? s::green : s::red);
    }
    return im;
}
}
