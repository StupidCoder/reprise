#include "wings_of_fury_amiga/game/frontend.hpp"
#include "amiga/topaz.hpp"
#include "platform/files.hpp"
#include "wings_of_fury_amiga/data/preview.hpp"
#include <algorithm>
#include <cstdio>
#include <fstream>

namespace wings::game {
namespace {

constexpr std::uint32_t a4 = 0x11AF52;
constexpr int fade_frames = 16;

// The story ($107E80).
constexpr std::uint32_t story_pointer = 0x1158E0; // -$5672
constexpr int story_lines = 0x35;
constexpr int story_width = 640, story_height = 200;
constexpr int story_line_spacing = 0xE;
constexpr int story_new_line_row = 0xC4;  // a new line is drawn this far down the view
constexpr int story_ring = 0xD2;          // the ring buffer's rows
constexpr int story_justify = 0x267;      // lines inside a paragraph are spread to this width
constexpr int story_frames_per_row = 4;   // $106EEE(2) twice per row
constexpr int copper_top = 5, copper_bottom = 0xC9;

// The pictures ($108022), with the frames each waits ($106EEE).
constexpr int logo_wait = 0x3C, broderbund_wait = 0x78, title_wait = 0x12C, credits_wait = 0x258;

// Rank selection ($108262, $108194, $108228).
constexpr int rank_entries = 8;
constexpr int rank_timeout = 0x708;
constexpr int stick_hold = 9;

// The hall of fame ($109472, $10967E, $109856).
constexpr int name_length = 0x10;
constexpr int name_x = 0x52, name_y = 0x66;
constexpr int table_wait = 0x708;
constexpr int table_entries = 10;
constexpr int score_entry_size = 0x24;

// The C64 key matrix (Joyport's keyboard) as the characters it types.
char key_char(int index) {
    static constexpr char table[64] = {
        0,   '\n', 0,   0,   0,   0,   0,   0,    // 0..7: DEL RETURN
        '3', 'w', 'a', '4', 'z', 's', 'e', 0,    // 8..15
        '5', 'r', 'd', '6', 'c', 'f', 't', 'x',  // 16..23
        '7', 'y', 'g', '8', 'b', 'h', 'u', 'v',  // 24..31
        '9', 'i', 'j', '0', 'm', 'k', 'o', 'n',  // 32..39
        0,   'p', 'l', '-', '.', '\'', 0,  ',',  // 40..47
        0,   0,   ';', 0,   0,   '=', ']', '/',  // 48..55
        '1', 0,   0,   '2', ' ', 0,   'q', 0,    // 56..63
    };
    return index >= 0 && index < 64 ? table[index] : 0;
}

std::vector<std::uint16_t> palette_of(const amiga::IlbmPicture* p) {
    if (p == nullptr) return {};
    return {p->palette.begin(), p->palette.end()};
}

std::vector<std::uint16_t> words_at(const amiga::LoadedImage& wings, std::uint32_t at, int count) {
    std::vector<std::uint16_t> out;
    for (int i = 0; i < count; ++i) out.push_back(wings.u16(at + std::uint32_t(i * 2)));
    return out;
}

// $105956 / $10591E: a text in `newarmyfont`, its pixels set in a row
// buffer `width` wide (12 rows), spread to `target` pixels when wider than
// the text (the extra shared out over the gaps, the first ones a pixel
// more). Nothing when the text is wider than the buffer.
std::vector<bool> text_row(const TickerFont& font, const std::string& text, int target, int width) {
    std::vector<bool> bits(std::size_t(width * font.height));
    const auto in_font = [&](unsigned char c) { return c >= font.first && c <= font.last; };
    const auto advance = [&](unsigned char c) { const int w = font.widths[std::size_t(c - font.first)]; return w == 0 ? 0xA : w; };
    int natural = 0;
    for (unsigned char c : text) {
        if (in_font(c)) natural += advance(c) + 1;
    }
    if (natural > width) return bits;
    int per = 0, extra_left = 0;
    const int gaps = int(text.size()) - 1;
    if (target - natural > 0 && gaps > 0) {
        per = (target - natural) / gaps;
        extra_left = (target - natural) % gaps;
    }
    int x = 0;
    for (unsigned char c : text) {
        if (!in_font(c)) continue;
        const int index = c - font.first;
        const int w = font.widths[std::size_t(index)];
        if (w != 0) {
            const int words = (w + 15) >> 4;
            const auto& glyph = font.glyphs[std::size_t(index)];
            for (int row = 0; row < font.height; ++row) {
                for (int k = 0; k < words; ++k) {
                    const std::uint16_t v = glyph[std::size_t(row * words + k)];
                    for (int b = 0; b < 16; ++b) {
                        const int px = x + k * 16 + b;
                        if ((v >> (15 - b) & 1) && px < width) bits[std::size_t(row * width + px)] = true;
                    }
                }
            }
        }
        x += advance(c) + 1 + per;
        if (extra_left-- > 0) ++x;
    }
    return bits;
}

// Text() with topaz 8: the baseline at y.
void topaz_text(IndexedCanvas& canvas, const std::string& text, int x, int y, int pen, int background, bool jam2) {
    for (const unsigned char c : text) {
        for (int row = 0; row < amiga::Topaz8::height; ++row) {
            for (int col = 0; col < amiga::Topaz8::width; ++col) {
                const int px = x + col, py = y - amiga::Topaz8::baseline + row;
                if (px < 0 || py < 0 || px >= canvas.width || py >= canvas.height) continue;
                if (amiga::Topaz8::pixel(c, col, row)) canvas.pixels[std::size_t(py * canvas.width + px)] = std::uint8_t(pen);
                else if (jam2) canvas.pixels[std::size_t(py * canvas.width + px)] = std::uint8_t(background);
            }
        }
        x += amiga::Topaz8::width;
    }
}

void line(IndexedCanvas& canvas, int x0, int y0, int x1, int y1, int pen) {
    for (int x = std::min(x0, x1); x <= std::max(x0, x1); ++x) {
        for (int y = std::min(y0, y1); y <= std::max(y0, y1); ++y) {
            if (x >= 0 && y >= 0 && x < canvas.width && y < canvas.height) canvas.pixels[std::size_t(y * canvas.width + x)] = std::uint8_t(pen);
        }
    }
}

void put_row(platform::Image& image, int y, const IndexedCanvas& canvas, int row, const std::vector<std::uint16_t>& colours,
             bool lowres) {
    for (int x = 0; x < canvas.width; ++x) {
        const auto index = canvas.at(x, row);
        const auto colour = data::to_color(index < colours.size() ? colours[index] : 0);
        if (lowres) {
            image.put(2 * x, y, colour);
            image.put(2 * x + 1, y, colour);
        } else {
            image.put(x, y, colour);
        }
    }
}

IndexedCanvas canvas_of(const amiga::IlbmPicture* p, int width, int height) {
    IndexedCanvas c(width, height);
    if (p == nullptr) return c;
    for (int y = 0; y < std::min(height, p->height); ++y) {
        for (int x = 0; x < std::min(width, p->width); ++x) c.pixels[std::size_t(y * width + x)] = p->pixels[std::size_t(y * p->width + x)];
    }
    return c;
}

} // namespace

std::uint16_t fade_colour(int step, std::uint16_t from, std::uint16_t to) {
    if (step == 0xF) return to;
    int out = from;
    out += ((((to & 0xF) - (from & 0xF)) * step) / 0xF) & 0xF;
    out += ((((to & 0xF0) - (from & 0xF0)) * step) / 0xF) & 0xF0;
    out += ((((to & 0xF00) - (from & 0xF00)) * step) / 0xF) & 0xF00;
    return std::uint16_t(out);
}

FrontEnd::FrontEnd(const data::Assets& assets, const TickerFont& font, std::filesystem::path score_file)
    : assets_(&assets), font_(&font), score_file_(std::move(score_file)) {
    // The story's lines; one is spread to the full width unless the next
    // one is empty (a paragraph's end), as the byte after its zero tells.
    const auto& wings = assets.program;
    std::uint32_t at = wings.u32(story_pointer);
    for (int i = 0; i < story_lines; ++i) {
        StoryLine l;
        while (wings.u8(at) != 0) l.text += char(wings.u8(at++));
        l.justified = wings.u8(at + 1) != 0;
        ++at;
        story_bits_.push_back(text_row(*font_, l.text, l.justified ? story_justify : 0, story_width));
        story_.push_back(std::move(l));
    }
    read_scores();
}

const amiga::IlbmPicture* FrontEnd::picture_named(const std::string& name) const {
    for (const auto& p : assets_->pictures) {
        if (p.name == name) return &p.picture;
    }
    return nullptr;
}

void FrontEnd::fade_to(const std::vector<std::uint16_t>& target) {
    fade_from_ = shown_.colours;
    fade_to_ = target;
    fade_from_.resize(std::max(fade_from_.size(), target.size()));
    fade_to_.resize(fade_from_.size());
    fade_step_ = 0;
}

// $107084 / $1073B0: sixteen frames, the colours one step further each.
bool FrontEnd::fading() {
    if (fade_step_ < 0) return false;
    shown_.colours.resize(fade_from_.size());
    for (std::size_t i = 0; i < fade_from_.size(); ++i) shown_.colours[i] = fade_colour(fade_step_, fade_from_[i], fade_to_[i]);
    if (++fade_step_ >= fade_frames) fade_step_ = -1;
    return true;
}

void FrontEnd::start_opening() {
    song_request_ = 2; // ($108022)
    screen_ = Screen::story;
    frame_ = 0;
    scroll_ = drawn_ = row_counter_ = 0;
    ring_ = story_ring;
    left_ = story_ring;
    fade_lines_ = 16;
    ending_ = false;
    story_y_.clear();
}

void FrontEnd::start_rank_select() {
    song_request_ = 4; // ($108262)
    screen_ = Screen::rank_select;
    selection_ = 0;
    idle_ = 0;
    repeat_ = 0;
    load_chosen_ = false;
    phase_ = 0;
    shown_.picture = picture_named("selectrank");
    shown_.colours.assign(32, 0);
    rank_canvas_ = canvas_of(shown_.picture, 320, 200);
    draw_rank_screen();
    fade_to(palette_of(shown_.picture));
    fire_was_ = true; // (a press still held from before does not choose)
}

const data::Shape* rank_bar(const data::Assets& assets, int entry) {
    for (const auto& l : assets.libraries) {
        if (l.name == "selectrank.shp") {
            return entry >= 0 && std::size_t(entry) < l.library.shapes.size() ? &l.library.shapes[std::size_t(entry)] : nullptr;
        }
    }
    return nullptr;
}

// $110E0A: the entry's bar XORed at the place it was cut from (+8, +$A).
void FrontEnd::draw_rank_screen() {
    if (const auto* bar = rank_bar(*assets_, selection_)) rank_canvas_.draw_xor(*bar, bar->sheet_x, bar->sheet_y);
}

void FrontEnd::start_game_over(long score, int rank) {
    song_request_ = 0; // ($109856)
    new_score_ = score;
    new_rank_ = rank;
    read_scores();
    name_.clear();
    cursor_ = 0;
    phase_ = 0;
    shown_.picture = nullptr;
    if (scores_.size() >= std::size_t(table_entries) && score > scores_[table_entries - 1].score) {
        screen_ = Screen::name_entry;
        shown_.colours.assign(16, 0);
        fade_to(words_at(assets_->program, a4 - 0x55A2, 16));
        keys_.fill(true); // (keys held now do not type)
    } else {
        screen_ = Screen::hall_of_fame;
        phase_ = 0;
    }
    fire_was_ = true;
}

FrontEnd::Result FrontEnd::step(const app::GameInput& input) {
    ++frame_;
    const bool fire = input.fire && !fire_was_;
    if (!input.fire) fire_was_ = false;
    switch (screen_) {
    case Screen::story: {
        if (ending_) {
            // The copper's fade lines shrink: the text darkens to nothing.
            if (frame_ % 2 == 0 && --fade_lines_ <= 0) {
                screen_ = Screen::pictures;
                song_request_ = 1;
                phase_ = 0;
                shown_.picture = picture_named("broderbund");
                shown_.colours.assign(32, 0);
                fade_to(words_at(assets_->program, a4 - 0x56B2, 32));
            }
            return Result::running;
        }
        if ((frame_ - 1) % story_frames_per_row == 0) {
            if (row_counter_ % story_line_spacing == 0) {
                if (drawn_ < story_lines) {
                    story_y_.push_back(scroll_ + story_new_line_row);
                    left_ = story_ring;
                }
                ++drawn_;
            }
        }
        if (frame_ % story_frames_per_row == 0) {
            --left_;
            ++row_counter_;
            ++scroll_;
            if (--ring_ <= 0) {
                ring_ = story_ring;
                row_counter_ = 0;
            }
            if (left_ < 0 || input.fire) {
                ending_ = true;
                frame_ = 0;
                fade_lines_ = 16;
            }
        }
        return Result::running;
    }
    case Screen::pictures: {
        if (fading()) return Result::running;
        if (wait_ > 0) {
            --wait_;
            if (fire) {
                phase_ = 8;
                fade_to(std::vector<std::uint16_t>(shown_.colours.size(), 0));
                wait_ = 0;
            }
            return Result::running;
        }
        switch (phase_++) {
        case 0: wait_ = logo_wait; break;     // the logo alone
        case 1: fade_to(palette_of(shown_.picture)); break;
        case 2: wait_ = broderbund_wait; break;
        case 3: fade_to(std::vector<std::uint16_t>(32, 0)); break;
        case 4:
            shown_.picture = picture_named("wingstitle");
            fade_to(palette_of(shown_.picture));
            wait_ = title_wait;
            break;
        case 5:
            fade_to(std::vector<std::uint16_t>(32, 0));
            break;
        case 6:
            shown_.picture = picture_named("creditscreen");
            fade_to(palette_of(shown_.picture));
            wait_ = credits_wait;
            break;
        case 7: fade_to(std::vector<std::uint16_t>(32, 0)); break;
        default:
            start_rank_select();
            return Result::running;
        }
        return Result::running;
    }
    case Screen::rank_select: {
        if (fading()) return Result::running;
        if (phase_ == 1) {
            // The choice made, the screen faded out: the music stops
            // ($102470) before the game.
            song_request_ = -2;
            phase_ = 2;
            return Result::running;
        }
        if (phase_ == 2) return Result::play;
        const bool choose = fire || input.c64_keys[1];
        if (choose || ++idle_ >= rank_timeout) {
            if (selection_ == rank_entries - 1) load_chosen_ = true;
            phase_ = 1;
            fade_to(std::vector<std::uint16_t>(32, 0));
            return Result::running;
        }
        const int move = input.up ? -1 : input.down ? 1 : 0;
        if (move == 0) {
            repeat_ = 0;
        } else if (repeat_ > 0) {
            --repeat_;
        } else {
            draw_rank_screen(); // (XOR: the old bar off)
            selection_ += move;
            if (selection_ < 0) selection_ = rank_entries - 1;
            if (selection_ >= rank_entries) selection_ = 0;
            draw_rank_screen();
            repeat_ = stick_hold;
        }
        return Result::running;
    }
    case Screen::name_entry: {
        if (fading()) return Result::running;
        if (phase_ == 1) {
            enter_score();
            screen_ = Screen::hall_of_fame;
            phase_ = 0;
            return Result::running;
        }
        if (edit_name(input) || fire) {
            phase_ = 1;
            fade_to(std::vector<std::uint16_t>(16, 0));
        }
        return Result::running;
    }
    case Screen::hall_of_fame: {
        if (fading()) return Result::running;
        switch (phase_) {
        case 0:
            shown_.colours.assign(48, 0);
            {
                auto target = palette_of(picture_named("hiscore.iff"));
                target.resize(32);
                const auto slab = palette_of(picture_named("hiscoreslab"));
                for (std::size_t i = 0; i < 16; ++i) target.push_back(i < slab.size() ? slab[i] : 0);
                fade_to(target);
            }
            wait_ = table_wait;
            phase_ = 1;
            break;
        case 1:
            if (--wait_ <= 0 || fire) {
                fade_to(std::vector<std::uint16_t>(shown_.colours.size(), 0));
                phase_ = 2;
            }
            break;
        default:
            start_rank_select();
            break;
        }
        return Result::running;
    }
    case Screen::saved_games: {
        if (fading()) return Result::running;
        if (phase_ == 2) {
            screen_ = Screen::none;
            return after_fade_;
        }
        const bool up = input.up && !up_was_, down = input.down && !down_was_;
        const bool ret = input.c64_keys[1] && !return_was_;
        int move = 0;
        bool chosen = false;
        if (slot_ < 6 && saving_) {
            // The line editor on the slot ($106086 at $2C, 28 characters).
            const int r = edit_line(input, slots_[std::size_t(slot_)], slot_cursor_, 0x1C);
            edited_[std::size_t(slot_)] = true;
            if (r == -1 || r == 1) move = r;
            else if (r == 2 || fire) chosen = true;
        } else {
            // $108194: up and down (stick or cursor keys), fire or Return.
            up_was_ = input.up;
            down_was_ = input.down;
            return_was_ = input.c64_keys[1];
            if (up) move = -1;
            else if (down) move = 1;
            else if (fire || ret) chosen = true;
        }
        if (move != 0) {
            // $108F7E: round the eight places; loading skips empty slots.
            do {
                slot_ += move;
                if (slot_ < 0) slot_ = 7;
                if (slot_ > 7) slot_ = 0;
            } while (!saving_ && slot_ < 6 && slots_[std::size_t(slot_)].empty());
            if (slot_ < 6) slot_cursor_ = int(slots_[std::size_t(slot_)].size());
            return Result::running;
        }
        if (!chosen) return Result::running;
        if (slot_ < 6) {
            // $106592: ':' and '/' would make a path; they become spaces.
            for (auto& c : slots_[std::size_t(slot_)]) {
                if (c == ':' || c == '/') c = ' ';
            }
        }
        const auto& name = slot_ < 6 ? slots_[std::size_t(slot_)] : std::string{};
        if (slot_ < 6 && !name.empty()) {
            if (saving_) {
                // $105E8A; then a slot renamed loses its old file (the first
                // one found, $109180).
                busy_ = "Saving game...";
                try {
                    platform::write_text(saved_game_file(name), game_text_);
                    for (int i = 0; i < found_; ++i) {
                        const auto& old = slots_found_[std::size_t(i)];
                        if (old.empty() || old == slots_[std::size_t(i)]) continue;
                        std::error_code ignored;
                        std::filesystem::remove(saved_game_file(old), ignored);
                        break;
                    }
                } catch (const std::exception&) {
                    // (A game that cannot be written is not saved.)
                }
                after_fade_ = Result::back;
            } else {
                // $105E1A.
                busy_ = "Loading game...";
                try {
                    const auto bytes = platform::read_file(saved_game_file(name));
                    loaded_game_.assign(bytes.begin(), bytes.end());
                    after_fade_ = Result::loaded;
                } catch (const std::exception&) {
                    after_fade_ = Result::back;
                }
            }
        } else {
            // Exit Game ($106E96: the program ends) or Cancel.
            after_fade_ = slot_ == 6 ? Result::exit_game : Result::back;
        }
        fade_to(std::vector<std::uint16_t>(16, 0)); // $1073B0
        phase_ = 2;
        return Result::running;
    }
    case Screen::none: break;
    }
    return Result::done;
}

std::filesystem::path FrontEnd::saved_game_file(const std::string& name) const {
    return score_file_.parent_path() / ("wof." + name);
}

// $108B96: the screen ($1069A4) with its texts and boxes, then the saved
// games found ($108A06: files `wof.<name>`, the first six; each slot 27
// characters at most).
void FrontEnd::start_saved_games(bool save, std::string game_text) {
    screen_ = Screen::saved_games;
    saving_ = save;
    game_text_ = std::move(game_text);
    loaded_game_.clear();
    busy_.clear();
    slots_ = {};
    slots_found_ = {};
    edited_ = {};
    std::vector<std::string> names;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(score_file_.parent_path(), error)) {
        const auto file = entry.path().filename().string();
        if (file.size() < 5 || file[3] != '.') continue;
        if (std::tolower(file[0]) != 'w' || std::tolower(file[1]) != 'o' || std::tolower(file[2]) != 'f') continue;
        names.push_back(file.substr(4, 0x1B));
    }
    std::sort(names.begin(), names.end());
    found_ = int(std::min<std::size_t>(names.size(), 6));
    for (int i = 0; i < found_; ++i) slots_[std::size_t(i)] = slots_found_[std::size_t(i)] = names[std::size_t(i)];
    slot_ = found_ == 0 && !save ? 7 : 0; // (nothing to load: on Cancel)
    slot_cursor_ = int(slots_[0].size());
    up_was_ = down_was_ = true;
    return_was_ = true;
    fire_was_ = true;
    keys_.fill(true);
    shown_.picture = nullptr;
    shown_.colours.assign(16, 0);
    fade_to(words_at(assets_->program, a4 - 0x55EE, 16)); // ($107084)
    phase_ = 1;
}

