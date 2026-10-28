#include "app/launcher.hpp"
#include "app/catalog.hpp"
#include "mindwalker_amiga/game/player_guide.hpp"
#include "bruce_lee_c64/game/room_chapter.hpp"
#include "bruce_lee_c64/game/background_article.hpp"
#include "ui/shelf.hpp"
#include <algorithm>
#include <span>
#include "marble_madness_amiga/game/mechanics_chapter.hpp"
#include <exception>
#include <cmath>
#include <fstream>
#include <tuple>
#include "platform/files.hpp"

namespace app {
using platform::Image;
namespace s = ui::shelf;
namespace {
// Two fully visible rows in the 960 x 640 logical canvas. All input and drawing
// use these same bounds; selection brings its row into view, including on pads.
constexpr int grid_x = 198, grid_y = 106, card_w = 350, card_h = 208, gap = 18;
constexpr int footer_y = 558;
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
    return {};
}
constexpr const char* filters[] = {"All games", "Commodore 64", "Amiga", "Planned"};
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
    std::ifstream settings(library_.data_directory() / "options" / "launcher.txt");
    std::string setting;
    while (std::getline(settings, setting))
        if (setting == "Reduced motion = On") reduced_motion_ = true;
}

std::vector<std::size_t> Launcher::visible_games() const {
    std::vector<std::size_t> result;
    const auto& games = library_.games();
    for (std::size_t i = 0; i < games.size(); ++i) {
        if (filter_ == 1 && games[i].machine != Machine::c64) continue;
        if (filter_ == 2 && games[i].machine != Machine::amiga) continue;
        if (filter_ == 3 && games[i].import) continue;
        result.push_back(i);
    }
    std::stable_sort(result.begin(), result.end(), [&](auto a, auto b) {
        if (games[a].release_year != games[b].release_year)
            return games[a].release_year < games[b].release_year;
        return games[a].title < games[b].title;
    });
    return result;
}
void Launcher::reveal_selection() {
    const auto games = visible_games();
    const auto at = std::find(games.begin(), games.end(), selected_);
    if (at == games.end()) return;
    const int row = int(at - games.begin()) / 2;
    first_row_ = std::clamp(first_row_, std::max(0, row - 1), row);
    first_row_ = std::min(first_row_, std::max(0, (int(games.size()) + 1) / 2 - 2));
}
void Launcher::select(std::size_t game) {
    if (library_.games().empty()) return;
    transition_from_.reset();
    hovered_.reset();
    if (selected_ != game) { save_preview_ = {}; previous_save_.reset(); }
    selected_ = std::min(game, library_.games().size() - 1);
    filter_ = 0;
    sidebar_focus_ = false;
    reveal_selection();
    scroll_from_ = first_row_; scroll_start_ = -10;
}
void Launcher::set_filter(int filter) {
    filter_ = std::clamp(filter, 0, 3);
    first_row_ = 0;
    const auto games = visible_games();
    if (!games.empty() && std::find(games.begin(), games.end(), selected_) == games.end()) selected_ = games.front();
    reveal_selection();
}
void Launcher::move(int columns, int rows) {
    const auto games = visible_games();
    if (games.empty()) return;
    int at = int(std::find(games.begin(), games.end(), selected_) - games.begin());
    if (at >= int(games.size())) at = 0;
    if (columns < 0 && at % 2 == 0) { sidebar_focus_ = true; sidebar_row_ = filter_; return; }
    if (rows) {
        const int target_row = std::clamp(at / 2 + rows, 0, (int(games.size()) - 1) / 2);
        at = std::min(target_row * 2 + at % 2, int(games.size()) - 1);
    }
    else if (columns && (columns < 0 || at % 2 == 0)) at = std::clamp(at + columns, 0, int(games.size()) - 1);
    selected_ = games[std::size_t(at)];
    reveal_selection();
}
std::string Launcher::status(std::size_t index) const {
    const auto& game = library_.games()[index];
    if (!game.import) return "Planned";
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
}
void Launcher::close_details() { details_ = false; confirm_remove_ = false; previews_.clear(); }
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
            if (action == 0 && entry.import) {
                if (!library_.installed(selected_))
                    return Request::choose_image;
                if (entry.start)
                    return Request::play;
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
                if (hit(in, 308, 178 + row * 106, 600, 96)) {
                    lesson_selected_ = lesson_list_.first() + row;
                    open_lesson(now);
                    return Request::none;
                }
            if (in.accept)
                open_lesson(now);
        } else if (!about_) {
            const auto pages = previews_.size() + 1;
            if (in.nav_right || hit(in, 858, 112, 54, 34))
                page_ = (page_ + 1) % pages;
            else if (in.nav_left || hit(in, 794, 112, 54, 34))
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
                sidebar_row_ = (sidebar_row_ + 4 + int(in.nav_down) - int(in.nav_up)) % 4;
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
            if (in.accept) activate();
        }
        for (int i = 0; i < 4; ++i) if (hit(in, 18, 110 + i * 46, 160, 38)) {
            set_filter(i); sidebar_row_ = i; sidebar_focus_ = true;
        }
        auto games = visible_games();
        const int page_step = int(hit(in, 902, 68, 32, 28)) - int(hit(in, 774, 68, 32, 28));
        if ((in.wheel || page_step) && !games.empty()) {
            const int rows = (int(games.size()) + 1) / 2;
            first_row_ = std::clamp(first_row_ + in.wheel + page_step * 2, 0, std::max(0, rows - 2));
            const int at = int(std::find(games.begin(), games.end(), selected_) - games.begin());
            selected_ = games[std::size_t(std::clamp(at, first_row_ * 2, std::min(int(games.size()) - 1, first_row_ * 2 + 3)))];
            sidebar_focus_ = false;
        }
        if (in.click) {
            if (const auto game = card_at(in.mouse_x, in.mouse_y, now)) {
                selected_ = *game; sidebar_focus_ = false;
                activate();
            }
        }
        if (hit(in, 798, 20, 138, 36)) pending_choice_ = true;
        if (hit(in, 824, 578, 112, 38)) open_details();
    }
    if (pending_choice_) { pending_choice_ = false; return Request::choose_image; }
    return Request::none;
}
Launcher::Request Launcher::update_confirm(const platform::Controls& in, double now) {
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
    const auto& game = library_.games()[selected_];
    ui::blend_rect(im, 0, 0, platform::ui_width, platform::ui_height, s::ink, 140);
    ui::rounded(im, dialog_x, dialog_y, dialog_w, dialog_h, 10, s::card);
    s::text(im, "Remove imported files?", dialog_x + 24, dialog_y + 20, 21, s::ink, true);
    s::wrap(im,
            "Joyport deletes the files it extracted from your copy of " + game.title +
                ". Your original image and persistent quick save are kept.",
            dialog_x + 24, dialog_y + 58, dialog_w - 48, 3, 15, s::muted);
    for (int i = 0; i < 2; ++i) {
        if (confirm_focus_ == i) {
            ui::rounded(im, (i == 0 ? cancel_x : confirm_x) - 3, dialog_button_y - 3, dialog_button_w + 6,
                        dialog_button_h + 6, 9, s::cobalt);
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
        const auto outcome = library_.import(image, games[selected_].import ? std::optional(selected_) : std::nullopt);
        if (outcome.game) {
            close_details(); select(*outcome.game);
            if (outcome.ok && !outcome.waiting) { import_game_ = *outcome.game; import_time_ = now; }
        }
        show_message(outcome.message, outcome.ok, now);
        if (!outcome.ok) break;
    }
}
void Launcher::show_error(std::string message, double now) { show_message(std::move(message), false, now); }

void Launcher::draw_card(Image& im, std::size_t index, int x, int y, double now) const {
    const auto& game = library_.games()[index];
    const bool focused = index == selected_ && !sidebar_focus_;
    const double emphasis = reduced_motion_ ? double(focused || hovered_ == index) : card_emphasis_[index];
    y -= int(std::lround(4 * emphasis));
    const double arrival = std::clamp((now - import_time_) / .32, 0.0, 1.0);
    if (!reduced_motion_ && index == import_game_) y -= int(std::lround(12 * (1 - arrival) * (1 - arrival)));
    for (int spread = 3; spread >= 1; --spread)
        ui::rounded(im, x - spread, y + 3 + spread, card_w + spread * 2, card_h, 9,
                    ui::mix(s::paper, s::muted, int(emphasis * (14 + (3 - spread) * 9))));
    ui::rounded(im, x - 3, y - 3, card_w + 6, card_h + 6, 9,
                ui::mix(s::line, s::cobalt, int(emphasis * 255)));
    ui::rounded(im, x, y, card_w, card_h, 7, s::card);
    if (!(omit_cover_ && index == selected_)) s::cover(im, game.id, x + 6, y + 6, card_w - 12, 154);
    s::text(im, game.title, x + 12, y + 163, 18, s::ink, true);
    s::text(im, std::to_string(game.release_year) + "   /   " + machine_name(game.machine) + "   /   " + status(index), x + 12, y + 187, 12, s::muted);
    if (index == import_game_ && now >= import_time_ && now - import_time_ < .8) {
        const int glow = reduced_motion_ ? 3 : int(3 + 5 * (1 - std::clamp((now - import_time_) / .8, 0.0, 1.0)));
        ui::rounded(im, x + card_w - 24 - glow, y + 174 - glow, 9 + 2 * glow, 9 + 2 * glow, 7,
                    ui::mix(s::card, s::green, 60));
    }
    ui::rounded(im, x + card_w - 24, y + 174, 9, 9, 4,
                library_.installed(index) && game.start ? s::green : s::muted);
}

void Launcher::open_lesson(double now) {
    if (library_.games()[selected_].id == "mindwalker_amiga") {
        chapter_ = std::make_unique<mindwalker::game::PlayerGuide>(lesson_selected_);
        return;
    }
    if(!library_.installed(selected_)) {show_error("Import your copy to open this lesson.",now);return;}
    try {
        const auto path=library_.content_directory(selected_);
        if(library_.games()[selected_].id=="marble_madness_amiga")
            chapter_=std::make_unique<marble_madness::game::MechanicsChapter>(path,lesson_selected_);
        else if(lesson_selected_==0) chapter_=std::make_unique<bruce_lee::game::EnemyChapter>(path);
        else if(lesson_selected_==1) chapter_=std::make_unique<bruce_lee::game::RoomChapter>(path);
        else chapter_=std::make_unique<bruce_lee::game::BackgroundArticle>(path);
    } catch(const std::exception& e){show_error(std::string("Could not open lesson: ")+e.what(),now);}
}
void Launcher::draw_lessons(Image& im) const {
    const bool guide = library_.games()[selected_].id == "mindwalker_amiga";
    ui::rounded(im,288,102,648,446,10,s::card);
    s::text(im, guide ? "How to play Mind Walker" : "Behind the games", 308, 119, 23, s::ink, true);
    s::text(im,
            guide ? "Choose a sequence, or start with the whole journey."
                  : "Choose a lesson. Explore at your own pace.",
            308, 153, 14, s::muted);
    for(int row=0;row<lesson_list_.shown();++row){
        const int index=lesson_list_.first()+row,y=178+row*106;
        const auto& lesson=lessons_for(library_.games()[selected_])[std::size_t(index)];
        ui::rounded(im,308,y,600,96,6,index==lesson_selected_?s::pale:s::paper);
        s::text(im,std::to_string(index+1)+"  "+lesson.title,322,y+12,19,s::ink,true);
        s::wrap(im,lesson.summary,322,y+39,564,2,14);
        s::text(im, (guide || library_.installed(selected_)) ? lesson.format : "Import your copy to open",
                322, y + 77, 12, s::muted);
    }
    lesson_list_.draw_bar(im,922,178,4,318,s::paper,s::muted);
    s::text(im,
            guide ? "Available before import. Read the rules without starting a game."
                  : "Illustrated explanations and live experiments share this library.",
            308, 516, 13, s::muted);
    s::text(im, "Up / Down or scroll: choose     Enter or click: open     Esc: details", 24, 582, 14,
            s::muted);
}

void Launcher::draw_overview(Image &im) const {
    const auto &game = library_.games()[selected_];
    const bool installed = library_.installed(selected_);
    ui::fill(im, s::paper);
    button(im, "<  Library", 24, 22, 104, 36);
    s::text(im, "Game details", 150, 25, 22, s::ink, true);
    s::right(im, std::string(machine_name(game.machine)) + " / " + std::to_string(game.release_year), 934, 30,
             15, s::muted);
    draw_identity(im);
    if (game.import)
        button(im, installed ? "Start" : "Import", 24, 374, 114, 40, detail_focus_ == 0);
    button(im, "Continue", 150, 374, 114, 40, detail_focus_ == 1 && save_preview_.available);
    if (!save_preview_.available)
        ui::blend_rect(im, 150, 374, 114, 40, s::paper, 150);
    button(im, "Documentation", 24, 434, 240, 40, detail_focus_ == 2);
    if (installed)
        button(im, "Delete game files", 24, 494, 240, 34, detail_focus_ == 3, true);
    ui::rounded(im, 316, 106, 620, 180, 9, s::card);
    s::wrap(im, game.description, 338, 126, 576, 6, 19, s::ink);
    ui::rounded(im, 316, 306, 620, 242, 9, s::card);
    if (save_preview_.image) {
        ui::rounded(im, 330, 320, 326, 214, 5, {20, 30, 34});
        ui::picture(im, *save_preview_.image, 336, 326, 314, 202);
    } else {
        ui::rounded(im, 330, 320, 326, 214, 5, s::paper);

    }
    s::wrap(im, save_preview_.available ? "Pick up where you left off" : "No saved game yet", 678, 326, 234,
            2, 18, s::ink);
    s::wrap(im, save_preview_.date, 678, 366, 234, 2, 14, s::muted);
    const std::string help =
        !save_preview_.error.empty() ? save_preview_.error
        : save_preview_.available ? "Continue restores this moment. Start begins a new game and keeps this "
                                    "save until you save again."
        : persistent_saves(game.id)
            ? "Use Quick save in the pause menu to keep your progress after closing the app."
            : "Persistent saves are not available for this game yet.";
    s::wrap(im, help, 678, 402, 234, 7, 16, save_preview_.error.empty() ? s::muted : s::red);
    s::text(im, "Up / Down: choose action     Enter: open     Esc: library", 24, 582, 14, s::muted);
}
void Launcher::draw_identity(Image &im) const {
    const auto &game = library_.games()[selected_];
    if (!omit_cover_) s::cover(im, game.id, 24, 106, 240, 158);
    s::wrap(im, game.title, 24, 280, 240, 2, 25, s::ink);
    s::text(im, status(selected_), 24, 340, 14, s::muted);
}

void Launcher::draw_details(Image& im) const {
    const auto& game = library_.games()[selected_];
    const bool installed = library_.installed(selected_);
    if (!documentation_) {
        draw_overview(im);
        return;
    }
    ui::fill(im, s::paper);
    button(im, "<  Details", 24, 22, 104, 36);
    s::text(im, "Documentation", 150, 25, 22, s::ink, true);
    s::right(im, std::string(machine_name(game.machine)) + " / " + std::to_string(game.release_year), 934, 30, 15, s::muted);
    ui::rect(im, 24, 78, 912, 1, s::line);
    draw_identity(im);
    button(im, "Extracted assets", 24, 374, 240, 40, !about_ && !lessons_);
    button(im, "Development notes", 24, 418, 240, 40, about_);
    if (!lessons_for(game).empty()) {
        button(im, game.id == "mindwalker_amiga" ? "How to play" : "Lessons", 24, lessons_y, 240,
                  side_button_h, lessons_);
        if(lessons_){draw_lessons(im);return;}
    }
    ui::rounded(im, 288, 102, 648, 446, 10, s::card);
    if (about_) {
        s::text(im, "Development notes", 312, 124, 23, s::ink, true);
        s::wrap(im, game.history_title, 312, 174, 594, 2, 24, s::ink);
        s::wrap(im, game.history, 312, 246, 594, 9, 19, s::ink);
        s::wrap(im, "Sources: " + game.history_sources, 312, 488, 594, 3, 13, s::muted);
        s::text(im, "Esc: details", 24, 582, 14, s::muted);
        return;
    }

    const auto pages = previews_.size() + 1;
    s::clipped(im, page_ == 0 ? (installed ? "Imported files" : "About this game") : previews_[page_ - 1].title, 308, 116, 460, 20, s::ink, true);
    if (pages > 1) { button(im, "<", 794, 112, 54, 34); button(im, ">", 858, 112, 54, 34); }
    if (page_ == 0) {
        if (!installed) {
            const auto parts = library_.imported_parts(selected_);
            s::wrap(im, game.import ? "Drop your own " + game.media + " onto this window, or choose Import your copy." +
                    (parts.empty() ? std::string() : " Imported so far: " + parts + ".") :
                "This title is on the roadmap. Import and play will become available in a future release.",
                312, 190, 592, 5, 18);

        } else {
            s::clipped(im, library_.release(selected_), 308, 151, 600, 13, s::muted);
            const auto files = library_.installed_files(selected_);
            for (int row = 0; row < files_.shown(); ++row) {
                const auto& [name, size] = files[std::size_t(files_.first() + row)];
                const int y = file_list_y + row * file_row;
                if (row % 2 == 0) ui::rect(im, 304, y, 612, file_row, s::paper);
                s::clipped(im, name, 314, y + 2, 408, 14);
                s::right(im, size_text(size), 900, y + 2, 13, s::muted);
            }
            files_.draw_bar(im, 924, file_list_y, 4, file_rows_shown * file_row, s::paper, s::muted);
            s::text(im, std::to_string(files.size()) + " files", 308, 470, 13, s::muted);
            s::wrap(im, details_error_.empty() ? (game.start ? "Original game data from your imported copy. Use the arrows to explore decoded graphics and levels." :
                "Native gameplay is still being rebuilt. Use the arrows to explore the data decoded so far.") : details_error_,
                308, 496, 600, 2, 14, details_error_.empty() ? s::muted : s::red);
        }
    } else {
        ui::rounded(im, 304, 160, 616, 370, 5, {20, 30, 34});
        ui::picture(im, previews_[page_ - 1].image, 310, 166, 604, 358);
    }
    ui::rect(im, 24, footer_y, 912, 1, s::line);
    s::text(im, "Left / Right: pages     Up / Down: scroll files     Esc: details", 24, 582, 14, s::muted);
    s::right(im, std::to_string(page_ + 1) + " / " + std::to_string(pages), 934, 582, 14, s::muted);
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
    if (reduced_motion_) return first_row_;
    return scroll_from_ + (first_row_ - scroll_from_) * ease((now - scroll_start_) / .24);
}

std::optional<std::size_t> Launcher::card_at(int x, int y, double now) const {
    // Hit-test the rendered positions, including partially visible entering rows.
    if (y < grid_y - 8 || y >= footer_y - 8) return std::nullopt;
    const auto games = visible_games();
    const double offset = scroll_position(now);
    for (int slot = 0; slot < int(games.size()); ++slot) {
        const auto index = games[std::size_t(slot)];
        const int cx = grid_x + slot % 2 * (card_w + gap);
        const double emphasis = reduced_motion_ ? double(index == selected_ || hovered_ == index) : card_emphasis_[index];
        const int cy = grid_y + int(std::lround((slot / 2 - offset) * (card_h + gap))) - int(std::lround(4 * emphasis));
        if (x >= cx && x < cx + card_w && y >= cy && y < cy + card_h) return index;
    }
    return std::nullopt;
}

std::array<int, 4> Launcher::cover_bounds(double now) const {
    const auto games = visible_games();
    const int slot = int(std::find(games.begin(), games.end(), selected_) - games.begin());
    const double emphasis = reduced_motion_ ? 1 : card_emphasis_[selected_];
    return {grid_x + slot % 2 * (card_w + gap) + 6,
            grid_y + int(std::lround((slot / 2 - scroll_position(now)) * (card_h + gap))) + 6 - int(std::lround(4 * emphasis)), card_w - 12, 154};
}

Launcher::Request Launcher::update(const platform::Controls& in, double now) {
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
    const auto request = update_input(in, now);
    if (filter_ != old_filter) {
        scroll_from_ = first_row_; scroll_start_ = -10; hovered_.reset();
    } else if (first_row_ != old_row) {
        scroll_from_ = old_scroll; scroll_start_ = now; hovered_.reset();
    }
    if (old && key() != before && !reduced_motion_ && request == Request::none) {
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
            ui::rect(*old, r[0], r[1], r[2], r[3], was_details ? s::paper : s::card);
        }
        transition_from_ = std::move(old);
        transition_start_ = now;
    }
    return request;
}

