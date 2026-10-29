// All illustrated tiles, run lengths and byte counts come from the imported
// files through the existing decoder. No reconstructed gameplay runs here.
#include "bruce_lee_c64/game/background_article.hpp"
#include "bruce_lee_c64/data/assets.hpp"
#include "bruce_lee_c64/data/preview.hpp"
#include "bruce_lee_c64/game/screen.hpp"
#include "ui/shelf.hpp"
#include <algorithm>
#include <set>
#include <stdexcept>
namespace bruce_lee::game {
namespace s = ui::shelf;
namespace {
void crop(platform::Image &out, const platform::Image &source, int sx, int sy, int sw, int sh, int x, int y,
          int scale) {
    for (int py = 0; py < sh * scale; ++py)
        for (int px = 0; px < sw * scale; ++px)
            out.put(x + px, y + py,
                    source.pixels[std::size_t((sy + py / scale) * source.width + sx + px / scale)]);
}
} // namespace
BackgroundArticle::BackgroundArticle(const std::filesystem::path &content) {
    const auto assets = data::load_assets(content);
    if (assets.rooms.empty())
        throw std::runtime_error("No imported room maps available.");
    const auto &room = assets.rooms.front();
    const auto flat = data::room_image(assets, 0);
    PlayScreen scene;
    scene.room = 0;
    const auto coloured = play_frame(assets, scene).rasterise().to_image(0, 0);
    ui::fill(page_, s::card);
    auto heading = [&](const char *title, int y) { s::text(page_, title, 24, y, 25, s::ink, true); };
    auto prose = [&](std::string text, int y, int lines = 3) { s::wrap(page_, text, 24, y, 824, lines, 17); };
    heading("How a room fits into a handful of bytes", 24);
    prose("A background looks like a picture. Its stored form is a sequence of small tile references, "
          "compressed wherever the same reference repeats. These illustrations and measurements come from "
          "your imported copy.",
          76, 4);
    crop(page_, coloured, 0, 24, 320, 176, 24, 176, 2);
    for (int x = 0; x <= 40; ++x)
        ui::rect(page_, 24 + x * 16, 176, 1, 352, s::muted);
    for (int y = 0; y <= 11; ++y)
        ui::rect(page_, 24, 176 + y * 32, 640, 1, s::muted);
    s::text(page_, "ROOM 1", 692, 188, 16, s::ink, true);
    s::wrap(page_, "40 columns", 692, 224, 158, 2, 17);
    s::text(page_, "11 tile rows", 692, 252, 17);
    s::text(page_, "440 cells", 692, 294, 21, s::cobalt, true);
    s::wrap(page_, "Grid lines mark the logical tiles. Each spans two character rows.", 692, 342, 150, 6, 15);
    heading("1  One reference, two character halves", 574);
    prose("Each logical tile is 8 by 16 display pixels. Its upper and lower halves use paired characters. "
          "The stored graphic has eight rows: the decoder doubles each row to produce the taller tile.",
          619, 3);
    std::set<int> seen;
    int n = 0;
    for (int index = 0; index < 440 && n < 3; ++index) {
        const int code = room.cells[std::size_t(index)] & 127;
        if (!seen.insert(code).second)
            continue;
        const int x = 24 + n * 280, sx = (index % 40) * 8, sy = (index / 40) * 16;
        s::text(page_, "Tile " + std::to_string(code), x, 704, 16, s::ink, true);
        // Compress every pair of expanded rows back to its actual stored row.
        platform::Image stored(8, 8);
        for (int y = 0; y < 8; ++y)
            for (int px = 0; px < 8; ++px)
                stored.put(px, y, flat.pixels[std::size_t((sy + y * 2) * 320 + sx + px)]);
        crop(page_, stored, 0, 0, 8, 8, x, 746, 6);
        s::text(page_, ">", x + 64, 774, 24, s::muted);
        crop(page_, flat, sx, sy, 8, 16, x + 100, 724, 6);
        ui::rect(page_, x + 98, 772, 52, 2, s::cobalt);
        s::text(page_, "8 stored rows", x, 836, 13);
        s::text(page_, "16 display rows", x + 112, 860, 13);
        ++n;
    }
    prose("The same tile graphics are reused throughout a room and across rooms sharing a character set. "
          "Larger walls and buildings are arrangements of these tiles, rather than whole stored pictures.",
          910, 3);
    heading("2  Repeat a tile, or copy a varied stretch", 1008);
    const data::Room::MapRun *repeated = nullptr;
    const data::Room::MapRun *literal = nullptr;
    for (const auto &run : room.runs) {
        if (run.first + run.count > 440)
            continue;
        if (!run.literal && (!repeated || run.count > repeated->count))
            repeated = &run;
        if (run.literal && (!literal || run.count > literal->count))
            literal = &run;
    }
    auto run_figure = [&](const data::Room::MapRun *run, int y) {
        if (!run)
            return;
        const int shown = std::min(8, run->count);
        const int bytes = run->literal ? 1 + run->count : 2;
        s::text(page_,
                std::string(run->literal ? "LITERAL" : "REPEAT") + "  /  " + std::to_string(run->count) +
                    " cells from " + std::to_string(bytes) + " bytes",
                24, y, 19, s::cobalt, true);
        for (int i = 0; i < shown; ++i) {
            const int index = run->first + i;
            crop(page_, flat, (index % 40) * 8, (index / 40) * 16, 8, 16, 24 + i * 48, y + 32, 4);
        }
        s::wrap(page_,
                run->literal
                    ? "One control byte, followed by every cell reference in this run."
                    : "One control byte says how many times; one cell reference says what to repeat.",
                442, y + 34, 392, 3, 16);
        s::text(page_,
                "First " + std::to_string(shown) + " cells shown; stream starts at cell " +
                    std::to_string(run->first + 1) + ".",
                24, y + 106, 13, s::muted);
    };
    run_figure(repeated, 1066);
    run_figure(literal, 1220);
    prose("These are actual records from room 1, not invented examples. Runs follow the flattened grid and "
          "can cross row boundaries. A separate end marker finishes the map stream.",
          1380, 3);
    heading("3  Color is a separate ingredient", 1480);
    crop(page_, flat, 0, 0, 320, 176, 24, 1554, 1);
    crop(page_, coloured, 0, 24, 320, 176, 460, 1554, 1);
    s::text(page_, "Shared play palette only", 24, 1523, 15, s::ink, true);
    s::text(page_, "With the room's color changes", 460, 1523, 15, s::ink, true);
    prose("A cell reference also carries a color flag. Room color settings and horizontal color changes "
          "complete the scene. The two views above use the same decoded map and graphics, with different "
          "color treatment.",
          1750, 3);
    heading("4  What does the compression save?", 1850);
    const int bytes = room.encoded_bytes;
    s::text(page_, "Room 1: expanded grid", 24, 1900, 17, s::ink, true);
    ui::rect(page_, 24, 1932, 660, 25, s::pale);
    s::text(page_, "440 bytes", 706, 1935, 17);
    s::text(page_, "Room 1: measured map stream", 24, 1980, 17, s::ink, true);
    ui::rect(page_, 24, 2012, std::max(1, bytes * 660 / 440), 25, s::cobalt);
    s::text(page_, std::to_string(bytes) + " bytes", 706, 2015, 17);
    prose(std::to_string(440 - bytes) +
              " fewer bytes than the 440-cell grid. The stream count includes every control byte, payload "
              "byte and the end marker. It does not include shared graphics or room metadata.",
          2060, 3);
    s::text(page_, "YOUR IMPORTED ROOMS", 24, 2160, 16, s::ink, true);
    s::text(page_, "Room", 36, 2197, 14, s::muted);
    s::text(page_, "Stored map", 188, 2197, 14, s::muted);
    s::text(page_, "Expanded grid", 360, 2197, 14, s::muted);
    s::text(page_, "Extra decoded cells", 596, 2197, 14, s::muted);
    int row = 0;
    for (const auto &r : assets.rooms) {
        const int y = 2230 + row * 25;
        ui::rect(page_, 24, y, 824, 24, row % 2 ? s::paper : s::pale);
        s::text(page_, std::to_string(row + 1), 36, y + 5, 13);
        s::text(page_, std::to_string(r.encoded_bytes) + " bytes", 188, y + 5, 13);
        s::text(page_, std::to_string(r.cells.size()) + " bytes", 360, y + 5, 13);
        s::text(page_, std::to_string(r.spill), 596, y + 5, 13);
        ++row;
    }
    prose("Some final runs expand beyond the visible grid. Those extra cells are counted separately above; "
          "their encoded bytes remain included. No adjacent-room pointer subtraction or guessed compression "
          "ratio is used.",
          2760, 3);
    prose("Separate costs: each shared play character set occupies 1,024 stored bytes and expands to 2,048 "
          "character bytes. Colors, room headers, lanterns and exits are additional data. The map savings "
          "are not a whole-game compression ratio.",
          2850, 4);
    s::wrap(page_,
            "This article explains Joyport's reconstructed decoder using your imported data. It is not a "
            "claim of perfect original-game equivalence. No gameplay runs and no progress is changed.",
            24, 2960, 824, 3, 13);
}
void BackgroundArticle::update(const platform::Controls &in, double) {
    scroll_ = std::clamp(scroll_ + in.wheel * 48 + (int(in.nav_down) - int(in.nav_up)) * 48 +
                             (int(in.page_down) - int(in.page_up)) * 416,
                         0, page_.height - 500);
}
platform::Image BackgroundArticle::draw() const {
    platform::Image im(960, 640);
    ui::fill(im, s::paper);
    s::button(im, "< Lessons", 24, 24, 112, 32);
    s::text(im, "Bruce Lee / Building a background", 154, 29, 23, s::ink, true);
    for (int y = 0; y < 500; ++y)
        for (int x = 0; x < 880; ++x)
            im.put(40 + x, 86 + y, page_.pixels[std::size_t((scroll_ + y) * 880 + x)]);
    ui::rect(im, 936, 86, 4, 500, s::line);
    const int thumb = 500 * 500 / page_.height;
    ui::rect(im, 936, 86 + (500 - thumb) * scroll_ / (page_.height - 500), 4, thumb, s::muted);
    s::text(im, "Illustrated article  /  Scroll, arrow keys or Page Up / Down to read.  Esc: lessons", 24,
            608, 13, s::muted);
    return im;
}
} // namespace bruce_lee::game