// $106086: the line editor. Letters and digits go in at the cursor (16 at
// most), Backspace and Delete take one out, the cursor keys move it;
// Return, up or down ends it.
bool FrontEnd::edit_name(const app::GameInput& input) {
    bool ends = input.up || input.down;
    for (int i = 0; i < 64; ++i) {
        const bool down = input.c64_keys[std::size_t(i)];
        const bool pressed = down && !keys_[std::size_t(i)];
        keys_[std::size_t(i)] = down;
        if (!pressed) continue;
        if (i == 1) ends = true;
        else if (i == 0) {
            if (cursor_ > 0) name_.erase(std::size_t(--cursor_), 1);
        } else if (const char c = key_char(i); c != 0 && int(name_.size()) < name_length) {
            name_.insert(name_.begin() + cursor_, c);
            cursor_ = std::min(cursor_ + 1, name_length);
        }
    }
    const bool space = input.space && !keys_were_;
    keys_were_ = input.space;
    if (space && int(name_.size()) < name_length) name_.insert(name_.begin() + cursor_++, ' ');
    if (input.left && !left_was_) cursor_ = std::max(cursor_ - 1, 0);
    if (input.right && !right_was_) cursor_ = std::min(cursor_ + 1, int(name_.size()));
    left_was_ = input.left;
    right_was_ = input.right;
    return ends;
}

