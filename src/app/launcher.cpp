#include "app/launcher.hpp"
#include "app/catalog.hpp"
#include "bruce_lee_c64/game/room_chapter.hpp"
#include "bruce_lee_c64/game/background_article.hpp"
#include "ui/shelf.hpp"
#include <algorithm>
#include <span>
#include "marble_madness_amiga/game/mechanics_chapter.hpp"
#include <exception>

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
constexpr int lessons_y = 474, remove_y = 520, side_button_h = 34;
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
std::span<const LessonEntry> lessons_for(const GameEntry& game) {
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
    selected_ = std::min(game, library_.games().size() - 1);
    filter_ = 0;
    sidebar_focus_ = false;
    reveal_selection();
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
    if (columns < 0 && at % 2 == 0) { sidebar_focus_ = true; return; }
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
    details_ = true;
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
void Launcher::close_details() { details_ = false; confirm_remove_ = false; previews_.clear(); }
void Launcher::activate() {
    const auto& game = library_.games()[selected_];
    if (!game.import) { open_details(); return; }
    if (!library_.installed(selected_)) pending_choice_ = true;
    else if (game.start) pending_play_ = true;
    else open_details();
}
Launcher::Request Launcher::update(const platform::Controls& in, double now) {
    if (chapter_) {
        if (in.back || hit(in,24,20,104,34)) chapter_.reset();
        else chapter_->update(in,now);
        return Request::none;
    }
    if (details_ && confirm_remove_) return update_confirm(in, now);
    if (details_) {
        if (hit(in,24,340,240,32)) {about_=true;lessons_=false;return Request::none;}
        if (hit(in,24,380,240,32)) {about_=false;lessons_=false;return Request::none;}
        if (about_ && (in.nav_left || in.nav_right)) {
            about_=false;
            lessons_=in.nav_left && !lessons_for(library_.games()[selected_]).empty();
            return Request::none;
        }
        if (!about_ && !lessons_ && page_==0 && in.nav_left) {
            about_=true;return Request::none;
        }
    }
    if (details_ && !lessons_for(library_.games()[selected_]).empty()) {
        if (hit(in,24,lessons_y,240,side_button_h)) {lessons_=true;about_=false; return Request::none;}
        if (lessons_) {
            if(in.nav_left || in.nav_right) {lessons_=false;about_=in.nav_left;return Request::none;}
            if(in.back || hit(in,24,22,104,36)) {close_details();return Request::none;}
            const int delta=int(in.nav_down)-int(in.nav_up)+in.wheel+
                (int(in.page_down)-int(in.page_up))*3;
            lesson_selected_=std::clamp(lesson_selected_+delta,0,int(lessons_for(library_.games()[selected_]).size())-1);
            if(lesson_selected_<lesson_list_.first())lesson_list_.scroll(lesson_selected_-lesson_list_.first());
            if(lesson_selected_>=lesson_list_.first()+3)lesson_list_.scroll(lesson_selected_-lesson_list_.first()-2);
            for(int row=0;row<lesson_list_.shown();++row) if(hit(in,308,178+row*106,600,96)) {
                lesson_selected_=lesson_list_.first()+row;open_lesson(now);return Request::none;
            }
            if(in.accept){open_lesson(now);return Request::none;}
            // Keep the ordinary import/play/remove controls available on the left.
            if(!hit(in,24,426,240,36)&&!hit(in,24,remove_y,240,side_button_h)&&!in.remove)return Request::none;
        }
    }
    if (details_) {
        const auto pages = previews_.size() + 1;
        if (in.back || in.accept || hit(in, 24, 22, 104, 36)) close_details();
        else if (!about_ && (in.nav_right || hit(in, 858, 112, 54, 34))) page_ = (page_ + 1) % pages;
        else if (!about_ && (in.nav_left || hit(in, 794, 112, 54, 34))) page_ = (page_ + pages - 1) % pages;
        else if (!about_ && page_ == 0 && files_.scrollable()) {
            files_.scroll(int(in.nav_down) - int(in.nav_up) + in.wheel);
            files_.page(int(in.page_down) - int(in.page_up));
        } else if (!about_ && in.nav_down) page_ = (page_ + 1) % pages;
        else if (!about_ && in.nav_up) page_ = (page_ + pages - 1) % pages;
        if (details_ && (in.remove || hit(in, 24, remove_y, 240, side_button_h)) && library_.installed(selected_)) {
            confirm_remove_ = true;
            confirm_focus_ = 0; // Cancel: Enter alone never deletes
            return Request::none;
        }
        if (details_ && hit(in, 24, 426, 240, 36) && library_.games()[selected_].import) {
            // The data viewer's action stays here; playing/importing uses the
            // same requests as the library so file dialogs and errors agree.
            if (!library_.installed(selected_)) pending_choice_ = true;
            else if (library_.games()[selected_].start) pending_play_ = true;
        }
    } else {
        if (sidebar_focus_) {
            if (in.nav_up || in.nav_down) set_filter((filter_ + 4 + int(in.nav_down) - int(in.nav_up)) % 4);
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
            set_filter(i); sidebar_focus_ = true;
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
        for (int slot = 0; slot < 4 && first_row_ * 2 + slot < int(games.size()); ++slot) {
            if (hit(in, grid_x + slot % 2 * (card_w + gap), grid_y + slot / 2 * (card_h + gap), card_w, card_h)) {
                const auto game = games[std::size_t(first_row_ * 2 + slot)];
                const bool launch = selected_ == game && !sidebar_focus_;
                selected_ = game; sidebar_focus_ = false;
                if (launch) activate();
            }
        }
        if (hit(in, 798, 20, 138, 36)) pending_choice_ = true;
        if (hit(in, 706, 578, 104, 38)) activate();
        if (in.info || (in.remove && library_.installed(selected_)) || hit(in, 824, 578, 112, 38)) open_details();
    }
    if (pending_choice_) { pending_choice_ = false; return Request::choose_image; }
    if (pending_play_) { pending_play_ = false; close_details(); return Request::play; }
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
    s::wrap(im, "Joyport deletes the files it extracted from your copy of " + game.title +
                ". Your own image file is not touched, so you can import it again.",
            dialog_x + 24, dialog_y + 58, dialog_w - 48, 3, 15, s::muted);
    for (int i = 0; i < 2; ++i) {
        if (confirm_focus_ == i) {
            ui::rounded(im, (i == 0 ? cancel_x : confirm_x) - 3, dialog_button_y - 3, dialog_button_w + 6,
                        dialog_button_h + 6, 9, s::cobalt);
        }
    }
    s::button(im, "Cancel", cancel_x, dialog_button_y, dialog_button_w, dialog_button_h);
    s::danger_button(im, "Remove", confirm_x, dialog_button_y, dialog_button_w, dialog_button_h, true);
}
void Launcher::show_message(std::string message, bool ok, double now) {
    message_ = std::move(message); message_ok_ = ok; message_time_ = now;
}
void Launcher::import(const std::vector<std::filesystem::path>& images, double now) {
    const auto& games = library_.games();
    for (const auto& image : images) {
        const auto outcome = library_.import(image, games[selected_].import ? std::optional(selected_) : std::nullopt);
        if (outcome.game) { close_details(); select(*outcome.game); }
        show_message(outcome.message, outcome.ok, now);
        if (!outcome.ok) break;
    }
}
void Launcher::show_error(std::string message, double now) { show_message(std::move(message), false, now); }

void Launcher::draw_card(Image& im, std::size_t index, int x, int y, double) const {
    const auto& game = library_.games()[index];
    const bool focused = index == selected_ && !sidebar_focus_;
    ui::rounded(im, x - 3, y - 3, card_w + 6, card_h + 6, 9, focused ? s::cobalt : s::line);
    ui::rounded(im, x, y, card_w, card_h, 7, s::card);
    s::cover(im, game.id, x + 6, y + 6, card_w - 12, 154);
    s::text(im, game.title, x + 12, y + 163, 18, s::ink, true);
    s::text(im, std::to_string(game.release_year) + "   /   " + machine_name(game.machine) + "   /   " + status(index), x + 12, y + 187, 12, s::muted);
    ui::rounded(im, x + card_w - 24, y + 174, 9, 9, 4,
                library_.installed(index) && game.start ? s::green : s::muted);
}

void Launcher::open_lesson(double now) {
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
    ui::rounded(im,288,102,648,446,10,s::card);
    s::text(im,"Behind the games",308,119,23,s::ink,true);
    s::text(im,"Choose a lesson. Explore at your own pace.",308,153,14,s::muted);
    for(int row=0;row<lesson_list_.shown();++row){
        const int index=lesson_list_.first()+row,y=178+row*106;
        const auto& lesson=lessons_for(library_.games()[selected_])[std::size_t(index)];
        ui::rounded(im,308,y,600,96,6,index==lesson_selected_?s::pale:s::paper);
        s::text(im,std::to_string(index+1)+"  "+lesson.title,322,y+12,19,s::ink,true);
        s::wrap(im,lesson.summary,322,y+39,564,2,14);
        s::text(im,library_.installed(selected_)?lesson.format:"Import your copy to open",322,y+77,12,s::muted);
    }
    lesson_list_.draw_bar(im,922,178,4,318,s::paper,s::muted);
    s::text(im,"Illustrated explanations and live experiments share this library.",308,516,13,s::muted);
    s::text(im,"Up / Down or scroll: choose     Enter or click: open     Esc: library",24,582,14,s::muted);
}

void Launcher::draw_details(Image& im) const {
    const auto& game = library_.games()[selected_];
    const bool installed = library_.installed(selected_);
    ui::fill(im, s::paper);
    s::button(im, "<  Library", 24, 22, 104, 36);
    s::text(im, "Game details", 150, 25, 22, s::ink, true);
    s::right(im, std::string(machine_name(game.machine)) + " / " + std::to_string(game.release_year), 934, 30, 15, s::muted);
    ui::rect(im, 24, 78, 912, 1, s::line);
    s::cover(im, game.id, 24, 106, 240, 110);
    s::wrap(im, game.title, 24, 234, 240, 2, 25, s::ink);
    s::text(im, status(selected_), 24, 304, 15, installed && game.start ? s::green : s::muted, true);
    s::button(im,"About the game",24,340,240,32,about_);
    s::button(im,"Extracted assets",24,380,240,32,!about_ && !lessons_);
    if (game.import && (!installed || game.start)) s::button(im, installed ? "Play game" : "Import your copy", 24, 426, 240, 36, true);
    if (installed) s::danger_button(im, "Remove imported files", 24, remove_y, 240, side_button_h);
    if (!lessons_for(game).empty()) {
        s::button(im,"Lessons",24,lessons_y,240,side_button_h,lessons_);
        if(lessons_){draw_lessons(im);return;}
    }
    ui::rounded(im, 288, 102, 648, 446, 10, s::card);
    if (about_) {
        s::text(im,"THE GAME",312,124,12,s::cobalt,true);
        s::wrap(im,game.description,312,152,594,6,19,s::ink);
        ui::rect(im,312,298,594,1,s::line);
        s::clipped(im,game.history_title,312,318,594,21,s::ink,true);
        s::wrap(im,game.history,312,354,594,6,17,s::ink);
        s::wrap(im,"Sources: " + game.history_sources,312,494,594,3,12,s::muted);
        s::text(im,"Left / Right: explore sections     Esc or Enter: library",24,582,14,s::muted);
        return;
    }

    const auto pages = previews_.size() + 1;
    s::clipped(im, page_ == 0 ? (installed ? "Imported files" : "About this game") : previews_[page_ - 1].title, 308, 116, 460, 20, s::ink, true);
    if (pages > 1) { s::button(im, "<", 794, 112, 54, 34); s::button(im, ">", 858, 112, 54, 34); }
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
    s::text(im, "Left / Right: pages     Up / Down: scroll files     Esc or Enter: library", 24, 582, 14, s::muted);
    s::right(im, std::to_string(page_ + 1) + " / " + std::to_string(pages), 934, 582, 14, s::muted);
}

Image Launcher::draw(double now) const {
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
    s::button(im, "+  Import game", 798, 20, 138, 36);
    ui::rect(im, 184, 78, 1, 465, s::line);
    s::text(im, "Your library", grid_x, 69, 23, s::ink, true);
    s::text(im, "Release year · oldest first", 390, 76, 13, s::muted);
    for (int i = 0; i < 4; ++i) {
        const bool active = i == filter_;
        if (active) ui::rounded(im, 18, 110 + i * 46, 160, 38, 7, sidebar_focus_ ? s::cobalt : s::pale);
        s::text(im, filters[i], 30, 118 + i * 46, 15, active && sidebar_focus_ ? s::white : s::ink, active);
    }
    s::text(im, "Your classics.", 30, 430, 16, s::muted);
    s::text(im, "Rebuilt to play.", 30, 454, 16, s::muted);
    s::text(im, "C64 + Amiga", 30, 505, 12, s::muted);
    const auto games = visible_games();
    const int first = first_row_ * 2;
    for (int slot = 0; slot < 4 && first + slot < int(games.size()); ++slot)
        draw_card(im, games[std::size_t(first + slot)], grid_x + slot % 2 * (card_w + gap), grid_y + slot / 2 * (card_h + gap), now);
    if (games.size() > 4) {
        ui::ScrollList scroll; scroll.resize((int(games.size()) + 1) / 2, 2); scroll.scroll(first_row_);
        scroll.draw_bar(im, 943, grid_y, 4, card_h * 2 + gap, s::line, s::muted);
    }
    if (games.size() > 4) {
        s::button(im, "<", 774, 68, 32, 28);
        s::button(im, ">", 902, 68, 32, 28);
    }
    s::right(im, std::to_string(std::min(first + 1, int(games.size()))) + "–" +
        std::to_string(std::min(first + 4, int(games.size()))) + " of " + std::to_string(games.size()), games.size() > 4 ? 890 : 934, 75, 13, s::muted);
    ui::rect(im, 0, footer_y, im.width, im.height - footer_y, s::card);
    ui::rect(im, 0, footer_y, im.width, 1, s::line);
    const auto& game = library_.games()[selected_];
    s::cover(im, game.id, 24, 576, 100, 46);
    s::text(im, game.title, 140, 573, 20, s::ink, true);
    s::text(im, std::to_string(game.release_year) + "   /   " + machine_name(game.machine) + "   /   " + status(selected_), 140, 602, 13, s::muted);
    s::text(im, "Arrows: choose   I: details", 444, 586, 13, s::muted);
    const auto action = !game.import ? "About" : !library_.installed(selected_) ? "Import" : game.start ? "Play" : "View data";
    s::button(im, action, 706, 578, 104, 38, true);
    s::button(im, "Details", 824, 578, 112, 38);
    if (!message_.empty() && now - message_time_ < 8) {
        ui::rounded(im, 198, 480, 718, 58, 7, s::card);
        ui::rect(im, 198, 488, 3, 42, message_ok_ ? s::green : s::red);
        s::wrap(im, message_, 212, 487, 690, 2, 15, message_ok_ ? s::green : s::red);
    }
    return im;
}
}
