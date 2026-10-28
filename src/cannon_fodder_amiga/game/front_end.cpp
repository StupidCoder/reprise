#include "cannon_fodder_amiga/game/front_end.hpp"
#include "amiga/adf.hpp"
#include "cannon_fodder_amiga/data/preview.hpp"
#include "cannon_fodder_amiga/game/panel.hpp"
#include <algorithm>
#include <memory>

namespace fodder::game {
namespace {

constexpr std::size_t file_offset(std::uint32_t address) {
    return address - 0x80000 + 0x1a; // program body starts 26 bytes into the file
}

// Frame tables and the width tables of their fonts.
constexpr std::uint32_t text_font_table = 0x8cfce;  // FONT.RAW glyphs
constexpr std::uint32_t text_font_widths = 0xa9da6;
constexpr std::uint32_t briefing_table = 0xa6dc8;   // rankfont.lbm
constexpr std::uint32_t hill_table = 0x8d3f6;       // hills.lbm
constexpr std::uint32_t widths_big = 0xa9c86;       // MISSION n, BRIEFING
constexpr std::uint32_t widths_small = 0xa9ce6;     // PHASE n OF m, objectives
constexpr std::uint32_t widths_header = 0xa9d46;    // HOME … AWAY

// Where the original loads the pictures the frames lie in.
constexpr std::uint32_t at_font = 0x57f60;    // FONT.RAW (after its 32 colour bytes)
constexpr std::uint32_t at_sheet = 0x3294a;   // rankfont.lbm, hills.lbm (copied there)

} // namespace

FrontFrames::FrontFrames(std::span<const std::uint8_t> program, std::uint32_t table)
    : program_(program), table_(table) {}

std::optional<FrontFrames::Frame> FrontFrames::frame(int group, int index) const {
    // The group pointers are unrelocated offsets from the program's start;
    // the frames' addresses are absolute (where the pictures are loaded).
    const auto g = file_offset(table_ + std::uint32_t(group) * 4);
    if (group < 0 || index < 0 || g + 4 > program_.size()) return std::nullopt;
    const auto entry = file_offset(0x80000 + amiga::read_be32(program_, g) + std::uint32_t(index) * 16);
    if (entry + 16 > program_.size()) return std::nullopt;
    Frame f;
    f.address = amiga::read_be32(program_, entry);
    f.width_words = amiga::read_be16(program_, entry + 8);
    f.height = amiga::read_be16(program_, entry + 10);
    f.offset_x = std::int8_t(program_[entry + 14]);
    return f;
}

platform::Image FrontScreen::image() const {
    // $8CE34: each gun less 15 - level (not below 0), per half of the palette.
    std::array<platform::Color, 64> colours{};
    for (std::size_t i = 0; i < colours.size(); ++i) {
        const int level = i < 16 ? fade_low : fade_high;
        const int down = 15 - std::clamp(level, 0, 15);
        const auto c = palette[i];
        const int r = std::max(((c >> 8) & 15) - down, 0), g = std::max(((c >> 4) & 15) - down, 0),
                  b = std::max((c & 15) - down, 0);
        colours[i] = data::to_color(amiga::Rgb4((r << 8) | (g << 4) | b));
    }
    platform::Image image(width, height);
    for (std::size_t i = 0; i < pixels.size(); ++i) image.pixels[i] = colours[std::min<std::size_t>(pixels[i], colours.size() - 1)];
    return image;
}

FrontEnd::FrontEnd(const data::Content& content, std::span<const std::uint8_t> program, const PanelArt& panel)
    : content_(content), panel_(panel), program_(program) {}

std::uint32_t FrontEnd::program_long(std::uint32_t address) const {
    const auto at = file_offset(address);
    return at + 4 <= program_.size() ? amiga::read_be32(program_, at) : 0;
}

std::string FrontEnd::program_text(std::uint32_t address) const {
    std::string text;
    for (auto at = file_offset(address); at < program_.size() && program_[at] < 0x80; ++at) {
        text += char(program_[at]);
    }
    return text;
}

// ---- drawing ----

void FrontEnd::draw_frame(const FrontFrames& frames, int group, int index, int x, int y) {
    const auto f = frames.frame(group, index);
    if (!f) return;
    // The picture loaded below the frame's address.
    const amiga::IlbmPicture* picture = nullptr;
    std::uint32_t base = 0;
    for (const auto& [at, p] : loaded_) {
        if (at <= f->address && at >= base) {
            base = at;
            picture = &p;
        }
    }
    if (!picture) return;
    const auto offset = f->address - base;
    const int sx = int(offset % 40) * 8, sy = int(offset / 40);
    const int w = (f->width_words - 1) * 16;
    x += f->offset_x;
    for (int line = 0; line < f->height; ++line) {
        for (int i = 0; i < w; ++i) {
            const int px = sx + i, py = sy + line;
            if (px >= picture->width || py >= picture->height) continue;
            const int c = picture->pixels[std::size_t(py * picture->width + px)];
            if (c == 0) continue; // cookie-cut
            const int dx = x + i, dy = y + line;
            if (dx < 0 || dy < 0 || dx >= FrontScreen::width || dy >= FrontScreen::height) continue;
            screen_buffer_.pixels[std::size_t(dy * FrontScreen::width + dx)] = std::uint8_t(c);
        }
    }
}

// $A997A: a text's width by the font's width table (a byte with bit 7 set
// other than $FF counts 10).
int FrontEnd::text_width(const std::string& text, std::uint32_t widths) const {
    int w = 0;
    for (const unsigned char c : text) {
        if (c >= 0x80) {
            w += 10;
            continue;
        }
        const auto at = file_offset(widths + c);
        w += at < program_.size() ? program_[at] : 0;
    }
    return w;
}

// $A9A06: A-Z are glyphs 0-25, 0-9 26-35, the marks ' . , ! 36-39, a-z
// 40-65; a space is a gap (or the glyph `space_glyph` when given, $821A6).
void FrontEnd::draw_text(const FrontFrames& frames, int group, std::uint32_t widths, const std::string& text, int x,
                         int y, int space_glyph) {
    static const std::string marks = "'.,!"; // $A9B00
    for (const unsigned char c : text) {
        int glyph = -1;
        if (c == ' ') glyph = space_glyph != 0 ? space_glyph : -1;
        else if (c >= '0' && c <= '9') glyph = c - '0' + 26;
        else if (c >= 'a' && c <= 'z') glyph = c - 'a' + 40;
        else if (c >= 'A' && c <= 'Z') glyph = c - 'A';
        else if (const auto m = marks.find(char(c)); m != std::string::npos) glyph = int(m) + 36;
        if (glyph >= 0) draw_frame(frames, group, glyph, x, y);
        const auto at = file_offset(widths + c);
        x += at < program_.size() ? program_[at] : 0;
    }
}

void FrontEnd::draw_centred(const FrontFrames& frames, int group, std::uint32_t widths, const std::string& text, int y,
                            int space_glyph) {
    draw_text(frames, group, widths, text, (FrontScreen::width - text_width(text, widths)) >> 1, y, space_glyph);
}

// ---- steps ----

void FrontEnd::run(Screen screen, std::vector<Step> steps) {
    screen_ = screen;
    steps_ = std::move(steps);
    step_ = 0;
    counter_ = 0;
    skipped_ = false;
}

// With the hill's and the load screen's pointer: the arrow ($23) on the
// sprites 0/1 in the screen's colours 16-31 (they fade with it), as
// Module::draw_pointer places it.
platform::Image FrontEnd::picture() const {
    if (!shows_pointer()) return screen_buffer_.image();
    auto screen = screen_buffer_;
    const auto piece = panel_.piece(0x23);
    const auto& pstuff = panel_.pstuff();
    const int left = pointer_x_ + 16, top = pointer_y_ - 4;
    for (int y = 0; y < piece.lines; ++y)
        for (int x = 0; x < std::min(piece.width, 16); ++x) {
            const int px = piece.x + x, py = piece.y + y, dx = left + x, dy = top + y;
            if (px >= pstuff.width || py >= pstuff.height || dx < 0 || dy < 0 || dx >= FrontScreen::width ||
                dy >= FrontScreen::height)
                continue;
            const int c = pstuff.pixels[std::size_t(py * pstuff.width + px)] & 15;
            if (c != 0) screen.pixels[std::size_t(dy * FrontScreen::width + dx)] = std::uint8_t(16 + c);
        }
    return screen.image();
}

void FrontEnd::tick(const app::GameInput& input) {
    if (screen_ == Screen::none) return;
    if (input.pointer) { // (the game's units, as in a mission)
        pointer_x_ = std::clamp(input.pointer_x - 16, -16, 0x12d);
        pointer_y_ = std::clamp(input.pointer_y + 4, 4, 0xe3);
    }
    while (step_ < steps_.size()) {
        if (!steps_[step_](input)) break;
        ++step_;
        counter_ = 0;
    }
    left_was_down_ = input.mouse_button;
    if (step_ >= steps_.size()) screen_ = Screen::none;
}

// $9D4B6: 16 steps of 3 frames from black.
FrontEnd::Step FrontEnd::fade_in() {
    return [this](const app::GameInput&) {
        if (counter_ == 0) screen_buffer_.fade_low = screen_buffer_.fade_high = 0;
        ++counter_;
        if (counter_ % 3 == 0) {
            ++screen_buffer_.fade_low;
            ++screen_buffer_.fade_high;
        }
        return counter_ >= 16 * 3;
    };
}

// $9D50A: down to black, a step every `frames_per_step` frames.
FrontEnd::Step FrontEnd::fade_out(int frames_per_step) {
    return [this, frames_per_step](const app::GameInput&) {
        if (counter_ == 0) screen_buffer_.fade_low = screen_buffer_.fade_high = 15;
        ++counter_;
        if (counter_ % frames_per_step == 0) {
            screen_buffer_.fade_low = std::max(screen_buffer_.fade_low - 1, 0);
            screen_buffer_.fade_high = std::max(screen_buffer_.fade_high - 1, 0);
        }
        return screen_buffer_.fade_low == 0 && screen_buffer_.fade_high == 0;
    };
}

// Waits `frames` (a fresh left click ends the wait and, in the intro, the
// whole presentation).
FrontEnd::Step FrontEnd::hold(int frames, bool click_ends) {
    return [this, frames, click_ends](const app::GameInput& input) {
        if (skipped_) return true;
        if (click_ends && input.mouse_button && !left_was_down_) {
            skipped_ = true;
            return true;
        }
        return ++counter_ >= frames;
    };
}

// ---- the intro ($9CBB0) ----

void FrontEnd::show_text_page(std::uint32_t page) {
    // $9CE08 / $9CEF6: a page's first byte names a picture ("1".."8".RAW;
    // $FF none, black); then lines (a line number, the text), $FF ends.
    screen_buffer_.clear();
    const auto first = file_offset(page);
    if (first >= program_.size()) return;
    const int picture = program_[first];
    screen_buffer_.palette.fill(0);
    if (picture != 0xff) {
        const auto raw = data::read_raw_screen(content_.read(std::string(1, char(picture)) + ".raw", 1));
        for (std::size_t i = 0; i < raw.palette.size() && i < 16; ++i) screen_buffer_.palette[i] = raw.palette[i];
        std::copy(raw.pixels.begin(), raw.pixels.begin() + std::min<std::size_t>(raw.pixels.size(), screen_buffer_.pixels.size()),
                  screen_buffer_.pixels.begin());
    } else {
        // $9CE08: colours 1-15 a ramp from white down.
        for (int i = 1; i < 16; ++i) screen_buffer_.palette[std::size_t(i)] = amiga::Rgb4(0xfff - 0x111 * (i - 1));
    }
    const FrontFrames font(program_, text_font_table);
    auto at = first + 1;
    while (at < program_.size() && program_[at] != 0xff) {
        const int y = program_[at++];
        std::string line;
        while (at < program_.size() && program_[at] < 0x80) line += char(program_[at++]);
        ++at; // the end byte
        draw_centred(font, 0, text_font_widths, line, y);
    }
}

void FrontEnd::show_picture(const std::string& file, int disk) {
    const auto raw = data::read_raw_screen(content_.read(file, disk));
    screen_buffer_.clear();
    screen_buffer_.palette.fill(0);
    for (std::size_t i = 0; i < raw.palette.size() && i < 32; ++i) screen_buffer_.palette[i] = raw.palette[i];
    std::copy(raw.pixels.begin(), raw.pixels.begin() + std::min<std::size_t>(raw.pixels.size(), screen_buffer_.pixels.size()),
              screen_buffer_.pixels.begin());
}

void FrontEnd::start_intro() {
    started_ = {Screen::intro, {}, {}, {}};
    loaded_.clear();
    loaded_.emplace(at_font, data::read_raw_screen(content_.read("font.raw", 1)));
    std::vector<Step> steps;
    const auto once = [](std::function<void()> f) { return [f](const app::GameInput&) { f(); return true; }; };
    steps.push_back(once([this] { song_ = 1; warx_ = true; music_fade_ = false; })); // $86FDA + song 1
    // The text page, 150 frames.
    steps.push_back(once([this] { show_text_page(0x80000 + program_long(0x9d03c)); }));
    steps.push_back(fade_in());
    steps.push_back(hold(150));
    steps.push_back(fade_out());
    // The title, 375 frames.
    steps.push_back([this](const app::GameInput&) { if (!skipped_) show_picture("cftitle.raw", 1); return true; });
    steps.push_back([this](const app::GameInput& i) { return skipped_ || fade_in()(i); });
    steps.push_back(hold(375));
    steps.push_back([this](const app::GameInput& i) { return fade_out()(i); });
    // The cast ($9CEF6): 13 pages, 300 frames over a picture, 150 without.
    for (int page = 0; page < 14; ++page) {
        const std::uint32_t pointer = program_long(0x9d08a + std::uint32_t(page) * 4);
        if (pointer & 0x80000000u) break;
        const std::uint32_t address = 0x80000 + pointer;
        const bool with_picture = program_[file_offset(address)] != 0xff;
        steps.push_back([this, address](const app::GameInput&) { if (!skipped_) show_text_page(address); return true; });
        steps.push_back([this](const app::GameInput& i) { return skipped_ || fade_in()(i); });
        steps.push_back(hold(with_picture ? 300 : 150));
        steps.push_back([this](const app::GameInput& i) { return skipped_ || fade_out()(i); });
    }
    // The publisher and the developer, 350 frames each; the title, 600.
    for (const auto& [file, frames] : std::vector<std::pair<std::string, int>>{
             {"virgpres.raw", 350}, {"sensprod.raw", 350}, {"cftitle.raw", 600}}) {
        steps.push_back([this, file](const app::GameInput&) { if (!skipped_) show_picture(file, 1); return true; });
        steps.push_back([this](const app::GameInput& i) { return skipped_ || fade_in()(i); });
        steps.push_back(hold(frames));
        steps.push_back([this](const app::GameInput& i) { return skipped_ || fade_out()(i); });
    }
    // $A3D8A: the music fades out over 64 frames.
    steps.push_back([this](const app::GameInput&) {
        screen_buffer_.fade_low = screen_buffer_.fade_high = 0;
        music_fade_ = true;
        return ++counter_ >= 64;
    });
    steps.push_back([this](const app::GameInput&) { // $86FA2: jon again, song 0
        music_fade_ = false;
        song_ = 0;
        warx_ = false;
        return true;
    });
    run(Screen::intro, std::move(steps));
}

// ---- the hill ($A56B0) ----

void FrontEnd::start_hill(const Campaign& campaign) {
    started_ = {Screen::hill, campaign, {}, {}};
    loaded_.clear();
    auto hills = amiga::read_ilbm(content_.read("hills.lbm", 2));
    screen_buffer_.clear();
    screen_buffer_.palette.fill(0);
    // The hill picture's colours (0-15) and the panel's (16-31, pstuff).
    for (std::size_t i = 0; i < hills.palette.size() && i < 32; ++i) screen_buffer_.palette[i] = hills.palette[i];
    // $A5A7E: the hill (rows 29-212, from x 48) goes to the screen 4 lines
    // higher and is cleared in the picture, which then serves as a sheet.
    auto sheet = hills;
    for (int y = 29; y < 213 && y < hills.height; ++y) {
        for (int x = 48; x < FrontScreen::width && x < hills.width; ++x) {
            screen_buffer_.pixels[std::size_t((y - 4) * FrontScreen::width + x)] = hills.pixels[std::size_t(y * hills.width + x)];
            sheet.pixels[std::size_t(y * sheet.width + x)] = 0;
        }
    }
    const FrontFrames frames(program_, hill_table);
    const auto source = [&](int group, int index, auto&& each) {
        const auto f = frames.frame(group, index);
        if (!f || f->address < at_sheet) return;
        const auto offset = f->address - at_sheet;
        each(int(offset % 40) * 8, int(offset / 40), (f->width_words - 1) * 16, f->height);
    };
    // $A5C3A: the graves (group 0, frames 0-8 by rank) and the walking
    // recruits (groups 1-12, two frames) scaled to the percentages of their
    // lists ($90D3E), each centred on its place, the next 16 to the right.
    for (auto at = file_offset(0x90d3e); at + 12 <= program_.size() && std::int16_t(amiga::read_be16(program_, at)) >= 0; at += 12) {
        const int group = amiga::read_be16(program_, at), index = amiga::read_be16(program_, at + 2);
        int cx = std::int16_t(amiga::read_be16(program_, at + 4)) + 8;
        const int cy = std::int16_t(amiga::read_be16(program_, at + 6));
        auto list = file_offset(0x80000 + amiga::read_be32(program_, at + 8));
        source(group, index, [&](int sx, int sy, int sw, int sh) {
            const auto original = sheet; // (the scaled copies read the unscaled ones)
            for (; list + 2 <= program_.size(); list += 2) {
                const int percent = std::int16_t(amiga::read_be16(program_, list));
                if (percent < 0) break;
                const int w = sw * percent / 100, h = sh * percent / 100;
                for (int v = 0; v < h; ++v) {
                    for (int u = 0; u < w; ++u) {
                        const int c = original.pixels[std::size_t((sy + v * sh / h) * original.width + sx + u * sw / w)];
                        const int dx = cx - w / 2 + u, dy = cy - h / 2 + v;
                        if (c != 0 && dx >= 0 && dy >= 0 && dx < sheet.width && dy < sheet.height)
                            sheet.pixels[std::size_t(dy * sheet.width + dx)] = std::uint8_t(c);
                    }
                }
                cx += 16;
            }
        });
    }
    // $A5B46: the standing recruits' pieces ($A5BC0: group, frame, x, y),
    // copied whole.
    for (auto at = file_offset(0xa5bc0); at + 8 <= program_.size() && std::int16_t(amiga::read_be16(program_, at)) >= 0; at += 8) {
        const int x = amiga::read_be16(program_, at + 4) & ~15, y = amiga::read_be16(program_, at + 6);
        source(amiga::read_be16(program_, at), amiga::read_be16(program_, at + 2), [&](int sx, int sy, int sw, int sh) {
            for (int v = 0; v < sh; ++v)
                for (int u = 0; u < sw; ++u)
                    if (x + u < sheet.width && y + v < sheet.height && sy + v < hills.height)
                        sheet.pixels[std::size_t((y + v) * sheet.width + x + u)] =
                            hills.pixels[std::size_t((sy + v) * hills.width + sx + u)];
        });
    }
    sheet_ = sheet;
    const FrontFrames graves(program_, 0x90f10);
    // $A5CC4: a grave per fallen recruit, the newest at place 360 - n of the
    // table $A5110 (x, y) and the older ones after it; the cell by rank
    // (min(rank / 2, 8)) and by its height on the hill (min((y - 20) / 32, 4)).
    loaded_.clear();
    loaded_.emplace(at_sheet, sheet);
    {
        const int count = std::min<int>(int(campaign.heroes.size()), 360);
        for (int j = 0; j < count; ++j) {
            const auto& hero = campaign.heroes[campaign.heroes.size() - 1 - std::size_t(j)];
            const auto at = file_offset(0xa5110 + std::uint32_t(360 - count + j) * 4);
            if (at + 4 > program_.size()) break;
            const int x = std::int16_t(amiga::read_be16(program_, at)), y = std::int16_t(amiga::read_be16(program_, at + 2));
            draw_frame(graves, std::min(hero.rank >> 1, 8), std::min(std::max((y - 0x14) >> 5, 0), 4), x, y);
        }
    }
    loaded_.clear();
    loaded_.emplace(at_sheet, hills);
    // $A622E: the header.
    draw_frame(frames, 0x16, 0, 0, 0);                        // LOAD
    const auto save_at = file_offset(0xa9e26 + std::uint32_t(std::max(campaign.missions_started - 1, 0)) * 2);
    const int save = save_at + 2 <= program_.size() ? amiga::read_be16(program_, save_at) : 0;
    draw_frame(frames, 0x17, save, 0x130, 0);                 // SAVE (or not here)
    draw_text(frames, 0xd, widths_header, program_text(0xa633a),
              (FrontScreen::width - text_width(program_text(0xa633a), widths_header)) >> 1, 0xa);
    draw_frame(frames, 0xe, 0, 0x9b, 0xa);                     // the colon
    const auto number = [&](int value, int right_edge_or_left, bool right_aligned) {
        const std::string digits = std::to_string(std::max(value, 0));
        const int x = right_aligned ? right_edge_or_left - 12 * int(digits.size()) : right_edge_or_left;
        for (std::size_t i = 0; i < digits.size(); ++i) draw_frame(frames, 0xd, digits[i] - '0' + 26, x + 12 * int(i), 0xa);
    };
    number(campaign.away, 0x9a, true);
    number(campaign.home, 0xa6, false);
    const auto& pstuff = panel_.pstuff();
    for (std::size_t i = 0; i < 16 && i < pstuff.palette.size(); ++i) screen_buffer_.palette[16 + i] = pstuff.palette[i];
    // $A63BA: the column on the left (the panel's sprites, colours 16-31 from
    // pstuff), from line 26: the hall of fame, and the recruits going (those
    // coming back at once, each new one when he arrives, $A60A4).
    hill_ = HillState{};
    for (const auto& f : campaign.fame) {
        if (f.name >= 0) hill_.column.fame.push_back({f.name, f.rank, f.kills});
    }
    int going = 0;
    for (std::size_t k = 0; k < campaign.roster.size() && int(k) < campaign.soldiers; ++k) {
        const auto& r = campaign.roster[k];
        if (r.name < 0) continue;
        if (going++ < campaign.returning) hill_.column.squad.push_back({r.name, r.rank, r.kills});
        else hill_.arriving.push_back({r.name, r.rank, r.kills});
    }
    for (auto at = file_offset(0x92fc8); at + 6 <= program_.size() && std::int16_t(amiga::read_be16(program_, at)) >= 0; at += 6) {
        hill_.path.push_back({std::int16_t(amiga::read_be16(program_, at)), std::int16_t(amiga::read_be16(program_, at + 2)),
                              std::int16_t(amiga::read_be16(program_, at + 4))});
    }
    // $A5D6E: the walkers' places, $B4 bytes in and then by the gaps $A5DA4.
    {
        int at = 0xb4;
        for (std::size_t k = 0; k < hill_.walkers.size(); ++k) {
            hill_.walkers[k] = at >= 0 ? at / 6 : -1;
            at -= amiga::read_be16(program_, file_offset(0xa5da4 + std::uint32_t(k) * 2));
        }
    }
    hill_.joining = int(hill_.arriving.size());
    hill_.queue = campaign.pool_at_mission;
    hill_.grave_variety = (int(campaign.heroes.size()) & 0x1e) >> 1; // ($80A0E - $8073C) / 2 & $1E
    hill_.background = screen_buffer_;
    // Twice before the colours come up ($A5A64 x 2), then a frame each.
    hill_walk();
    hill_walk();
    draw_hill();
    song_ = -1;
    std::vector<Step> steps;
    steps.push_back(fade_in());
    // $A58F8 on a click (in the game's pointer units: column - 16, line + 4):
    // LOAD at the top left, SAVE at the top right (where the mission allows
    // it), anywhere below the header goes on.
    hill_choice_ = HillChoice::go_on;
    save_allowed_ = save == 0;
    steps.push_back([this](const app::GameInput& input) {
        if (hill_walk() && !hill_.arriving.empty()) { // $A60A4: his name comes up
            hill_.column.squad.push_back(hill_.arriving.front());
            hill_.arriving.erase(hill_.arriving.begin());
        }
        draw_hill();
        if (!(input.mouse_button && !left_was_down_ && input.pointer)) return false;
        const int x = input.pointer_x - 16, y = input.pointer_y + 4;
        if (y < 0x1b) {
            if (x + 16 < 16) {
                hill_choice_ = HillChoice::load;
                return true;
            }
            if (x >= 0x11e && save_allowed_) {
                hill_choice_ = HillChoice::save;
                return true;
            }
            return false;
        }
        return true;
    });
    steps.push_back(fade_out());
    run(Screen::hill, std::move(steps));
}

void FrontEnd::draw_hill() {
    const auto palette = screen_buffer_.palette;
    const int low = screen_buffer_.fade_low, high = screen_buffer_.fade_high;
    screen_buffer_ = hill_.background;
    screen_buffer_.palette = palette;
    screen_buffer_.fade_low = low;
    screen_buffer_.fade_high = high;
    // $A5FBC: every path point with a recruit: the picture by his look
    // and step and the road's turn, the size by how far down the road.
    loaded_.clear();
    loaded_.emplace(at_sheet, sheet_);
    const FrontFrames sheet(program_, 0x90f10);
    for (std::size_t k = 0; k < hill_.path.size(); ++k) {
        const auto& p = hill_.path[k];
        if (p[2] == 0) continue;
        int group = p[2] - 1 < 3 ? 9 : 6 + p[2];
        if (p[2] - 1 >= 3) {
            if (p[1] >= 0xb7) group += 4;
            else if (p[1] > 0x7c) group += 2;
        }
        const auto offset = int(k + 1) * 6;
        draw_frame(sheet, group, offset <= 0x1a4 ? 0 : offset <= 0x2dc ? 1 : 2, p[0], p[1]);
    }
    const auto panel = compose_hill_column(panel_, hill_.column);
    for (int y = 0; y < panel_height && y + 26 < FrontScreen::height; ++y) {
        for (int x = 0; x < panel_width; ++x) {
            const auto c = panel[std::size_t(y * panel_width + x)];
            if (c != 0) screen_buffer_.pixels[std::size_t((y + 26) * FrontScreen::width + x)] = std::uint8_t(16 + c);
        }
    }
}

// One frame of the recruits walking down the road; true when one reached
// the squad.
bool FrontEnd::hill_walk() {
    const auto look = [this](int k) { return int(amiga::read_be16(program_, file_offset(0xa5ece + std::uint32_t(k & 15) * 2))); };
    auto& path = hill_.path;
    const int end = int(path.size()) - 1;
    const auto state = [&](int k) -> int& { return path[std::size_t(std::clamp(k, 0, end))][2]; };
    if (!hill_.queued) {
        // $A5D50: the recruits are placed afresh each frame.
        for (auto& p : path) {
            if (p[2] >= 4) p[2] = 0;
        }
        // $A5DC4: those waiting stand at the road's end, every 4th place.
        int variety = hill_.grave_variety;
        int at = 0x660 / 6;
        for (int n = 0; n < hill_.queue; ++n) {
            if (state(at) != 0) --at;
            state(at) = look(variety);
            variety = (variety + 1) & 15;
            at -= 4;
        }
        // Blocked when the first walker is within 5 free places of the
        // end or of someone standing.
        bool blocked = false;
        for (int k = hill_.walkers[0], free = 5; free > 0;) {
            const int s = state(k);
            if (s == 0) {
                if (k >= 0x660 / 6) {
                    blocked = true;
                    break;
                }
                ++k;
                --free;
            } else if (s <= 3) {
                ++k;
            } else {
                blocked = true;
                break;
            }
        }
        if (!blocked) {
            hill_.toggle += 0x8000;
            hill_.toggle = (hill_.toggle & 0xffff) | ((hill_.toggle & 0x10000) ? 0x10000 : 0);
        } else {
            hill_.toggle = (hill_.toggle & 0xffff) | 0x10000;
        }
        const int frame = int(hill_.toggle >> 16) & 1;
        int table = 0;
        for (auto& w : hill_.walkers) {
            if (w < 0) continue;
            int k = w;
            const int s = frame + look(table++);
            if (k > end || path[std::size_t(k)][0] < 0) k = 0;
            if (state(k) != 0) ++k;
            state(k) = s;
            if (!blocked) ++k;
            w = k;
        }
        if (blocked) hill_.queued = true;
        return false;
    }
    // $A5EEE: closed up: the front one (from the road's end) goes into
    // the squad while new recruits are due; the others close up.
    bool joined = false;
    if (hill_.joining > 0) {
        int k = end + 1;
        while (k > 0) {
            --k;
            if (path[std::size_t(k)][2] >= 4) break;
            if (k == 0) return false;
        }
        const int s = path[std::size_t(k)][2] ^ 1;
        path[std::size_t(k)][2] = 0;
        if (k + 1 > end) {
            --hill_.joining;
            joined = true;
        } else {
            path[std::size_t(k + 1)][2] = s;
        }
    }
    hill_.toggle ^= 0xffff0000u;
    for (int k = end; k >= 0; --k) {
        if (path[std::size_t(k)][2] < 4 || k >= 0x65a / 6) continue;
        bool free = true;
        for (int n = 1; n <= 4 && k + n <= end; ++n) {
            const int s = path[std::size_t(k + n)][2];
            if (s > 3) {
                free = false;
                break;
            }
        }
        if (!free) continue;
        int s = path[std::size_t(k)][2];
        path[std::size_t(k)][2] = 0;
        if (hill_.toggle & 0x80000000u) s ^= 1;
        if (path[std::size_t(k + 1)][2] == 0) path[std::size_t(k + 1)][2] = s;
        else if (k + 2 <= end) path[std::size_t(k + 2)][2] = s;
    }
    return joined;
}

// ---- the briefing ($A6614) ----

void FrontEnd::start_briefing(const Campaign& campaign, const MissionTables& tables) {
    started_ = {Screen::briefing, campaign, {}, {}};
    // $A66FE: rows 160-207 of pstuff.lbm (its fonts) go where the frames
    // are; the colours are font.pl8's.
    loaded_.clear();
    loaded_.emplace(at_sheet, amiga::read_ilbm(content_.read("pstuff.lbm", 1)));
    screen_buffer_.clear();
    screen_buffer_.palette.fill(0);
    const auto colours = data::read_palette(content_.read("font.pl8", 2));
    for (std::size_t i = 0; i < colours.size() && i < 16; ++i) screen_buffer_.palette[i] = colours[i];
    const FrontFrames frames(program_, briefing_table);
    // $A812C: the bar, MISSION n, the mission's name, the phase's name.
    draw_frame(frames, 2, 0, 0, 0x50);
    draw_centred(frames, 1, widths_big, program_text(0xa821a) + std::to_string(campaign.missions_started), 0, 0x25);
    const std::string mission = program_text(0x80000 + program_long(0xa83e8 + std::uint32_t(campaign.missions_started - 1) * 4));
    const std::string phase = program_text(0x80000 + program_long(0xa8448 + std::uint32_t(campaign.phase) * 4));
    draw_centred(frames, 1, widths_big, phase, 0x30, 0x25); // (at line $815A0 = $30)
    // $A7E56: BRIEFING, its box, PHASE n OF m, the objectives.
    draw_centred(frames, 3, widths_big, program_text(0xa7f8e), 0x60, 0x25);
    rectangle(0x29, 0x5b, 0xee, 0x82, 3);
    rectangle(0x28, 0x5a, 0xee, 0x82, 2);
    draw_centred(frames, 0, widths_small,
                 program_text(0xa7f97) + std::to_string(campaign.phase_in_mission + 1) + program_text(0xa7f9e) +
                     std::to_string(campaign.phases_in_mission),
                 0x1d);
    (void)mission;
    int y = 0x84;
    std::array<bool, 8> set{};
    for (const int code : tables.objectives(campaign.phase)) {
        if (code >= 1 && code <= 8) set[std::size_t(code - 1)] = true;
    }
    for (int code = 0; code < 8; ++code) {
        if (!set[std::size_t(code)]) continue;
        draw_centred(frames, 0, widths_small, program_text(0x80000 + program_long(0xa4cda + std::uint32_t(code) * 4)), y);
        y += 0xc;
    }
    std::vector<Step> steps;
    steps.push_back(fade_in());
    // $A7FA4, once the phase is loaded: the soldiers, the recruits left, GO
    // FOR IT.
    steps.push_back([this](const app::GameInput&) {
        const auto& campaign = started_.campaign;
        const FrontFrames frames(program_, briefing_table);
        const int n = campaign.soldiers;
        draw_centred(frames, 0, widths_small,
                     program_text(0xa80b6) + std::to_string(n) + program_text(n == 1 ? 0xa80cf : 0xa80bc), 0x76);
        const int left = campaign.pool;
        draw_centred(frames, 0, widths_small,
                     left == 0 ? program_text(0xa8108)
                               : std::to_string(left) + program_text(left == 1 ? 0xa80f5 : 0xa80e1),
                     0xba);
        draw_centred(frames, 3, widths_big, program_text(0xa8121), 0xca, 0x25);
        return true;
    });
    steps.push_back([this](const app::GameInput& input) { return input.mouse_button && !left_was_down_; });
    steps.push_back(fade_out());
    run(Screen::briefing, std::move(steps));
}

// ---- the debrief ($A8AF6) ----

void FrontEnd::blit_block(int index, int x, int y) {
    // $A918A: 8 bytes each: the source address (in morphbig.lbm at $57F60 or
    // rankfont.lbm at $3294A), its width in words and its height; copied
    // whole (colour 0 too).
    const auto at = file_offset(0xa918a + std::uint32_t(index) * 8);
    if (at + 8 > program_.size()) return;
    const auto address = amiga::read_be32(program_, at);
    const int w = amiga::read_be16(program_, at + 4) * 16, h = amiga::read_be16(program_, at + 6);
    const amiga::IlbmPicture* picture = nullptr;
    std::uint32_t base = 0;
    for (const auto& [b, p] : loaded_) {
        if (b <= address && b >= base) {
            base = b;
            picture = &p;
        }
    }
    if (!picture) return;
    const int sx = int((address - base) % 40) * 8, sy = int((address - base) / 40);
    for (int j = 0; j < h; ++j) {
        for (int i = 0; i < w; ++i) {
            const int px = sx + i, py = sy + j, dx = x + i, dy = y + j;
            if (px >= picture->width || py >= picture->height) continue;
            if (dx < 0 || dy < 0 || dx >= FrontScreen::width || dy >= FrontScreen::height) continue;
            screen_buffer_.pixels[std::size_t(dy * FrontScreen::width + dx)] =
                picture->pixels[std::size_t(py * picture->width + px)];
        }
    }
}

void FrontEnd::start_debrief(const Campaign& campaign) {
    started_ = {Screen::debrief, campaign, {}, {}};
    loaded_.clear();
    auto morph = amiga::read_ilbm(content_.read("morphbig.lbm", 1));
    auto rank = amiga::read_ilbm(content_.read("rankfont.lbm", 1));
    screen_buffer_.clear();
    screen_buffer_.palette.fill(0);
    for (std::size_t i = 0; i < morph.palette.size() && i < 32; ++i) screen_buffer_.palette[i] = morph.palette[i];
    loaded_.emplace(at_sheet, std::move(rank));
    loaded_.emplace(at_font, std::move(morph));
    song_ = 0; // jon's ($A3C60)
    warx_ = false;
    screen_buffer_.fade_low = screen_buffer_.fade_high = 0;
    const FrontFrames frames(program_, 0xa91e2);
    // ($A8BD6 draws a chevron at ($60, $74) before clearing planes 1-4 again;
    // the recorded screen has none of it: not shown.)
    auto& d = debrief_;
    d = DebriefState{};
    // $A8C30: this mission's fallen ($80A16) first, under LOST IN SERVICE
    // with poppies; else HEROES IN VICTORY with medals at once.
    const bool fallen = campaign.heroes.size() > campaign.heroes_this_mission;
    d.banner_timer = -1;
    d.listing_fallen = fallen;
    d.line_timer = 0x2c;
    if (fallen) {
        blit_block(8, 0x30, 0);
        blit_block(0, 0, 0x40);
        blit_block(4, 0xf0, 0x40);
        d.fallen_banner = true;
    } else {
        blit_block(9, 0x30, 0);
        d.banner_timer = 0;
        blit_block(3, 0, 0x40);
        blit_block(7, 0xf0, 0x40);
        promotion_timer_ = 0x95;
    }
    d.roster_left = 7;
    d.nobody_back = std::none_of(campaign.roster.begin(), campaign.roster.end(), [](const Recruit& r) { return r.on_map; });
    // $A9124: "Mission n" at the bottom.
    draw_centred(frames, 4, 0xa9b66, program_text(0xa9180) + std::to_string(campaign.missions_started), 0xdc);
    // $A8E10: the new ranks of the recruits back (+ phases served, at most 15).
    for (const auto& r : campaign.roster) {
        if (r.name >= 0 && r.on_map) d.promoted.push_back(std::min(r.rank + r.served, 15));
    }
    std::vector<Step> steps;
    steps.push_back([this](const app::GameInput& input) { return debrief_frame(input.mouse_button); });
    run(Screen::debrief, std::move(steps));
}

// A frame of the debrief: the blocking fades ($A3346 up, $A33A8 down: a
// level every 3 frames) or half a round.
bool FrontEnd::debrief_frame(bool held) {
    auto& d = debrief_;
    using Mode = DebriefState::Mode;
    const auto fade = [&](bool up) {
        ++d.frames;
        if (d.frames % 3 == 0) {
            const int level = up ? d.frames / 3 - 1 : 16 - d.frames / 3;
            screen_buffer_.fade_low = screen_buffer_.fade_high = level;
        }
        return d.frames >= 48;
    };
    switch (d.mode) {
    case Mode::fade_in:
        if (fade(true)) {
            d.mode = Mode::rounds;
            d.frames = 0;
        }
        return false;
    case Mode::rounds:
        if (++d.frames < 2) return false;
        d.frames = 0;
        debrief_round();
        return d.mode == Mode::done;
    case Mode::fade_out_middle:
        if (fade(false)) {
            d.fade_wait = 2;
            d.mode = Mode::rounds;
            d.frames = 0;
            debrief_after_round(held);
        }
        return d.mode == Mode::done;
    case Mode::fade_in_middle:
        if (fade(true)) {
            d.fade_wait = -1;
            promotion_timer_ = 0x79;
            d.mode = Mode::rounds;
            d.frames = 0;
            debrief_after_round(held);
        }
        return d.mode == Mode::done;
    case Mode::fade_out_end:
        if (fade(false)) d.mode = Mode::done;
        return d.mode == Mode::done;
    case Mode::done:
        return true;
    }
    return true;
}

// One round of the loop at $A8D1E.
void FrontEnd::debrief_round() {
    auto& d = debrief_;
    using Mode = DebriefState::Mode;
    ++d.round;
    // $A2750: the names' column (x 96-223, lines 46-255) up a line.
    for (int y = 45; y < 255; ++y)
        std::copy_n(&screen_buffer_.pixels[std::size_t((y + 1) * FrontScreen::width + 96)], 128,
                    &screen_buffer_.pixels[std::size_t(y * FrontScreen::width + 96)]);
    const FrontFrames frames(program_, 0xa91e2);
    // $A8F3C: listing the heroes, every 44 rounds the next new rank goes
    // where their lines pass (y $80).
    if (!d.listing_fallen) {
        promotion_timer_ = std::int16_t(promotion_timer_ - 1);
        if (promotion_timer_ == 0) {
            promotion_timer_ = 0x2c;
            if (d.promoted_next < int(d.promoted.size())) {
                const int r = d.promoted[std::size_t(d.promoted_next++)];
                draw_frame(frames, 0, 0, 0x60, 0x80);
                draw_frame(frames, 2, r, 0x64, 0x83);
                draw_frame(frames, 1, 0, 0xc8, 0x80);
                draw_frame(frames, 2, r, 0xcc, 0x83);
            }
        }
    }
    d.line_timer = std::int16_t(d.line_timer - 1);
    if (d.line_timer == 0) {
        debrief_line();
    } else {
        if (d.fallen_banner) {
            // $A8E92: $A4 rounds after the fallen, the heroes' banner and
            // medals (table $A8EDE) while the screen is dark.
            bool draw = d.banner_timer == 0;
            if (!draw) {
                d.banner_timer = std::int16_t(d.banner_timer - 1);
                if (d.banner_timer == 0) {
                    d.banner_step = 0;
                    draw = true;
                }
            }
            if (draw) {
                for (;; d.banner_step += 8) {
                    const auto at = file_offset(0xa8ede + std::uint32_t(d.banner_step));
                    const int block = std::int16_t(amiga::read_be16(program_, at));
                    if (block < 0) break;
                    blit_block(block, amiga::read_be16(program_, at + 2), amiga::read_be16(program_, at + 4));
                }
            }
            if (d.banner_timer == 2) d.fade_due = true;
        }
        if (d.fade_due && d.fade_wait >= 0) {
            if (d.fade_wait == 0) {
                if (d.nobody_back) {
                    d.mode = Mode::fade_out_end;
                    d.frames = 0;
                    return;
                }
                d.mode = Mode::fade_out_middle;
                d.frames = 0;
                return; // (the round goes on after the fade)
            }
            if (--d.fade_wait == 0) {
                d.mode = Mode::fade_in_middle;
                d.frames = 0;
                return;
            }
        }
    }
    debrief_after_round(false);
}

// $A8DA2: the left button (held) or the count after the last line ends it.
void FrontEnd::debrief_after_round(bool held) {
    auto& d = debrief_;
    if (held || (d.end_timer != 0 && --d.end_timer == 0)) {
        d.mode = DebriefState::Mode::fade_out_end;
        d.frames = 0;
    }
}

// $A8FC4: the next line at the bottom: a fallen recruit, else the next
// recruit back; after the fallen the heroes follow; after the last line a
// count of $AA rounds.
void FrontEnd::debrief_line() {
    auto& d = debrief_;
    const auto& campaign = started_.campaign;
    int name = -1, rank = 0;
    const auto finished = [&] {
        if (d.end_timer == 0) d.end_timer = 0xaa;
    };
    if (d.listing_fallen) {
        const std::size_t k = campaign.heroes_this_mission + std::size_t(d.fallen_next);
        if (k < campaign.heroes.size()) {
            name = campaign.heroes[k].name;
            rank = campaign.heroes[k].rank;
            ++d.fallen_next;
        }
        if (name < 0) {
            d.listing_fallen = false;
            d.banner_timer = 0xa4;
            d.line_timer = 0xb4;
            return;
        }
    } else {
        if (d.nobody_back || d.roster_left < 0) return finished();
        while (true) {
            if (d.roster_next >= 8) return finished();
            const auto& r = campaign.roster[std::size_t(d.roster_next)];
            if (r.on_map) break;
            ++d.roster_next;
            if (--d.roster_left < 0) return finished();
        }
        const auto& r = campaign.roster[std::size_t(d.roster_next++)];
        --d.roster_left;
        if (r.rank < 0) return finished();
        name = r.name;
        rank = d.line_rank = r.rank;
    }
    d.line_timer = 0x2c;
    // $A9052: the stone bar, the name (6 letters at most, centred + 2) and
    // the rank on both sides.
    blit_block(10, 0x60, 0xe9);
    std::string text;
    const auto at = file_offset(0x917e0 + std::uint32_t(name) * 9);
    for (std::size_t i = 0; i < 6 && at + i < program_.size() && program_[at + i] != ' '; ++i) text += char(program_[at + i]);
    const FrontFrames frames(program_, 0xa91e2);
    draw_text(frames, 3, 0xa9b06, text, ((FrontScreen::width - text_width(text, 0xa9b06)) >> 1) + 2, 0xef);
    draw_frame(frames, 2, rank, 0x64, 0xec);
    draw_frame(frames, 2, rank, 0xcc, 0xec);
}

// ---- the end ($9CAB2) ----

void FrontEnd::start_ending() {
    started_ = {Screen::ending, {}, {}, {}};
    loaded_.clear();
    show_picture("won.raw", 1);
    song_ = 1; // warx's ($86FDA)
    warx_ = true;
    std::vector<Step> steps;
    steps.push_back(fade_in());
    // (The original stops here for good; a click leaves.)
    steps.push_back([this](const app::GameInput& input) { return input.mouse_button && !left_was_down_; });
    steps.push_back(fade_out());
    run(Screen::ending, std::move(steps));
}

// ---- the parallax ($A766C) ----

void FrontEnd::start_parallax(const Campaign& campaign, const std::string& land) {
    started_ = {Screen::parallax, campaign, land, {}};
    const int disk = 2;
    // $A78BA: the landscape's picture and colours ($86CC0's list: sky.pl8 at
    // $80102, mid $80112, fgnd $80122, fgn2 $80132, heli.pal $80142 for the
    // sprites, font.pl8 $80162 for the names). Here: 0-7 font, 8-15 sky,
    // 16-23 mid, 24-31 fgnd, 32-39 fgn2, 48-63 the sprites.
    const auto play = amiga::read_ilbm(content_.read(land + "play.lbm", disk));
    screen_buffer_.clear();
    screen_buffer_.palette.fill(0);
    const std::array<std::pair<std::string, std::size_t>, 6> colours{{{"font.pl8", 0}, {land + "sky.pl8", 8},
                                                                      {land + "mid.pl8", 16}, {land + "fgnd.pl8", 24},
                                                                      {land + "fgn2.pl8", 32}, {land + "heli.pal", 48}}};
    for (const auto& [file, at] : colours) {
        const auto c = data::read_palette(content_.read(file, file == "font.pl8" ? 2 : disk));
        const std::size_t n = at == 48 ? 16 : 8;
        for (std::size_t i = 0; i < n && i < c.size(); ++i) screen_buffer_.palette[at + i] = c[i];
    }
    // $A8A3C: both playfields cleared; $A7DF6: the header routine ($A812C,
    // line $815A0 = 20: MISSION n and, below, the mission's name) draws into
    // a 320-wide buffer, whose first 20 rows go to the left half of
    // playfield 1's top and the next 20 to the right half of its row 210
    // (where the name comes in from the right).
    constexpr int pf_width = 640, pf_height = 256;
    playfield1_.assign(std::size_t(pf_width * pf_height), 0);
    playfield2_.assign(std::size_t(pf_width * pf_height), 0);
    loaded_.clear();
    loaded_.emplace(at_sheet, amiga::read_ilbm(content_.read("pstuff.lbm", 1))); // the font, as the briefing's
    {
        const FrontFrames frames(program_, briefing_table);
        draw_centred(frames, 1, widths_big, program_text(0xa821a) + std::to_string(campaign.missions_started), 0, 0x25);
        const std::string name =
            program_text(0x80000 + program_long(0xa83e8 + std::uint32_t(std::max(campaign.missions_started - 1, 0)) * 4));
        draw_centred(frames, 1, widths_big, name, 0x14, 0x25);
        for (int y = 0; y < 40; ++y) {
            const int row = y < 20 ? y : 210 + (y - 20), x0 = y < 20 ? 0 : 320;
            for (int x = 0; x < FrontScreen::width; ++x)
                playfield1_[std::size_t(row * pf_width + x0 + x)] = screen_buffer_.pixels[std::size_t(y * FrontScreen::width + x)] & 7;
        }
        screen_buffer_.clear();
    }
    // $A8A5E, table $A8AD8 (playfield, first picture row, rows, first
    // playfield row): the picture's 3 planes, each row twice side by side.
    for (int k = 0; k < 3; ++k) {
        const auto at = file_offset(0xa8ad8 + std::uint32_t(k) * 10);
        auto& pf = amiga::read_be32(program_, at) >= 0x71000 ? playfield1_ : playfield2_;
        const int from = amiga::read_be16(program_, at + 4), rows = amiga::read_be16(program_, at + 6),
                  to = amiga::read_be16(program_, at + 8);
        for (int r = 0; r < rows && from + r < play.height && to + r < pf_height; ++r) {
            for (int x = 0; x < 320 && x < play.width; ++x) {
                const auto c = std::uint8_t(play.pixels[std::size_t((from + r) * play.width + x)] & 7);
                pf[std::size_t((to + r) * pf_width + x)] = c;
                pf[std::size_t((to + r) * pf_width + x + 320)] = c;
            }
        }
    }
    song_ = 7;
    warx_ = false;
    // $A777A…: the fade at 0, the helicopter at ($150, $1E) with its first
    // leg taken (the heading and its aim are only set to $180 afterwards).
    parallax_ = ParallaxState{};
    helicopter_leg();
    parallax_.heading = parallax_.wanted = 0x180;
    std::vector<Step> steps;
    steps.push_back([this](const app::GameInput& input) {
        parallax_frame(input.mouse_button && !left_was_down_);
        return parallax_.ended;
    });
    run(Screen::parallax, std::move(steps));
}

// $A7924: the sprites go where the helicopter is; a new leg of $A7A20
// (heading, speed, ticks; a negative count ends the flight) when this one
// has run out; then it moves, turns towards the leg's heading and changes
// its speed by 1 towards the leg's.
void FrontEnd::helicopter_leg() {
    auto& st = parallax_;
    st.sprite_x = std::int16_t(std::uint32_t(st.heli_x) >> 16);
    st.sprite_y = std::int16_t(std::uint32_t(st.heli_y) >> 16);
    if (--st.leg_left < 0) {
        const auto at = file_offset(0xa7a20 + std::uint32_t(st.leg) * 6);
        st.wanted = amiga::read_be16(program_, at);
        st.wanted_speed = std::int16_t(amiga::read_be16(program_, at + 2));
        st.leg_left = std::int16_t(amiga::read_be16(program_, at + 4));
        if (st.leg_left < 0) return;
        ++st.leg;
    }
    st.heading &= 0x1fe;
    st.heli_x += std::int32_t(tables_sine(st.heading) >> 2) * st.speed;
    st.heli_y += std::int32_t(tables_sine(st.heading + 0x80) >> 2) * st.speed;
    const int turn = ((((std::uint16_t(st.wanted - st.heading) >> 5) - 1) ^ 0xf) & 0xf);
    st.heading = (st.heading + std::int8_t(program_[file_offset(0x955c0 + std::uint32_t(turn))]) * 4) & 0x1fe;
    if (st.speed != st.wanted_speed) st.speed += st.speed < st.wanted_speed ? 1 : -1;
}

// One round of the loop at $A77C2 (a frame).
void FrontEnd::parallax_frame(bool click) {
    auto& st = parallax_;
    if (click) st.clicked = true;
    // $A7AE6: on every second frame the colours come up a step while the
    // helicopter is right of $50 and no click came (it flies once they are
    // full), else they go down (and the screen ends at black).
    const bool even = (st.vblank++ & 1) == 0;
    if (!st.clicked && st.sprite_x - 0x20 > 0x30) {
        if (even && st.fade < 16) ++st.fade;
        if (st.fade >= 16) helicopter_leg();
    } else {
        if (even) {
            if (st.fade == 0) st.ended = true;
            else --st.fade;
        }
        helicopter_leg();
    }
    // $A7A5C: the name comes in by 4 to $C; the layers scroll left by 1/2,
    // 1, 2 and 4 pixels (each wrapping at 320).
    if (st.name_x != 0xc) st.name_x = std::max(st.name_x - 4, 0xc);
    std::int32_t step = 0x8000;
    for (auto& v : st.scroll) {
        v -= step;
        if (v < 0) v += FrontScreen::width << 16;
        step <<= 1;
    }
    // $A77EE: the rotor's picture, one of three.
    st.rotor_shown = st.rotor;
    st.rotor = (st.rotor + 1) % 3;
    screen_buffer_.fade_low = screen_buffer_.fade_high = std::min(st.fade, 15);
    draw_parallax();
}

// The copper list ($A7514, $A7544 at $3220…): two playfields of 3 planes,
// a display window 288 wide ($2C91-$2CB1), and bands from lines 24, 92, 152
// and 204 that point the playfields at other rows, scroll them by other
// layers (word steps in the pointers, the rest in BPLCON1: $A7CEC, $A7C66),
// give them other colours ($A7B5A, faded by $826E4) and the other playfield
// in front (BPLCON2 $64, $24, $44). The helicopter's sprites on top.
void FrontEnd::draw_parallax() {
    const auto& st = parallax_;
    constexpr int pf_width = 640;
    const int name = st.name_x, name_bottom = 0x158 - st.name_x;
    const auto layer = [&](int k) { return int(std::int16_t(std::uint32_t(st.scroll[std::size_t(k)]) >> 16)); };
    struct Field {
        int row_back;  // playfield row = line - this
        int coarse;    // the scroll in the pointers
        int fine;      // the scroll in BPLCON1 (-1: none, delay 0)
        int colours;   // first of its 8 colours
    };
    struct Band {
        Field one, two;
        bool two_in_front;
        int background; // COLOR00
    };
    const auto field = [](int back, int coarse, int fine, int colours) { return Field{back, coarse, fine, colours}; };
    const Band bands[5] = {
        {field(0, name, name, 0), field(0, layer(1), -1, 16), true, 0},                     // lines 0-23
        {field(4, layer(0), layer(0), 8), field(0, layer(1), layer(1), 16), true, 8},        // 24-91
        {field(4, layer(2), layer(2), 24), field(0, layer(1), layer(1), 16), false, 24},     // 92-151
        {field(4, layer(2), layer(2), 24), field(4, layer(3), layer(3), 32), true, 24},      // 152-203
        {field(4, name_bottom, name_bottom, 0), field(4, layer(3), -1, 32), true, 0}};       // 204-255
    const auto fetch = [&](const std::vector<std::uint8_t>& pf, const Field& f, int line, int x) {
        const int delay = f.fine < 0 ? 0 : (f.fine & 15) ^ 15;
        const int row = line - f.row_back, column = x - delay + (f.coarse & ~15);
        if (row < 0 || row >= 256 || column < 0 || column >= pf_width) return 0;
        return int(pf[std::size_t(row * pf_width + column)]);
    };
    for (int line = 0; line < FrontScreen::height; ++line) {
        const auto& b = bands[line < 24 ? 0 : line < 92 ? 1 : line < 152 ? 2 : line < 204 ? 3 : 4];
        auto* out = &screen_buffer_.pixels[std::size_t(line * FrontScreen::width)];
        for (int x = 0; x < FrontScreen::width; ++x) {
            int c = b.background;
            if (x >= 16 && x < 304) { // the display window
                const int one = fetch(playfield1_, b.one, line, x), two = fetch(playfield2_, b.two, line, x);
                if (b.two_in_front) c = two ? b.two.colours + two : one ? b.one.colours + one : c;
                else c = one ? b.one.colours + one : two ? b.two.colours + two : c;
            }
            out[x] = std::uint8_t(c);
        }
    }
    // The helicopter: its body ($CB, 48 x 23, with the rotor $CD-$CF over its
    // top two lines) on the attached sprites 2-7 at the sprite position - $20
    // ($9C78A), its nose on the pointer's sprites 0/1 ($CC) at + 16 ($9C67E);
    // colours from heli.pal; inside the display window only.
    const auto& pstuff = panel_.pstuff();
    // $895BC: a piece's colours (4 planes) into the sprites' data, 16
    // columns a sprite pair; $9C65A copies one line more than the table's
    // height (a dbra count), which `lines` already counts.
    const auto copy = [&](int image, int width, std::vector<std::uint8_t>& into) {
        const auto piece = panel_.piece(image);
        for (int y = 0; y < piece.lines && y * width < int(into.size()); ++y)
            for (int x = 0; x < width && x < piece.width; ++x) {
                const int px = piece.x + x, py = piece.y + y;
                into[std::size_t(y * width + x)] =
                    px < pstuff.width && py < pstuff.height ? std::uint8_t(pstuff.pixels[std::size_t(py * pstuff.width + px)] & 15) : 0;
            }
    };
    const auto show = [&](const std::vector<std::uint8_t>& picture, int width, int left, int top) {
        const int lines = int(picture.size()) / width;
        for (int y = 0; y < lines; ++y)
            for (int x = 0; x < width; ++x) {
                const int c = picture[std::size_t(y * width + x)], dx = left + x, dy = top + y;
                if (c == 0 || dx < 16 || dx >= 304 || dy < 0 || dy >= FrontScreen::height) continue;
                screen_buffer_.pixels[std::size_t(dy * FrontScreen::width + dx)] = std::uint8_t(48 + c);
            }
    };
    // (The sprites are placed where $A7924 last put them; the rotor's
    // picture goes straight into the sprites' memory each round.)
    const int x = st.sprite_x, y = st.sprite_y;
    std::vector<std::uint8_t> body(std::size_t(48 * panel_.piece(0xcb).lines), 0);
    copy(0xcb, 48, body);
    copy(0xcd + st.rotor, 48, body); // over its top lines
    show(body, 48, x - 0x20, y & 0xff);
    std::vector<std::uint8_t> nose(std::size_t(16 * panel_.piece(0xcc).lines), 0);
    copy(0xcc, 16, nose);
    show(nose, 16, x + 16, y);
}

int FrontEnd::tables_sine(int direction) const { // $A168E
    const auto at = file_offset(0xa168e + std::uint32_t(direction & 0x1fe));
    return std::int16_t(amiga::read_be16(program_, at));
}

// ---- quick saves ----

void FrontEnd::restart(const MissionTables& tables) {
    const auto started = started_; // (each start records its arguments afresh)
    switch (started.screen) {
    case Screen::none: break;
    case Screen::intro: start_intro(); break;
    case Screen::hill: start_hill(started.campaign); break;
    case Screen::parallax: start_parallax(started.campaign, started.landscape); break;
    case Screen::briefing: start_briefing(started.campaign, tables); break;
    case Screen::debrief: start_debrief(started.campaign); break;
    case Screen::ending: start_ending(); break;
    case Screen::saves: start_save_list(started.names); break;
    }
}

// ---- the saved games ($A9E56) ----

// $A0D70: a rectangle of four lines ($A0DB8 stops a point short of each
// end; the last line runs one further): every point of its outline.
void FrontEnd::rectangle(int x, int y, int w, int h, int colour) {
    const auto put = [&](int px, int py) {
        if (px >= 0 && py >= 0 && px < FrontScreen::width && py < FrontScreen::height)
            screen_buffer_.pixels[std::size_t(py * FrontScreen::width + px)] = std::uint8_t(colour);
    };
    for (int i = 0; i <= w; ++i) {
        put(x + i, y);
        put(x + i, y + h);
    }
    for (int j = 0; j <= h; ++j) {
        put(x, y + j);
        put(x + w, y + j);
    }
}

// The text's place ($A997A: centred, its width; $828A8: the tallest glyph
// drawn by $A2464) and its click area ($AAB56).
FrontEnd::Area FrontEnd::text_area(const std::string& text, int y) const {
    const FrontFrames frames(program_, hill_table);
    const int w = text_width(text, widths_header), x = int(unsigned(FrontScreen::width - w) >> 1);
    int h = 0;
    for (const unsigned char c : text) {
        int glyph = -1;
        if (c >= '0' && c <= '9') glyph = c - '0' + 26;
        else if (c >= 'a' && c <= 'z') glyph = c - 'a' + 40;
        else if (c >= 'A' && c <= 'Z') glyph = c - 'A';
        else if (const auto m = std::string("'.,!").find(char(c)); m != std::string::npos) glyph = int(m) + 36;
        if (glyph < 0) continue;
        if (const auto f = frames.frame(0xd, glyph)) h = std::max(h, f->height);
    }
    return {x - 3, w + 4, y, h + 5};
}

void FrontEnd::boxed_text(const std::string& text, int y, int outer, int inner) {
    const FrontFrames frames(program_, hill_table);
    const auto a = text_area(text, y);
    draw_text(frames, 0xd, widths_header, text, a.x + 3, y);
    rectangle(a.x + 1, y - 2, a.w, a.h, inner);
    rectangle(a.x, y - 3, a.w, a.h, outer);
}

std::vector<std::array<int, 4>> FrontEnd::draw_disk_request() {
    start_save_list({});
    screen_buffer_.clear();
    const FrontFrames frames(program_, hill_table);
    draw_centred(frames, 0xd, widths_header, program_text(0xaa21c), 0x42);
    boxed_text(program_text(0xaa232), 0x6c);
    boxed_text(program_text(0xaa235), 0x8c);
    std::vector<std::array<int, 4>> areas;
    for (const auto& [text, y] : {std::pair{program_text(0xaa232), 0x6c}, std::pair{program_text(0xaa235), 0x8c}}) {
        const auto a = text_area(text, y);
        areas.push_back({a.x, a.w, a.y, a.h});
    }
    return areas;
}

// $AA082: the five games from the first shown, $3E + 21 apart.
void FrontEnd::draw_save_names() {
    const auto& names = started_.names;
    for (int row = 0; row < 5; ++row) {
        const int k = save_list_.first + row;
        if (k >= int(names.size())) break;
        boxed_text(names[std::size_t(k)], 0x3e + 0x15 * row, 2, 3);
    }
}

void FrontEnd::start_save_list(const std::vector<std::string>& names) {
    started_ = {Screen::saves, {}, {}, names};
    // $AAB98: the hill's sheet for its header font; $AAAD0: the screen
    // cleared (the hill's colours stay).
    loaded_.clear();
    const auto hills = amiga::read_ilbm(content_.read("hills.lbm", 2));
    screen_buffer_.clear();
    screen_buffer_.palette.fill(0);
    for (std::size_t i = 0; i < hills.palette.size() && i < 16; ++i) screen_buffer_.palette[i] = hills.palette[i];
    const auto& pstuff = panel_.pstuff();
    for (std::size_t i = 0; i < 16 && i < pstuff.palette.size(); ++i) screen_buffer_.palette[16 + i] = pstuff.palette[i];
    loaded_.emplace(at_sheet, hills);
    const FrontFrames frames(program_, hill_table);
    draw_centred(frames, 0xd, widths_header, program_text(0xaa016), 0xc); // SELECT FILE
    boxed_text(program_text(0xaa030), 0x24);                               // UP
    boxed_text(program_text(0xaa033), 0xae);                               // DOWN
    boxed_text(program_text(0xaa022), 0xc8);                               // CONTINUE GAME
    save_list_ = SaveListState{};
    draw_save_names();
    screen_buffer_.fade_low = screen_buffer_.fade_high = 0;
    picked_ = -1;
    std::vector<Step> steps;
    steps.push_back([this](const app::GameInput& input) { return save_list_frame(input); });
    run(Screen::saves, std::move(steps));
}

bool FrontEnd::save_list_frame(const app::GameInput& input) {
    auto& st = save_list_;
    using Mode = SaveListState::Mode;
    switch (st.mode) {
    case Mode::fade_in: // $AAC0E: a level a frame, 0-15
        screen_buffer_.fade_low = screen_buffer_.fade_high = st.level;
        if (++st.level == 16) st.mode = Mode::wait;
        return false;
    case Mode::fade_out: // $AAC44: down a level a frame from 15
        if (--st.level < 0) {
            st.level = 0;
            st.mode = Mode::done;
        }
        screen_buffer_.fade_low = screen_buffer_.fade_high = st.level;
        return st.mode == Mode::done;
    case Mode::done:
        return true;
    case Mode::wait:
        break;
    }
    // $AAB10: a new press over an area ($A0662: the first that holds the
    // pointer; the game's pointer is the picture's column - 16, line + 4).
    if (!(input.mouse_button && !left_was_down_ && input.pointer)) return false;
    const int px = input.pointer_x, py = input.pointer_y + 4;
    const auto in = [&](const Area& a) { return px >= a.x && px <= a.x + a.w && py >= a.y && py <= a.y + a.h; };
    const int count = int(started_.names.size());
    const auto redraw = [&] { // $A9F6C: lines 58-165 cleared, the names again
        std::fill(screen_buffer_.pixels.begin() + 58 * FrontScreen::width,
                  screen_buffer_.pixels.begin() + 166 * FrontScreen::width, std::uint8_t(0));
        draw_save_names();
    };
    if (in(text_area(program_text(0xaa030), 0x24))) { // $AA038
        st.first = std::max(st.first - 4, 0);
        redraw();
        return false;
    }
    if (in(text_area(program_text(0xaa033), 0xae))) { // $AA050
        st.first += 4;
        if (st.first + 5 >= count) st.first = std::max(count - 5, 0);
        redraw();
        return false;
    }
    if (in(text_area(program_text(0xaa022), 0xc8))) { // $AA4C4
        picked_ = -1;
        st.mode = Mode::fade_out;
        st.level = 15;
        return false;
    }
    for (int row = 0; row < 5 && st.first + row < count; ++row) {
        if (!in(text_area(started_.names[std::size_t(st.first + row)], 0x3e + 0x15 * row))) continue;
        picked_ = st.first + int(unsigned(py - 0x3e) / 0x15); // $AA0F4
        if (picked_ >= count) picked_ = -1;
        st.mode = Mode::fade_out;
        st.level = 15;
        return false;
    }
    return false;
}

} // namespace fodder::game