int FrontEnd::edit_line(const app::GameInput& input, std::string& text, int& cursor, int length) {
    int result = 0;
    if (input.up && !up_was_) result = -1;
    if (input.down && !down_was_) result = 1;
    up_was_ = input.up;
    down_was_ = input.down;
    for (int i = 0; i < 64; ++i) {
        const bool down = input.c64_keys[std::size_t(i)];
        const bool pressed = down && !keys_[std::size_t(i)];
        keys_[std::size_t(i)] = down;
        if (!pressed) continue;
        if (i == 1) result = 2;
        else if (i == 0) {
            if (cursor > 0) text.erase(std::size_t(--cursor), 1);
        } else if (const char c = key_char(i); c != 0 && int(text.size()) < length) {
            text.insert(text.begin() + cursor, c);
            cursor = std::min(cursor + 1, length);
        }
    }
    const bool space = input.space && !keys_were_;
    keys_were_ = input.space;
    if (space && int(text.size()) < length) text.insert(text.begin() + cursor++, ' ');
    if (input.left && !left_was_) cursor = std::max(cursor - 1, 0);
    if (input.right && !right_was_) cursor = std::min(cursor + 1, int(text.size()));
    left_was_ = input.left;
    right_was_ = input.right;
    return result;
}

void FrontEnd::read_scores() {
    scores_.clear();
    for (const auto& s : assets_->high_scores) scores_.push_back({long(s.score), s.rank, s.name});
    // The table as last written (the original writes `highscore` back).
    std::ifstream in(score_file_, std::ios::binary);
    if (in) {
        std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), {});
        if (bytes.size() >= std::size_t(table_entries * score_entry_size)) {
            scores_.clear();
            for (int i = 0; i < table_entries; ++i) {
                const auto* b = bytes.data() + i * score_entry_size;
                ScoreEntry e;
                e.score = long(std::int32_t(std::uint32_t(b[0]) << 24 | b[1] << 16 | b[2] << 8 | b[3]));
                e.rank = b[4] << 8 | b[5];
                for (int k = 6; k < score_entry_size && b[k] != 0; ++k) e.name += char(b[k]);
                scores_.push_back(std::move(e));
            }
        }
    }
    scores_.resize(table_entries);
}

