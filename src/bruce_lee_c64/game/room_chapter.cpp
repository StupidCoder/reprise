// The experiment invokes the same room-effect update as World::pass; it never
// prints the opening itself and does not run actor movement or combat.
// Room numbering shown to readers is one-based; the reconstruction is zero-based.
#include "bruce_lee_c64/game/room_chapter.hpp"
#include "bruce_lee_c64/game/screen.hpp"
#include "ui/shelf.hpp"
#include <algorithm>
namespace bruce_lee::game {
namespace s = ui::shelf;
namespace {
bool hit(const platform::Controls &c, int x, int y, int w, int h) {
    return c.click && c.mouse_x >= x && c.mouse_x < x + w && c.mouse_y >= y && c.mouse_y < y + h;
}
void enlarged(platform::Image &out, const platform::Image &source, int x, int y, int width, int height) {
    for (int dy = 0; dy < height; ++dy)
        for (int dx = 0; dx < width; ++dx)
            out.put(x + dx, y + dy,
                    source.pixels[std::size_t((dy * source.height / height) * source.width +
                                              dx * source.width / width)]);
}
} // namespace
RoomChapter::RoomChapter(const std::filesystem::path &content) : assets_(data::load_assets(content)) {
    reset();
}
void RoomChapter::reset() {
    running_ = false;
    credit_ = 0;
    world_ = std::make_unique<World>(assets_);
    world_->new_game();
    for (int i = 0; i < 4; ++i) {
        const int room = i == 3 ? 6 : i;
        auto &p = world_->progress[std::size_t(room)];
        p.remaining = cleared_[std::size_t(i)] ? 0 : assets_.lanterns_per_room[std::size_t(room)];
        std::fill(p.lanterns.begin(), p.lanterns.end(), !cleared_[std::size_t(i)]);
    }
    // Entry also runs the room rule. Temporarily block this floor's trigger so
    // the laboratory can display its closed state before the first update.
    const auto room7 = world_->progress[6].remaining;
    world_->progress[6].remaining = 0;
    world_->enter_room(1, assets_.start_x[1], assets_.start_y[1], false);
    world_->progress[6].remaining = room7;
    for (auto &actor : world_->actors.slots)
        actor.flags = 0;
    before_ = world_->field;
    before_picture_ = world_frame(*world_).rasterise().to_image(0, 0);
}
void RoomChapter::step() {
    world_->ninja_timer = 255;
    world_->yamo_timer = 255;
    for (int i = 0; i < 3; ++i)
        world_->frame();
    world_->pending_sounds.clear();
    world_->step_room_effects();
    if (stage() == 4)
        running_ = false;
}
int RoomChapter::stage() const {
    const int n = world_->room_counters[0];
    return n >= 128 ? 4 : 3 - n;
}
int RoomChapter::changed_cells() const {
    int count = 0;
    for (int row = 23; row < 25; ++row)
        for (int col = 0; col < 40; ++col)
            count += before_.at(col, row).code != world_->field.at(col, row).code;
    return count;
}
void RoomChapter::update(const platform::Controls &in, double now) {
    const double dt = last_ < 0 ? 0 : std::clamp(now - last_, 0.0, .1);
    last_ = now;
    if (hit(in, 656, 24, 136, 32)) {
        article_ = false;
        return;
    }
    if (hit(in, 800, 24, 136, 32)) {
        article_ = true;
        return;
    }
    if (article_) {
        article_scroll_ =
            std::clamp(article_scroll_ + in.wheel * 36 + (int(in.nav_down) - int(in.nav_up)) * 36 +
                           (int(in.page_down) - int(in.page_up)) * 280,
                       0, 400);
        return;
    }
    for (int i = 0; i < 4; ++i)
        if (hit(in, 624, 132 + i * 47, 304, 38)) {
            cleared_[std::size_t(i)] = !cleared_[std::size_t(i)];
            reset();
            return;
        }
    if (hit(in, 24, 86, 110, 34) || in.accept)
        running_ = !running_;
    if (hit(in, 144, 86, 110, 34)) {
        running_ = false;
        step();
    }
    if (hit(in, 264, 86, 110, 34))
        reset();
    if (running_) {
        credit_ += dt;
        while (credit_ >= .4) {
            credit_ -= .4;
            step();
            if (!running_)
                break;
        }
    }
}
platform::Image RoomChapter::draw() const {
    platform::Image im(960, 640);
    ui::fill(im, s::paper);
    s::button(im, "< Lessons", 24, 24, 112, 32);
    s::text(im, "Bruce Lee / A floor opens", 152, 27, 23, s::ink, true);
    s::button(im, "Experiment", 656, 24, 136, 32, !article_);
    s::button(im, "How it works", 800, 24, 136, 32, article_);
    const auto current = world_frame(*world_).rasterise().to_image(0, 0);
    if (article_) {
        // A scrollable illustrated article: paragraphs, figures and a comparison
        // table, with no simulation controls or requirement for gameplay input.
        platform::Image page(880, 900);
        ui::fill(page, s::card);
        s::text(page, "A rule chooses WHEN. Data supplies WHAT.", 24, 20, 25, s::ink, true);
        s::wrap(page,
                "This floor is a useful example of the boundary between code and data. In Joyport's "
                "reconstruction, a rule specific to room 2 checks progress in other rooms. It is not a "
                "general-purpose script interpreter executing a room program.",
                24, 66, 820, 4, 18);
        s::text(page, "CLOSED FLOOR", 24, 170, 14, s::muted, true);
        s::text(page, "CURRENT EXPERIMENT", 460, 170, 14, s::muted, true);
        enlarged(page, before_picture_, 24, 198, 384, 240);
        enlarged(page, current, 460, 198, 384, 240);
        s::wrap(page,
                "The highlighted experiment concerns the bottom floor. Opening it changes character cells in "
                "the playfield. The terrain system reads that same grid, so this is more than a picture "
                "painted over solid ground.",
                24, 460, 820, 3, 17);
        const char *left[] = {"When to start", "Opening cadence", "What each stage draws",
                              "How the result affects play"};
        const char *right[] = {"Room-specific progress conditions",
                               "A counter, advancing on alternate logic passes",
                               "Character patterns read from your imported copy",
                               "The shared playfield is also read as terrain"};
        for (int i = 0; i < 4; ++i) {
            ui::rect(page, 24, 550 + i * 48, 820, 46, i % 2 ? s::paper : s::pale);
            s::text(page, left[i], 36, 565 + i * 48, 15, s::ink, true);
            s::text(page, right[i], 270, 565 + i * 48, 14);
        }
        s::wrap(page,
                "Four eligible updates apply the floor's opening sequence. Once finished, the room's counter "
                "prevents it from repeating during this visit. Reset recreates the visit; changing a "
                "prerequisite also resets this experiment.",
                24, 772, 820, 4, 17);
        for (int y = 0; y < 500; ++y)
            for (int x = 0; x < 880; ++x)
                im.put(40 + x, 86 + y, page.pixels[std::size_t((y + article_scroll_) * 880 + x)]);
        ui::rect(im, 936, 86, 4, 500, s::line);
        ui::rect(im, 936, 86 + article_scroll_ * 220 / 400, 4, 280, s::muted);
        s::text(im, "Scroll / Up / Down / Page keys to read. Experiment state is paused while reading.", 24,
                604, 13, s::muted);
        return im;
    }
    s::button(im, running_ ? "Pause" : "Run", 24, 86, 110, 34, true);
    s::button(im, "Step pass", 144, 86, 110, 34);
    s::button(im, "Reset", 264, 86, 110, 34);
    s::text(im,"Conditions reset the demo.",394,98,12,s::muted);
    s::text(im, "ROOM 2  /  live reconstructed playfield", 24, 140, 16, s::ink, true);
    enlarged(im, current, 24, 176, 576, 360);
    // Mark only cells actually changed in the floor rows, relative to entry.
    for (int row = 23; row < 25; ++row)
        for (int col = 0; col < 40; ++col)
            if (before_.at(col, row).code != world_->field.at(col, row).code) {
                const int x = 24 + col * 576 / 40, y = 176 + row * 360 / 25;
                ui::rect(im, x, y, 14, 2, s::cobalt);
                ui::rect(im, x, y + 12, 14, 2, s::cobalt);
            }
    s::text(im, "Trigger conditions  /  click to change", 624, 90, 16, s::ink, true);
    for (int i = 0; i < 4; ++i) {
        const int room = i == 3 ? 7 : i + 1;
        s::button(im,
                  "Room " + std::to_string(room) + (cleared_[std::size_t(i)] ? ": cleared" : ": not cleared"),
                  624, 132 + i * 47, 304, 38, cleared_[std::size_t(i)]);
    }
    const bool ready = cleared_[0] && cleared_[1] && cleared_[2] && !cleared_[3];
    s::text(im, ready ? "Conditions met" : "Waiting for prerequisites", 624, 338, 20,
            ready ? s::green : s::ink, true);
    s::wrap(im,
            "Rooms 1, 2 and 3 must be cleared. Room 7 must NOT be cleared. Here, cleared means no lanterns "
            "remain.",
            624, 373, 300, 4, 16);
    s::text(im, "OPENING SEQUENCE", 624, 458, 13, s::muted, true);
    for (int i = 0; i < 4; ++i) {
        ui::rounded(im, 624 + i * 77, 486, 66, 40, 4, stage() > i ? s::cobalt : s::card);
        s::text(im, std::to_string(i + 1), 650 + i * 77, 498, 17, stage() > i ? s::white : s::ink, true);
    }
    s::text(im, std::to_string(changed_cells()) + " floor cells differ from entry", 624, 547, 14, s::muted);
    s::text(im, "Blue marks: changed floor cells", 24, 554, 14, s::cobalt, true);
    s::text(im, "One stage per alternate logic pass. Reset to repeat.", 24, 580, 14, s::muted);
    s::text(im,
            "Practice progress only. No saves. This explains the reconstruction, not a proof of original "
            "accuracy.",
            24, 614, 12, s::muted);
    return im;
}
} // namespace bruce_lee::game