void Launcher::button(Image& im, std::string_view text, int x, int y, int w, int h, bool primary, bool danger) const {
    const bool pressed = press_keyboard_ ? primary : press_x_ >= x && press_x_ < x + w && press_y_ >= y && press_y_ < y + h;
    const double strength = reduced_motion_ || !pressed ? 0 : 1 - ease((draw_time_ - press_time_) / .14);
    ui::rounded(im, x, y + 3, w, h, 7, s::line);
    y += int(std::lround(2 * strength));
    if (danger) s::danger_button(im, text, x, y, w, h, primary);
    else s::button(im, text, x, y, w, h, primary);
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
    if (!reduced_motion_ && details_ && !documentation_ && save_preview_.image && now - save_fade_start_ < .22) {
        Image previous = result;
        ui::rect(previous, 336, 326, 314, 202, {20, 30, 34});
        if (previous_save_) ui::picture(previous, *previous_save_, 336, 326, 314, 202);
        dissolve(result, previous, 336, 326, 314, 202, ease((now - save_fade_start_) / .22));
    }
    if (transitioning) {
        const double t = ease((now - transition_start_) / duration);
        if (transition_ == Transition::asset)
            dissolve(result, *transition_from_, 304, 160, 616, 370, t);
        else if (transition_ == Transition::document)
            dissolve(result, *transition_from_, 288, 102, 648, 446, t, int(std::lround(8 * (1 - t))));
        else dissolve(result, *transition_from_, 0, 0, result.width, result.height, t, 0, true);
        if (transition_ == Transition::cover) {
            std::array<int, 4> r{};
            for (int n = 0; n < 4; ++n) r[n] = int(std::lround(cover_from_[n] + (cover_to_[n] - cover_from_[n]) * t));
            ui::rounded(result, r[0] - 2, r[1] + 4, r[2] + 4, r[3] + 2, 6, s::line);
            s::cover(result, library_.games()[selected_].id, r[0], r[1], r[2], r[3]);
        }
    }
    return result;
}