void FrontEnd::write_scores() const {
    std::vector<std::uint8_t> bytes(std::size_t(table_entries * score_entry_size), 0);
    for (int i = 0; i < table_entries; ++i) {
        auto* b = bytes.data() + i * score_entry_size;
        const auto& e = scores_[std::size_t(i)];
        const auto s = std::uint32_t(e.score);
        b[0] = std::uint8_t(s >> 24);
        b[1] = std::uint8_t(s >> 16);
        b[2] = std::uint8_t(s >> 8);
        b[3] = std::uint8_t(s);
        b[4] = std::uint8_t(e.rank >> 8);
        b[5] = std::uint8_t(e.rank);
        for (std::size_t k = 0; k < e.name.size() && k < std::size_t(score_entry_size - 7); ++k) b[6 + k] = std::uint8_t(e.name[k]);
    }
    try {
        platform::write_file(score_file_, bytes);
    } catch (...) {
        // (A table that cannot be written is kept for this run.)
    }
}

// $109472 end and $109320: the new score over the tenth, the table sorted
// (bubble sort, higher first), written back ($109288).
void FrontEnd::enter_score() {
    scores_[table_entries - 1] = {new_score_, new_rank_, name_};
    for (bool swapped = true; swapped;) {
        swapped = false;
        for (int i = 0; i < table_entries - 1; ++i) {
            if (scores_[std::size_t(i)].score < scores_[std::size_t(i + 1)].score) {
                std::swap(scores_[std::size_t(i)], scores_[std::size_t(i + 1)]);
                swapped = true;
            }
        }
    }
    write_scores();
}

