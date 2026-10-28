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
constexpr std::uint32_t at_picture = 0x75f60; // the RAW screens

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
    std::array<platform::Color, 48> colours{};
    for (std::size_t i = 0; i < 48; ++i) {
        const int level = i < 16 ? fade_low : fade_high;
        const int down = 15 - std::clamp(level, 0, 15);
        const auto c = palette[i];
        const int r = std::max(((c >> 8) & 15) - down, 0), g = std::max(((c >> 4) & 15) - down, 0),
                  b = std::max((c & 15) - down, 0);
        colours[i] = data::to_color(amiga::Rgb4((r << 8) | (g << 4) | b));
    }
    platform::Image image(width, height);
    for (std::size_t i = 0; i < pixels.size(); ++i) image.pixels[i] = colours[std::min<std::size_t>(pixels[i], 47)];
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

void FrontEnd::tick(const app::GameInput& input) {
    if (screen_ == Screen::none) return;
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
    loaded_.clear();
    loaded_.emplace(at_font, data::read_raw_screen(content_.read("font.raw", 1)));
    std::vector<Step> steps;
    const auto once = [this](std::function<void()> f) { return [f](const app::GameInput&) { f(); return true; }; };
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
    loaded_.clear();
    auto hills = amiga::read_ilbm(content_.read("hills.lbm", 2));
    loaded_.emplace(at_sheet, hills);
    screen_buffer_.clear();
    screen_buffer_.palette.fill(0);
    // The hill picture's colours (0-15) and the panel's (16-31, pstuff).
    for (std::size_t i = 0; i < hills.palette.size() && i < 32; ++i) screen_buffer_.palette[i] = hills.palette[i];
    // The background: the hill itself, from the picture's right part.
    for (int y = 0; y < FrontScreen::height && y < hills.height; ++y) {
        for (int x = 48; x < FrontScreen::width && x < hills.width; ++x) {
            if (y < 0x1b || y >= 0xd5) continue;
            screen_buffer_.pixels[std::size_t(y * FrontScreen::width + x)] =
                hills.pixels[std::size_t(y * hills.width + x)];
        }
    }
    const FrontFrames frames(program_, hill_table);
    // (The graves of the fallen, $A5CC4 with the table $90F10 and the
    // positions $A5110, are not drawn yet: their frames lie in a buffer
    // whose layout is still open.)
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
    // $A63BA: the column on the left (the panel's sprites, colours 16-31 from
    // pstuff), from line 26.
    HillColumn column;
    for (const auto& f : campaign.fame) {
        if (f.name >= 0) column.fame.push_back({f.name, f.rank, f.kills});
    }
    for (std::size_t k = 0; k < campaign.roster.size() && int(k) < campaign.soldiers; ++k) {
        const auto& r = campaign.roster[k];
        if (r.name >= 0) column.squad.push_back({r.name, r.rank, r.kills});
    }
    const auto panel = compose_hill_column(panel_, column);
    const auto& pstuff = panel_.pstuff();
    for (std::size_t i = 0; i < 16 && i < pstuff.palette.size(); ++i) screen_buffer_.palette[16 + i] = pstuff.palette[i];
    for (int y = 0; y < panel_height && y + 26 < FrontScreen::height; ++y) {
        for (int x = 0; x < panel_width; ++x) {
            const auto c = panel[std::size_t(y * panel_width + x)];
            if (c != 0) screen_buffer_.pixels[std::size_t((y + 26) * FrontScreen::width + x)] = std::uint8_t(16 + c);
        }
    }
    song_ = -1;
    std::vector<Step> steps;
    steps.push_back(fade_in());
    // $A58F8 on a click (in the game's pointer units: column - 16, line + 4):
    // LOAD at the top left, SAVE at the top right (where the mission allows
    // it), anywhere below the header goes on.
    hill_choice_ = HillChoice::go_on;
    save_allowed_ = save == 0;
    steps.push_back([this](const app::GameInput& input) {
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

// ---- the briefing ($A6614) ----

void FrontEnd::start_briefing(const Campaign& campaign, const MissionTables& tables) {
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
    const auto box = [&](int x, int y, int w, int h, int colour) { // $A0D70
        for (int i = 0; i <= w; ++i) {
            screen_buffer_.pixels[std::size_t(y * FrontScreen::width + x + i)] = std::uint8_t(colour);
            screen_buffer_.pixels[std::size_t((y + h) * FrontScreen::width + x + i)] = std::uint8_t(colour);
        }
        for (int j = 0; j <= h; ++j) {
            screen_buffer_.pixels[std::size_t((y + j) * FrontScreen::width + x)] = std::uint8_t(colour);
            screen_buffer_.pixels[std::size_t((y + j) * FrontScreen::width + x + w)] = std::uint8_t(colour);
        }
    };
    box(0x29, 0x5b, 0xee, 0x82, 3);
    box(0x28, 0x5a, 0xee, 0x82, 2);
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
    steps.push_back([this, &campaign](const app::GameInput&) {
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
    loaded_.clear();
    auto morph = amiga::read_ilbm(content_.read("morphbig.lbm", 1));
    auto rank = amiga::read_ilbm(content_.read("rankfont.lbm", 1));
    screen_buffer_.clear();
    screen_buffer_.palette.fill(0);
    for (std::size_t i = 0; i < morph.palette.size() && i < 32; ++i) screen_buffer_.palette[i] = morph.palette[i];
    loaded_.emplace(at_sheet, std::move(rank));
    loaded_.emplace(at_font, std::move(morph));
    const bool fallen = campaign.heroes.size() > campaign.heroes_this_mission;
    song_ = 0;
    warx_ = false;
    // The banner and the two pictures beside it ($A8C4A …).
    if (fallen) {
        blit_block(8, 0x30, 0);    // LOST IN SERVICE
        blit_block(0, 0, 0x4a);    // poppies
        blit_block(4, 0xf0, 0x4a);
    } else {
        blit_block(9, 0x30, 0);    // HEROES IN VICTORY
        blit_block(3, 0, 0x4a);    // medals
        blit_block(7, 0xf0, 0x4a);
    }
    const FrontFrames text(program_, 0xa91e2);
    // $A9124: "Mission n" (group 4, widths $A9B66; the original draws it at
    // the bottom of its scrolling screen, here where it comes to rest).
    draw_centred(text, 4, 0xa9b66, program_text(0xa9180) + std::to_string(campaign.missions_started), 0x50);
    // The names: the mission's fallen, else the recruits who come back (with
    // their new ranks, $A8E10).
    struct Line { int name, rank; };
    std::vector<Line> lines;
    if (fallen) {
        for (std::size_t i = campaign.heroes_this_mission; i < campaign.heroes.size(); ++i)
            lines.push_back({campaign.heroes[i].name, campaign.heroes[i].rank});
    } else {
        for (const auto& r : campaign.roster) {
            if (r.name >= 0 && r.on_map) lines.push_back({r.name, std::min(r.rank + r.served, 15)});
        }
    }
    std::vector<Step> steps;
    steps.push_back(fade_in());
    for (std::size_t k = 0; k < lines.size(); ++k) {
        const int y = 0x85 + 0x2c * int(k);
        const auto line = lines[k];
        steps.push_back([this, line, y](const app::GameInput&) {
            if (++counter_ < 0x2c * 2) return false; // $82166: 44 rounds of 2 frames
            blit_block(10, 0x60, y);
            const FrontFrames frames(program_, 0xa91e2);
            draw_frame(frames, 2, line.rank, 0x64, y + 4);
            draw_frame(frames, 2, line.rank, 0xcc, y + 4);
            std::string name;
            const auto at = file_offset(0x917e0 + std::uint32_t(line.name) * 9);
            for (std::size_t i = 0; i < 6 && at + i < program_.size() && program_[at + i] != ' '; ++i) name += char(program_[at + i]);
            draw_centred(frames, 3, 0xa9b06, name, y + 5);
            return true;
        });
    }
    steps.push_back(hold(0xb4 * 2, false)); // $82166 = $B4 after the last
    steps.push_back([this](const app::GameInput& i) { return fade_out()(i); });
    run(Screen::debrief, std::move(steps));
}

// ---- the end ($9CAB2) ----

void FrontEnd::start_ending() {
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
    loaded_.clear();
    const int disk = 2;
    auto play = std::make_shared<amiga::IlbmPicture>(amiga::read_ilbm(content_.read(land + "play.lbm", disk)));
    const std::array<std::string, 4> palettes{land + "sky.pl8", land + "mid.pl8", land + "fgnd.pl8", land + "fgn2.pl8"};
    screen_buffer_.palette.fill(0);
    for (std::size_t band = 0; band < 4; ++band) {
        const auto colours = data::read_palette(content_.read(palettes[band], disk));
        for (std::size_t i = 0; i < 8 && i < colours.size(); ++i) screen_buffer_.palette[band * 8 + i] = colours[i];
    }
    const auto& pstuff = panel_.pstuff();
    for (std::size_t i = 0; i < 16 && i < pstuff.palette.size(); ++i) screen_buffer_.palette[32 + i] = pstuff.palette[i];
    loaded_.emplace(at_sheet, amiga::read_ilbm(content_.read("pstuff.lbm", 1))); // the name's font (as the briefing's)
    song_ = 7;
    warx_ = false;
    struct State {
        // $826B4…$826C0: the layers' scroll (16.16), slower to faster.
        std::array<std::int32_t, 4> scroll{};
        int name_x{0x138};                          // $826B0
        std::int32_t heli_x{0x150 << 16}, heli_y{0x1e << 16}; // $826CC, $826D0
        int heading{0x180}, wanted{0x180}, speed{}, wanted_speed{}; // $826DC, $826DE, $826D4, $826D8
        int leg{}, leg_left{};                      // $826E0, $826E2
        int fade{};                                 // $826E4
        bool leaving{};
        int frame{};
    };
    auto st = std::make_shared<State>();
    const std::string name = program_text(0x80000 + program_long(0xa83e8 + std::uint32_t(std::max(campaign.missions_started - 1, 0)) * 4));
    const auto draw = [this, play, st, name] {
        screen_buffer_.clear();
        // The bands (lines; the play picture's rows; the layer; the palette).
        struct Band { int top, bottom, source, layer, colours; };
        static constexpr Band bands[] = {{0, 44, 0, 0, 0},      // clouds (sky.pl8)
                                         {22, 88, 180, 1, 8},    // mountains (mid.pl8)
                                         {76, 133, 62, 2, 16},   // the canopy (fgnd.pl8)
                                         {132, 188, 120, 2, 16}, // the undergrowth (fgnd.pl8)
                                         {150, 196, 250, 3, 24}};    // the foreground (fgn2.pl8)
        for (const auto& b : bands) {
            const int shift = int(std::uint32_t(st->scroll[std::size_t(b.layer)]) >> 16) % FrontScreen::width;
            for (int y = b.top; y < b.bottom; ++y) {
                const int sy = b.source + (y - b.top);
                if (sy >= play->height) break;
                for (int x = 0; x < FrontScreen::width; ++x) {
                    const int sx = (x + shift) % FrontScreen::width;
                    const int c = play->pixels[std::size_t(sy * play->width + sx)] & 7;
                    if (c == 0 && b.layer != 0) continue; // the playfields' clear colour
                    screen_buffer_.pixels[std::size_t(y * FrontScreen::width + x)] = std::uint8_t(b.colours + c);
                }
            }
        }
        // The helicopter: the pointer sprite's image $CC.
        const auto piece = panel_.piece(0xcc);
        const auto& pstuff = panel_.pstuff();
        const int hx = int(st->heli_x >> 16) + 16, hy = int(st->heli_y >> 16) - 4;
        for (int y = 0; y < piece.lines; ++y) {
            for (int x = 0; x < 16; ++x) {
                const int c = pstuff.pixels[std::size_t((piece.y + y) * pstuff.width + piece.x + x)];
                const int dx = hx + x, dy = hy + y;
                if (c == 0 || dx < 0 || dy < 0 || dx >= FrontScreen::width || dy >= FrontScreen::height) continue;
                screen_buffer_.pixels[std::size_t(dy * FrontScreen::width + dx)] = std::uint8_t(32 + c);
            }
        }
        // The mission's name, sliding in.
        const FrontFrames frames(program_, briefing_table);
        draw_text(frames, 1, widths_big, name, st->name_x, 0xcc, 0x25);
    };
    std::vector<Step> steps;
    steps.push_back([this, st, draw](const app::GameInput& input) {
        // $A7A5C: the layers scroll; the name comes in by 4 to $C.
        st->scroll[0] -= 0x8000;
        st->scroll[1] -= 0x10000;
        st->scroll[2] -= 0x20000;
        st->scroll[3] -= 0x40000;
        for (auto& v : st->scroll) {
            if (v < 0) v += FrontScreen::width << 16;
        }
        if (st->name_x != 0xc) st->name_x = std::max(st->name_x - 4, 0xc);
        // $A7AE6: fading in while the helicopter is right of 80 and no click
        // came; then out, and the screen ends.
        if (input.mouse_button && !left_was_down_) st->leaving = true;
        const bool even = (st->frame++ & 1) == 0;
        if (!st->leaving && (st->heli_x >> 16) - 0x20 > 0x30) {
            if (even && st->fade < 16) ++st->fade;
        } else {
            st->leaving = true;
            if (even) {
                if (st->fade == 0) return true;
                --st->fade;
            }
        }
        // $A7924: the helicopter's legs (heading, speed, ticks; -1 ends).
        if (st->fade > 15 || st->leaving) {
            if (--st->leg_left < 0) {
                const auto at = file_offset(0xa7a20 + std::uint32_t(st->leg) * 6);
                const int duration = std::int16_t(amiga::read_be16(program_, at + 4));
                if (duration >= 0) {
                    st->wanted = amiga::read_be16(program_, at);
                    st->wanted_speed = std::int16_t(amiga::read_be16(program_, at + 2));
                    st->leg_left = duration;
                    st->leg += 1;
                }
            }
            if (st->leg_left >= 0) {
                if (st->speed < st->wanted_speed) ++st->speed;
                else if (st->speed > st->wanted_speed) --st->speed;
                st->heli_x += std::int32_t(tables_sine(st->heading) >> 2) * st->speed;
                st->heli_y += std::int32_t(tables_sine(st->heading + 0x80) >> 2) * st->speed;
                const int diff = ((((st->wanted - st->heading) >> 5) - 1) ^ 0xf) & 0xf;
                st->heading = (st->heading + std::int8_t(program_[file_offset(0x955c0 + std::uint32_t(diff))]) * 4) & 0x1fe;
            }
        }
        screen_buffer_.fade_low = screen_buffer_.fade_high = std::min(st->fade, 15);
        draw();
        return false;
    });
    run(Screen::parallax, std::move(steps));
}

int FrontEnd::tables_sine(int direction) const { // $A168E
    const auto at = file_offset(0xa168e + std::uint32_t(direction & 0x1fe));
    return std::int16_t(amiga::read_be16(program_, at));
}

} // namespace fodder::game