Image Launcher::draw_scene(double now) const {
    if (chapter_) return chapter_->draw();
    Image im(platform::ui_width, platform::ui_height);
    if (details_) {
        draw_details(im);
        if (!message_.empty() && now - message_time_ < 8) {
            ui::rounded(im, 300, 480, 624, 60, 6, s::card);
            s::wrap(im, message_, 312, 486, 600, 2, 15, message_ok_ ? s::green : s::red);
        }
        if (confirm_remove_) draw_confirm(im);
        return im;
    }
    ui::fill(im, s::paper);
    s::port(im, 24, 27);
    s::text(im, "Joyport", 82, 15, 35, s::ink, true);
    button(im, "+  Import game", 798, 20, 138, 36);
    ui::rect(im, 184, 78, 1, 465, s::line);
    s::text(im, "Your library", grid_x, 69, 23, s::ink, true);
    s::text(im, "Release year · oldest first", 390, 76, 13, s::muted);
    for (int i = 0; i < 4; ++i) {
        const bool active = i == filter_;
        if (active) ui::rounded(im, 18, 110 + i * 46, 160, 38, 7, sidebar_focus_ && sidebar_row_ == i ? s::cobalt : s::pale);
        s::text(im, filters[i], 30, 118 + i * 46, 15, active && sidebar_focus_ && sidebar_row_ == i ? s::white : s::ink, active);
    }
    s::text(im, "Your classics.", 30, 430, 16, s::muted);
    s::text(im, "Rebuilt to play.", 30, 454, 16, s::muted);
    s::text(im, "C64 + Amiga", 30, 505, 12, s::muted);
    const auto games = visible_games();
    const int first = first_row_ * 2;
    // Render the continuous strip separately so incoming/outgoing rows are
    // clipped beneath the stationary header and footer, including their shadows.
    Image strip(platform::ui_width, platform::ui_height);
    ui::fill(strip, s::paper);
    const double offset = scroll_position(now);
    for (int slot = 0; slot < int(games.size()); ++slot) {
        const int y = grid_y + int(std::lround((slot / 2 - offset) * (card_h + gap)));
        if (y + card_h + 8 < grid_y - 8 || y - 8 >= footer_y - 8) continue;
        draw_card(strip, games[std::size_t(slot)], grid_x + slot % 2 * (card_w + gap), y, now);
    }
    for (int y = grid_y - 8; y < footer_y - 8; ++y)
        for (int x = grid_x - 8; x < 940; ++x)
            im.put(x, y, strip.pixels[std::size_t(y * strip.width + x)]);
    if (games.size() > 4) {
        const int rows = (int(games.size()) + 1) / 2;
        const int track = card_h * 2 + gap, thumb = track * 2 / rows;
        ui::rounded(im, 943, grid_y, 4, track, 2, s::line);
        ui::rounded(im, 943, grid_y + int(std::lround((track - thumb) * offset / (rows - 2))), 4, thumb, 2, s::muted);
    }
    if (games.size() > 4) {
        button(im, "<", 774, 68, 32, 28);
        button(im, ">", 902, 68, 32, 28);
    }
    s::right(im, std::to_string(std::min(first + 1, int(games.size()))) + "–" +
        std::to_string(std::min(first + 4, int(games.size()))) + " of " + std::to_string(games.size()), games.size() > 4 ? 890 : 934, 75, 13, s::muted);
    ui::rect(im, 0, footer_y, im.width, im.height - footer_y, s::card);
    ui::rect(im, 0, footer_y, im.width, 1, s::line);
    const auto& game = library_.games()[selected_];
    s::cover(im, game.id, 24, 576, 100, 46);
    s::text(im, game.title, 140, 573, 20, s::ink, true);
    s::text(im, std::to_string(game.release_year) + "   /   " + machine_name(game.machine) + "   /   " + status(selected_), 140, 602, 13, s::muted);
    s::text(im, "Arrows: choose   Enter: details", 444, 586, 13, s::muted);
    button(im, "Details", 824, 578, 112, 38);
    if (!message_.empty() && now - message_time_ < 8) {
        ui::rounded(im, 198, 480, 718, 58, 7, s::card);
        ui::rect(im, 198, 488, 3, 42, message_ok_ ? s::green : s::red);
        s::wrap(im, message_, 212, 487, 690, 2, 15, message_ok_ ? s::green : s::red);
    }
    return im;
}
}