platform::Image FrontEnd::picture(int height) const {
    platform::Image image(640, height);
    switch (screen_) {
    case Screen::story: {
        // The rows' colour 1 from the copper list ($107D4E): a fade from
        // black over the first and the last lines.
        const int n = fade_lines_;
        for (int y = 0; y < story_height && y < height; ++y) {
            // (Each colour shows from the line after its copper position:
            // measured on the original's screen.)
            const int line = y - 1;
            int level = 0;
            if (line < copper_top) level = 0;
            else if (line < copper_top + n) level = line - copper_top;
            else if (line <= copper_bottom - n) level = n - 1;
            else if (line <= copper_bottom) level = copper_bottom - line;
            const auto colour = data::to_color(std::uint16_t(level * 0x111));
            const int row = scroll_ + y;
            for (std::size_t i = 0; i < story_y_.size(); ++i) {
                const int top = story_y_[i];
                if (row < top || row >= top + font_->height) continue;
                const auto& bits = story_bits_[i];
                for (int x = 0; x < story_width; ++x) {
                    if (bits[std::size_t((row - top) * story_width + x)]) image.put(x, y, colour);
                }
            }
        }
        break;
    }
    case Screen::pictures: {
        const auto canvas = canvas_of(shown_.picture, 320, 200);
        for (int y = 0; y < 200 && y < height; ++y) put_row(image, y, canvas, y, shown_.colours, true);
        break;
    }
    case Screen::rank_select:
        for (int y = 0; y < 200 && y < height; ++y) put_row(image, y, rank_canvas_, y, shown_.colours, true);
        break;
    case Screen::name_entry: {
        // $109472 on a 320 x 200 screen of four planes.
        IndexedCanvas c(320, 200);
        topaz_text(c, "Your name is to be entered", 0x34, 0x53, 8, 0, false);
        topaz_text(c, "in the hall of fame.", 0x5B, 0x5C, 8, 0, false);
        line(c, 0x50, 100, 0xE1, 100, 2);
        line(c, 0xE1, 100, 0xE1, 0x71, 2);
        line(c, 0x50, 0x71, 0xE1, 0x71, 2);
        line(c, 0x50, 0x71, 0x50, 100, 2);
        std::string field = name_;
        field.resize(std::size_t(name_length) + 1, ' ');
        topaz_text(c, field, name_x, name_y + amiga::Topaz8::baseline, 6, 0, true);
        // $106032: the cursor, complemented.
        for (int y = name_y; y < name_y + 8; ++y) {
            for (int x = name_x + cursor_ * 8; x < name_x + cursor_ * 8 + 8; ++x) c.pixels[std::size_t(y * 320 + x)] ^= 0xF;
        }
        for (int y = 0; y < 200 && y < height; ++y) put_row(image, y, c, y, shown_.colours, true);
        break;
    }
    case Screen::saved_games: {
        // $108B96 on a 320 x 200 screen of four planes, all in pen 15 ($28F).
        IndexedCanvas c(320, 200);
        topaz_text(c, saving_ ? "Save" : "Load", 0x55, 0x13, 15, 0, false);
        topaz_text(c, "Game", 127, 19, 15, 0, false); // (the texts -$5656)
        topaz_text(c, "Exit Game", 63, 193, 15, 0, false);
        topaz_text(c, "Cancel", 222, 194, 15, 0, false);
        static constexpr int boxes[8][4] = {{43, 59, 282, 69},   {43, 75, 282, 85},   {43, 91, 282, 101},
                                            {43, 107, 282, 117}, {43, 123, 282, 133}, {43, 139, 282, 149},
                                            {43, 183, 147, 199}, {205, 183, 285, 199}}; // (-$5636)
        for (const auto& b : boxes) {
            line(c, b[0], b[1], b[2], b[1], 15);
            line(c, b[2], b[1], b[2], b[3], 15);
            line(c, b[0], b[3], b[2], b[3], 15);
            line(c, b[0], b[1], b[0], b[3], 15);
        }
        for (int i = 0; i < 6; ++i) {
            const int y = 0x3D + i * 16;
            const auto& name = slots_[std::size_t(i)];
            if (edited_[std::size_t(i)]) {
                // The line editor's field, with its cursor complemented.
                std::string field = name;
                field.resize(0x1D, ' ');
                topaz_text(c, field, 0x2C, y + amiga::Topaz8::baseline, 15, 0, true);
                if (saving_ && i == slot_) {
                    for (int yy = y; yy < y + 8; ++yy) {
                        for (int x = 0x2C + slot_cursor_ * 8; x < 0x2C + slot_cursor_ * 8 + 8 && x < 320; ++x) c.pixels[std::size_t(yy * 320 + x)] ^= 0xF;
                    }
                }
            } else {
                topaz_text(c, name, 0x2D, y + amiga::Topaz8::baseline, 15, 0, false); // ($108958)
            }
        }
        // The choice, complemented ($108E10: a slot when loading; the buttons).
        const auto complement = [&](int x0, int y0, int x1, int y1) {
            for (int y = y0; y <= y1; ++y) {
                for (int x = x0; x <= x1; ++x) c.pixels[std::size_t(y * 320 + x)] ^= 0xF;
            }
        };
        if (slot_ < 6 && !saving_) complement(0x2C, 0x3D + slot_ * 16 - 1, 0x2C + 0xED, 0x3D + slot_ * 16 + 7);
        if (slot_ == 6) complement(0x2D, 0xB9, 0x91, 0xC5);
        if (slot_ == 7) complement(0xCF, 0xB9, 0x11B, 0xC5);
        if (!busy_.empty()) topaz_text(c, busy_, 10, 10, 1, 0, false); // ($109118, pen 1)
        for (int y = 0; y < 200 && y < height; ++y) put_row(image, y, c, y, shown_.colours, true);
        break;
    }
    case Screen::hall_of_fame: {
        // $106D7A: the medals (320 x 75, lowres) over the slab (640 x 145,
        // hires, from line $4C), each with its colours.
        const auto top = canvas_of(picture_named("hiscore.iff"), 320, 0x4B);
        auto slab = canvas_of(picture_named("hiscoreslab"), 640, 0x91);
        // $10967E: the table drawn three times: black a pixel either side,
        // then in colour 15.
        static constexpr int pens[3] = {0, 0, 15}, offsets[3] = {-1, 1, 0};
        const auto text = [&](const std::string& s, int x, int y, int pen) {
            const auto bits = text_row(*font_, s, 0, 640);
            for (int row = 0; row < font_->height; ++row) {
                for (int px = 0; px < 640; ++px) {
                    if (!bits[std::size_t(row * 640 + px)]) continue;
                    const int sx = x + px, sy = y + row;
                    if (sx >= 0 && sx < slab.width && sy >= 0 && sy < slab.height) slab.pixels[std::size_t(sy * slab.width + sx)] = std::uint8_t(pen);
                }
            }
        };
        for (int pass = 0; pass < 3; ++pass) {
            for (int r = 0; r < table_entries; ++r) {
                const auto& e = scores_[std::size_t(r)];
                if (e.score == 0) continue;
                const int x = offsets[pass] + 0xD, y = r * 0xC + offsets[pass] + 6;
                char number[8], score[16], rank[24];
                std::snprintf(number, sizeof number, "%d", r + 1);
                std::snprintf(score, sizeof score, "%-6ld", e.score);
                const std::string rank_name =
                    e.rank >= 0 && std::size_t(e.rank) < assets_->rank_names.size() ? assets_->rank_names[std::size_t(e.rank)] : "";
                std::snprintf(rank, sizeof rank, "%-12s", rank_name.c_str());
                text(number, x, y, pens[pass]);
                text(score, x + 0x28, y, pens[pass]);
                text(rank, x + 0x96, y, pens[pass]);
                text(e.name, x + 300, y, pens[pass]);
            }
        }
        std::vector<std::uint16_t> top_colours(shown_.colours.begin(), shown_.colours.begin() + std::min<std::ptrdiff_t>(32, std::ssize(shown_.colours)));
        std::vector<std::uint16_t> slab_colours;
        if (shown_.colours.size() > 32) slab_colours.assign(shown_.colours.begin() + 32, shown_.colours.end());
        for (int y = 0; y < 0x4B && y < height; ++y) put_row(image, y, top, y, top_colours, true);
        for (int y = 0; y < 0x91 && 0x4C + y < height; ++y) put_row(image, 0x4C + y, slab, y, slab_colours, false);
        break;
    }
    case Screen::none: break;
    }
    return image;
}

} // namespace wings::game
